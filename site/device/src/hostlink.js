// HostLink v1: matches modules/core_hostlink, including little-endian CRC.
export const MAX_PAYLOAD = 512;
export const FRAME = Object.freeze({HELLO: 1, HELLO_ACK: 2, ACK: 3,
  GET_CONFIG: 0x11, SET_CONFIG: 0x12, STATUS: 0x82,
  MANAGEMENT_REQUEST: 0x20, MANAGEMENT_RESPONSE: 0x21, MANAGEMENT_EVENT: 0x87});
export const CAPABILITY = Object.freeze({CONFIG: 1 << 1, MANAGEMENT: 1 << 10, CHANNEL_POSITION_PRECISION: 1 << 11, SHARED_SIGNATURE_RECOVERY: 1 << 12, SHARED_CONFIRMATION_RECOVERY: 1 << 13, AUTO_INTERFACE_IFAC: 1 << 14});

export function crc16(bytes) {
  let crc = 0xffff;
  for (const byte of bytes) {
    crc ^= byte << 8;
    for (let bit = 0; bit < 8; bit++) crc = ((crc << 1) ^ (crc & 0x8000 ? 0x1021 : 0)) & 0xffff;
  }
  return crc;
}

export function encodeFrame(type, sequence, payload = new Uint8Array()) {
  if (!Number.isInteger(type) || type < 0 || type > 255 ||
      !Number.isInteger(sequence) || sequence < 0 || sequence > 65535 ||
      !(payload instanceof Uint8Array) || payload.length > MAX_PAYLOAD) throw new RangeError('Invalid HostLink frame');
  const bytes = new Uint8Array(10 + payload.length), data = new DataView(bytes.buffer);
  bytes.set([0x48, 0x4c, 1, type]);
  data.setUint16(4, sequence, true); data.setUint16(6, payload.length, true);
  bytes.set(payload, 8);
  data.setUint16(bytes.length - 2, crc16(bytes.subarray(0, -2)), true);
  return bytes;
}

