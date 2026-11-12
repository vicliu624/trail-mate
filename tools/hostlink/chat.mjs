#!/usr/bin/env node
import {readFile, open, rename, access} from 'node:fs/promises';
import {constants} from 'node:fs';
import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {HostlinkConnection} from '../../site/device/src/hostlink.js';
import {ManagementSession} from '../../site/device/src/management-protocol.js';
import {ChatClient} from '../../site/device/src/chat-client.js';
import {PythonSerialPort} from './python-serial-port.mjs';

const replacer = (_, value) => value instanceof Uint8Array ? {$bytes: Buffer.from(value).toString('hex')} : value instanceof Map ? Object.fromEntries(value) : value;
const reviver = (_, value) => value && typeof value.$bytes === 'string' ? new Uint8Array(Buffer.from(value.$bytes, 'hex')) : value;
export async function main(args = process.argv.slice(2)) {
  let python = process.env.PYTHON ?? (process.platform === 'win32' ? 'python' : 'python3');
  let port, text, team = false, channel = 0, intentPath, recoverPath, seconds = 10;
  for (let i = 0; i < args.length; ++i) {
    const arg = args[i];
    if (arg === '--help') {
      console.log('Usage: node tools/hostlink/chat.mjs [--python PYTHON] PORT [--listen SECONDS]\n  [--team --send TEXT --intent NEW_FILE] | [--channel N --send TEXT --intent NEW_FILE]\n  [--recover INTENT_FILE]\nRequires PySerial and npm ci --ignore-scripts --prefix site. Receives JSON events.\nSending requires a NEW durable intent file. Recovery queries only; it never resends.');
      return;
    }
    if (arg === '--python') python = args[++i];
    else if (arg === '--listen') seconds = Number(args[++i]);
    else if (arg === '--team') team = true;
    else if (arg === '--channel') channel = Number(args[++i]);
    else if (arg === '--send') text = args[++i];
    else if (arg === '--intent') intentPath = resolve(args[++i]);
    else if (arg === '--recover') recoverPath = resolve(args[++i]);
    else if (!port && !arg.startsWith('-')) port = arg;
    else throw Error(`Unexpected argument ${arg}`);
  }
  if (!port || !python || !Number.isFinite(seconds) || seconds < 0 || seconds > 3600 ||
      !Number.isInteger(channel) || channel < 0 || channel > 7 || text !== undefined && (!intentPath || recoverPath)) throw Error('Invalid chat arguments; use --help.');
  if (text !== undefined) {
    try {await access(intentPath, constants.F_OK); throw Error('Intent file already exists. Use --recover; never resend an old intent.');}
    catch (error) {if (error.code !== 'ENOENT') throw error;}
  }
  let first = true;
  const persistIntent = async value => {
    const destination = first ? intentPath : `${intentPath}.pending`;
    const file = await open(destination, 'wx', 0o600);
    try {await file.writeFile(JSON.stringify(value, replacer, 2) + '\n'); await file.sync();} finally {await file.close();}
    if (!first) await rename(destination, intentPath);
    first = false;
  };
  const connection = new HostlinkConnection(new PythonSerialPort(python, port));
  let session, client;
  const print = value => console.log(JSON.stringify(value, replacer));
  try {
    await connection.open();
    session = new ManagementSession(connection);
    client = new ChatClient(connection, session, {persistIntent});
    client.subscribe(print);
    const caps = await client.capabilities();
    print({kind: 'capabilities', ...caps});
    if (recoverPath) print({kind: 'recovery', ...await client.recover(JSON.parse(await readFile(recoverPath, 'utf8'), reviver))});
    if (text !== undefined) {
      if (team && !caps.team) throw Error('Device has no active keyed Team.');
      const target = team ? [caps.protocol, caps.team[2], 2, caps.team[0]] : [caps.protocol, channel, 0, new Uint8Array()];
      const intent = await client.prepare(target, text);
      print({kind: 'submission', ...await client.send(intent)});
    }
    await new Promise(done => setTimeout(done, seconds * 1000));
  } finally {
    client?.close(); session?.close(); await connection.close(); print({kind: 'closed', serialClosed: true});
  }
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href)
  main().catch(error => {console.error(error.message); process.exitCode = 1;});
