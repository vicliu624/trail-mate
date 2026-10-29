import assert from 'node:assert/strict';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {spawnSync} from 'node:child_process';
import {convertRecording} from '../../../site/tracks/src/recording-convert.js';

const directory = mkdtempSync(join(tmpdir(), 'trailmate-tracker-reader-'));
try {
  const result = spawnSync(process.argv[2], [directory], {encoding: 'utf8'});
  assert.equal(result.status, 0, result.stdout + result.stderr);
  const binary = convertRecording(new Uint8Array(readFileSync(join(directory, 'recording.bin'))), {format: 'binary'});
  assert.equal(binary.profile, 'idf-tmtrk1-le20');
  assert.equal(binary.pointCount, 1);
  const binaryXml = new TextDecoder().decode(binary.kml);
  assert.ok(binaryXml.includes('-45.1234567 12.3456789 -12'));
  assert.ok(binaryXml.includes('&quot;speed_mps&quot;:1.25'));
  const csv = convertRecording(new Uint8Array(readFileSync(join(directory, 'recording.csv'))), {format: 'csv'});
  assert.equal(csv.profile, 'idf-csv-v1');
  assert.equal(csv.pointCount, 1);
  assert.ok(new TextDecoder().decode(csv.kml).includes('-45.1234567,12.3456789'));
  const gpx = readFileSync(join(directory, 'recording.gpx'), 'utf8');
  assert.equal((gpx.match(/<trkpt /g) || []).length, 2);
  assert.equal((gpx.match(/<\/gpx>/g) || []).length, 1);
  console.log('Production recording converter accepts native CSV and TMTRK1 files');
} finally {
  rmSync(directory, {recursive: true, force: true});
}
