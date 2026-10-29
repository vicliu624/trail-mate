import assert from 'node:assert/strict';
import http from 'node:http';
import {readFile} from 'node:fs/promises';
import path from 'node:path';
import {chromium} from '../../site/node_modules/playwright/index.mjs';
import {locales} from '../../site/i18n/registry.js';
import {kmlWorkspaceText} from '../../site/i18n/kml-workspace-labels.js';
import {configOnboardingText} from '../../site/i18n/config-onboarding-labels.js';

const root=path.resolve('site');
const types={'.js':'text/javascript','.css':'text/css','.html':'text/html','.json':'application/json','.png':'image/png','.svg':'image/svg+xml','.woff2':'font/woff2'};
const server=http.createServer(async(req,res)=>{
 try{
  let pathname=decodeURIComponent(new URL(req.url,'http://localhost').pathname);
  if(pathname.endsWith('/'))pathname+='index.html';
  const filename=path.resolve(root,'.'+pathname);
  if(!filename.startsWith(root+path.sep)){res.writeHead(403).end();return;}
  const bytes=await readFile(filename);
  res.writeHead(200,{'Content-Type':types[path.extname(filename)]??'application/octet-stream'}).end(bytes);
 }catch{res.writeHead(404).end();}
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const base=`http://127.0.0.1:${server.address().port}`;
let browser;
try{
 browser=await chromium.launch({headless:true,channel:process.env.TRAIL_MATE_TEST_BROWSER_CHANNEL||undefined});
 const page=await browser.newPage();const errors=[];page.on('pageerror',error=>errors.push(error.message));
 await page.goto(base+'/tracks/?lang=zh-Hans&task=keep');
 await page.locator('.management-language select').waitFor();
 await page.locator('input[type=file]').first().setInputFiles({name:'language-test.kml',mimeType:'application/vnd.google-earth.kml+xml',buffer:Buffer.from('<kml xmlns="http://www.opengis.net/kml/2.2"><Placemark><name>Original</name><LineString><coordinates>120,30 120.01,30.01</coordinates></LineString></Placemark></kml>')});
 const title=page.locator('.tracks-editor input').first();await title.fill('Unsaved user title');
 await page.getByLabel('要素名称',{exact:true}).fill('Unsaved feature name');
 await page.getByLabel('要素说明',{exact:true}).fill('Unsaved description');
 await page.getByLabel('经度,纬度,高度 · 每行一个点',{exact:true}).fill('120,30,100\n120.02,30.03,110');
 const mapTransform=await page.locator('.tracks-map .leaflet-map-pane').getAttribute('style');
 await page.evaluate(()=>{globalThis.retainedEditor=document.querySelector('.tracks-editor input');});
 for(const locale of locales){
  await page.locator('.management-language select').selectOption(locale.id);
  await page.waitForFunction(id=>document.documentElement.lang===id&&new URL(location.href).searchParams.get('lang')===id,locale.id);
  await page.waitForFunction(async()=>{const module=await import('/i18n/management-runtime.js');return document.title===module.managementLanguageSnapshot().tracksTitle+' · Trail Mate';});
  assert.equal(await title.inputValue(),'Unsaved user title');
  const text=kmlWorkspaceText(locale.id);
  assert.equal(await page.getByLabel(text.name,{exact:true}).inputValue(),'Unsaved feature name');
  assert.equal(await page.getByLabel(text.description,{exact:true}).inputValue(),'Unsaved description');
  assert.equal(await page.getByLabel(text.coordinates,{exact:true}).inputValue(),'120,30,100\n120.02,30.03,110');
  assert.equal(await page.getByLabel(text.coordinates,{exact:true}).getAttribute('dir'),'ltr');
  assert.equal(await page.locator('.tracks-map .leaflet-map-pane').getAttribute('style'),mapTransform);
  assert.equal(await page.evaluate(()=>globalThis.retainedEditor===document.querySelector('.tracks-editor input')),true);
  assert.equal(await page.getAttribute('html','dir'),locale.direction);
  assert.equal(new URL(page.url()).searchParams.get('task'),'keep');
  assert.equal(new URL(await page.locator('.site-primary a[href*="/device/"]').getAttribute('href')).searchParams.get('lang'),locale.id);
 }
 await page.setViewportSize({width:375,height:812});
 await page.locator('.management-language select').selectOption('ar');
 await page.waitForFunction(()=>document.documentElement.dir==='rtl');
 // The language control itself must remain within the narrow viewport.
 const box=await page.locator('.management-language select').boundingBox();assert.ok(box.x>=0&&box.x+box.width<=375);
 await page.goto(base+'/device/?lang=ar');await page.locator('.management-language select').waitFor();
 assert.equal(await page.getAttribute('html','dir'),'rtl');
 await page.locator('.device-nav button').last().click();
 for(const locale of locales){
  await page.locator('.management-language select').selectOption(locale.id);
  await page.waitForFunction(id=>document.documentElement.lang===id,locale.id);
  await page.waitForFunction(async()=>{const module=await import('/i18n/management-runtime.js');const snapshot=module.managementLanguageSnapshot();return document.querySelector('.device-nav button:last-child').textContent===snapshot.text.otherSettings;});
  assert.equal(await page.locator('.device-nav button.selected').count(),1);
  assert.equal(await page.locator('.device-nav button.selected').textContent(),await page.locator('.device-nav button').last().textContent());
  const onboarding=configOnboardingText(locale.id);await page.getByRole('heading',{name:onboarding.startTitle,exact:true}).waitFor();await page.getByText(onboarding.startHelp,{exact:true}).waitFor();await page.getByText(onboarding.capabilities,{exact:true}).waitFor();
 }
 await page.locator('.management-language select').selectOption('en');await page.waitForFunction(()=>document.documentElement.dir==='ltr');
 assert.equal(new URL(await page.locator('.site-primary a[href*="/tracks/"]').getAttribute('href')).searchParams.get('lang'),'en');
 assert.deepEqual(errors,[]);
 console.log('Built management pages: 11 live language switches preserve unsaved KML input and DOM identity; URL/navigation, Arabic direction, narrow selector and device switch passed.');
}finally{await browser?.close();await new Promise(resolve=>server.close(resolve));}
