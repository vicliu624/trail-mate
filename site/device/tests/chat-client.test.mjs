import test from 'node:test';
import assert from 'node:assert/strict';
import {ChatClient, AppDataAssembler, decodeAppChunk, decodeTextEvent, decodeTxResult, decodeTeamChat} from '../src/chat-client.js';
import {FRAME} from '../src/hostlink.js';
import {OPERATION} from '../src/management-protocol.js';

const id = byte => new Uint8Array(16).fill(byte);
const target = [1, 0, 0, new Uint8Array()];
function fixture({sendError, persistError} = {}) {
  let listener;
  const calls = [], persisted = [], store = id(1), context = id(2), owner = id(3);
  const connection = {subscribe: fn => {listener = fn; return () => {listener = null;};}};
  const session = {id: owner, async request(code, body) {
    calls.push({code, body});
    if (code === OPERATION.CHAT_CAPABILITIES) return [1, store, context, 1, 256, 16, 15, [new Uint8Array(8).fill(6), 1, 2]];
    const intent = persisted.at(-1);
    const hash = new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(intent.text)));
    const receipt = [intent.operation, intent.owner, hash, 1, id(9), 1, 1, 0];
    if (code === OPERATION.CHAT_SEND) {assert.equal(intent.attempted, true); if (sendError) throw sendError; return receipt;}
    if (code === OPERATION.CHAT_SEND_GET) return [0, receipt];
    throw Error('Unexpected operation');
  }};
  const client = new ChatClient(connection, session, {randomBytes: () => id(4), persistIntent: async intent => {
    if (persistError && intent.attempted) throw persistError;
    persisted.push(structuredClone(intent));
  }});
  return {client, calls, persisted, store, context, owner, emit: event => listener?.(event), session};
}
test('persists intent and no-retry fence before one correlated send', async () => {
  const f = fixture(), intent = await f.client.prepare(target, 'hello');
  assert.equal(f.persisted.length, 1);
  const result = await f.client.send(intent);
  assert.equal(result.submission, 'submitted'); assert.equal(result.delivery, 'queued');
  assert.equal(f.calls.filter(call => call.code === OPERATION.CHAT_SEND).length, 1);
  await assert.rejects(f.client.send(intent), {code: 'RECOVERY_REQUIRED'});
  f.client.close();
});
test('reply loss is lookup-only, including a new session after reconnect', async () => {
  const f = fixture({sendError: Error('lost reply')}), intent = await f.client.prepare(target, 'hello');
  await assert.rejects(f.client.send(intent), /lost reply/);
  assert.equal(intent.attempted, true);
  f.session.id = id(8);
  const recovered = await f.client.recover(intent);
  assert.equal(recovered.submission, 'submitted');
  assert.deepEqual(f.calls.at(-1).body[1], id(3));
  assert.equal(f.calls.filter(call => call.code === OPERATION.CHAT_SEND).length, 1);
  f.client.close();
});
test('failed durable fence causes no transport send', async () => {
  const f = fixture({persistError: Error('quota exceeded')}), intent = await f.client.prepare(target, 'hello');
  await assert.rejects(f.client.send(intent), /quota/);
  assert.equal(intent.attempted, true);
  assert.equal(f.calls.filter(call => call.code === OPERATION.CHAT_SEND).length, 0);
  f.client.close();
});
test('rejects another device operation store without querying or replaying', async () => {
  const f = fixture(), intent = await f.client.prepare(target, 'hello');
  f.store[0] = 7;
  await assert.rejects(f.client.recover(intent), {code: 'STALE_STORE'});
  assert.equal(f.calls.filter(call => call.code === OPERATION.CHAT_SEND_GET).length, 0);
  f.client.close();
});
test('byte limits, full target identity and unpaired UTF-16 are enforced', async () => {
  const f = fixture();
  await assert.rejects(f.client.prepare(target, '中'.repeat(86)), /text/);
  await assert.rejects(f.client.prepare(target, '\ud800'), /text/);
  await assert.rejects(f.client.prepare([1, 0, 1, new Uint8Array(4)], 'hello'), /target/);
  await assert.rejects(f.client.prepare([2, 0, 1, new Uint8Array(4)], 'hello'), /target/);
  assert.equal(f.persisted.length, 0); f.client.close();
});
test('RX text and legacy result parse strictly without claiming remote delivery', () => {
  const bytes = new Uint8Array(24), view = new DataView(bytes.buffer);
  view.setUint32(0, 123, true); view.setUint16(17, 5, true); bytes.set(new TextEncoder().encode('hello'), 19);
  assert.equal(decodeTextEvent(bytes).text, 'hello');
  assert.throws(() => decodeTextEvent(bytes.subarray(0, 23)), /Truncated/);
  const result = decodeTxResult(Uint8Array.of(123, 0, 0, 0, 1));
  assert.equal(result.success, true); assert.equal(result.delivery, undefined);
  assert.throws(() => decodeTxResult(Uint8Array.of(1, 0, 0, 0, 2)), /Invalid/);
});
function app(offset, body, total = 5, packetId = 7) {
  const bytes = new Uint8Array(40 + body.length + 6), view = new DataView(bytes.buffer);
  view.setUint32(0, 303, true); view.setUint32(4, 123, true);
  bytes[13] = offset + body.length < total ? 8 : 0;
  view.setUint32(30, total, true); view.setUint32(34, offset, true); view.setUint16(38, body.length, true);
  bytes.set(body, 40); bytes.set([15, 4, packetId, 0, 0, 0], 40 + body.length);
  return decodeAppChunk(bytes);
}
test('bounded assembler reconstructs Team body and rejects missing/overlapping chunks', () => {
  const assembler = new AppDataAssembler();
  assert.equal(assembler.accept(app(0, [1, 2])), null);
  assert.deepEqual(assembler.accept(app(2, [3, 4, 5])).payload, Uint8Array.of(1, 2, 3, 4, 5));
  assert.throws(() => assembler.accept(app(2, [3, 4, 5])), /missing/);
  assembler.accept(app(0, [1, 2]));
  assert.throws(() => assembler.accept(app(1, [2, 3, 4, 5])), /missing/);
});
test('packet IDs separate interleaved bodies and disconnect clears partial state', () => {
  let now = 0;
  const assembler = new AppDataAssembler({maxStreams: 1, ttlMs: 10, now: () => now});
  assembler.accept(app(0, [1, 2]));
  assert.throws(() => assembler.accept(app(0, [1, 2], 5, 8)), {code: 'BUSY'});
  now = 11;
  assert.throws(() => assembler.accept(app(2, [3, 4, 5])), /missing/);
  assembler.accept(app(0, [1, 2])); assembler.clear();
  assert.throws(() => assembler.accept(app(2, [3, 4, 5])), /missing/);
});
test('chat listener coexists with management frames and never closes shared session', () => {
  const f = fixture(), events = [];
  f.client.subscribe(event => events.push(event));
  f.emit({type: 'frame', frame: {type: FRAME.MANAGEMENT_RESPONSE, payload: new Uint8Array()}});
  f.emit({type: 'frame', frame: {type: FRAME.TX_RESULT, payload: Uint8Array.of(1, 0, 0, 0, 1)}});
  assert.equal(events.length, 1); assert.equal(events[0].kind, 'send-result');
  f.emit({type: 'frame', frame: {type: FRAME.RX_MSG, payload: new Uint8Array()}});
  assert.equal(events.at(-1).kind, 'receive-error');
  f.client.close(); assert.equal(typeof f.session.request, 'function');
});
test('Team submission uses the full current Team ID and retains the common send path', async () => {
  const f = fixture();
  const intent = await f.client.prepare([1, 2, 2, new Uint8Array(8).fill(6)], 'Team hello');
  assert.equal((await f.client.send(intent)).submission, 'submitted');
  await assert.rejects(f.client.prepare([1, 2, 2, new Uint8Array(8).fill(7)], 'wrong Team'), {code: 'STALE_CONTEXT'});
  f.client.close();
});
test('fragmented application data without a packet ID cannot be mixed silently', () => {
  const assembler = new AppDataAssembler();
  assert.throws(() => assembler.accept(app(0, [1, 2], 5, 0)), /packet ID/);
  assembler.accept(app(0, [1, 2]));
  assert.throws(() => assembler.accept(app(0, [9, 9])), /Conflicting/);
});
test('Team text decoder preserves full wire IDs and validates Unicode and version', () => {
  const text = new TextEncoder().encode('队伍 hello'), bytes = new Uint8Array(16 + text.length), view = new DataView(bytes.buffer);
  bytes.set([1, 1]); view.setUint32(4, 0x12345678, true); view.setUint32(12, 0xa1b3b57c, true); bytes.set(text, 16);
  const decoded = decodeTeamChat(bytes);
  assert.equal(decoded.text, '队伍 hello'); assert.equal(decoded.from, 0xa1b3b57c); assert.equal(decoded.messageId, 0x12345678);
  bytes[0] = 2; assert.throws(() => decodeTeamChat(bytes), /Invalid/);
  bytes[0] = 1; bytes[16] = 0xff; assert.throws(() => decodeTeamChat(bytes));
});
