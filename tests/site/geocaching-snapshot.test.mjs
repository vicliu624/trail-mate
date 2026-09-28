import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {test} from 'node:test';
import {SnapshotClient} from '../../site/geocaching/src/snapshot-client.js';
import {decode, encode, verifySigned} from '../../site/geocaching/src/protocol.js';
import {DirectoryClient} from '../../site/geocaching/src/protocol-client.js';

const bytes = await readFile(new URL('../../modules/core_geocaching/tests/fixtures/signed-cache-v1.bin', import.meta.url));
const verified = await verifySigned(decode(bytes));
const manifest = {version:1, updatedAt:'2026-09-28T00:00:00Z', summaries:[Buffer.from(encode(verified.summary)).toString('base64')]};
const fetcher = async url => new Response(String(url).endsWith('index.json') ? JSON.stringify(manifest) : bytes);

test('published directory filters locally and verifies full signed details', async () => {
  let event;
  const client = new SnapshotClient(value => event=value, fetcher);
  await client.load('https://example.org/geocaching/data/public/index.json');
  client.query([-90,-180,90,180],1,null,7);
  assert.equal(event.rows.length,1);
  assert.equal(event.queryToken,7);
  assert.equal(event.snapshotAt,manifest.updatedAt);
  const detail = await client.get(verified.cacheId);
  assert.equal(detail.record[9],verified.record[9]);
  assert.equal(detail.isCurrent,false);
  client.query([-90,-180,90,180],4,null,8);
  assert.equal(event.rows.length,0);
  client.query([-90,-180,90,180],1,{type:'Feature',geometry:{type:'Polygon',coordinates:[[[0,0],[1,0],[1,1],[0,1],[0,0]]]}},9);
  assert.equal(event.rows.length,0);
  const damaged=Uint8Array.from(bytes); damaged[damaged.length-1]^=1;
  client.fetcher=async()=>new Response(damaged);
  await assert.rejects(client.get(verified.cacheId),/signature/);
});

test('worker keeps query, details and GPX available when WSS cannot connect', async () => {
  const events=[], originalFetch=globalThis.fetch, originalConnect=DirectoryClient.prototype.connect;
  globalThis.self={postMessage:value=>events.push(value)};
  globalThis.fetch=fetcher;
  DirectoryClient.prototype.connect=async()=>{throw Error('Bridge offline');};
  try {
    await import('../../site/geocaching/src/reticulum-worker.js');
    await self.onmessage({data:{id:1,command:'snapshot',args:{url:'https://example.org/data/public/index.json'}}});
    await self.onmessage({data:{id:2,command:'connect',args:{url:'wss://offline.example.org'}}});
    assert.equal(events.find(value=>value.id===2).error,'Bridge offline');
    await self.onmessage({data:{id:3,command:'query',args:{bounds:[-90,-180,90,180],stateMask:1,queryToken:1}}});
    assert.equal(events.find(value=>value.event?.type==='results').event.rows.length,1);
    await self.onmessage({data:{id:4,command:'get',args:{cacheId:verified.cacheId}}});
    assert.equal(events.find(value=>value.id===4).result.record[9],verified.record[9]);
    await self.onmessage({data:{id:5,command:'download',args:{cacheIds:[verified.cacheId]}}});
    const download=events.find(value=>value.id===5).result;
    assert.equal(download.succeeded,1); assert.equal(download.failures.length,0);
    assert.match(download.gpx,/<gpx/);
  } finally {
    globalThis.fetch=originalFetch; DirectoryClient.prototype.connect=originalConnect;
    delete globalThis.self;
  }
});

test('published San Jose record remains readable and verifies against its summary', async () => {
  const base=new URL('../../site/geocaching/data/public/',import.meta.url);
  const fetchFiles=async url=>new Response(await readFile(new URL(url)));
  let event;
  const client=new SnapshotClient(value=>event=value,fetchFiles);
  await client.load(new URL('index.json',base));
  client.query([-90,-180,90,180],1,null,1);
  const row=event.rows.find(row=>row.summary[6]==='San Jose Galleon');
  assert.ok(row);
  const detail=await client.get(row.id);
  assert.ok(detail.record[9].length>20);
  assert.equal(detail.cacheId,row.id);
});

test('worker uses only a matching published revision after a live detail failure or disconnect', async () => {
  const events=[], originalFetch=globalThis.fetch;
  const methods=['connect','query','get','close'];
  const original=Object.fromEntries(methods.map(name=>[name,DirectoryClient.prototype[name]]));
  let live, attempts=0;
  globalThis.self={postMessage:value=>events.push(value)};
  globalThis.fetch=fetcher;
  DirectoryClient.prototype.connect=async function() { live=this; this.notify({type:'directory'}); };
  DirectoryClient.prototype.query=async function() {
    this.rows.set(verified.cacheId,{summary:verified.summary,conflict:false});
  };
  DirectoryClient.prototype.get=async()=>{attempts++; throw Error('Live request failed');};
  DirectoryClient.prototype.close=async()=>{};
  let sequence=0;
  const send=async(command,args={})=>{
    const id=++sequence;
    await self.onmessage({data:{id,command,args}});
    return events.find(value=>value.id===id);
  };
  try {
    await import('../../site/geocaching/src/reticulum-worker.js?disconnect-regression');
    await send('snapshot',{url:'https://example.org/data/public/index.json'});
    await send('connect',{url:'wss://example.org'});
    await send('query',{});
    const detail=await send('get',{cacheId:verified.cacheId});
    assert.equal(detail.result.record[9],verified.record[9]);
    assert.equal(detail.result.isCurrent,false);
    assert.equal(detail.result.snapshotAt,manifest.updatedAt);
    assert.equal(attempts,1);
    live.notify({type:'status',state:'disconnected'});
    const download=await send('download',{cacheIds:[verified.cacheId]});
    assert.equal(download.result.succeeded,1);
    assert.equal(attempts,1,'disconnected clients must not delay fallback with another request');
    const row=live.rows.get(verified.cacheId);
    row.summary=[...verified.summary];
    row.summary[1]++;
    assert.match((await send('get',{cacheId:verified.cacheId})).error,/interrupted/);
    row.summary=[...verified.summary];
    row.summary[2]=Uint8Array.from(row.summary[2]); row.summary[2][0]^=1;
    assert.match((await send('get',{cacheId:verified.cacheId})).error,/interrupted/);
    row.summary=verified.summary; row.conflict=true;
    assert.match((await send('get',{cacheId:verified.cacheId})).error,/interrupted/);
    row.conflict=false;
    await send('disconnect');
    assert.equal((await send('get',{cacheId:verified.cacheId})).result.cacheId,verified.cacheId);
  } finally {
    globalThis.fetch=originalFetch;
    for (const name of methods) DirectoryClient.prototype[name]=original[name];
    delete globalThis.self;
  }
});
