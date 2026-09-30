import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {test} from 'node:test';

const page=await readFile(new URL('../../site/geocaching/src/page.js',import.meta.url),'utf8');
const rpcSource=page.slice(page.indexOf('function rpc('),page.indexOf('const notice ='));
const recovery=page.slice(page.indexOf('function recoverWorker()'),page.indexOf('\nworker.onmessage=receiveWorkerMessage;'))
  .replaceAll('import.meta.url','baseUrl');
function fixture() {
  const messages=[],timers=[],nodes=new Map();let terminated=0,created=0,started=0;
  const worker={postMessage:message=>messages.push(message),terminate:()=>terminated++};
  const create=new Function('initialWorker','Worker','setTimeout','clearTimeout','$','notice','tr','onMessage','startService','baseUrl',
    `let worker=initialWorker,nextId=0,serviceStopped=false,serviceRetry=null;const pending=new Map();
     const receiveWorkerMessage=onMessage;const updateSelection=()=>{};
     ${rpcSource}\n${recovery}\nreturn {rpc,rejectPending,recoverWorker,size:()=>pending.size};`);
  const runtime=create(worker,function(){created++;return {...worker};},(callback,delay)=>{const t={callback,delay};timers.push(t);return t;},
    ()=>{},id=>{if(!nodes.has(id))nodes.set(id,{});return nodes.get(id);},()=>{},en=>en,
    message=>messages.push(message),()=>started++,'https://example.org/geocaching/src/page.js');
  return {runtime,worker,messages,timers,counts:()=>({terminated,created,started})};
}
test('failed worker submission releases its request slot',async()=>{
  const f=fixture();f.worker.postMessage=()=>{throw Error('unavailable');};
  await assert.rejects(f.runtime.rpc('get'),/unavailable/);
  assert.equal(f.runtime.size(),0);
});
test('worker failure rejects outstanding requests and schedules one live restart',async()=>{
  const f=fixture();
  const one=assert.rejects(f.runtime.rpc('get'),/interrupted/);
  const two=assert.rejects(f.runtime.rpc('query'),/interrupted/);
  f.runtime.recoverWorker();f.runtime.recoverWorker();await Promise.all([one,two]);
  assert.equal(f.runtime.size(),0);assert.equal(f.timers.length,1);assert.equal(f.timers[0].delay,5000);
  assert.equal(f.messages.at(-1).data.event.state,'disconnected');
  f.timers[0].callback();assert.deepEqual(f.counts(),{terminated:1,created:1,started:1});
});
