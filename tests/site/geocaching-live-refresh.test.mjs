import assert from 'node:assert/strict';
import {test} from 'node:test';
import {createLiveRefresh} from '../../site/geocaching/src/live-refresh.js';
import {DirectoryClient} from '../../site/geocaching/src/protocol-client.js';

function fixture(refresh) {
  let now=0, nextId=0, visible=true, available=true, calls=0;
  const timers=new Map();
  const loop=createLiveRefresh({refresh:async()=>{calls++; await refresh?.();},
    canRefresh:()=>available, isVisible:()=>visible,
    setTimer:(fn,delay)=>{const id=++nextId;timers.set(id,{fn,at:now+delay});return id;},
    clearTimer:id=>timers.delete(id)});
  return {loop, timers, calls:()=>calls, visible:value=>visible=value, available:value=>available=value,
    async tick(ms) {
      const end=now+ms;
      while (timers.size) {
        const [id,timer]=[...timers].sort((a,b)=>a[1].at-b[1].at)[0];
        if(timer.at>end)break;
        now=timer.at; timers.delete(id); await timer.fn();
      }
      now=end;
    }, fire() {const [id,timer]=[...timers][0];timers.delete(id);now=timer.at;return timer.fn();}};
}

test('live results refresh every minute and stop when disconnected or closed',async()=>{
  const f=fixture(); await f.tick(120000); assert.equal(f.calls(),0);
  f.loop.setReady(true); await f.tick(59999); assert.equal(f.calls(),0);
  await f.tick(1); assert.equal(f.calls(),1);
  await f.tick(120000); assert.equal(f.calls(),3);
  f.loop.setReady(false); await f.tick(120000); assert.equal(f.calls(),3);
  f.loop.setReady(true); f.loop.stop(); await f.tick(120000); assert.equal(f.calls(),3);
  f.loop.resume(); assert.equal(f.timers.size,0);
});

test('hidden pages pause all refresh work and returning to the page refreshes immediately',async()=>{
  const f=fixture(); f.loop.setReady(true); f.visible(false);
  await f.tick(300000); assert.equal(f.calls(),0); assert.equal(f.timers.size,0);
  f.visible(true); f.loop.resume(); await f.tick(0); assert.equal(f.calls(),1);
  await f.tick(60000); assert.equal(f.calls(),2);
});

test('details and downloads defer refresh without sending an overlapping request',async()=>{
  const f=fixture(); f.loop.setReady(true); f.available(false);
  await f.tick(80000); assert.equal(f.calls(),0); assert.equal(f.timers.size,1);
  f.available(true); await f.tick(5000); assert.equal(f.calls(),1);
  f.loop.touch(); await f.tick(59999); assert.equal(f.calls(),1);
  await f.tick(1); assert.equal(f.calls(),2);
});

test('a slow query never overlaps itself and resume does not start a second query',async()=>{
  let resolve;
  const f=fixture(()=>new Promise(done=>resolve=done)); f.loop.setReady(true);
  const running=f.fire(); assert.equal(f.calls(),1); assert.equal(f.timers.size,0);
  f.loop.resume(); f.loop.touch(); await f.tick(300000); assert.equal(f.calls(),1);
  resolve(); await running; assert.equal(f.timers.size,1);
  f.loop.stop(); assert.equal(f.timers.size,0);
});

test('a failed refresh keeps one bounded retry and can recover later',async()=>{
  let fail=true;
  const f=fixture(()=>{if(fail)throw Error('directory unavailable');}); f.loop.setReady(true);
  await f.tick(60000); assert.equal(f.calls(),1); assert.equal(f.timers.size,1);
  fail=false; await f.tick(60000); assert.equal(f.calls(),2);
});

const summary=(id,revision=1)=>[new Uint8Array(32).fill(id),revision,new Uint8Array(32).fill(revision),
  0,0,0,`Cache ${id}`,2,2,0,100];
function directoryFixture() {
  const events=[],client=new DirectoryClient(event=>events.push(event));
  const directory={key:'test',name:'Directory',ready:true,limit:20};
  client.directories.set(directory.key,directory);
  return {client,events};
}

test('refresh commits new publications and removals atomically using a fresh query snapshot',async()=>{
  const {client,events}=directoryFixture();
  client.request=async()=>[new Uint8Array(16).fill(1),[summary(1),summary(2)],null,60];
  await client.query([-10,-10,10,10],3,null,1);
  const oldRows=client.rows;
  let resolve;
  client.request=async(directory,operation,body)=>{
    assert.equal(operation,2); assert.equal(body[4],null,'fresh query cannot reuse an old snapshot cursor');
    return new Promise(done=>resolve=done);
  };
  const count=events.length;
  const pending=client.query([-10,-10,10,10],3,null,2,{refresh:true});
  await new Promise(setImmediate);
  assert.equal(client.rows,oldRows,'visible details keep their rows while the response is pending');
  assert.equal(events.length,count,'no empty or partial live result replaces the current view');
  resolve([new Uint8Array(16).fill(2),[summary(2,2),summary(3)],null,60]); await pending;
  assert.deepEqual([...client.rows.values()].map(row=>[row.summary[6],row.summary[1]]),[['Cache 2',2],['Cache 3',1]]);
  assert.equal(events.length,count+1); assert.equal(events.at(-1).queryToken,2);
});

test('automatic refresh retains loaded pagination depth without crawling unopened pages',async()=>{
  const {client,events}=directoryFixture();
  let revision=1,reads=0;
  client.request=async(directory,operation,body)=>{
    reads++; const page=body[4]?.[0] || 0;
    return [new Uint8Array(16).fill(revision),[summary(page+1,revision)],new Uint8Array([page+1]),60];
  };
  await client.query([-10,-10,10,10],3,null,1); await client.more(); await client.more();
  assert.equal(reads,3); assert.equal(client.rows.size,3);
  const count=events.length; revision=2;
  await client.query([-10,-10,10,10],3,null,2,{refresh:true});
  assert.equal(reads,6,'only the three pages opened by the visitor are refreshed');
  assert.equal(client.rows.size,3); assert.equal(events.length,count+1,'refresh has one atomic result event');
  assert.ok([...client.rows.values()].every(row=>row.summary[1]===2));
  assert.equal(client.pages.length,1); await client.more(); assert.equal(client.rows.size,4);
});

test('failed automatic queries retain prior rows and surface a live failure instead of empty success',async()=>{
  const {client,events}=directoryFixture();
  client.request=async()=>[new Uint8Array(16),[summary(1)],null,60];
  await client.query([-10,-10,10,10],3,null,1);
  const oldRows=client.rows;
  client.request=async()=>{throw Error('Directory offline');};
  await assert.rejects(client.query([-10,-10,10,10],3,null,2,{refresh:true}),/All directory queries failed/);
  assert.equal(client.rows,oldRows);
  assert.equal(events.at(-1).type,'source-error');
});
