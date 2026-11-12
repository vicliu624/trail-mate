import {FRAME, HostlinkError} from './hostlink.js';
import {OPERATION} from './management-protocol.js';

const encoder = new TextEncoder(), decoder = new TextDecoder('utf-8', {fatal: true});
const isBytes = (value, length) => value instanceof Uint8Array && value.length === length;
const same = (a, b) => a instanceof Uint8Array && b instanceof Uint8Array && a.length === b.length && a.every((v, i) => v === b[i]);
const nonzero = value => value.some(byte => byte !== 0);
const protocolError = message => new HostlinkError(message, 'PROTOCOL');
const copy = value => value instanceof Uint8Array ? value.slice() : Array.isArray(value) ? value.map(copy) : value;
const hex = value => Array.from(value, byte => byte.toString(16).padStart(2, '0')).join('');
const u32 = (data, offset) => data.getUint32(offset, true);
function frameView(payload, minimum) {
  if (!(payload instanceof Uint8Array) || payload.length < minimum || payload.length > 512) throw protocolError('Invalid chat event length');
  return new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
}
export function decodeRxMetadata(payload) {
  const fields = new Map();
  for (let offset = 0; offset < payload.length;) {
    if (payload.length - offset < 2) throw protocolError('Truncated RX metadata');
    const key = payload[offset++], size = payload[offset++];
    if (size > payload.length - offset || fields.has(key)) throw protocolError('Invalid RX metadata');
    fields.set(key, payload.slice(offset, offset + size)); offset += size;
  }
  return fields;
}
export function decodeTextEvent(payload) {
  const data = frameView(payload, 19), length = data.getUint16(17, true);
  if (length > payload.length - 19) throw protocolError('Truncated text event');
  return {kind: 'message', messageId: u32(data, 0), from: u32(data, 4), to: u32(data, 8),
    channel: payload[12], timestamp: u32(data, 13), text: decoder.decode(payload.subarray(19, 19 + length)),
    metadata: decodeRxMetadata(payload.subarray(19 + length))};
}
export function decodeTxResult(payload) {
  const data = frameView(payload, 5);
  if (payload.length !== 5 || payload[4] > 1) throw protocolError('Invalid send result');
  // This legacy boolean never proves remote delivery. Use receipt lookup for
  // the protocol's actual Queued/Sent/Delivered/Failed state.
  return {kind: 'send-result', messageId: u32(data, 0), success: payload[4] === 1};
}
export function decodeAppChunk(payload) {
  const data = frameView(payload, 40), total = u32(data, 30), offset = u32(data, 34), size = data.getUint16(38, true);
  if (offset > total || size > total - offset || size > payload.length - 40 ||
      (!!(payload[13] & 8) !== (offset + size < total)) || (size === 0 && total !== 0)) throw protocolError('Invalid application chunk');
  const metadata = decodeRxMetadata(payload.subarray(40 + size));
  const packetId = metadata.get(15);
  if (packetId && packetId.length !== 4) throw protocolError('Invalid packet ID metadata');
  return {kind: 'app-chunk', port: u32(data, 0), from: u32(data, 4), to: u32(data, 8), channel: payload[12],
    flags: payload[13], teamId: payload.slice(14, 22), teamKeyId: u32(data, 22), timestamp: u32(data, 26),
    total, offset, chunk: payload.slice(40, 40 + size), metadata,
    packetId: packetId ? u32(new DataView(packetId.buffer, packetId.byteOffset, packetId.byteLength), 0) : null};
}
export function decodeTeamChat(payload) {
  if (!(payload instanceof Uint8Array) || payload.length < 16 || payload[0] !== 1 || ![1, 2, 3].includes(payload[1])) throw protocolError('Invalid Team chat body');
  const data = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  return {version: 1, type: payload[1], flags: data.getUint16(2, true), messageId: u32(data, 4), timestamp: u32(data, 8),
    from: u32(data, 12), payload: payload.slice(16), ...(payload[1] === 1 ? {text: decoder.decode(payload.subarray(16))} : {})};
}

