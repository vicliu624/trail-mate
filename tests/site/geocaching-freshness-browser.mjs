// Deterministic DOM/lifecycle acceptance. Signed LXMF transport is covered by
// geocaching-browser.mjs; this substitutes worker replies to control updates.
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {readFile} from 'node:fs/promises';
import {extname,resolve,sep} from 'node:path';
import {createRequire} from 'node:module';
const require=createRequire(new URL('../../site/package.json',import.meta.url));
const {chromium}=require('playwright');
const root=resolve('site');
const server=createServer(async(req,res)=>{
  const path=new URL(req.url,'http://localhost').pathname;
  const file=resolve(root,path.slice('/trail-mate/'.length)+(path.endsWith('/')?'index.html':''));
  if(!path.startsWith('/trail-mate/') || !file.startsWith(root+sep)){res.writeHead(404).end();return;}
  try {
    const type={'.js':'text/javascript','.css':'text/css','.html':'text/html','.svg':'image/svg+xml','.png':'image/png'}[extname(file)] || 'application/octet-stream';
    res.writeHead(200,{'Content-Type':type});res.end(await readFile(file));
  } catch {res.writeHead(404).end();}
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
let browser;
try {
  browser=await chromium.launch({headless:true,channel:'msedge'});
  const context=await browser.newContext({locale:'en-US'});
  await context.route('https://tile.openstreetmap.org/**',route=>route.fulfill({contentType:'image/svg+xml',body:'<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256"/>'}));
  await context.addInitScript(()=>{
    navigator.geolocation.getCurrentPosition=(success,fail)=>fail(Error('unavailable'));
    const row=(id,name,revision=1)=>({id,summary:[new Uint8Array(32),revision,new Uint8Array(32),0,0,0,name,2,2,0,100],conflict:false});
    window.liveRows=[row('first','First cache'),row('second','Second cache')];
    window.liveQueries=[];
    window.Worker=class {
      postMessage({id,command,args}) {
        queueMicrotask(()=>{
          let result;
          if(command==='connect')this.onmessage({data:{event:{type:'directory',count:1}}});
          else if(command==='query') {
            window.liveQueries.push(args);
            this.onmessage({data:{event:{type:'results',queryToken:args.queryToken,rows:window.liveRows,more:false}}});
          } else if(command==='get')result={isCurrent:true,record:{3:window.liveRows.find(row=>row.id===args.cacheId).summary[1],9:'Description',10:'Hint'},authorHash:'Author',checkedAt:Date.now(),sourceName:'Directory'};
          this.onmessage({data:{id,result}});
        });
      }
      terminate() {}
    };
  });
  const page=await context.newPage(),errors=[];
  page.on('pageerror',error=>errors.push(error.message));
  await page.clock.install();
  await page.goto(`http://127.0.0.1:${server.address().port}/trail-mate/geocaching/`);
  await page.waitForFunction(()=>window.liveQueries.length===1);
  await page.locator('#results input').first().check();
  await page.getByRole('button',{name:'First cache',exact:false}).click();
  await page.waitForFunction(()=>document.querySelector('#detail-state').textContent==='AUTHOR SIGNATURE VERIFIED');
  await page.evaluate(()=>{
    const summary=[...window.liveRows[1].summary]; summary[6]='New publication';
    window.liveRows=[window.liveRows[0],{...window.liveRows[1],id:'third',summary}];
  });
  await page.clock.runFor(60000);
  assert.equal(await page.locator('#results').innerText(), 'First cache\nActive · D 1 / T 1 · v1\nNew publication\nActive · D 1 / T 1 · v1');
  assert.equal(await page.locator('#results input').first().isChecked(),true,'refresh preserves an unchanged selection');
  assert.equal(await page.locator('#detail').isVisible(),true,'refresh preserves verified details for unchanged caches');
  const queries=await page.evaluate(()=>window.liveQueries);
  assert.equal(queries.length,2); assert.equal(queries[1].refresh,true);
  assert.deepEqual(queries[1].bounds,queries[0].bounds); assert.equal(queries[1].stateMask,queries[0].stateMask);
  const icons=await page.locator('.cache-marker img').evaluateAll(images=>images.map(image=>({src:image.src,loaded:image.complete&&image.naturalWidth>0})));
  assert.ok(icons.length && icons.every(image=>/geocaching-.*\.svg$/.test(image.src) && image.loaded));
  await page.evaluate(()=>{
    const summary=[...window.liveRows[0].summary]; summary[1]=2; summary[2]=new Uint8Array(32).fill(2);
    window.liveRows=[{...window.liveRows[0],summary},window.liveRows[1]];
  });
  await page.clock.runFor(60000);
  assert.equal(await page.locator('#detail').isVisible(),true,'an updated cache retains its readable previous details');
  assert.equal(await page.locator('#download-one').isDisabled(),true,'stale details cannot offer a current-version download');
  assert.match(await page.locator('#detail-state').innerText(),/Cache updated/);
  await page.getByRole('button',{name:'First cache',exact:false}).click();
  await page.waitForFunction(()=>document.querySelector('#detail-state').textContent==='AUTHOR SIGNATURE VERIFIED');
  assert.equal(await page.locator('#download-one').isDisabled(),false);
  await page.evaluate(()=>{window.liveRows=window.liveRows.slice(1);});
  await page.clock.runFor(60000);
  assert.equal(await page.locator('#detail').isVisible(),false,'an archived/removed result closes its detail');
  assert.equal(await page.locator('#results li').count(),1); assert.equal(await page.locator('#selection-count').innerText(),'');
  await page.evaluate(()=>window.dispatchEvent(new Event('online')));
  await page.clock.runFor(1);
  assert.equal(await page.evaluate(()=>window.liveQueries.length),5,'resume requeries immediately');
  assert.deepEqual(errors,[]);
  console.log(JSON.stringify({status:'passed',automaticRefresh:true,preservedSelection:true,preservedDetail:true,updatedDetail:true,removedCache:true,resumeRefresh:true,sharedSvg:true}));
} finally {
  await browser?.close();
  await new Promise(resolve=>server.close(resolve));
}
