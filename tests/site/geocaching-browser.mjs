import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createServer} from 'node:http';
import {createServer as tcpServer} from 'node:net';
import {mkdir,readFile,writeFile} from 'node:fs/promises';
import {appendFileSync} from 'node:fs';
import {resolve,extname,sep} from 'node:path';
import {createRequire} from 'node:module';
import {parseArgs} from 'node:util';
const require = createRequire(new URL('../../site/package.json',import.meta.url));
const {chromium} = require('playwright');
const {values} = parseArgs({options:{work:{type:'string'},python:{type:'string',default:'python'},channel:{type:'string'},'multi-upstream':{type:'boolean',default:false},reconnect:{type:'boolean',default:false},'late-bridge':{type:'boolean',default:false}}});
if (!values.work) throw Error('Specify a new --work directory');
const work = resolve(values.work), root = resolve('site');
await mkdir(work,{recursive:false});
const children=[];
let browser, page;
const errors=[], consoleLog=[], packetSizes=[];

async function freePort() {
  const server=tcpServer(); await new Promise(r=>server.listen(0,'127.0.0.1',r));
  const port=server.address().port; await new Promise(r=>server.close(r)); return port;
}
function launch(name,args) {
  const child=spawn(values.python,args,{windowsHide:true,stdio:['ignore','pipe','pipe']});
  const entry={name,child,output:'',done:null};
  const record=data=>{entry.output+=data;appendFileSync(resolve(work,`${name}.log`),data);};
  child.stdout.on('data',record); child.stderr.on('data',record);
  entry.done=new Promise((resolve,reject)=>{child.on('error',reject);child.on('exit',code=>resolve(code));});
  children.push(entry); return entry;
}
async function waitReady(entry,event) {
  const deadline=Date.now()+20000;
  while(Date.now()<deadline) {
    for(const line of entry.output.split('\n')) {
      try {const data=JSON.parse(line); if(data.event===event)return data;} catch {}
    }
    if(entry.child.exitCode!==null)throw Error(`${entry.name} exited: ${entry.output}`);
    await new Promise(r=>setTimeout(r,100));
  }
  throw Error(`${entry.name} not ready: ${entry.output}`);
}
const http=createServer(async(req,res)=>{
  const path=decodeURIComponent(new URL(req.url,'http://localhost').pathname);
  if(!path.startsWith('/trail-mate/')){res.writeHead(404).end();return;}
  const file=resolve(root,path.slice('/trail-mate/'.length)+(path.endsWith('/')?'index.html':''));
  if(!file.startsWith(root+sep)){res.writeHead(403).end();return;}
  try {
    const type={'.js':'text/javascript','.css':'text/css','.html':'text/html','.png':'image/png','.map':'application/json'}[extname(file)]||'application/octet-stream';
    const content=await readFile(file);
    res.writeHead(200,{'Content-Type':type});res.end(content);
  } catch {res.writeHead(404).end();}
});
await new Promise(r=>http.listen(0,'127.0.0.1',r));
const origin=`http://127.0.0.1:${http.address().port}`;
try {
  const tcpPort=await freePort();
  const service=launch('directory',['tools/geocaching/browser_test_service.py','--state',resolve(work,'directory'),'--port',String(tcpPort),
    '--startup-delay',values['late-bridge']?'15':'5']);
  const directoryReady=await waitReady(service,'ready');
  let gateway, bridgeTcpPort=tcpPort, faultStarted=null, recoveryMs=null, cutInterface=null;
  if(values['multi-upstream']) {
    bridgeTcpPort=await freePort();
    gateway=launch('gateway',['tools/geocaching/browser_test_gateway.py','--state',resolve(work,'gateway'),
      '--port',String(bridgeTcpPort),'--upstream-port',String(tcpPort),'--destination',directoryReady.delivery]);
    await waitReady(gateway,'gateway_ready');
  }
  const bridgePort=values['late-bridge']?await freePort():0;
  const startBridge=()=>launch('bridge',['tools/geocaching/packet_bridge.py','--port',String(bridgePort),'--tcp-port',String(bridgeTcpPort),
    '--origin',origin,'--run-seconds','160']);
  let bridge=values['late-bridge']?null:startBridge();
  const connection=bridge?await waitReady(bridge,'bridge_ready'):{port:bridgePort};
  browser=await chromium.launch({headless:true,...(values.channel?{channel:values.channel}:{})});
  const context=await browser.newContext({viewport:{width:1440,height:960},locale:'en-US',acceptDownloads:true});
  await context.route('**/geocaching/network.json',route=>route.fulfill({json:{endpoint:`ws://127.0.0.1:${connection.port}`}}));
  // Never automate bulk/prefetch requests against the public OSM tile service.
  await context.route('https://tile.openstreetmap.org/**',route=>route.fulfill({contentType:'image/svg+xml',
    body:'<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256"><rect width="256" height="256" fill="#dceae4"/><path d="M0 0H256V256H0Z" fill="none" stroke="#cadfd3"/></svg>'}));
  page=await context.newPage();
  page.on('pageerror',error=>errors.push(error.message));
  page.on('console',message=>{const line=`${message.type()}: ${message.text()}`;consoleLog.push(line);appendFileSync(resolve(work,'browser.log'),line+'\n');});
  page.on('websocket',socket=>{socket.on('framesent',event=>packetSizes.push(Buffer.byteLength(event.payload)));socket.on('framereceived',event=>packetSizes.push(Buffer.byteLength(event.payload)));});
  await page.goto(`${origin}/trail-mate/geocaching/`);
  assert.equal(await page.locator('#endpoint, #connect, #connect-form, #disconnect').count(),0,'Visitors must not configure the network');
  if(values['late-bridge']) {
    await page.waitForFunction(()=>document.querySelector('#connection-state').textContent==='Service unavailable',{},{timeout:10000});
    assert.equal(packetSizes.length,0,'Initial dial must actually fail before the bridge starts');
    bridge=startBridge();
    await waitReady(bridge,'bridge_ready');
  }
  await page.waitForFunction(()=>document.querySelector('#results').children.length===2,{},{timeout:85000});
  if(values['late-bridge']) console.log(JSON.stringify({event:'initial_dial_failure_recovered_without_reload'}));
  assert.ok(await page.locator('#search').evaluate(button=>button.className.includes('animal-btn-')),'Search uses the actual Animal Island UI Button');
  assert.ok(await page.locator('.brand-logo').evaluate(img=>img.complete && img.naturalWidth>0),'Brand image loads without the full site preparation build');
  if(gateway) {
    await writeFile(resolve(work,'gateway','drop-upstream'),'');
    const cut=await waitReady(gateway,'gateway_uplink_dropped');
    assert.equal(cut.online_uplinks,1);
    cutInterface=cut.interface; faultStarted=Date.now();
    console.log(JSON.stringify({event:'active_tcp_upstream_cut',interface:cut.interface}));
  }
  await page.getByRole('button',{name:/Browser Resource/}).click();
  await page.waitForFunction(()=>document.querySelector('#detail-state').textContent==='AUTHOR SIGNATURE VERIFIED',{},{timeout:45000});
  if(gateway) {
    recoveryMs=Date.now()-faultStarted;
    const paths=gateway.output.split('\n').flatMap(line=>{try{const item=JSON.parse(line);return item.event==='gateway_path'?[item.interface]:[];}catch{return [];}});
    assert.ok(paths.length>=2 && paths.at(-1)!==cutInterface,'Native RNS path moved to the surviving uplink');
    console.log(JSON.stringify({event:'signed_detail_after_upstream_loss',recoveryMs,interface:paths.at(-1)}));
  }
  assert.equal(await page.locator('#detail-content b').count(),0,'Remote text must not become HTML');
  await page.screenshot({path:resolve(work,'desktop.png'),fullPage:true});
  const downloadPromise=page.waitForEvent('download');
  await page.locator('#download-one').click();
  const download=await downloadPromise;
  assert.match(download.suggestedFilename(),/^[0-9a-f]{64}\.gpx$/);
  await download.saveAs(resolve(work,'download.gpx'));
  const xml=await readFile(resolve(work,'download.gpx'),'utf8');
  assert.ok(xml.includes('&lt;b&gt;plain text&lt;/b&gt; &amp;'));
  const xmlValid=await page.evaluate(text=>!new DOMParser().parseFromString(text,'application/xml').querySelector('parsererror'),xml);
  assert.ok(xmlValid);
  await page.setViewportSize({width:390,height:844});
  await page.screenshot({path:resolve(work,'mobile.png'),fullPage:true});
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
  await page.locator('#close-detail').click();
  await page.screenshot({path:resolve(work,'mobile-map.png'),fullPage:true});
  await page.setViewportSize({width:1440,height:960});
  await page.screenshot({path:resolve(work,'desktop-map.png'),fullPage:true});
  if(values.reconnect) {
    bridge.child.kill();
    await bridge.done;
    await page.waitForFunction(()=>document.querySelector('#search').disabled,{},{timeout:10000});
    const replacement=launch('bridge-reconnected',['tools/geocaching/packet_bridge.py','--port',String(connection.port),
      '--tcp-port',String(bridgeTcpPort),'--origin',origin,'--run-seconds','90']);
    await waitReady(replacement,'bridge_ready');
    await page.waitForFunction(()=>!document.querySelector('#search').disabled,{},{timeout:60000});
    await page.locator('#search').click();
    await page.waitForFunction(()=>document.querySelector('#notice').textContent.startsWith('Loaded from live directory responses.'),{},{timeout:45000});
    assert.equal(await page.locator('#results li').count(),2);
    console.log(JSON.stringify({event:'browser_reconnected_without_reload'}));
  }
  assert.ok(packetSizes.length>10 && packetSizes.every(size=>size>19&&size<=500),'Bridge carries bounded raw RNS packets');
  assert.deepEqual(errors,[]);
  await writeFile(resolve(work,'browser.log'),consoleLog.join('\n'));
  await writeFile(resolve(work,'result.json'),JSON.stringify({status:'passed',rawPackets:packetSizes.length,maxPacket:Math.max(...packetSizes),
    gpxBytes:Buffer.byteLength(xml),subpath:'/trail-mate/geocaching/',activeUpstreamCut:values['multi-upstream'],recoveryMs,bridgeReconnect:values.reconnect,initialDialRecovery:values['late-bridge'],resource:'large signed record verified and downloaded'},null,2));
  console.log(JSON.stringify({event:'browser_lxmf_gpx_verified',rawPackets:packetSizes.length,gpxBytes:Buffer.byteLength(xml),work}));
} catch (error) {
  if (page) {
    await page.screenshot({path:resolve(work,'failure.png'),fullPage:true}).catch(()=>{});
    await writeFile(resolve(work,'page-state.txt'),await page.locator('body').innerText()).catch(()=>{});
  }
  throw error;
} finally {
  await writeFile(resolve(work,'browser.log'),consoleLog.join('\n'));
  await browser?.close();
  await writeFile(resolve(work,'directory','stop'),'').catch(()=>{});
  await writeFile(resolve(work,'gateway','stop'),'').catch(()=>{});
  for(const entry of children) {
    if(entry.name.startsWith('bridge')&&entry.child.exitCode===null)entry.child.kill();
    let timer;
    await Promise.race([entry.done,new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error(`${entry.name} shutdown timed out`)),15000);})])
      .catch(()=>entry.child.kill()).finally(()=>clearTimeout(timer));
    await writeFile(resolve(work,`${entry.name}.log`),entry.output);
  }
  await new Promise(r=>http.close(r));
}
