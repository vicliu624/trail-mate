import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {test} from 'node:test';
import {decode, encode, verifySigned, viewportBoxes} from '../../site/geocaching/src/protocol.js';
import {makeGpx} from '../../site/geocaching/src/gpx-download.js';
import {compressionProvider} from '../../site/geocaching/src/compression.js';

const fixtures = new URL('../../modules/core_geocaching/tests/fixtures/', import.meta.url);
const signed = decode(await readFile(new URL('signed-cache-v1.bin', fixtures)));

test('browser verifies the independent checked-in author signature and directory summary', async () => {
  const response = decode(await readFile(new URL('query-response-v1.bin', fixtures)));
  const verified = await verifySigned(signed, response[5][1][0]);
  assert.equal(verified.record[8], 'Test');
  assert.equal(verified.cacheId.length, 64);
  const altered = [signed[0], Uint8Array.from(signed[1])]; altered[1][0] ^= 1;
  await assert.rejects(verifySigned(altered), /signature/);
  const wrongSummary = [...response[5][1][0]]; wrongSummary[6] = 'Changed name';
  await assert.rejects(verifySigned(signed, wrongSummary), /summary differs/);
});

test('CMP1 rejects over-allocation, trailing bytes, floats, maps, bools and nonminimal integers', () => {
  for (const raw of [[0xdd,255,255,255,255],[0x90,0],[0xc3],[0x80],[0xcc,1],[0xd9,1,0xff]]) {
    assert.throws(() => decode(Uint8Array.from(raw)));
  }
  const maxSequence = 0xffffffffffffffffn;
  assert.equal(decode(encode(maxSequence)), maxSequence);
  assert.equal(decode(encode(253402300799)), 253402300799);
});

test('viewports split the date line, normalize world copies and retain polar coordinates', () => {
  assert.deepEqual(viewportBoxes([-90,170,90,190]), [[-900000000,1700000000,900000000,1800000000],[-900000000,-1800000000,900000000,-1700000000]]);
  assert.deepEqual(viewportBoxes([-100,-400,100,400]), [[-900000000,-1800000000,900000000,1800000000]]);
  assert.deepEqual(viewportBoxes([0,370,10,380]), [[0,100000000,100000000,200000000]]);
});

test('GPX retains signed original, full ID, Groundspeak fields and no fabricated platform ID', async () => {
  const gpx = await makeGpx([signed]);
  assert.match(gpx, /xmlns="http:\/\/www.topografix.com\/GPX\/1\/1"/);
  assert.match(gpx, /<groundspeak:cache available="True" archived="False">/);
  assert.match(gpx, /<tm:payload encoding="base64">/);
  assert.ok(gpx.includes(Buffer.from(signed[0]).toString('base64')));
  assert.ok(gpx.includes(Buffer.from(signed[1]).toString('base64')));
  assert.ok(gpx.includes((await verifySigned(signed)).cacheId));
  assert.doesNotMatch(gpx, /groundspeak:cache id=|<groundspeak:owner/);
  await assert.rejects(makeGpx(Array(21).fill(signed)), /1–20/);
});

test('bzip2 provider refuses oversized advertised buffers before decompression', () => {
  assert.throws(() => compressionProvider.decompress(new Uint8Array(20), 16385), /limit/);
  assert.throws(() => compressionProvider.decompress(new Uint8Array(16385), 100), /limit/);
});

test('Python bzip2 stream expands to exactly 8192 bytes and rejects a false size', () => {
  const compressed = Uint8Array.from(Buffer.from('QlpoOTFBWSZTWabbZGsAABIkAIAEIAAACCAAMMwFU2piBQDxdyRThQkKbbZGsA==','base64'));
  const decoded = compressionProvider.decompress(compressed,8192);
  assert.equal(decoded.length,8192);
  assert.ok(decoded.every(value=>value===65));
  assert.throws(()=>compressionProvider.decompress(compressed,8191),/expands/);
  assert.throws(()=>compressionProvider.decompress(compressed,8193),/size mismatch/);
});
