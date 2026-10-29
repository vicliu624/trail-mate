import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createConnection} from 'node:net';
import {createInterface} from 'node:readline';
import {Duplex} from 'node:stream';
import {once} from 'node:events';
import {HostlinkConnection} from '../../../site/device/src/hostlink.js';
import {ManagementSession, OPERATION} from '../../../site/device/src/management-protocol.js';

export async function fixture(root, mode) {
  const peer = spawn(process.argv[2], [root, mode], {stdio: ['pipe', 'pipe', 'pipe']});
  peer.stdin.on('error', () => {});
  const exited = once(peer, 'exit');
  const queued = new Map(), waiting = new Map();
  let diagnostics = '';
  peer.stderr.on('data', bytes => { diagnostics = (diagnostics + bytes.toString()).slice(-2000); });
  createInterface({input: peer.stdout}).on('line', line => {
    const prefix = line.startsWith('ENDPOINT ') ? 'ENDPOINT' : line.startsWith('GATE ') ? 'GATE' : null;
    if (!prefix) return;
    const waiter = waiting.get(prefix)?.shift();
    if (waiter) waiter(line.slice(prefix.length + 1));
    else { const rows = queued.get(prefix) ?? []; rows.push(line.slice(prefix.length + 1)); queued.set(prefix, rows); }
  });
  const next = prefix => {
    const row = queued.get(prefix)?.shift();
    if (row) return Promise.resolve(row);
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error(`fixture ${prefix} timed out: ${diagnostics}`)), 15000);
      const rows = waiting.get(prefix) ?? [];
      rows.push(value => { clearTimeout(timer); resolve(value); }); waiting.set(prefix, rows);
    });
  };
  return {peer, next, async stop() {
    if (peer.exitCode === null && peer.signalCode === null) peer.stdin.end('quit\n');
    const timer = setTimeout(() => peer.kill('SIGKILL'), 5000);
    const [code, signal] = await exited; clearTimeout(timer);
    assert.equal(signal, null, diagnostics); assert.equal(code, 0, diagnostics);
  }};
}
export async function connect(endpoint) {
  const url = new URL(endpoint);
  const control = {loseNextRestore: false, loseNextOperation: null, dropIncoming: false};
  const port = {
    async open() {
      this.socket = createConnection({host: url.hostname, port: Number(url.port)});
      await once(this.socket, 'connect');
      const streams = Duplex.toWeb(this.socket);
      this.readable = streams.readable.pipeThrough(new TransformStream({transform(bytes, output) { if (!control.dropIncoming) output.enqueue(bytes); }}));
      this.writable = streams.writable;
    },
    async setSignals() {}, async close() { this.socket?.destroy(); },
  };
  const connection = new HostlinkConnection(port);
  await connection.open();
  const session = new ManagementSession(connection);
  const request = session.request.bind(session);
  session.request = (operation, body, options) => {
    if ((operation === OPERATION.IDENTITY_BACKUP_RESTORE && control.loseNextRestore) || operation === control.loseNextOperation) control.dropIncoming = true;
    return request(operation, body, options);
  };
  const caps = await session.request(OPERATION.CAPABILITIES, []);
  assert.ok(caps[4].includes(OPERATION.IDENTITY_BACKUP_EXPORT) && caps[4].includes(OPERATION.IDENTITY_BACKUP_RESTORE));
  assert.deepEqual(await session.request(OPERATION.IDENTITY_RESTORE_FORMATS, []), [0, 1]);
  return {connection, session, operations: caps[4], control};
}
