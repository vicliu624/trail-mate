import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {mkdtemp, mkdir, readFile, writeFile, readdir, rm} from 'node:fs/promises';
import {spawnSync} from 'node:child_process';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fixture, connect} from './linux_identity_test_transport.mjs';
import {readDeviceTrackCatalogPage} from '../../../site/tracks/src/device-catalog.js';
import {buildDeviceTrackSnapshot} from '../../../site/tracks/src/device-snapshot-build.js';
import {readDeviceTrackSnapshot} from '../../../site/tracks/src/device-snapshot.js';
import {convertRecording} from '../../../site/tracks/src/recording-convert.js';
import {OPERATION, MANAGEMENT_STATUS as STATUS} from '../../../site/device/src/management-protocol.js';
globalThis.crypto ??= webcrypto;
const root = await mkdtemp(join(tmpdir(), 'trailmate-track-snapshots-'));
let app, client;
try {
  const recorded = join(root, 'recorded');
  await mkdir(recorded);
  const result = spawnSync(process.argv[3], [recorded], {encoding: 'utf8'});
  assert.equal(result.status, 0, result.stdout + result.stderr);
  app = await fixture(root, 'seed');
  const endpoint = await app.next('ENDPOINT');
  client = await connect(endpoint);
  for (const operation of [81, 86, 87, 88]) assert.ok(client.operations.includes(operation));
  const tracks = join(root, 'tracks'); await mkdir(tracks);
  async function entry(name) {
    let cursor = null;
    do {
      const page = await readDeviceTrackCatalogPage(client.session, cursor);
      if (page.entry?.name === name) return {row: page.entry, media: page.media};
      cursor = page.cursor;
    } while (cursor);
    throw Error(`Missing recording ${name}`);
  }
  for (const [extension, format] of [['gpx', 1], ['csv', 2], ['bin', 3], ['kml', 0]]) {
    const source = extension === 'kml' ? Buffer.from(convertRecording(new Uint8Array(await readFile(join(recorded, 'recording.csv'))), {format: 'csv'}).kml) : await readFile(join(recorded, `recording.${extension}`));
    const name = `recording.${extension}`;
    await writeFile(join(tracks, name), source);
    const selected = await entry(name);
    const built = await buildDeviceTrackSnapshot(client.session, selected.row, selected.media, {pollIntervalMs: 5});
    if (extension === 'gpx') {
      // A sealed container survives source mutation and a fresh TCP/session.
      await writeFile(join(tracks, name), 'source changed after seal');
      await client.connection.close(); client = await connect(endpoint);
    }
    const read = await readDeviceTrackSnapshot(client.session, built.snapshotId);
    assert.equal(read.format, format); assert.deepEqual(Buffer.from(read.source), source);
    if (extension === 'csv' || extension === 'bin') assert.equal(convertRecording(read.source, {format: extension === 'bin' ? 'binary' : 'csv'}).pointCount, 1);
    const release = async () => client.session.request(OPERATION.TRACK_SNAPSHOT_RELEASE, [built.snapshotId]);
    assert.deepEqual(await release(), []); assert.deepEqual(await release(), []);
    await assert.rejects(readDeviceTrackSnapshot(client.session, built.snapshotId), error => error.code === STATUS.EXPIRED);
  }
  await writeFile(join(tracks, 'large.gpx'), Buffer.alloc(1024 * 1024, 65));
  const selected = await entry('large.gpx'), id = new Uint8Array(16).fill(11);
  const selection = [id, selected.media, selected.row.name, Number(selected.row.bytes), selected.row.format];
  const started = await client.session.request(OPERATION.TRACK_SNAPSHOT_BUILD, selection); assert.equal(started[1], 1);
  await assert.rejects(client.session.request(OPERATION.IDENTITY_BACKUP_RESTORE, []), error => error.code === STATUS.BUSY);
  const cancelled = await client.session.request(OPERATION.TRACK_SNAPSHOT_CANCEL, [id]); assert.ok([2, 5].includes(cancelled[1]));
  await assert.rejects(readDeviceTrackSnapshot(client.session, id), error => error.code === STATUS.EXPIRED);
  assert.ok(!(await readdir(join(root, 'trailmate/web-tracks'))).some(name => name.endsWith('.new')));

  // Header success cannot conceal corruption of sealed payload bytes.
  const small = await entry('recording.csv');
  const built = await buildDeviceTrackSnapshot(client.session, small.row, small.media, {pollIntervalMs: 5});
  const path = join(root, 'trailmate/web-tracks', `${Buffer.from(built.snapshotId).toString('hex')}.tsn`);
  const bytes = await readFile(path); bytes[64] ^= 1; await writeFile(path, bytes);
  await assert.rejects(readDeviceTrackSnapshot(client.session, built.snapshotId), error => error.code === 'SNAPSHOT_HASH_MISMATCH');
  await client.session.request(OPERATION.TRACK_SNAPSHOT_RELEASE, [built.snapshotId]);
  console.log('LINUX_TRACK_SNAPSHOT_FORMAL_IMPORT_PASSED');
} finally {
  await client?.connection.close(); await app?.stop(); await rm(root, {recursive: true, force: true});
}