// At most one in-flight body per stream key, four streams, and 16 KiB per
// body. Expired, missing-first, overlapping and conflicting chunks are lost
// explicitly; they are never concatenated into a plausible wrong message.
export class AppDataAssembler {
  #pending = new Map();
  constructor({maxBytes = 16384, maxStreams = 4, ttlMs = 5000, now = () => Date.now()} = {}) {
    if (!Number.isSafeInteger(maxBytes) || maxBytes < 1 || maxBytes > 65536 || !Number.isSafeInteger(maxStreams) ||
        maxStreams < 1 || maxStreams > 8 || !Number.isFinite(ttlMs) || ttlMs <= 0) throw new TypeError('Invalid application receive limits');
    Object.assign(this, {maxBytes, maxStreams, ttlMs, now});
  }
  clear() { this.#pending.clear(); }
  accept(chunk) {
    const now = this.now();
    for (const [key, state] of this.#pending) if (now - state.time >= this.ttlMs) this.#pending.delete(key);
    if (chunk.total > this.maxBytes) throw new HostlinkError('Application body exceeds receive limit', 'LIMIT_EXCEEDED');
    if (chunk.total > chunk.chunk.length && (!chunk.packetId || chunk.packetId === 0)) throw protocolError('Chunked application body has no unique packet ID');
    const key = [chunk.port, chunk.from, chunk.to, chunk.channel, hex(chunk.teamId), chunk.teamKeyId, chunk.timestamp, chunk.packetId].join(':');
    if (chunk.offset === 0) {
      if (this.#pending.has(key)) { this.#pending.delete(key); throw protocolError('Conflicting first application chunk'); }
      if (this.#pending.size >= this.maxStreams) throw new HostlinkError('Application receive slots are full', 'BUSY');
      this.#pending.set(key, {bytes: new Uint8Array(chunk.total), offset: 0, time: now, flags: chunk.flags & 7});
    }
    const state = this.#pending.get(key);
    if (!state || state.offset !== chunk.offset || state.bytes.length !== chunk.total || state.flags !== (chunk.flags & 7)) {
      this.#pending.delete(key); throw protocolError('Application chunks are missing or inconsistent');
    }
    state.bytes.set(chunk.chunk, chunk.offset); state.offset += chunk.chunk.length; state.time = now;
    if (state.offset < state.bytes.length) return null;
    this.#pending.delete(key);
    const {offset, chunk: ignored, total, ...message} = chunk;
    return {...message, kind: 'app-message', payload: state.bytes};
  }
}

function capabilities(row) {
  if (!Array.isArray(row) || row.length !== 8 || row[0] !== 1 || !isBytes(row[1], 16) || !nonzero(row[1]) ||
      !isBytes(row[2], 16) || !Number.isInteger(row[3]) || row[3] < 1 || row[3] > 4 ||
      !Number.isInteger(row[4]) || row[4] < 1 || row[4] > 256 || !Number.isInteger(row[5]) || row[5] < 1 || row[5] > 16 || row[6] !== 15 ||
      !(row[7] === null || Array.isArray(row[7]) && row[7].length === 3 && isBytes(row[7][0], 8) && nonzero(row[7][0]) &&
        Number.isInteger(row[7][1]) && row[7][1] >= 0 && row[7][1] <= 0xffffffff && row[7][2] === 2)) throw protocolError('Invalid chat capabilities');
  return {version: row[0], store: row[1].slice(), context: row[2].slice(), protocol: row[3], maxTextBytes: row[4], retainedOperations: row[5], features: row[6], team: copy(row[7])};
}
async function receipt(row, intent) {
  if (!Array.isArray(row) || row.length !== 8 || !same(row[0], intent.operation) || !same(row[1], intent.owner) ||
      !isBytes(row[2], 32) || !Number.isInteger(row[3]) || row[3] < 0 || row[3] > 3 ||
      !(row[4] === null || isBytes(row[4], 16)) || !Number.isInteger(row[5]) || row[5] < 0 || row[5] > 4 ||
      !Number.isInteger(row[6]) || row[6] < 0 || row[6] > 0xffffffff || !Number.isInteger(row[7]) || row[7] < 0 || row[7] > 65535) throw protocolError('Invalid chat receipt');
  const hash = new Uint8Array(await crypto.subtle.digest('SHA-256', encoder.encode(intent.text)));
  if (!same(hash, row[2])) throw protocolError('Chat receipt text does not match the retained intent');
  return {operation: row[0], owner: row[1], textHash: row[2], submission: ['reserved', 'submitted', 'rejected', 'uncertain'][row[3]],
    messageRef: row[4], delivery: ['unknown', 'queued', 'sent', 'delivered', 'failed'][row[5]], revision: row[6], failure: row[7]};
}
function validateTarget(target, protocol) {
  if (!Array.isArray(target) || target.length !== 4 || target[0] !== protocol ||
      !Number.isInteger(target[1]) || target[1] < 0 || target[1] >= (target[2] === 2 ? 3 : protocol === 2 ? 8 : 2) ||
      ![0, 1, 2].includes(target[2])) throw new TypeError('Invalid chat target');
  const size = target[2] === 0 ? 0 : target[2] === 2 ? 8 : protocol === 1 ? 4 : protocol === 4 ? 16 : -1;
  if (!isBytes(target[3], size) || size > 0 && !nonzero(target[3]) || protocol === 3 || protocol === 4 && target[2] === 0 ||
      target[2] === 2 && target[1] !== 2) throw new TypeError('Unsupported chat target');
}

// Share the caller's existing connection AND management session. A chat
// consumer must never close the shared session used by settings/GC/routes.
export class ChatClient {
  #connection; #session; #persist; #random; #unsubscribe; #caps; #pending = false; #closed = false; #listeners = new Set(); #assembler;
  constructor(connection, session, {persistIntent, randomBytes = size => crypto.getRandomValues(new Uint8Array(size)), receiveLimits} = {}) {
    this.#connection = connection; this.#session = session; this.#persist = persistIntent; this.#random = randomBytes;
    this.#assembler = new AppDataAssembler(receiveLimits);
    this.#unsubscribe = connection.subscribe(event => {
      if (event.type === 'state' && event.state === 'disconnected') { this.#assembler.clear(); this.#emit({kind: 'disconnected'}); }
      if (event.type !== 'frame') return;
      try {
        const frame = event.frame;
        if (frame.type === FRAME.RX_MSG) this.#emit(decodeTextEvent(frame.payload));
        if (frame.type === FRAME.TX_RESULT) this.#emit(decodeTxResult(frame.payload));
        if (frame.type === FRAME.APP_DATA) {
          const body = this.#assembler.accept(decodeAppChunk(frame.payload));
          if (body) {
            if (body.port === 303 && body.flags & 1) body.teamChat = decodeTeamChat(body.payload);
            this.#emit(body);
          }
        }
        if (frame.type === FRAME.TEAM_STATE) this.#emit({kind: 'team-state', payload: frame.payload.slice()});
      } catch (error) { this.#emit({kind: 'receive-error', error}); }
    });
  }
  subscribe(listener) { this.#listeners.add(listener); return () => this.#listeners.delete(listener); }
  #emit(event) { for (const listener of this.#listeners) { try { listener(event); } catch (error) { this.#connection.onListenerError?.(error); } } }
  async capabilities() {
    if (this.#closed) throw new HostlinkError('Chat client closed', 'DISCONNECTED');
    this.#caps = capabilities(await this.#session.request(OPERATION.CHAT_CAPABILITIES, []));
    return {...this.#caps, store: this.#caps.store.slice(), context: this.#caps.context.slice(), team: copy(this.#caps.team)};
  }
  async prepare(target, text) {
    if (typeof this.#persist !== 'function') throw new TypeError('Provide durable persistIntent before sending');
    const caps = await this.capabilities();
    validateTarget(target, caps.protocol);
    if (target[2] === 2 && (!caps.team || !same(target[3], caps.team[0]))) throw new HostlinkError('The requested Team is not active', 'STALE_CONTEXT');
    if (typeof text !== 'string' || text.includes('\0') || !text.length || encoder.encode(text).length > caps.maxTextBytes || decoder.decode(encoder.encode(text)) !== text) throw new TypeError('Invalid chat text');
    const operation = this.#random(16);
    if (!isBytes(operation, 16) || !nonzero(operation)) throw new TypeError('Invalid operation ID');
    const intent = {version: 1, store: caps.store.slice(), context: caps.context.slice(), owner: this.#session.id,
      operation: operation.slice(), target: copy(target), text, attempted: false};
    await this.#persist(structuredClone(intent));
    return intent;
  }
  async send(intent) {
    if (this.#closed) throw new HostlinkError('Chat client closed', 'DISCONNECTED');
    if (this.#pending) throw new HostlinkError('Wait for the current chat submission', 'BUSY');
    if (!intent || intent.version !== 1 || !same(intent.owner, this.#session.id) || intent.attempted !== false ||
        !isBytes(intent.store, 16) || !isBytes(intent.context, 16) || !isBytes(intent.operation, 16) ||
        typeof this.#persist !== 'function') throw new HostlinkError('Recover this intent using lookup; do not resend it', 'RECOVERY_REQUIRED');
    validateTarget(intent.target, this.#caps?.protocol);
    if (!same(intent.store, this.#caps.store) || !same(intent.context, this.#caps.context)) throw new HostlinkError('Refresh the changed chat context', 'STALE_CONTEXT');
    this.#pending = true;
    try {
      // Persist the no-retry fence BEFORE asking the transport to write. Even
      // a failed/uncertain transport request must be recovered by lookup only.
      intent.attempted = true;
      const reserved = structuredClone(intent);
      await this.#persist(reserved);
      const row = await this.#session.request(OPERATION.CHAT_SEND,
        [[reserved.store, reserved.context], reserved.operation, reserved.target, reserved.text]);
      const result = await receipt(row, reserved);
      await this.#persist({...reserved, receipt: result});
      return result;
    } finally { this.#pending = false; }
  }
  async recover(intent) {
    const caps = await this.capabilities();
    if (!same(intent.store, caps.store)) throw new HostlinkError('This device has a different chat operation store', 'STALE_STORE');
    const row = await this.#session.request(OPERATION.CHAT_SEND_GET, [intent.store, intent.owner, intent.operation]);
    if (!Array.isArray(row) || row.length !== 2 || ![0, 1].includes(row[0]) || row[0] === 1 && row[1] !== null) throw protocolError('Invalid chat lookup');
    // Not retained is deliberately not translated into "not sent".
    return row[0] === 0 ? receipt(row[1], intent) : {submission: 'history-unavailable'};
  }
  close() { if (this.#closed) return; this.#closed = true; this.#unsubscribe(); this.#assembler.clear(); this.#listeners.clear(); }
}
