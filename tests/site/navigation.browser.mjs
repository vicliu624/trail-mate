import {chromium} from '../../site/node_modules/playwright/index.mjs';
import http from 'node:http';
import fs from 'node:fs/promises';
import path from 'node:path';
import assert from 'node:assert/strict';
import {navigationText} from '../../site/navigation.js';
import {locales} from '../../site/i18n/registry.js';

const root=path.resolve('site'),prefix='/trail-mate/';
const server=http.createServer(async(req,res)=>{
 try{
  const pathname=decodeURIComponent(new URL(req.url,'http://localhost').pathname);
  if(!pathname.startsWith(prefix))throw Error('prefix');
  const file=path.resolve(root,pathname.slice(prefix.length)+(pathname.endsWith('/')?'index.html':''));
  if(!file.startsWith(root+path.sep))throw Error('path');
  const data=await fs.readFile(pathname===prefix+'assets/showcase/brand-logo.png'?path.resolve('docs/images/logo_big.png'):file);
  res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css','.json':'application/json','.png':'image/png','.svg':'image/svg+xml','.wasm':'application/wasm','.woff2':'font/woff2'})[path.extname(file)]??'application/octet-stream');
  res.end(data);
 }catch{res.writeHead(404).end();}
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const origin=`http://127.0.0.1:${server.address().port}`,base=origin+prefix;
const browser=await chromium.launch({channel:process.env.TRAIL_MATE_TEST_BROWSER_CHANNEL,headless:true});
const page=await browser.newPage({viewport:{width:1440,height:1080}}),errors=[],checks=[];
page.on('pageerror',error=>errors.push(String(error)));
// This job builds the Web tools, while Pages separately builds and verifies
// the firmware/WASM guide. Do not start that optional preview without its
// manifest here. Navigation, homepage cards and all tool bundles remain real.
await page.route('**/native-explorer.js',route=>route.fulfill({contentType:'text/javascript',body:''}));
// External font/install modules are not required to navigate.
await page.route(/https:\/\/(fonts\.(googleapis|gstatic)\.com|unpkg\.com)\//,route=>route.fulfill({contentType:'text/javascript',body:''}));
const output='.codex-build/web-navigation';
await fs.mkdir(output,{recursive:true});
async function inspect(current,locale){
 await page.waitForFunction(()=>document.querySelectorAll('.site-primary a').length===3);
 await page.waitForFunction(locale=>document.documentElement.lang===locale,locale);
 await page.waitForFunction(locale=>document.querySelector('.site-primary a')?.href.includes('lang='+locale),locale);
 assert.ok(await page.locator('.site-header select').evaluateAll(selects=>selects.every(select=>parseFloat(getComputedStyle(select).fontSize)>=12&&select.selectedOptions[0]?.textContent.trim())),`${current}/${locale}: language choice must remain readable`);
 if(current==='geocaching')assert.ok(await page.locator('#geocaching-connection-controls button').isVisible(),'USB controls stay in the page action row');
 const links=page.locator('.site-primary a'),text=navigationText(locale);
 assert.equal(await links.count(),3);
 for(const [index,key] of ['device','tracks','geocaching'].entries()){
  const link=links.nth(index),url=new URL(await link.getAttribute('href'),page.url());
  assert.equal(url.pathname,prefix+key+'/');
  assert.equal(url.searchParams.get('lang'),locale);
  assert.equal(await link.innerText(),text[key]);
  assert.equal(await link.getAttribute('aria-current'),current===key?'page':null);
 }
 const brand=new URL(await page.locator('.site-brand').getAttribute('href'));
 assert.equal(brand.pathname,prefix);assert.equal(brand.searchParams.get('lang'),locale);
 for(const width of [1440,1024,768,390,320]){
  await page.setViewportSize({width,height:1080});
  assert.ok(await page.locator('.site-header').evaluate(header=>{
   const box=header.getBoundingClientRect();
   return box.left>=0&&box.right<=innerWidth&&[...header.querySelectorAll('.site-primary a,.site-brand,.site-utilities')].every(element=>{
    const r=element.getBoundingClientRect();return r.left>=box.left&&r.right<=box.right&&r.width>0;
   });
  }),`${current}/${locale}: header must fit at ${width}px`);
  if(current==='home')assert.ok(await page.locator('.side-nav').evaluate(nav=>{
   const links=[...nav.querySelectorAll('a')];
   return links.every((link,index)=>{
    const box=link.getBoundingClientRect(),range=document.createRange();range.selectNodeContents(link);
    const text=range.getBoundingClientRect();
    return text.left>=box.left&&text.right<=box.right&&links.slice(index+1).every(other=>{
     const b=other.getBoundingClientRect();return box.right<=b.left||b.right<=box.left||box.bottom<=b.top||b.bottom<=box.top;
    });
   });
  }),`homepage outline text/links must not overlap: ${locale}/${width}`);
 }
 await page.setViewportSize({width:1440,height:1080});
 checks.push({page:current,locale,widths:[1440,1024,768,390,320]});
}
try{
 await page.goto(base,{waitUntil:'domcontentloaded'});
 await page.waitForFunction(()=>document.querySelector('.site-primary a')?.href.includes('lang=en'));
 await page.waitForFunction(()=>document.querySelector('.brand-logo')?.naturalWidth>0);
 await inspect('home','en');
 assert.equal(await page.locator('.web-tool-card').count(),3);
 await page.screenshot({path:output+'/home-en.png'});
 // Exercise the real cross-page journey, including returning home via the logo.
 for(const key of ['device','tracks','geocaching']){
  await page.locator('.site-primary a').nth(['device','tracks','geocaching'].indexOf(key)).click();
  await inspect(key,'en');
  await page.screenshot({path:`${output}/${key}-en.png`});
 }
 await page.locator('.site-brand').click();await inspect('home','en');
 // The three large homepage cards all lead to their advertised tool.
 for(const [index,key] of ['device','tracks','geocaching'].entries()){
  await page.locator('.web-tool-card').nth(index).click();await inspect(key,'en');
  await page.locator('.site-brand').click();await inspect('home','en');
 }
 const resources=page.locator('.site-resources'),summary=resources.locator('summary');
 await summary.focus();await summary.press('Enter');assert.ok(await resources.evaluate(x=>x.open));
 await summary.press('Escape');assert.ok(!await resources.evaluate(x=>x.open));
 assert.ok(await summary.evaluate(x=>document.activeElement===x));
 await summary.click();await page.locator('h1').click();assert.ok(!await resources.evaluate(x=>x.open));
 // Locale continuity across every app, including RTL and long translated labels.
 for(const {id} of locales){
  await page.goto(base+'device/?lang='+id,{waitUntil:'domcontentloaded'});await inspect('device',id);
  await page.locator('.site-primary a').nth(1).click();await inspect('tracks',id);
  await page.locator('.site-primary a').nth(2).click();await inspect('geocaching',id);
  await page.locator('.site-brand').click();await inspect('home',id);
 }
 await page.goto(base+'tracks/?lang=en',{waitUntil:'domcontentloaded'});
 await page.locator('.management-language select').selectOption('zh-Hans');await inspect('tracks','zh-Hans');
 await page.locator('.site-primary a').first().click();await inspect('device','zh-Hans');
 await page.locator('.management-language select').selectOption('en');await inspect('device','en');
 await page.locator('.site-flash').click();await page.waitForURL(url=>url.hash==='#webflasher');
 assert.equal(new URL(page.url()).searchParams.get('lang'),'en');
 await page.goto(base+'?lang=en',{waitUntil:'domcontentloaded'});
 await page.setViewportSize({width:390,height:844});
 await page.screenshot({path:output+'/home-mobile-en.png'});
 assert.deepEqual(errors,[]);
 await fs.writeFile(output+'/result.json',JSON.stringify({checks,pageErrors:errors},null,2));
 console.log(`Navigation passed: ${checks.length} page/locale checks, five widths, real tool/card/return/Flash journeys, keyboard resource menu.`);
}finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