// Incrementally resynchronises after noise, invalid versions and corrupt frames.
// Storage is bounded to one maximum frame even for an arbitrarily large read.
export class FrameDecoder {
  #bytes = [];
  reset() { this.#bytes.length = 0; }
  get bufferedBytes() { return this.#bytes.length; }
  push(bytes) {
    if (!(bytes instanceof Uint8Array)) throw new TypeError('Expected serial bytes');
    const frames = [];
    for (const byte of bytes) {
      this.#bytes.push(byte);
      while (this.#bytes.length) {
        const b = this.#bytes;
        if (b[0] !== 0x48 || (b.length > 1 && b[1] !== 0x4c)) { b.shift(); continue; }
        if (b.length < 8) break;
        const length = b[6] | b[7] << 8;
        if (b[2] !== 1 || length > MAX_PAYLOAD) { b.shift(); continue; }
        const total = length + 10;
        if (b.length < total) break;
        const raw = Uint8Array.from(b.slice(0, total));
        if (crc16(raw.subarray(0, -2)) !== (raw[total - 2] | raw[total - 1] << 8)) { b.shift(); continue; }
        frames.push({type: b[3], sequence: b[4] | b[5] << 8, payload: raw.slice(8, -2)});
        b.splice(0, total);
      }
    }
    return frames;
  }
}

export function decodeHello(payload) {
  if (!(payload instanceof Uint8Array) || payload.length < 10) throw new Error('Truncated HostLink handshake');
  const data = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  const version = data.getUint16(0, true), maxPayload = data.getUint16(2, true), capabilities = data.getUint32(4, true);
  if (version !== 1 || maxPayload < 1 || maxPayload > MAX_PAYLOAD) throw new Error('Unsupported HostLink handshake');
  const decoder = new TextDecoder('utf-8', {fatal: true});
  let offset = 8;
  const string = () => {
    if (offset >= payload.length) throw new Error('Truncated HostLink identity');
    const length = payload[offset++];
    if (offset + length > payload.length) throw new Error('Truncated HostLink identity');
    const value = decoder.decode(payload.subarray(offset, offset + length)); offset += length;
    return value;
  };
  const model = string(), firmware = string();
  if (offset !== payload.length) throw new Error('Trailing HostLink handshake bytes');
  return Object.freeze({version, maxPayload, capabilities, model, firmware});
}

export class HostlinkError extends Error {
  constructor(message, code) { super(message); this.name = 'HostlinkError'; this.code = code; }
}

// The caller obtains a port in a user gesture using navigator.serial.requestPort().
// This class never auto-prompts or infers author identity from USB identifiers.
export class HostlinkConnection {
  #port; #reader; #writer; #readTask; #pending = new Map(); #listeners = new Set();
  #decoder = new FrameDecoder(); #sequence = 0; #state = 'disconnected'; #closing;
  #writes = Promise.resolve(); #hello;
  constructor(port, {timeoutMs = 5000, onListenerError = () => {}} = {}) {
    if (!port || !Number.isFinite(timeoutMs) || timeoutMs <= 0) throw new TypeError('Invalid serial connection');
    this.#port = port; this.timeoutMs = timeoutMs; this.onListenerError = onListenerError;
  }
  get state() { return this.#state; }
  get hello() { return this.#hello; }
  subscribe(listener) { this.#listeners.add(listener); return () => this.#listeners.delete(listener); }
  #emit(event) {
    for (const listener of this.#listeners) { try { listener(event); } catch (error) { this.onListenerError(error); } }
  }
  #fail(error) {
    for (const pending of this.#pending.values()) { clearTimeout(pending.timer); pending.reject(error); }
    this.#pending.clear();
  }
  async open({baudRate = 115200} = {}) {
    if (this.#state !== 'disconnected') throw new HostlinkError('Serial connection is already open', 'BUSY');
    this.#state = 'connecting'; this.#emit({type: 'state', state: this.#state});
    try {
      await this.#port.open({baudRate, bufferSize: 2048});
      // Arduino TinyUSB reports CONNECTED only when both DTR and RTS are set.
      await this.#port.setSignals({dataTerminalReady: true, requestToSend: true});
      if (!this.#port.readable || !this.#port.writable) throw new HostlinkError('Serial streams unavailable', 'DISCONNECTED');
      this.#reader = this.#port.readable.getReader(); this.#writer = this.#port.writable.getWriter();
      this.#readTask = this.#read();
      const frame = await this.request(FRAME.HELLO, new Uint8Array(), {responseType: FRAME.HELLO_ACK});
      this.#hello = decodeHello(frame.payload);
      this.#state = 'ready'; this.#emit({type: 'state', state: 'ready', hello: this.#hello});
      return this.#hello;
    } catch (error) { await this.close(); throw error; }
  }
  async #read() {
    try {
      while (this.#reader) {
        const {value, done} = await this.#reader.read();
        if (done) break;
        for (const frame of this.#decoder.push(value)) {
          const pending = this.#pending.get(frame.sequence);
          if (pending && frame.type === FRAME.ACK) {
            if (frame.payload.length !== 1) this.#settle(frame.sequence, new HostlinkError('Invalid command acknowledgement', 'PROTOCOL'));
            else if (frame.payload[0] !== 0) this.#settle(frame.sequence, new HostlinkError('Device rejected command', frame.payload[0]));
            else if (pending.responseType === FRAME.ACK) this.#settle(frame.sequence, null, frame);
          } else if (pending && frame.type === pending.responseType) this.#settle(frame.sequence, null, frame);
          this.#emit({type: 'frame', frame});
        }
      }
    } catch (error) {
      if (this.#state !== 'closing') this.#emit({type: 'error', error});
    } finally {
      this.#fail(new HostlinkError('Serial connection ended', 'DISCONNECTED'));
      if (this.#state !== 'closing') {
        // Start cleanup without awaiting our own read task. The public
        // disconnected event is emitted only after both stream locks release.
        void this.close();
      }
    }
  }
  #settle(sequence, error, frame) {
    const pending = this.#pending.get(sequence);
    if (!pending) return;
    clearTimeout(pending.timer); this.#pending.delete(sequence);
    if (error) pending.reject(error); else pending.resolve(frame);
  }
  request(type, payload = new Uint8Array(), {responseType = FRAME.ACK, timeoutMs = this.timeoutMs, sensitive = false} = {}) {
    if (!this.#writer || (this.#state !== 'ready' && !(this.#state === 'connecting' && type === FRAME.HELLO))) {
      return Promise.reject(new HostlinkError('Connect the device first', 'DISCONNECTED'));
    }
    if (!Number.isFinite(timeoutMs) || timeoutMs <= 0) return Promise.reject(new RangeError('Invalid request timeout'));
    if (this.#pending.size >= 16) return Promise.reject(new HostlinkError('Too many pending serial requests', 'BUSY'));
    // Do not reuse a sequence within a connection: a late response must never
    // resolve a different request after wraparound. Reconnect at exhaustion.
    if (this.#sequence >= 65535) return Promise.reject(new HostlinkError('Reconnect to renew request sequences', 'EXHAUSTED'));
    const sequence = ++this.#sequence;
    let bytes;
    try {
      if (this.#hello && payload.length > this.#hello.maxPayload) throw new RangeError('Device frame budget exceeded');
      bytes = encodeFrame(type, sequence, payload);
    } catch (error) { return Promise.reject(error); }
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => this.#settle(sequence, new HostlinkError('Device response timed out', 'TIMEOUT')), timeoutMs);
      this.#pending.set(sequence, {resolve, reject, timer, responseType});
      this.#writes = this.#writes.catch(() => {}).then(async () => {
        try {
        if (!this.#pending.has(sequence)) return;
        if (!this.#writer) throw new HostlinkError('Serial connection ended', 'DISCONNECTED');
        await this.#writer.ready; await this.#writer.write(bytes);
        } finally { if(sensitive)bytes.fill(0); }
      }).catch(error => this.#settle(sequence, error));
    });
  }
  close() {
    if (this.#closing) return this.#closing;
    this.#closing = this.#close().finally(() => { this.#closing = undefined; });
    return this.#closing;
  }
  async #close() {
    this.#state = 'closing'; this.#fail(new HostlinkError('Serial connection closed', 'DISCONNECTED'));
    try { await this.#reader?.cancel(); } catch {}
    await this.#readTask;
    this.#reader?.releaseLock(); this.#reader = undefined;
    try { await this.#writer?.abort(); } catch {}
    await this.#writes;
    this.#writer?.releaseLock(); this.#writer = undefined;
    try { await this.#port.setSignals({dataTerminalReady: false, requestToSend: false}); } catch {}
    try { await this.#port.close(); } catch {}
    this.#decoder.reset(); this.#hello = undefined; this.#sequence = 0;
    this.#state = 'disconnected'; this.#emit({type: 'state', state: 'disconnected'});
  }
}
