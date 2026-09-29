import assert from 'node:assert/strict';
import {test} from 'node:test';
import {Destination, DestType, Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {DirectoryClient} from '../../site/geocaching/src/protocol-client.js';
import {encode} from '../../site/geocaching/src/protocol.js';
import {WebSocketClientInterface} from '../../site/node_modules/@reticulum/core/src/interfaces/websocket.js';

test('a long outage remains recoverable beyond five failed dials and close cancels retries', async () => {
  const originalConnect=WebSocketClientInterface.prototype.connect;
  WebSocketClientInterface.prototype.connect=async()=>{};
  const client=new DirectoryClient();
  try {
    await client.connect('wss://example.org/');
    const iface=client.interface, waits=[];
    let dials=0;
    iface._sleepInterruptible=async ms=>{waits.push(ms);};
    iface._establishConnection=async()=>{
      if(++dials<8)throw Error('NAS offline');
    };
    await iface._runReconnectLoop();
    assert.equal(dials,8,'a long outage must not permanently disable the map');
    assert.deepEqual(waits,Array(8).fill(15000),'retries must be rate bounded');
    assert.equal(iface._reconnecting,false);
    await client.close();
    await iface._runReconnectLoop();
    assert.equal(dials,8,'closed clients must not reconnect');
  } finally {
    WebSocketClientInterface.prototype.connect=originalConnect;
    await client.close();
  }
});

test('a failed initial capability check can retry on a later signed announce', async () => {
  const events=[],client=new DirectoryClient(event=>events.push(event));
  const identity=await Identity.generate();
  client.rns={transport:{rememberIdentity:async()=>{}}};
  const service=await Destination.OUT('trailmate.geocache.directory',DestType.SINGLE,identity,client.rns);
  const delivery=await Destination.OUT('lxmf.delivery',DestType.SINGLE,identity,client.rns);
  const announcement={destinationHash:service.destinationHash,identity,
    appData:encode([1,delivery.destinationHash,new Uint8Array(16),0,'Directory']),
    packet:{getHash:async()=>new Uint8Array(32)}};
  let attempts=0;
  client.checkDirectory=async entry=>{
    if(++attempts===1)throw Error('Temporary timeout');
    entry.ready=true;
  };
  await client.discover(announcement);
  assert.equal(client.directories.size,0);
  assert.equal(events[0].type,'source-error');
  await client.discover(announcement);
  assert.equal(client.directories.size,1);
  await client.discover(announcement);
  assert.equal(attempts,2,'ready directories still suppress duplicate checks');
});

test('failed reconnect validation permits rediscovery and a fresh capability check', async () => {
  const events=[],client=new DirectoryClient(event=>events.push(event));
  const identity=await Identity.generate();
  client.rns={transport:{rememberIdentity:async()=>{}}};
  const service=await Destination.OUT('trailmate.geocache.directory',DestType.SINGLE,identity,client.rns);
  const delivery=await Destination.OUT('lxmf.delivery',DestType.SINGLE,identity,client.rns);
  const announcement={destinationHash:service.destinationHash,identity,
    appData:encode([1,delivery.destinationHash,new Uint8Array(16),0,'Directory']),
    packet:{getHash:async()=>new Uint8Array(32)}};
  let fail=false,checks=0;
  client.router={recoverDirectLink:async()=>{}};
  client.request=async()=>{
    checks++;
    if(fail)throw Error('Reconnect timeout');
    return [[1],[1],[0,1,2,3,4],8192,4096,20,0,0,2,0,1,0];
  };
  await client.discover(announcement);
  assert.equal(events.filter(e=>e.type==='directory').length,1);
  const old=[...client.directories.values()][0];
  fail=true;
  await client.revalidateDirectories();
  assert.equal(old.ready,false,'failed peer cannot remain queryable');
  assert.equal(client.directories.size,0,'later announces must not be suppressed');
  fail=false;
  await client.discover(announcement);
  assert.equal(checks,3);
  assert.equal(events.filter(e=>e.type==='directory').length,2);
  assert.equal([...client.directories.values()][0].ready,true);
  await client.discover(announcement);
  assert.equal(checks,3,'healthy peer still suppresses duplicate announcements');
});
