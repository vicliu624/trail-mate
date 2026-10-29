import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {mkdtemp, mkdir, writeFile, rm, symlink, link, rename} from 'node:fs/promises';
import {spawn} from 'node:child_process';
import {once} from 'node:events';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fixture, connect} from './linux_identity_test_transport.mjs';
import {readDeviceTrackCatalogPage} from '../../../site/tracks/src/device-catalog.js';
import {OPERATION, MANAGEMENT_STATUS} from '../../../site/device/src/management-protocol.js';
globalThis.crypto ??= webcrypto;
const root = await mkdtemp(join(tmpdir(), 'trailmate-track-catalog-'));
let app, client, locker;
try {
  app = await fixture(root, 'seed');
  client = await connect(await app.next('ENDPOINT'));
  const {session, operations} = client;
  assert.ok(operations.includes(OPERATION.TRACK_LIST));
  let page = await readDeviceTrackCatalogPage(session);
  assert.equal(page.entry, null); assert.equal(page.cursor, null);
  const directory = join(root, 'tracks');
  await mkdir(directory, {recursive: true});
  const expected = new Map([['one.GPX', 1], ['山路.csv', 2], ['three.bin', 3], ['route.kml', 0]]);
  for (const [name] of expected) await writeFile(join(directory, name), 'public-track');
  await writeFile(join(directory, 'active.bin'), 'control');
  await writeFile(join(directory, '.hidden.gpx'), 'hidden');
  await writeFile(join(directory, 'ignored.txt'), 'ignored');
  await mkdir(join(directory, 'subdir.gpx'));
  await writeFile(join(root, 'outside.gpx'), 'outside');
  await symlink(join(root, 'outside.gpx'), join(directory, 'symlink.gpx'));
  await link(join(root, 'outside.gpx'), join(directory, 'hardlink.gpx'));

  // A separate process owns the same flock protocol as the actual recorder.
  locker = spawn('python3', ['-c', 'import fcntl,sys; f=open(sys.argv[1],"rb"); fcntl.flock(f,fcntl.LOCK_EX); print("LOCKED",flush=True); sys.stdin.read()', join(directory, 'one.GPX')], {stdio: ['pipe', 'pipe', 'inherit']});
  await once(locker.stdout, 'data');
  const rows = new Map();
  let cursor = null;
  for (let request = 0; request < 16; ++request) {
    page = await readDeviceTrackCatalogPage(session, cursor);
    if (page.entry) {
      assert.ok(!rows.has(page.entry.name));
      rows.set(page.entry.name, page.entry);
    }
    cursor = page.cursor;
    if (!cursor) break;
  }
  assert.equal(cursor, null);
  assert.deepEqual([...rows.keys()].sort(), [...expected.keys()].sort());
  for (const [name, kind] of expected) {
    assert.equal(rows.get(name).format, kind);
    assert.equal(rows.get(name).bytes, 12n);
    assert.equal(rows.get(name).recording, name === 'one.GPX');
  }
  const exited = once(locker, 'exit'); locker.stdin.end(); await exited; locker = null;
  // Track catalogs are not constrained by the upload journal's 192-entry cap.
  for (let index = 0; index < 200; ++index) await writeFile(join(directory, `older-${index}.gpx`), 'older');
  cursor = null;
  let total = 0;
  do {
    page = await readDeviceTrackCatalogPage(session, cursor); cursor = page.cursor;
    if (page.entry) { ++total; assert.equal(page.entry.recording, false); }
  } while (cursor);
  assert.equal(total, 204);
  page = await readDeviceTrackCatalogPage(session);
  assert.ok(page.cursor);
  await writeFile(join(directory, 'new.gpx'), 'new');
  await assert.rejects(readDeviceTrackCatalogPage(session, page.cursor), error => error.code === 'CATALOG_CHANGED');
  for (const body of [[], [1], [[0, 1]], [[0, 32], 1]])
    await assert.rejects(session.request(OPERATION.TRACK_LIST, body), error => error.code === MANAGEMENT_STATUS.INVALID_FIELD);
  await rename(directory, `${directory}-original`);
  await symlink(join(root), directory);
  await assert.rejects(readDeviceTrackCatalogPage(session), error => error.code === MANAGEMENT_STATUS.STORAGE_UNAVAILABLE);
  await rm(directory); await rename(`${directory}-original`, directory);
  await writeFile(Buffer.concat([Buffer.from(`${directory}/bad-`), Buffer.from([0xff]), Buffer.from('.gpx')]), 'bad');
  cursor = null;
  await assert.rejects((async () => {
    do { page = await readDeviceTrackCatalogPage(session, cursor); cursor = page.cursor; } while (cursor);
  })(), error => error.code === MANAGEMENT_STATUS.STORAGE_UNAVAILABLE);
  console.log('LINUX_TRACK_CATALOG_FORMAL_CLIENT_PASSED');
} finally {
  if (locker) { locker.stdin.end(); await once(locker, 'exit'); }
  await client?.connection.close();
  await app?.stop();
  await rm(root, {recursive: true, force: true});
}
