import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {fileURLToPath} from 'node:url';

// Web Serial compatible adapter: the CLI and browser use precisely the same
// HostlinkConnection, management correlation and chat client implementations.
export class PythonSerialPort {
  #child; #ended; #pending = new Map(); #sequence = 0; #controller; #lines; #closing = false;
  constructor(python, port) { this.python = python; this.port = port; }
  async open({baudRate}) {
    if (this.#child) throw Error('Serial port already open');
    this.#child = spawn(this.python, [fileURLToPath(new URL('./serial_stream.py', import.meta.url)), this.port, String(baudRate)],
      {stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true});
    const child = this.#child;
    this.#ended = new Promise(resolve => child.once('close', resolve));
    this.readable = new ReadableStream({start: controller => {this.#controller = controller;},
      cancel: () => {this.#controller = null; child.stdin.end();}});
    this.writable = new WritableStream({write: bytes => this.#request({write: Buffer.from(bytes).toString('hex')}),
      abort: () => {child.stdin.end();}});
    this.#lines = createInterface({input: child.stdout});
    let readyResolve, readyReject;
    const ready = new Promise((resolve, reject) => {readyResolve = resolve; readyReject = reject;});
    const fail = error => {
      readyReject(error);
      for (const pending of this.#pending.values()) {clearTimeout(pending.timer); pending.reject(error);}
      this.#pending.clear();
      if (this.#controller) {this.#controller.error(error); this.#controller = null;}
    };
    child.on('error', fail); child.stdin.on('error', fail);
    child.stderr.on('data', () => {});
    child.once('close', code => {
      if (!this.#closing) fail(Error(`Serial helper ended (${code})`));
      if (this.#controller) {this.#controller.close(); this.#controller = null;}
    });
    this.#lines.on('line', line => {
      try {
        if (line.length > 4096) throw Error('Serial helper response exceeds bound');
        const value = JSON.parse(line);
        if (value.error) throw Error(value.error);
        if (value.ready) {readyResolve(); return;}
        if (typeof value.data === 'string') {
          if (!/^(?:[0-9a-f]{2}){1,512}$/.test(value.data)) throw Error('Invalid serial stream bytes');
          if (this.#controller && this.#controller.desiredSize < -128) throw Error('Serial receive consumer stalled');
          this.#controller?.enqueue(new Uint8Array(Buffer.from(value.data, 'hex'))); return;
        }
        const pending = this.#pending.get(value.id);
        if (pending && value.ok) {clearTimeout(pending.timer); this.#pending.delete(value.id); pending.resolve();}
      } catch (error) {fail(error);}
    });
    const timer = setTimeout(() => readyReject(Error('Serial helper startup timed out')), 8000);
    try {await ready;} catch (error) {await this.close(); throw error;} finally {clearTimeout(timer);}
  }
  #request(command) {
    if (!this.#child || this.#closing || this.#pending.size >= 8) return Promise.reject(Error('Serial transport unavailable'));
    const id = ++this.#sequence;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {this.#pending.delete(id); reject(Error('Serial write/control timed out'));}, 5000);
      this.#pending.set(id, {resolve, reject, timer});
      this.#child.stdin.write(JSON.stringify({id, ...command}) + '\n', error => {
        if (!error) return;
        clearTimeout(timer); this.#pending.delete(id); reject(error);
      });
    });
  }
  setSignals(signals) { return this.#request({signals}); }
  async close() {
    if (!this.#child) return;
    this.#closing = true;
    this.#child.stdin.end();
    const timer = setTimeout(() => this.#child?.kill(), 3000);
    await this.#ended;
    clearTimeout(timer);
    this.#lines.close(); this.#child = null;
    for (const pending of this.#pending.values()) {clearTimeout(pending.timer); pending.reject(Error('Serial transport closed'));}
    this.#pending.clear();
  }
}
