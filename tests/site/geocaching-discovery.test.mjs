import assert from 'node:assert/strict';
import {test} from 'node:test';
import {Destination, DestType, Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {DirectoryClient} from '../../site/geocaching/src/protocol-client.js';
import {encode} from '../../site/geocaching/src/protocol.js';

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
