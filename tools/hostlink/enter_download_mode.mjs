#!/usr/bin/env node
// Explicit USB-only transition; framing and MessagePack reuse the device client.
import {spawn, execFile} from 'node:child_process';
import {randomBytes} from 'node:crypto';
import {createInterface} from 'node:readline';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {promisify} from 'node:util';
import {CAPABILITY, FRAME, FrameDecoder, decodeHello, encodeFrame} from '../../site/device/src/hostlink.js';
import {OPERATION, decodeManagement, encodeManagement} from '../../site/device/src/management-protocol.js';

const execute = promisify(execFile);
const equal = (a, b) => a.length === b.length && a.every((value, i) => value === b[i]);

export function downloadCandidates(before, current, selected) {
  const physical = value => value?.replace(/:x\.\d+$/i, '');
  const mac = value => {
    const normalized = value?.replace(/[:\-\s]/g, '').toUpperCase();
    return /^[0-9A-F]{12}$/.test(normalized ?? '') ? normalized : undefined;
  };
  const previous = new Set(before.filter(value => value.pid === 0x1001).map(value => value.port));
  return current.filter(value => {
    if (value.vid !== 0x303a || value.pid !== 0x1001) return false;
    const expected = mac(selected.serial), actual = mac(value.serial);
    if (expected && actual) return expected === actual;
    if (selected.location && value.location) return physical(selected.location) === physical(value.location);
    return !previous.has(value.port);
  });
}

export async function requestDownloadMode(exchange, random = size => new Uint8Array(randomBytes(size))) {
  const hello = decodeHello((await exchange(FRAME.HELLO, 1, new Uint8Array(), FRAME.HELLO_ACK)).payload);
  if (!(hello.capabilities & CAPABILITY.MANAGEMENT)) throw Error('Firmware does not support HostLink management.');
  const session = random(16);
  let sequence = 1;
  const management = async (operation, body) => {
    const request = random(16);
    const reply = decodeManagement((await exchange(FRAME.MANAGEMENT_REQUEST, ++sequence,
      encodeManagement(session, request, operation, body), FRAME.MANAGEMENT_RESPONSE)).payload);
    if (!equal(reply.session, session) || !equal(reply.request, request)) throw Error('Uncorrelated management response.');
    if (reply.code !== 0) throw Error(`Device refused the request (status ${reply.code}).`);
    return reply.body;
  };
  const capabilities = await management(OPERATION.CAPABILITIES, []);
  if (!Array.isArray(capabilities) || capabilities.length !== 6 || capabilities[0] !== 1 ||
      !Array.isArray(capabilities[4]) || !capabilities[4].includes(OPERATION.ENTER_BOOTLOADER))
    throw Error('Firmware does not advertise EnterBootloader; update it once using its existing download procedure.');
  const reply = await management(OPERATION.ENTER_BOOTLOADER, [true]);
  if (!Array.isArray(reply) || reply.length !== 1 || reply[0] !== true) throw Error('Invalid download-mode acknowledgement.');
  return {accepted: true, model: hello.model, firmware: hello.firmware};
}

function serialExchange(python, port) {
  const helper = fileURLToPath(new URL('./serial_transport.py', import.meta.url));
  const child = spawn(python, [helper, port], {stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true});
  const pending = [];
  const fail = error => { for (const entry of pending.splice(0)) entry.reject(error); };
  child.on('error', fail);
  child.stdin.on('error', fail);
  child.stderr.on('data', () => {});
  const ended = new Promise(resolve => child.once('close', resolve));
  const lines = createInterface({input: child.stdout});
  lines.on('line', line => {
    const entry = pending.shift();
    if (!entry) return;
    try {
      const result = JSON.parse(line);
      if (result.error || !result.hex) throw Error(result.error ?? 'Serial exchange returned no response.');
      const frames = new FrameDecoder().push(new Uint8Array(Buffer.from(result.hex, 'hex')));
      if (frames.length !== 1 || frames[0].type !== entry.response || frames[0].sequence !== entry.sequence)
        throw Error('Invalid serial response framing.');
      entry.resolve(frames[0]);
    } catch (error) { entry.reject(error); }
  });
  child.once('close', () => fail(Error('Serial transport closed before the response.')));
  return {
    exchange(type, sequence, payload, response) {
      return new Promise((resolve, reject) => {
        pending.push({resolve, reject, sequence, response});
        child.stdin.write(JSON.stringify({hex: Buffer.from(encodeFrame(type, sequence, payload)).toString('hex'), sequence, response}) + '\n');
      });
    },
    async close() { child.stdin.end(); await ended; lines.close(); }
  };
}

async function ports(python) {
  const code = "import json;from serial.tools import list_ports;print(json.dumps([{'port':p.device,'vid':p.vid,'pid':p.pid,'location':p.location,'serial':p.serial_number} for p in list_ports.comports() if p.vid==0x303a]))";
  const result = await execute(python, ['-c', code], {windowsHide: true});
  return JSON.parse(result.stdout);
}

export async function main(args = process.argv.slice(2)) {
  let python = process.env.PYTHON ?? (process.platform === 'win32' ? 'python' : 'python3');
  let port;
  for (let i = 0; i < args.length; ++i) {
    if (args[i] === '--help') {
      console.log('Usage: node tools/hostlink/enter_download_mode.mjs [--python PYTHON] PORT\nRequires PySerial. Preserves NVS/SD; explicitly requests ROM download mode.');
      return;
    }
    if (args[i] === '--python' && i + 1 < args.length) python = args[++i];
    else if (!port && !args[i].startsWith('-')) port = args[i];
    else throw Error('Expected one serial port and optional --python executable.');
  }
  if (!port) throw Error('Provide the current application serial port. Use --help for usage.');
  const before = await ports(python);
  const selected = before.find(value => value.port === port);
  if (!selected) throw Error('The selected port is not a present Espressif USB device.');
  const transport = serialExchange(python, port);
  let result;
  try { result = await requestDownloadMode(transport.exchange); }
  finally { await transport.close(); }
  const deadline = Date.now() + 8000;
  while (Date.now() < deadline) {
    const candidates = downloadCandidates(before, await ports(python), selected);
    if (candidates.length === 1) {
      console.log(JSON.stringify({...result, applicationPort: port, downloadPort: candidates[0].port, serialClosed: true}));
      return;
    }
    await new Promise(resolve => setTimeout(resolve, 250));
  }
  throw Error('The command was acknowledged, but a matching ROM download port was not observed. Do not repeat blindly; inspect the device state.');
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href)
  main().catch(error => { console.error(error.message); process.exitCode = 1; });
