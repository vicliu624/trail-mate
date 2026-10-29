import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {mkdtemp, readFile, readdir, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fixture, connect} from './linux_identity_test_transport.mjs';
import {proveDeviceAuthor} from '../../../site/device/src/author-identity.js';
import {OPERATION, MANAGEMENT_STATUS as STATUS} from '../../../site/device/src/management-protocol.js';
import {encode, decode} from '../../../site/geocaching/src/protocol.js';
import {encodeTrackManifest, verifyTrackManifest} from '../../../site/tracks/src/manifest.js';
import {signDeviceTrackManifest} from '../../../site/tracks/src/device-local-signature.js';
globalThis.crypto ??= webcrypto;
const id = () => crypto.getRandomValues(new Uint8Array(16));
const hash = async bytes => new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
for (const [version, assets] of [[1, false], [2, true], [2, false]]) {
  const root = await mkdtemp(join(tmpdir(), 'trailmate-track-signing-'));
  let app, client;
  try {
    app = await fixture(root, 'seed'); const endpoint = await app.next('ENDPOINT'); client = await connect(endpoint);
    const author = await proveDeviceAuthor(client.session);
    const kml = new TextEncoder().encode('<kml xmlns="http://www.opengis.net/kml/2.2"><Document><Placemark>' + (assets ? '<description><![CDATA[<img src="photos/example.jpg">]]></description>' : '') + '<Point><coordinates>1,2</coordinates></Point></Placemark></Document></kml>');
    const kmlHash = await hash(kml), image = new Uint8Array(await readFile(process.argv[3])), imageHash = await hash(image);
    const index = encode([1, kmlHash, kml.length, [['photo', imageHash, image.length, 'image/jpeg', 'photos/example.jpg', 0, 1]]]);
    const record = [version, author.publicKey, id(), 0, 1, null, 0, 100, 100, 'Local trail', '', [10000000, 20000000, 10000000, 20000000], [0, null, null, 1], [kmlHash, kml.length, 'application/vnd.google-earth.kml+xml', 1]];
    if (version === 2) record.push([0, [], 0, 1, 0, 0, 0, null], assets ? [await hash(index), index.length, 1, 1] : null);
    const manifest = encodeTrackManifest(record);
    const input = {kml, manifest, author, operationId: id(), kmlUploadId: id(), manifestUploadId: id(),
      ...(assets ? {attachmentIndex: index, attachmentFiles: [{id: 'photo', content: image}], attachmentUploadIds: [{id: 'photo', uploadId: id()}], indexUploadId: id(), packageUploadId: id()} : {})};
    const request = client.session.request.bind(client.session); let retained;
    const operation = version === 2 ? OPERATION.TRACK_PACKAGE_SIGN_PREPARE : OPERATION.TRACK_SIGN_PREPARE;
    client.session.request = (op, body, options) => { if (op === operation) retained = body.map(value => value instanceof Uint8Array ? value.slice() : value); return request(op, body, options); };
    const result = await signDeviceTrackManifest(client.session, input, {operations: client.operations, prepareOptions: {retryMs: 10, timeoutMs: 20000}});
    assert.ok(result.prepared && !result.published); assert.deepEqual(result.signed[0], manifest);
    const proof = await verifyTrackManifest(result.signed, {expectedAuthor: author.publicKey});
    assert.deepEqual(proof.objectId, result.objectId); assert.deepEqual(proof.revisionHash, result.revisionHash);
    const first = await client.session.request(operation, retained); assert.equal(first[1], 1); assert.deepEqual(first[5], result.signed[1]);
    if (version === 1) assert.deepEqual(await client.session.request(OPERATION.TRACK_PREPARE, retained), first.slice(0, 5));
    const directory = join(root, 'trailmate/tracks', Buffer.from(result.objectId).toString('hex'));
    const files = await Promise.all((await readdir(directory)).map(name => readFile(join(directory, name))));
    assert.ok(files.some(bytes => bytes.equals(Buffer.from(kml))));
    const signed = files.find(bytes => bytes[0] === 0x92);
    assert.ok(signed); assert.deepEqual(decode(new Uint8Array(signed), 4166), result.signed);
    if (assets) { assert.ok(files.some(bytes => bytes.equals(Buffer.from(index)))); assert.ok(files.some(bytes => bytes.equals(Buffer.from(image)))); }
    // The durable reservation/signature/head and content can reconstruct the
    // original result after all connection-owned state has been discarded.
    await client.connection.close(); client = await connect(endpoint);
    let recovered;
    for (let step = 0; step < 100; ++step) {
      recovered = await client.session.request(operation, retained);
      if (recovered[1] === 1) break;
      await new Promise(resolve => setTimeout(resolve, 10));
    }
    assert.equal(recovered[1], 1); assert.deepEqual(recovered[5], first[5]);
    await assert.rejects(client.session.request(operation, retained.map((value, index) => index === 3 ? value + 1 : value)), error => error.code === STATUS.STALE_IDENTITY);
    const conflict = retained.map((value, index) => index === 0 ? id() : value);
    await assert.rejects((async () => {
      for (let step = 0; step < 100; ++step) { await client.session.request(operation, conflict); await new Promise(resolve => setTimeout(resolve, 10)); }
      throw Error('Conflicting operation was accepted');
    })(), error => error.code === STATUS.CONFLICT);
    const after = await proveDeviceAuthor(client.session); assert.deepEqual(after.publicKey, author.publicKey); assert.equal(after.generation, author.generation);
    console.log('LINUX_TRACK_SIGNING_FORMAL_CLIENT_PASSED', version, assets ? 'assets' : 'no-assets');
  } finally {
    await client?.connection.close(); await app?.stop(); await rm(root, {recursive: true, force: true});
  }
}
