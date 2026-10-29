import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {mkdtemp, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fixture, connect} from './linux_identity_test_transport.mjs';
import {OPERATION, MANAGEMENT_STATUS as STATUS} from '../../../site/device/src/management-protocol.js';
import {proveDeviceAuthor} from '../../../site/device/src/author-identity.js';
import {readDeviceRadioIdentity, exportEncryptedRadioIdentity, decryptRadioIdentityBackup,
        clearRadioIdentityBackup, restoreRadioIdentityBackup, verifyRestoredRadioIdentity} from '../../../site/device/src/radio-identity.js';
import {uploadTrackFile} from '../../../site/tracks/src/device-upload.js';

globalThis.crypto ??= webcrypto;
const password = 'native radio fixture password';
async function restore(link, backup, author, current, failed) {
  const decoded = await decryptRadioIdentityBackup(backup.bytes, password);
  try { return await restoreRadioIdentityBackup(link.session, decoded, author, current, {operations: link.operations, consent: true}); }
  catch (error) {
    if (failed && error.code === STATUS.STORAGE_UNAVAILABLE) return null;
    assert.equal(error.code, 'RADIO_IDENTITY_OUTCOME_UNKNOWN');
    return {state: 'indeterminate', expectation: error.expectation};
  } finally { clearRadioIdentityBackup(decoded); }
}
for (const [kind, failed] of [[1, false], [2, false], [1, true], [2, true]]) {
  const root = await mkdtemp(join(tmpdir(), 'trailmate-radio-lifecycle-'));
  let source, target, a, b, backup;
  try {
    source = await fixture(join(root, 'source'), 'radio-source');
    target = await fixture(join(root, 'target'), failed ? kind === 1 ? 'radio-fail-mc' : 'radio-fail-mt' : 'seed');
    a = await connect(await source.next('ENDPOINT')); b = await connect(await target.next('ENDPOINT'));
    for (const operation of [7, 8, 9]) assert(b.operations.includes(operation));
    const targetAuthor = await proveDeviceAuthor(b.session);
    const current = await readDeviceRadioIdentity(b.session, kind, targetAuthor, {operations: b.operations});
    assert.equal(current.generation, 0);
    if (kind === 2 && !failed) assert.equal(current.present, false);
    const sourceAuthor = await proveDeviceAuthor(a.session);
    await assert.rejects(a.session.request(8, [sourceAuthor.generation, kind, false]), error => error.code === STATUS.UNAUTHORIZED);
    backup = await exportEncryptedRadioIdentity(a.session, kind, password, {operations: a.operations, consent: true});
    let decoded;
    try {
      decoded = await decryptRadioIdentityBackup(backup.bytes, password);
      assert.equal(decoded.identity.kind, kind);
      assert.equal(decoded.identity.privateKey.length, kind === 1 ? 64 : 32);
      const request = [targetAuthor.generation, targetAuthor.publicKey, decoded.signed[0], decoded.signed[1], current.generation, current.publicKey, true];
      await assert.rejects(b.session.request(9, [...request.slice(0, 6), false]), error => error.code === STATUS.UNAUTHORIZED);
      await assert.rejects(b.session.request(9, [targetAuthor.generation + 1, ...request.slice(1)]), error => error.code === STATUS.STALE_IDENTITY);
      await assert.rejects(b.session.request(9, [...request.slice(0, 4), current.generation + 1, ...request.slice(5)]), error => error.code === STATUS.CONFLICT);
      const wrong = current.publicKey?.slice() ?? new Uint8Array(32).fill(1); wrong[0] ^= 0x10;
      await assert.rejects(b.session.request(9, [...request.slice(0, 5), wrong, true]), error => error.code === STATUS.CONFLICT);
      const forged = decoded.signed[1].slice(); forged[0] ^= 1;
      await assert.rejects(b.session.request(9, [...request.slice(0, 3), forged, ...request.slice(4)]), error => error.code === STATUS.INVALID_FIELD); forged.fill(0);
      target.peer.stdin.write('gate-on\n'); assert.equal(await target.next('GATE'), 'active');
      await assert.rejects(b.session.request(9, request), error => error.code === STATUS.BUSY);
      target.peer.stdin.write('gate-off\n'); assert.equal(await target.next('GATE'), 'idle');
      await uploadTrackFile(b.session, new TextEncoder().encode('radio identity isolated upload'), {id: new Uint8Array(16).fill(7), finishPollMs: 10, finishTimeoutMs: 5000});
      await assert.rejects(b.session.request(9, request), error => error.code === STATUS.BUSY);
      await b.session.request(OPERATION.SESSION_CLOSE, []);
    } finally { clearRadioIdentityBackup(decoded); }
    if (kind === 2 && !failed) b.control.loseNextOperation = 9;
    const outcome = await restore(b, backup, targetAuthor, current, failed);
    if (kind === 2 && !failed) assert.equal(outcome.state, 'indeterminate');
    await b.connection.close(); b = await connect(await target.next('ENDPOINT'));
    const afterAuthor = await proveDeviceAuthor(b.session);
    assert.deepEqual(afterAuthor.publicKey, targetAuthor.publicKey); assert.equal(afterAuthor.generation, targetAuthor.generation);
    if (failed) {
      const unchanged = await readDeviceRadioIdentity(b.session, kind, afterAuthor, {operations: b.operations});
      assert.equal(unchanged.generation, current.generation); assert.deepEqual(unchanged.publicKey, current.publicKey);
      if (outcome) await assert.rejects(verifyRestoredRadioIdentity(b.session, outcome.expectation, {operations: b.operations}), error => error.code === 'RADIO_IDENTITY_NOT_CONFIRMED');
    } else {
      const verified = await verifyRestoredRadioIdentity(b.session, outcome.expectation, {operations: b.operations});
      assert.equal(verified.generation, 1); assert.equal(Buffer.from(verified.publicKey).toString('hex'), backup.publicKey);
      const same = await exportEncryptedRadioIdentity(b.session, kind, password, {operations: b.operations, consent: true});
      try {
        const repeated = await restore(b, same, afterAuthor, verified, false);
        await b.connection.close(); b = await connect(await target.next('ENDPOINT'));
        assert.equal((await verifyRestoredRadioIdentity(b.session, repeated.expectation, {operations: b.operations})).generation, 2);
      } finally { same.bytes.fill(0); }
    }
    console.log('RADIO_IDENTITY_LIFECYCLE_PASSED', kind, failed ? 'rollback' : 'restore');
  } finally {
    backup?.bytes.fill(0);
    await a?.connection.close(); await b?.connection.close();
    await source?.stop(); await target?.stop();
    await rm(root, {recursive: true, force: true});
  }
}
