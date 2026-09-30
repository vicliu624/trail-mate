import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {test} from 'node:test';

// Exercise the production startup function without the map/DOM dependencies.
const page = await readFile(new URL('../../site/geocaching/src/page.js', import.meta.url), 'utf8');
const startup = page.slice(page.indexOf('let serviceRetry ='), page.indexOf("$('search').onclick"))
  .replaceAll('import.meta.url', 'baseUrl');
function fixture(fetch) {
  const timers = [], calls = [], nodes = new Map();
  const create = new Function('fetch', 'rpc', '$', 'tr', 'notice', 'setTimeout', 'clearTimeout', 'baseUrl',
    `${startup}\nreturn {startService, stop:()=>{serviceStopped=true;clearTimeout(serviceRetry);},delay:()=>serviceRetryDelay};`);
  const service = create(fetch, async (...args)=>calls.push(args), id=>{
    if (!nodes.has(id)) nodes.set(id, {});
    return nodes.get(id);
  }, en=>en, ()=>{}, (callback, delay)=>{const timer={callback,delay};timers.push(timer);return timer;},
  timer=>{if(timer)timer.cancelled=true;}, 'https://example.org/geocaching/src/page.js');
  return {service,timers,calls};
}
const config = {ok:true,json:async()=>({endpoint:'wss://example.org:18434/',discoverySeeds:[]})};
test('startup retries deployment failures with capped backoff and recovers', async()=>{
  let failures=6;
  const {service,timers,calls}=fixture(async()=>{if(failures-->0)throw Error('offline');return config;});
  await service.startService();
  for(let i=0;i<6;i++)await timers[i].callback();
  assert.deepEqual(timers.map(timer=>timer.delay),[5000,10000,20000,40000,60000,60000]);
  assert.equal(calls.length,1);
  assert.equal(service.delay(),5000);
});
test('startup does not overlap or connect after page exit during fetch', async()=>{
  let resolve;
  const {service,calls,timers}=fixture(()=>new Promise(done=>{resolve=done;}));
  const pending=service.startService();
  await service.startService();
  service.stop();resolve(config);await pending;
  assert.equal(calls.length,0);assert.equal(timers.length,0);
});
test('stopping startup cancels retry and prevents further work', async()=>{
  let fetches=0;
  const {service,timers}=fixture(async()=>{fetches++;throw Error('offline');});
  await service.startService();service.stop();
  assert.equal(timers[0].cancelled,true);
  await timers[0].callback();assert.equal(fetches,1);
});
