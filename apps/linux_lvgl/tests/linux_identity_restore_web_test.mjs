import {fixture, connect} from './linux_identity_test_transport.mjs';
import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {mkdtemp, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {OPERATION, MANAGEMENT_STATUS as STATUS} from '../../../site/device/src/management-protocol.js';
import {proveDeviceAuthor} from '../../../site/device/src/author-identity.js';
import {exportEncryptedDeviceIdentity} from '../../../site/device/src/identity-backup-export.js';
import {decryptIdentityBackup, clearDecryptedIdentityBackup} from '../../../site/device/src/identity-backup-file.js';
import {previewEncryptedDeviceIdentity} from '../../../site/device/src/identity-backup-preview.js';
import {verifyRestoredDeviceIdentity} from '../../../site/device/src/identity-restore-verification.js';
import {uploadTrackFile} from '../../../site/tracks/src/device-upload.js';

globalThis.crypto ??= webcrypto;
const password = 'native restore fixture password';
async function restore(link, backup, expectedFailure = false) {
  const preview = await previewEncryptedDeviceIdentity(backup.bytes, password, {session: link.session, operations: link.operations});
  assert.equal(preview.metadata.restoreSupported, true);
  let outcome;
  try { outcome = await preview.restore({consent: true}); }
  catch (error) {
    if (expectedFailure && error.code === STATUS.STORAGE_UNAVAILABLE) return;
    assert.equal(error.code, 'IDENTITY_RESTORE_OUTCOME_UNKNOWN');
    outcome = {state: 'indeterminate', expectation: error.expectation};
  }
  if (expectedFailure) assert.equal(outcome.state, 'indeterminate');
  return outcome;
}
for (const mode of ['seed', 'expanded', 'write-failure']) {
  const root = await mkdtemp(join(tmpdir(), 'trailmate-identity-lifecycle-'));
  let source, target, a, b, backup;
  try {
    source = await fixture(join(root, 'source'), mode === 'expanded' ? 'expanded' : 'seed');
    target = await fixture(join(root, 'target'), mode === 'write-failure' ? mode : 'seed');
    a = await connect(await source.next('ENDPOINT'));
    b = await connect(await target.next('ENDPOINT'));
    backup = await exportEncryptedDeviceIdentity(a.session, password, {consent: true});
    const before = await proveDeviceAuthor(b.session);
    assert.notDeepEqual(before.publicKey, backup.author.publicKey);
    await assert.rejects(b.session.request(OPERATION.IDENTITY_BACKUP_RESTORE, []), error => error.code === STATUS.INVALID_FIELD);
    let decoded;
    try {
      decoded = await decryptIdentityBackup(backup.bytes, password);
      assert.equal(decoded.identity.format, mode === 'expanded' ? 0 : 1);
      const request = [before.generation, before.publicKey, decoded.signed[0], decoded.signed[1], true];
      await assert.rejects(b.session.request(OPERATION.IDENTITY_BACKUP_RESTORE, [...request.slice(0, 4), false]), error => error.code === STATUS.UNAUTHORIZED);
      await assert.rejects(b.session.request(OPERATION.IDENTITY_BACKUP_RESTORE, [before.generation + 1, ...request.slice(1)]), error => error.code === STATUS.STALE_IDENTITY);
      const wrongTarget = before.publicKey.slice(); wrongTarget[0] ^= 1;
      await assert.rejects(b.session.request(OPERATION.IDENTITY_BACKUP_RESTORE, [before.generation, wrongTarget, ...request.slice(2)]), error => error.code === STATUS.STALE_IDENTITY);
      const forged = decoded.signed[1].slice(); forged[0] ^= 1;
      await assert.rejects(b.session.request(OPERATION.IDENTITY_BACKUP_RESTORE, [before.generation, before.publicKey, decoded.signed[0], forged, true]), error => error.code === STATUS.INVALID_FIELD);
      forged.fill(0);
      target.peer.stdin.write('gate-on\n'); assert.equal(await target.next('GATE'), 'active');
      await assert.rejects(b.session.request(OPERATION.IDENTITY_BACKUP_RESTORE, request), error => error.code === STATUS.BUSY);
      target.peer.stdin.write('gate-off\n'); assert.equal(await target.next('GATE'), 'idle');
      await uploadTrackFile(b.session, new TextEncoder().encode('isolated pending upload'), {id: new Uint8Array(16).fill(7), finishPollMs: 10, finishTimeoutMs: 5000});
      await assert.rejects(b.session.request(OPERATION.IDENTITY_BACKUP_RESTORE, request), error => error.code === STATUS.BUSY);
      await b.session.request(OPERATION.SESSION_CLOSE, []);
      assert.deepEqual((await proveDeviceAuthor(b.session)).publicKey, before.publicKey);
    } finally { clearDecryptedIdentityBackup(decoded); }
    if (mode === 'expanded') b.control.loseNextRestore = true;
    const outcome = await restore(b, backup, mode === 'write-failure');
    if (mode === 'expanded') assert.equal(outcome.state, 'indeterminate');
    await b.connection.close();
    b = await connect(await target.next('ENDPOINT'));
    if (mode === 'write-failure') {
      const unchanged = await proveDeviceAuthor(b.session);
      assert.deepEqual(unchanged.publicKey, before.publicKey); assert.equal(unchanged.generation, before.generation);
      if (outcome?.expectation) await assert.rejects(verifyRestoredDeviceIdentity(b.session, outcome.expectation), error => error.code === 'IDENTITY_RESTORE_NOT_CONFIRMED');
    } else {
      assert.equal(outcome.expectation.generation, before.generation + 1);
      assert.equal((await verifyRestoredDeviceIdentity(b.session, outcome.expectation)).state, 'verified');
      assert.deepEqual((await proveDeviceAuthor(b.session)).publicKey, backup.author.publicKey);
      if (mode === 'expanded') {
        const same = await exportEncryptedDeviceIdentity(b.session, password, {consent: true});
        try {
          const repeated = await restore(b, same);
          await b.connection.close(); b = await connect(await target.next('ENDPOINT'));
          assert.equal(repeated.expectation.generation, before.generation + 2);
          assert.equal((await verifyRestoredDeviceIdentity(b.session, repeated.expectation)).state, 'verified');
        } finally { same.bytes.fill(0); }
      }
    }
    console.log('IDENTITY_RESTORE_LIFECYCLE_PASSED', mode);
  } finally {
    backup?.bytes.fill(0);
    await a?.connection.close(); await b?.connection.close();
    await source?.stop(); await target?.stop();
    await rm(root, {recursive: true, force: true});
  }
}
