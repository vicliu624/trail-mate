// Capture the current production bundles. Reads public directories only; never
// connects USB, signs an object, or publishes to the public network.
import {chromium} from '../../site/node_modules/playwright/index.mjs';
import http from 'node:http';
import fs from 'node:fs/promises';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
const root=path.resolve('site'),output=path.resolve(process.env.TRAIL_MATE_DOCS_OUTPUT??'docs/images/release-1.0.0');
await fs.mkdir(output,{recursive:true});
const server=http.createServer(async(req,res)=>{
 try{
  const pathname=decodeURIComponent(new URL(req.url,'http://localhost').pathname);
  if(!pathname.startsWith('/trail-mate/'))throw Error('prefix');
  const file=path.resolve(root,pathname.slice(12)+(pathname.endsWith('/')?'index.html':''));
  if(!file.startsWith(root+path.sep))throw Error('path');
  res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css','.json':'application/json','.png':'image/png','.svg':'image/svg+xml','.woff2':'font/woff2','.ttf':'font/ttf','.wasm':'application/wasm'})[path.extname(file)]??'application/octet-stream');
  res.end(await fs.readFile(file));
 }catch{res.writeHead(404).end();}
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const origin=`http://127.0.0.1:${server.address().port}`;
const browser=await chromium.launch({channel:process.env.TRAIL_MATE_TEST_BROWSER_CHANNEL,headless:true});
const page=await browser.newPage({viewport:{width:1440,height:1080}}),errors=[],receipt=[];
page.on('pageerror',error=>errors.push(String(error)));
await page.route(/https:\/\/(fonts\.(googleapis|gstatic)\.com|unpkg\.com)\//,route=>route.fulfill({contentType:'text/javascript',body:''}));
async function capture(name,element=page){
 assert.equal(await page.locator('html').getAttribute('lang'),'en');
 assert.equal(await page.locator('.site-primary a').first().textContent(),'Device management');
 assert.ok(await page.locator('.brand-logo').evaluate(image=>image.complete&&image.naturalWidth>0));
 await element.screenshot({path:path.join(output,name)});
 receipt.push({file:name,url:page.url(),locale:'en',viewport:page.viewportSize()});
}
const points=[
 [9.70,47.20,1030],[9.705,47.204,1090],[9.710,47.207,1180],[9.716,47.210,1310],
 [9.722,47.214,1450],[9.729,47.216,1390],[9.735,47.220,1280],
];
const route=`<kml xmlns="http://www.opengis.net/kml/2.2" xmlns:gx="http://www.google.com/kml/ext/2.2"><Document><name>Alpine ridge walk</name><description>Sample route for the Web workspace guide.</description><Placemark><name>Ridge trail</name><description>Sample elevations and timestamps for planning.</description><gx:Track><altitudeMode>absolute</altitudeMode>${points.map((p,i)=>`<when>2026-10-17T08:${String(i*5).padStart(2,'0')}:00Z</when>`).join('')}${points.map(p=>`<gx:coord>${p.join(' ')}</gx:coord>`).join('')}</gx:Track></Placemark></Document></kml>`;
try{
 await page.goto(origin+'/trail-mate/?lang=en',{waitUntil:'domcontentloaded'});
 await page.waitForFunction(()=>document.querySelector('.brand-logo')?.naturalWidth>0&&document.querySelector('.site-primary a')?.href.includes('lang=en'));
 const release=JSON.parse(await fs.readFile(path.join(root,'data/latest-release.json'),'utf8'));
 await page.getByText(`v${release.version} ready for install`,{exact:true}).waitFor();
 await capture('web-home-tools.png');
 await page.locator('.site-primary a').nth(1).click();
 await page.locator('.sidebar > input[type=file]').setInputFiles({name:'alpine-ridge.kml',mimeType:'application/vnd.google-earth.kml+xml',buffer:Buffer.from(route)});
 await page.locator('.tracks-row').waitFor();
 await page.locator('.tracks-elevation svg').waitFor();
 // Keep the route preview visible while recording the current controls.
 await page.evaluate(()=>scrollTo(0,0));
 await capture('web-track-workspace.png');
 await page.locator('.tracks-elevation').scrollIntoViewIfNeeded();
 await capture('web-track-elevation.png',page.locator('.tracks-elevation'));
 // Cancelling the browser's leave prompt must preserve local route edits.
 const name=page.locator('.tracks-editor>label').first().locator('input');
 await name.fill('Alpine ridge walk · unsaved');
 let prompts=0;const cancel=async dialog=>{assert.equal(dialog.type(),'beforeunload');prompts++;await dialog.dismiss();};
 page.on('dialog',cancel);
 await page.locator('.site-primary a').first().click({noWaitAfter:true});
 assert.equal(prompts,1);assert.equal(await name.inputValue(),'Alpine ridge walk · unsaved');
 page.off('dialog',cancel);page.on('dialog',dialog=>dialog.accept());
 // A separate browser context leaves the sample draft and unsaved inputs intact.
 const regional=await browser.newPage({viewport:{width:1440,height:1080}});
 regional.on('pageerror',error=>errors.push(String(error)));
 regional.on('console',message=>{if(message.type()==='error')console.error('Regional page:',message.text());});
 // The public gateway validates Origin. Serve this exact local build under the
 // site's origin in the isolated capture browser; do not replace network data.
 await regional.route('https://vicliu624.github.io/trail-mate/**',async route=>{
  const pathname=decodeURIComponent(new URL(route.request().url()).pathname),file=path.resolve(root,pathname.slice(12)+(pathname.endsWith('/')?'index.html':''));
  assert.ok(file.startsWith(root+path.sep));
  try{await route.fulfill({body:await fs.readFile(file),contentType:({'.html':'text/html','.js':'text/javascript','.css':'text/css','.json':'application/json','.png':'image/png','.svg':'image/svg+xml','.woff2':'font/woff2','.ttf':'font/ttf'})[path.extname(file)]??'application/octet-stream'});}catch{await route.fulfill({status:404,body:''});}
 });
 await regional.goto('https://vicliu624.github.io/trail-mate/tracks/?lang=en',{waitUntil:'domcontentloaded'});
 console.log('Current English route captures and cancelled unsaved navigation passed; connecting read-only directory.');
 try{await regional.getByText('Track directory verified.',{exact:true}).waitFor({timeout:90000});}
 catch(error){console.error(await regional.locator('.track-directories').innerText());await regional.screenshot({path:'.codex-build/web-navigation/region-connection-failed.png'});throw error;}
 const regions=regional.locator('.shared-track-regions');
 await regions.getByRole('button',{name:'Load regions',exact:true}).click();
 const select=regions.getByLabel('Select region',{exact:true});
 await select.waitFor({state:'attached',timeout:60000});
 for(let count=0;!await select.getByRole('option',{name:'Austria',exact:true}).count();count++){
  assert.ok(count<32,'Austria must occur in the country catalog');
  const previous=await select.locator('option').count();
  await regions.getByRole('button',{name:'More regions',exact:true}).click();
  await regional.waitForFunction(previous=>document.querySelector('.shared-track-regions select').options.length>previous,previous,{timeout:30000});
 }
 await select.selectOption({label:'Austria'});
 await regions.getByRole('button',{name:'Austria',exact:true}).waitFor();
 await regional.getByRole('button',{name:'Search selected region',exact:true}).click();
 await regional.getByText('No shared tracks found in this area.',{exact:true}).waitFor({timeout:90000});
 await regional.evaluate(()=>{const region=document.querySelector('.shared-track-regions'),header=document.querySelector('.site-header');scrollBy(0,region.getBoundingClientRect().top-header.getBoundingClientRect().bottom-18);});
 await regional.screenshot({path:path.join(output,'web-track-country-search.png')});
 receipt.push({file:'web-track-country-search.png',url:regional.url(),locale:'en',publicReadOnly:true,assets:'local production build served under site origin for gateway Origin policy'});
 await select.selectOption({label:'Vorarlberg'});
 await regions.getByRole('button',{name:'Vorarlberg',exact:true}).waitFor();
 await regional.getByRole('button',{name:'Search selected region',exact:true}).click();
 await regional.getByText('No shared tracks found in this area.',{exact:true}).waitFor({timeout:90000});
 await regional.evaluate(()=>{const region=document.querySelector('.shared-track-regions'),header=document.querySelector('.site-header');scrollBy(0,region.getBoundingClientRect().top-header.getBoundingClientRect().bottom-18);});
 await regional.screenshot({path:path.join(output,'web-track-administrative-search.png')});
 receipt.push({file:'web-track-administrative-search.png',url:regional.url(),locale:'en',publicReadOnly:true,assets:'local production build served under site origin for gateway Origin policy'});
 assert.deepEqual(errors,[]);
 const bundles=['site/components-dist/components.js','site/components-dist/components.css','site/device/dist/device.js','site/device/dist/device.css','site/tracks/dist/tracks.js','site/tracks/dist/tracks.css','site/geocaching/dist/page.js','site/geocaching/dist/page.css','site/geocaching/dist/reticulum-worker.js'];
 const digest=bytes=>createHash('sha256').update(bytes).digest('hex'),bundleSha256={};
 let builtAt=0;
 for(const file of bundles){bundleSha256[file]=digest(await fs.readFile(file));builtAt=Math.max(builtAt,(await fs.stat(file)).mtimeMs);}
 receipt.push(
  {file:'web-config-percent.png',locale:'en',command:'node tests/site/device-page.browser.mjs --release-docs',kind:'production controls; synthetic USB fixture'},
  {file:'web-device-management.png',locale:'en',command:'node tests/site/device-page.browser.mjs --release-docs',kind:'production UI; synthetic USB author/configuration',viewport:{width:1440,height:1000}},
  {file:'web-geocaching-maintenance.png',locale:'en',command:'node tests/site/geocaching-network-maintenance.browser.mjs --release-docs',kind:'production UI/Worker/native loopback directory; synthetic USB author with real signatures'},
 );
 for(const item of receipt){const file=path.join(output,item.file);assert.ok((await fs.stat(file)).mtimeMs>=builtAt,`Recapture ${item.file} after rebuilding all bundles`);item.sha256=digest(await fs.readFile(file));}
 execFileSync('git',['diff','--quiet','--','site']);
 const sourceRevision=execFileSync('git',['rev-parse','HEAD'],{encoding:'utf8'}).trim(),sourceBranch=execFileSync('git',['branch','--show-current'],{encoding:'utf8'}).trim();
 await fs.writeFile(path.join(output,'web-capture-receipt.json'),JSON.stringify({capturedAt:new Date().toISOString(),sourceRevision,sourceBranch,bundleSha256,captures:receipt,pageErrors:errors},null,2)+'\n');
 console.log('Current English homepage, route/elevation, live read-only country/administrative queries and cancelled route navigation passed.');
}finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
