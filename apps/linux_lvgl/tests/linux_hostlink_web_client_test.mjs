import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {webcrypto} from 'node:crypto';
import {mkdtemp, rm, readFile, cp, mkdir} from 'node:fs/promises';
import {createConnection} from 'node:net';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {createInterface} from 'node:readline';
import {Duplex} from 'node:stream';
import {once} from 'node:events';
import {HostlinkConnection, CAPABILITY} from '../../../site/device/src/hostlink.js';
import {ManagementSession, OPERATION, MANAGEMENT_STATUS} from '../../../site/device/src/management-protocol.js';
import {proveDeviceAuthor} from '../../../site/device/src/author-identity.js';
import {exportEncryptedDeviceIdentity} from '../../../site/device/src/identity-backup-export.js';
import {decryptIdentityBackup, clearDecryptedIdentityBackup} from '../../../site/device/src/identity-backup-file.js';
import {uploadTrackFile, queryTrackUpload, discardTrackUpload} from '../../../site/tracks/src/device-upload.js';
import {installDeviceTrackPackage} from '../../../site/tracks/src/device-package-install.js';
import {queryInstalledDevicePackage} from '../../../site/tracks/src/device-package-query.js';
import {decode} from '../../../site/geocaching/src/protocol.js';

globalThis.crypto ??= webcrypto;
const root = await mkdtemp(join(tmpdir(), 'trailmate-hostlink-web-'));
const peer = spawn(process.argv[2], [root, process.argv[3]], {stdio: ['pipe', 'pipe', 'inherit']});
const exited = once(peer, 'exit');
let connection;
try {
  const lines = createInterface({input: peer.stdout});
  const [endpoint] = await Promise.race([
    once(lines, 'line'),
    exited.then(() => { throw new Error('Native fixture exited before publishing its endpoint'); }),
  ]);
  const url = new URL(endpoint);
  // Exercise the production Web Serial client over equivalent byte streams;
  // this shim does not claim physical USB/Web Serial browser interoperability.
  const port = {
    async open() {
      this.socket = createConnection({host: url.hostname, port: Number(url.port)});
      await once(this.socket, 'connect');
      const streams = Duplex.toWeb(this.socket);
      this.readable = streams.readable;
      this.writable = streams.writable;
    },
    async setSignals() {},
    async close() { this.socket?.destroy(); },
  };
  connection = new HostlinkConnection(port);
  const hello = await connection.open();
  assert.equal(hello.capabilities, (1 << 3) | CAPABILITY.MANAGEMENT | CAPABILITY.AUTO_INTERFACE_IFAC); // Shared CapStatus bit.
  const session = new ManagementSession(connection, {sessionId: new Uint8Array(16).fill(1)});
  const other = new ManagementSession(connection, {sessionId: new Uint8Array(16).fill(2)});
  const requestId = new Uint8Array(16).fill(9);
  const capabilities = await Promise.all([
    session.request(OPERATION.CAPABILITIES, [], {requestId}),
    other.request(OPERATION.CAPABILITIES, [], {requestId}),
  ]);
  const supported = [1, 512, 320, 1, [0, 1, 2, 3, 4, 7, 8, 32, 33, 34, 35, 36, 37, 38, 80, 81, 82, 86, 87, 88, 96, 97, 98, 99, 100, 101], 0];
  assert.deepEqual(capabilities, [supported, supported]);
  const author = await proveDeviceAuthor(session);
  const repeated = await proveDeviceAuthor(other);
  assert.deepEqual(repeated.publicKey, author.publicKey);
  assert.equal(repeated.generation, author.generation);
  await assert.rejects(session.request(OPERATION.IDENTITY_BACKUP_EXPORT, [author.generation, false]), error => error.code === MANAGEMENT_STATUS.UNAUTHORIZED);
  await assert.rejects(session.request(OPERATION.IDENTITY_BACKUP_EXPORT, [author.generation + 1, true]), error => error.code === MANAGEMENT_STATUS.STALE_IDENTITY);
  await assert.rejects(session.request(OPERATION.IDENTITY_BACKUP_RESTORE, []), error => error.code === MANAGEMENT_STATUS.UNSUPPORTED);
  const backup = await exportEncryptedDeviceIdentity(session, 'native backup fixture password', {consent: true});
  let restored;
  try {
    restored = await decryptIdentityBackup(backup.bytes, 'native backup fixture password');
    assert.equal(restored.identity.format, 1);
    assert.equal(restored.identity.privateKey.length, 64);
    assert.equal(restored.identity.generation, author.generation);
    assert.deepEqual(restored.identity.publicKey, author.publicKey);
    assert.deepEqual(restored.identity.identityHash, author.identityHash);
    await assert.rejects(decryptIdentityBackup(backup.bytes, 'wrong fixture password'), error => error.code === 'IDENTITY_BACKUP_AUTH_FAILED');
  } finally { clearDecryptedIdentityBackup(restored); backup.bytes.fill(0); }
  const uploadId = new Uint8Array(16).fill(7);
  const content = new TextEncoder().encode('<kml>' + 'upload-fixture '.repeat(60) + '</kml>');
  const receipt = await uploadTrackFile(session, content, {id: uploadId, finishPollMs: 10, finishTimeoutMs: 5000});
  assert.equal(receipt.sealed, true);
  assert.equal(receipt.published, false);
  assert.equal((await queryTrackUpload(session, uploadId, content.length)).offset, content.length);
  await assert.rejects(queryTrackUpload(other, uploadId, content.length), error => error.code === MANAGEMENT_STATUS.UNAUTHORIZED);
  await discardTrackUpload(session, uploadId);
  const retainedId = new Uint8Array(16).fill(8);
  const retained = await uploadTrackFile(session, content, {id: retainedId, finishPollMs: 10, finishTimeoutMs: 5000});
  await assert.rejects(session.request(OPERATION.TRANSFER_LIST_OWNED, [3, null]), error => error.code === MANAGEMENT_STATUS.BUSY);
  await session.request(OPERATION.SESSION_CLOSE, []);
  let cursor = null, ownedRows = 0;
  for (let count = 0;; ++count) {
    assert.ok(count < 16);
    const [media, next, row] = await session.request(OPERATION.TRANSFER_LIST_OWNED, [3, cursor]);
    if (row) { assert.deepEqual(row[0], retainedId); assert.equal(row[5], 2); assert.equal(row[6], content.length); ++ownedRows; }
    if (next === null) break;
    cursor = [media, next];
  }
  assert.ok(ownedRows > 0);
  const wrongHash = retained.sha256.slice(); wrongHash[0] ^= 1;
  await assert.rejects(session.request(OPERATION.TRANSFER_CLEANUP, [retainedId, 3, content.length, wrongHash]), error => error.code === MANAGEMENT_STATUS.CONFLICT);
  assert.deepEqual(await session.request(OPERATION.TRANSFER_CLEANUP, [retainedId, 3, content.length, retained.sha256]), []);
  assert.deepEqual(await session.request(OPERATION.TRANSFER_CLEANUP, [retainedId, 3, content.length, retained.sha256]), []);
  await assert.rejects(session.request(OPERATION.TRACK_PACKAGE_INSTALL, []), error => error.code === MANAGEMENT_STATUS.INVALID_FIELD);
  const packageInput = {
    author, signed: decode(new Uint8Array(await readFile(join(root, 'package.cmp'))), 4166),
    kml: new Uint8Array(await readFile(join(root, 'package.kml'))),
    attachmentIndex: new Uint8Array(await readFile(join(root, 'package-index.cmp'))),
    attachmentFiles: [{id: 'photo', content: new Uint8Array(await readFile(join(root, 'package-photo.jpg')))}],
    operationId: new Uint8Array(16).fill(10), kmlUploadId: new Uint8Array(16).fill(11), signedUploadId: new Uint8Array(16).fill(12),
    indexUploadId: new Uint8Array(16).fill(13), packageUploadId: new Uint8Array(16).fill(14),
    attachmentUploadIds: [{id: 'photo', uploadId: new Uint8Array(16).fill(15)}],
  };
  const installed = await installDeviceTrackPackage(session, packageInput, {operations: supported[4], retryMs: 10, timeoutMs: 15000});
  assert.equal(installed.installed, true);
  const queried = await queryInstalledDevicePackage(session, {author, operationId: new Uint8Array(16).fill(16), objectId: installed.objectId, revisionHash: installed.revisionHash},
    {operations: supported[4], retryMs: 10, timeoutMs: 15000});
  assert.equal(queried.installed, true);
  assert.deepEqual(new Uint8Array(await readFile(join(root, 'sd', installed.target.slice(1)))), packageInput.kml);
  if (process.env.TRAIL_MATE_EXPORT_PACKAGE_FIXTURE) {
    const destination = process.env.TRAIL_MATE_EXPORT_PACKAGE_FIXTURE;
    await mkdir(join(destination, 'trailmate'), {recursive: true});
    await cp(join(root, 'sd', 'routes'), join(destination, 'routes'), {recursive: true});
    await cp(join(root, 'sd', 'trailmate', 'packages'), join(destination, 'trailmate', 'packages'), {recursive: true});
    console.log('Exported public route/cache fixture:', destination);
  }
  assert.deepEqual(await session.request(OPERATION.SESSION_CLOSE, []), []);
  session.close();
  assert.deepEqual(await other.request(OPERATION.SESSION_CLOSE, []), []);
  other.close();
  console.log('Production HostlinkConnection, ManagementSession and device-author proof passed over native TCP');
} finally {
  await connection?.close();
  peer.stdin.end('quit\n');
  const timer = setTimeout(() => peer.kill('SIGKILL'), 5000);
  const [code, signal] = await exited;
  clearTimeout(timer);
  await rm(root, {recursive: true, force: true});
  assert.equal(signal, null);
  assert.equal(code, 0);
}
