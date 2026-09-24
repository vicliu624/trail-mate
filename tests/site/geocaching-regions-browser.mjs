import {createRequire} from 'node:module';
import {createServer} from 'node:http';
import {readFile,mkdir} from 'node:fs/promises';
import {resolve,extname,sep} from 'node:path';
import assert from 'node:assert/strict';
const require=createRequire(new URL('../../site/package.json',import.meta.url));
const {chromium}=require('playwright');
const root=resolve('site');
const server=createServer(async(req,res)=>{
  const path=resolve(root,decodeURIComponent(new URL(req.url,'http://localhost').pathname).replace(/^\//,'')+(req.url.endsWith('/')?'index.html':''));
  if(!path.startsWith(root+sep)){res.writeHead(403).end();return;}
  try{res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css','.json':'application/json','.png':'image/png','.svg':'image/svg+xml'})[extname(path)]||'application/octet-stream');res.end(await readFile(path));}
  catch{res.writeHead(404).end();}
});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const browser=await chromium.launch({channel:'msedge',headless:true});
try{
  const context=await browser.newContext({locale:'zh-CN',viewport:{width:1440,height:1000},permissions:['geolocation'],geolocation:{latitude:31.23,longitude:121.46,accuracy:1000}});
  await context.route('https://tile.openstreetmap.org/**',r=>r.fulfill({contentType:'image/svg+xml',body:'<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256"><rect width="256" height="256" fill="#dceae4"/></svg>'}));
  await context.addInitScript(()=>{
    window.requests=[];
    window.Worker=class {
      postMessage(data){window.requests.push(data);setTimeout(()=>{
        this.onmessage?.({data:{id:data.id,result:null}});
        if(data.command==='connect')this.onmessage?.({data:{event:{type:'directory',count:1}}});
        if(data.command==='query')this.onmessage?.({data:{event:{type:'results',queryToken:data.args.queryToken,rows:[],more:false}}});
      },0);}
      terminate(){}
    };
  });
  const page=await context.newPage(), errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  const origin=`http://127.0.0.1:${server.address().port}`;
  await page.goto(origin+'/geocaching/');
  await page.waitForFunction(()=>window.requests.some(r=>r.command==='query'));
  const first=await page.evaluate(()=>window.requests.find(r=>r.command==='query').args);
  assert.ok(first.bounds[0]<31.23 && first.bounds[2]>31.23 && first.bounds[2]-first.bounds[0]<2);
  await page.selectOption('#region-kind','countries');
  await page.waitForFunction(()=>!document.querySelector('#region-select').disabled);
  await page.fill('#region-search','中国');
  await page.selectOption('#region-select','CHN');
  await page.click('#region-apply');
  await page.waitForFunction(()=>window.requests.some(r=>r.args.region?.id==='CHN'));
  await mkdir('.codex-build/region-ui',{recursive:true});
  await page.screenshot({path:'.codex-build/region-ui/country-desktop.png',fullPage:true});
  await page.selectOption('#region-kind','seas');
  await page.waitForFunction(()=>!document.querySelector('#region-select').disabled,{},{timeout:30000});
  await page.fill('#region-search','Atlantic');
  const ocean=await page.locator('#region-select option').nth(1).getAttribute('value');
  await page.selectOption('#region-select',ocean);await page.click('#region-apply');
  await page.waitForFunction(()=>window.requests.some(r=>r.args.region?.properties.kind==='sea'));
  await page.screenshot({path:'.codex-build/region-ui/sea-desktop.png',fullPage:true});
  await page.setViewportSize({width:390,height:844});
  assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
  await page.screenshot({path:'.codex-build/region-ui/sea-mobile.png',fullPage:true});
  assert.deepEqual(errors,[]);
  await page.setViewportSize({width:1440,height:1000});await page.goto(origin+'/');
  await page.waitForFunction(()=>document.querySelectorAll('.side-nav .navigation-icon').length===8);
  const icons=await page.locator('.side-nav > a').evaluateAll(links=>links.map(link=>{
    const a=link.getBoundingClientRect(),i=link.previousElementSibling.getBoundingClientRect();
    return {label:link.textContent,aligned:i.right<=a.left+1 && Math.abs((i.y+i.height/2)-(a.y+a.height/2))<3};
  }));
  assert.ok(icons.every(icon=>icon.aligned),JSON.stringify(icons));
  assert.equal(await page.locator('.top-actions a[href="./geocaching/"]').count(),1);
  await page.screenshot({path:'.codex-build/region-ui/navigation.png',fullPage:false});
  console.log(JSON.stringify({firstQuery:first.bounds,icons,errors}));
  assert.ok(errors.every(error=>error==='Native manifest: HTTP 404'),'Only the absent local simulator manifest is expected');
}finally{await browser.close();await new Promise(r=>server.close(r));}
