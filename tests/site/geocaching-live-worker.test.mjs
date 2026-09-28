import assert from 'node:assert/strict';
import {test} from 'node:test';
import {DirectoryClient} from '../../site/geocaching/src/protocol-client.js';

test('worker requires live queries and never substitutes deployed cache records', async () => {
  const events=[], originals={};
  let live, revision=1, fail=false, queries=0;
  const previousSelf=globalThis.self, previousFetch=globalThis.fetch;
  globalThis.self={postMessage:value=>events.push(value)};
  globalThis.fetch=async()=>{throw Error('Unexpected static data fetch');};
  for(const name of ['connect','close','query','get']) originals[name]=DirectoryClient.prototype[name];
  DirectoryClient.prototype.connect=async function(){live=this;this.notify({type:'directory'});};
  DirectoryClient.prototype.close=async()=>{};
  DirectoryClient.prototype.query=async function(bounds,mask,region,queryToken){
    queries++;
    if(fail)throw Error('Live query failed');
    this.notify({type:'results',queryToken,rows:[{id:'remote-cache',revision}]});
  };
  DirectoryClient.prototype.get=async()=>{throw Error('Live detail failed');};
  let id=0;
  const send=async(command,args={})=>{
    const request=++id;
    await self.onmessage({data:{id:request,command,args}});
    return events.find(event=>event.id===request);
  };
  try {
    await import('../../site/geocaching/src/reticulum-worker.js');
    assert.match((await send('query')).error,/unavailable/);
    await send('connect',{url:'wss://example.org'});
    assert.match((await send('snapshot')).error,/Unknown command/);
    await send('query',{queryToken:1});
    revision=2;
    await send('query',{queryToken:2});
    assert.deepEqual(events.filter(event=>event.event?.type==='results').map(event=>event.event.rows[0].revision),[1,2]);
    assert.equal(queries,2);
    fail=true;
    assert.equal((await send('query')).error,'Live query failed');
    assert.equal((await send('get',{cacheId:'remote-cache'})).error,'Live detail failed');
    live.notify({type:'status',state:'disconnected'});
    for(const command of ['query','more','get','download'])assert.match((await send(command)).error,/unavailable/);
    await send('disconnect');
  } finally {
    Object.assign(DirectoryClient.prototype,originals);
    globalThis.self=previousSelf;globalThis.fetch=previousFetch;
  }
});
