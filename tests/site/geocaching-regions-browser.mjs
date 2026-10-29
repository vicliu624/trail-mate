import {createRequire} from 'node:module';
import {createServer} from 'node:http';
import {readFile,mkdir} from 'node:fs/promises';
import {resolve,extname,sep} from 'node:path';
import assert from 'node:assert/strict';
import {locales} from '../../site/i18n/registry.js';
import {geocachingBrowseText} from '../../site/i18n/geocaching-browse-labels.js';
import {geocachingStatusText} from '../../site/i18n/geocaching-status-labels.js';
const require=createRequire(new URL('../../site/package.json',import.meta.url));
const {chromium}=require('playwright');
const root=resolve('site');
const server=createServer(async(req,res)=>{
  const pathname=decodeURIComponent(new URL(req.url,'http://localhost').pathname);
  const path=resolve(root,pathname.replace(/^\//,'')+(pathname.endsWith('/')?'index.html':''));
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
    window.requests=[];window.workerStarts=0;
    window.Worker=class {
      constructor(){++window.workerStarts;}
      postMessage(data){window.requests.push(data);setTimeout(()=>{
        this.onmessage?.({data:{id:data.id,result:data.command==='get'?{isCurrent:true,record:[null,null,null,1,null,null,null,null,null,'Original cache description',''],authorHash:'ab'.repeat(32),checkedAt:1760140800000,sourceName:'Original directory'}:null}});
        if(data.command==='connect')this.onmessage?.({data:{event:{type:'directory',count:1}}});
        if(data.command==='query')this.onmessage?.({data:{event:{type:'results',queryToken:data.args.queryToken,rows:[{id:'01'.repeat(16),summary:[new Uint8Array(16).fill(1),1,new Uint8Array(32).fill(2),0,312300000,1214600000,'Locale cache',2,2],conflict:false}],more:false}}});
      },0);}
      terminate(){}
    };
  });
  const page=await context.newPage(), errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  const origin=`http://127.0.0.1:${server.address().port}`;
  await page.goto(origin+'/geocaching/?lang=zh-Hans');
  await page.waitForFunction(()=>window.requests.some(r=>r.command==='query'));
  const first=await page.evaluate(()=>window.requests.find(r=>r.command==='query').args);
  assert.ok(first.bounds[0]<31.23 && first.bounds[2]>31.23 && first.bounds[2]-first.bounds[0]<2);
  await page.selectOption('#region-kind','countries');
  await page.waitForFunction(()=>!document.querySelector('#region-select').disabled);
  await page.fill('#region-search','中国');
  await page.selectOption('#region-select','CHN');
  await page.click('#region-apply');
  await page.waitForFunction(()=>window.requests.some(r=>r.args.region?.id==='CHN'));
  await page.getByLabel('选择 Locale cache',{exact:true}).check();
  await page.locator('#results button.result').click();await page.getByText(geocachingStatusText('zh-Hans').signatureVerified,{exact:true}).waitFor();
  await page.locator('#detail-content details').evaluate(node=>node.open=true);
  await page.waitForFunction(()=>!document.querySelector('#map').classList.contains('leaflet-zoom-anim'));
  const requestsBefore=await page.evaluate(()=>requests.length),workerStarts=await page.evaluate(()=>workerStarts),mapStyle=await page.locator('#map .leaflet-map-pane').getAttribute('style');
  for(const locale of locales){const labels=geocachingBrowseText(locale.id),status=geocachingStatusText(locale.id);await page.locator('.management-language select').selectOption(locale.id);await page.getByRole('button',{name:labels.downloadSelected,exact:true}).waitFor();assert.equal(await page.locator('#connection-state').textContent(),status.ready);assert.equal(await page.locator('#notice').textContent(),status.resultsLoaded);assert.equal(await page.locator('#detail-state').textContent(),status.signatureVerified);assert.equal(await page.locator('#detail-content summary').textContent(),status.showHint);assert.equal(await page.locator('#detail-content details p').textContent(),status.noHint);assert.equal(await page.locator('#detail-content details').evaluate(node=>node.open),true);assert.equal(await page.getByText('Original cache description',{exact:true}).count(),1);assert.ok((await page.locator('#detail-content .fingerprint').textContent()).includes('ab'.repeat(32)));assert.equal(await page.getByLabel(labels.selectCache.replace('{name}','Locale cache'),{exact:true}).isChecked(),true);assert.equal(await page.locator('#region-select').inputValue(),'CHN');assert.equal(await page.locator('#region-search').inputValue(),'中国');assert.equal(await page.evaluate(()=>requests.length),requestsBefore);assert.equal(await page.evaluate(()=>workerStarts),workerStarts);assert.equal(await page.locator('#map .leaflet-map-pane').getAttribute('style'),mapStyle);assert.equal(await page.getAttribute('html','dir'),locale.direction);assert.equal(new URL(await page.locator('.site-primary a[href*="/tracks/"]').getAttribute('href')).searchParams.get('lang'),locale.id);}
  await page.locator('.management-language select').selectOption('zh-Hans');
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
  assert.equal(await page.locator('.site-primary a[href*="/geocaching/"]').count(),1);
  await page.screenshot({path:'.codex-build/region-ui/navigation.png',fullPage:false});
  console.log(JSON.stringify({firstQuery:first.bounds,icons,errors}));
  assert.ok(errors.every(error=>error==='Native manifest: HTTP 404'),'Only the absent local simulator manifest is expected');
}finally{await browser.close();await new Promise(r=>server.close(r));}
