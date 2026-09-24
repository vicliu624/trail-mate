import {Identity, MsgPack, toHex} from '@reticulum/core';

export const APP_TYPE = 'trailmate.geocache';
export const encode = value => MsgPack.encode(value);
export const equal = (a, b) => a instanceof Uint8Array && b instanceof Uint8Array &&
  a.length === b.length && a.every((value, index) => value === b[index]);
export const bytes = (value, size) => value instanceof Uint8Array && value.length === size;
export const integer = (value, min, max) => Number.isSafeInteger(value) && value >= min && value <= max;
const utf8 = new TextEncoder();
const strictUtf8 = new TextDecoder('utf-8', {fatal: true});

// Parse only CMP1 types, bounding collections BEFORE allocation. Re-encoding
// enforces shortest integer/string/bin/array headers and rejects trailing data.
export function decode(raw, limit = 8192) {
  if (!(raw instanceof Uint8Array) || !raw.length || raw.length > limit) throw Error('Invalid payload size');
  const view = new DataView(raw.buffer, raw.byteOffset, raw.byteLength);
  let offset = 0, elements = 0;
  const take = count => {
    if (offset + count > raw.length) throw Error('Truncated CMP1');
    const start = offset;
    offset += count;
    return start;
  };
  const uint = count => {
    const start = take(count);
    return count === 1 ? view.getUint8(start) : count === 2 ? view.getUint16(start) : view.getUint32(start);
  };
  const read = depth => {
    if (depth > 8) throw Error('CMP1 nesting limit');
    const tag = uint(1);
    if (tag <= 0x7f) return tag;
    if (tag >= 0xe0) return tag - 256;
    if (tag === 0xc0) return null;
    if ([0xcc, 0xcd, 0xce].includes(tag)) return uint(2 ** (tag - 0xcc));
    if ([0xd0, 0xd1, 0xd2].includes(tag)) {
      const count = 2 ** (tag - 0xd0), start = take(count);
      return count === 1 ? view.getInt8(start) : count === 2 ? view.getInt16(start) : view.getInt32(start);
    }
    if (tag === 0xcf || tag === 0xd3) {
      const start = take(8), value = tag === 0xcf ? view.getBigUint64(start) : view.getBigInt64(start);
      return value <= BigInt(Number.MAX_SAFE_INTEGER) && value >= BigInt(Number.MIN_SAFE_INTEGER) ? Number(value) : value;
    }
    let length, kind;
    if ((tag & 0xf0) === 0x90) { length = tag & 15; kind = 'array'; }
    else if (tag === 0xdc || tag === 0xdd) { length = uint(tag === 0xdc ? 2 : 4); kind = 'array'; }
    else if ((tag & 0xe0) === 0xa0) { length = tag & 31; kind = 'string'; }
    else if (tag >= 0xd9 && tag <= 0xdb) { length = uint(2 ** (tag - 0xd9)); kind = 'string'; }
    else if (tag >= 0xc4 && tag <= 0xc6) { length = uint(2 ** (tag - 0xc4)); kind = 'binary'; }
    else throw Error('Forbidden CMP1 type');
    if (kind === 'array') {
      elements += length;
      if (elements > 2048 || length > raw.length - offset) throw Error('CMP1 collection limit');
      return Array.from({length}, () => read(depth + 1));
    }
    const start = take(length), data = raw.slice(start, start + length);
    return kind === 'string' ? strictUtf8.decode(data) : data;
  };
  const value = read(0);
  if (offset !== raw.length || !equal(encode(value), raw)) throw Error('Noncanonical CMP1');
  return value;
}

export function validText(value, limit, multiline = false, nonempty = false) {
  return typeof value === 'string' && utf8.encode(value).length <= limit &&
    (!nonempty || value.trim().length > 0) &&
    ![...value].some(c => {
      const n = c.codePointAt(0);
      return (n < 32 && !(multiline && (n === 9 || n === 10))) ||
        (n >= 127 && n <= 159) || n === 65534 || n === 65535 || (n >= 0xd800 && n <= 0xdfff);
    });
}

const concat = (...parts) => {
  const result = new Uint8Array(parts.reduce((size, part) => size + part.length, 0));
  let offset = 0;
  for (const part of parts) { result.set(part, offset); offset += part.length; }
  return result;
};

export function validateSummary(row) {
  if (!Array.isArray(row) || row.length !== 11 || !bytes(row[0], 32) || !bytes(row[2], 32) ||
      !validText(row[6], 96, false, true)) throw Error('Invalid directory summary');
  for (const [index, low, high] of [[1,1,0xffffffff],[3,0,2],[4,-900000000,900000000],
    [5,-1800000000,1799999999],[7,2,10],[8,2,10],[9,0,5],[10,1,4166]]) {
    if (!integer(row[index], low, high)) throw Error('Invalid directory summary field');
  }
  return row;
}

export async function verifySigned(signed, expectedSummary = null) {
  if (!Array.isArray(signed) || signed.length !== 2 || !(signed[0] instanceof Uint8Array) ||
      signed[0].length > 4096 || !bytes(signed[1], 64)) throw Error('Invalid signed record');
  const record = decode(signed[0], 4096);
  if (!Array.isArray(record) || record.length !== 16 || record[0] !== 1 || !bytes(record[1], 64) ||
      !bytes(record[2], 16)) throw Error('Invalid record schema');
  for (const [index, low, high] of [[3,1,0xffffffff],[5,0,2],[6,-900000000,900000000],
    [7,-1800000000,1799999999],[11,2,10],[12,2,10],[13,0,5],[14,0,253402300799],[15,0,253402300799]]) {
    if (!integer(record[index], low, high)) throw Error('Invalid signed field');
  }
  if ((record[3] === 1 ? record[4] !== null : !bytes(record[4], 32)) ||
      !validText(record[8], 96, false, true) || !validText(record[9], 2048, true) ||
      !validText(record[10], 512, true)) throw Error('Invalid record text or predecessor');
  const author = await Identity.fromPublicKey(record[1]);
  if (!await author.validate(signed[1], concat(utf8.encode('trailmate.geocache/sign/v1\0'), signed[0]))) {
    throw Error('Author signature verification failed');
  }
  const cacheId = await Identity.fullHash(concat(utf8.encode('trailmate.geocache/id/v1\0'), record[1], record[2]));
  const revisionHash = await Identity.fullHash(concat(utf8.encode('trailmate.geocache/revision/v1\0'), signed[0]));
  const summary = [cacheId, record[3], revisionHash, record[5], record[6], record[7], record[8],
    record[11], record[12], record[13], encode(signed).length];
  if (expectedSummary && !equal(encode(expectedSummary), encode(summary))) throw Error('Directory summary differs from signed object');
  return {record, signed, summary, cacheId: toHex(cacheId), revisionHash: toHex(revisionHash),
    authorHash: toHex(await Identity.fullHash(record[1]))};
}

export function viewportBoxes([south, west, north, east]) {
  if (![south, west, north, east].every(Number.isFinite) || south > north || west > east) throw Error('Invalid map bounds');
  const latitude = n => Math.round(Math.max(-90, Math.min(90, n)) * 1e7);
  south = latitude(south); north = latitude(north);
  if (east - west >= 360) return [[south, -1800000000, north, 1800000000]];
  const span = east - west;
  west = ((west + 180) % 360 + 360) % 360 - 180;
  east = west + span;
  return east <= 180 ? [[south, Math.round(west * 1e7), north, Math.round(east * 1e7)]] :
    [[south, Math.round(west * 1e7), north, 1800000000], [south, -1800000000, north, Math.round((east - 360) * 1e7)]];
}
