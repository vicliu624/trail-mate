// Actual built maintenance page, Worker, browser RNS/LXMF and native directory.
// USB is simulated by default; explicit hardware mode forwards real Wio frames.
import {chromium} from '../../site/node_modules/playwright/index.mjs';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {FrameDecoder,encodeFrame,FRAME} from '../../site/device/src/hostlink.js';
import {decodeManagement,encodeManagement} from '../../site/device/src/management-protocol.js';
import {authorProofMessage} from '../../site/device/src/author-identity.js';
import {encode,decode,equal,verifySigned} from '../../site/geocaching/src/protocol.js';
import {verifyPublicationReceipt} from '../../site/geocaching/src/publication-receipt.js';
import {spawn,spawnSync} from 'node:child_process';import http from 'node:http';import net from 'node:net';import path from 'node:path';import fs from 'node:fs/promises';import assert from 'node:assert/strict';
import {locales} from '../../site/i18n/registry.js';
import {geocachingIpPublicationText} from '../../site/i18n/geocaching-ip-publication-labels.js';
import {geocachingDeviceSyncText} from '../../site/i18n/geocaching-device-sync-labels.js';
import {GeocachingHardwareExchange} from '../../tools/hostlink/geocaching_hardware_exchange.mjs';
import {saveIPPublication} from '../../site/geocaching/src/ip-publication-journal.js';
const args=process.argv.slice(2),option=name=>args[args.indexOf(name)+1],hardwarePort=args.includes('--hardware-port')?option('--hardware-port'):null;
const releaseDocs=args.includes('--release-docs');
const releaseOutput=process.env.TRAIL_MATE_DOCS_OUTPUT??'docs/images/release-1.0.0';
await fs.mkdir(releaseOutput,{recursive:true});
if(releaseDocs&&hardwarePort)throw Error('Documentation captures require the isolated synthetic author, never a physical device');
if(hardwarePort&&(!/^COM\d+$/.test(hardwarePort)||!args.includes('--serial-python')||!args.includes('--expected-author')||!/^[a-f0-9]{64}$/.test(option('--expected-author'))))throw Error('Hardware mode requires --hardware-port COMxx --serial-python <pyserial Python> --expected-author <fingerprint>');
const resumeDraftId=args.includes('--resume-draft')?option('--resume-draft'):null;
const resumeConfirmed=args.includes('--resume-confirmed-record');
const signatureCheckpoint=args.includes('--resume-signature-checkpoint')?option('--resume-signature-checkpoint'):null;
if(signatureCheckpoint&&!resumeConfirmed)throw Error('Signature resume requires the original verified v1 record and receipt');
if(resumeConfirmed&&(!resumeDraftId||!args.includes('--resume-confirmed-receipt')))throw Error('Confirmed resume requires the exact draft, signed v1 record and directory receipt');
if(resumeDraftId&&(!hardwarePort||!args.includes('--resume-name')||!/^Wio网页验收-[a-z0-9]+$/.test(option('--resume-name'))||!/^[a-f0-9]{32}$/.test(resumeDraftId)))throw Error('Resume requires explicit hardware, isolated acceptance name and ID');
const testName=hardwarePort?(resumeDraftId?option('--resume-name'):'Wio网页验收-'+Date.now().toString(36)):releaseDocs?'Riverside trail cache':'真实网络网页藏宝点',updatedName=hardwarePort?testName+'-v2':releaseDocs?'Riverside trail cache · updated':'第二版网络藏宝点';
const python=process.env.TRAIL_MATE_TRACK_PYTHON??path.resolve('.codex-build/reticulum-directory/venv/'+(process.platform==='win32'?'Scripts/python.exe':'bin/python'));
const work=path.resolve(hardwarePort?'.codex-build/hostlink-wio-l2/geocaching-network-page':'.codex-build/hostlink-codec/geocaching-network-page'),state=path.join(work,'state'),root=path.resolve('site');await fs.mkdir(work,{recursive:true});assert.equal(path.dirname(state),work);await fs.mkdir(state,{recursive:true});await fs.writeFile(path.join(work,'NOTES.txt'),hardwarePort?'Explicit real Wio L2 USB frames through a test browser API bridge, actual page/Worker/native loopback directory. Creates one isolated test geocache and three versions; no public Internet/RF/config/key operation. Created2026-10-13. Retain compact acceptance logs/public signed records; existing device records must remain intact.':'Actual built Geocaching maintenance page/Worker and native loopback service. Synthetic USB identity only, never physical device/RF/public directory. Created2026-10-13, current working branch. Retain compact logs/results until acceptance; synthetic state reused for debugging, cleanup after verification.');
const children=[],sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));let endpoint,seeds,browser,service,bridge,hardware;
function start(label,args){const child=spawn(python,args,{windowsHide:true,stdio:['ignore','pipe','pipe']}),entry={child,output:'',label};const append=bytes=>{entry.output+=bytes;};child.stdout.on('data',append);child.stderr.on('data',append);entry.done=new Promise((resolve,reject)=>{child.on('error',reject);child.on('exit',resolve);});children.push(entry);return entry;}
async function ready(entry,event){const deadline=Date.now()+20000;while(Date.now()<deadline){for(const line of entry.output.split('\n'))try{const row=JSON.parse(line);if(row.event===event)return row;}catch{}if(entry.child.exitCode!==null)throw Error(entry.output);await sleep(100);}throw Error('Not ready: '+entry.output);}
async function freePort(){const socket=net.createServer();await new Promise(resolve=>socket.listen(0,'127.0.0.1',resolve));const port=socket.address().port;await new Promise(resolve=>socket.close(resolve));return port;}
const server=http.createServer(async(req,res)=>{try{const name=new URL(req.url,'http://localhost').pathname;if(name==='/trail-mate/geocaching/network.json'){res.setHeader('Content-Type','application/json');res.end(JSON.stringify({endpoint,discoverySeeds:seeds}));return;}if(!name.startsWith('/trail-mate/'))throw Error('prefix');const file=path.resolve(root,name.slice(12)+(name.endsWith('/')?'index.html':''));if(!file.startsWith(root+path.sep))throw Error('path');res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css','.json':'application/json','.png':'image/png','.svg':'image/svg+xml','.woff2':'font/woff2','.ttf':'font/ttf'})[path.extname(file)]??'application/octet-stream');res.end(await fs.readFile(file));}catch{res.writeHead(404).end();}});await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const origin='http://127.0.0.1:'+server.address().port;
const author=await Identity.generate(),publicKey=await author.getPublicKey(),nameHash=(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10),destination=await Identity.truncatedHash(Uint8Array.from([...nameHash,...author.identityHash])),info=[publicKey,author.identityHash,destination,7];
const drafts=new Map(),transfers=new Map(),signatures=new Map(),confirmations=new Map(),signedVersions=[],calls=[];let catalog=0,signCount=0,confirmCount=0;
let resumeBaseHash=null,resumeSignature=null,publicationSeed=[];
if(resumeConfirmed){
 const signed=JSON.parse(await fs.readFile(option('--resume-confirmed-record'),'utf8')).map(value=>Uint8Array.from(value)),verified=await verifySigned(signed),record=verified.record;
 assert.ok(equal(record[2],Uint8Array.from(Buffer.from(resumeDraftId,'hex'))));assert.equal(record[3],1);assert.equal(record[5],0);assert.equal(record[8],testName);assert.equal(record[6],310000000);assert.equal(record[7],1210000000);
 assert.equal(Buffer.from(await crypto.subtle.digest('SHA-256',record[1])).toString('hex'),option('--expected-author'));
 const receipt=Object.fromEntries(Object.entries(JSON.parse(await fs.readFile(option('--resume-confirmed-receipt'),'utf8'))).map(([key,value])=>[key,Uint8Array.from(value)]));await verifyPublicationReceipt([receipt.raw,receipt.signature],signed,receipt);
 resumeBaseHash=verified.summary[2].slice();signedVersions.push(signed);
 if(signatureCheckpoint){
  const checkpoint=JSON.parse(await fs.readFile(signatureCheckpoint,'utf8'));
  const raw=Uint8Array.from(Buffer.from(checkpoint.unsignedInput,'base64')),recordV2=decode(raw,4096);
  assert.equal(checkpoint.version,1);assert.equal(checkpoint.authorFingerprint,option('--expected-author'));
  assert.equal(checkpoint.testName,testName);assert.equal(checkpoint.draftId,resumeDraftId);
  assert.equal(raw.length,checkpoint.bytes);assert.equal(Buffer.from(raw).toString('base64'),checkpoint.unsignedInput);
  assert.equal(Buffer.from(await crypto.subtle.digest('SHA-256',raw)).toString('hex'),checkpoint.hash);
  assert.ok(equal(recordV2[1],record[1])&&equal(recordV2[2],record[2])&&equal(recordV2[4],resumeBaseHash));
  assert.equal(recordV2[3],2);assert.equal(recordV2[5],0);assert.equal(recordV2[6],310000000);assert.equal(recordV2[7],1210000000);
  assert.equal(recordV2[8],updatedName);assert.equal(recordV2[14],record[14]);assert.ok(recordV2[15]>=record[15]);
  for(const key of ['operationId','transferId','owner'])assert.ok(/^[a-f0-9]{32}$/.test(checkpoint[key])&&!/^0+$/.test(checkpoint[key]));
  resumeSignature={raw,hash:Uint8Array.from(Buffer.from(checkpoint.hash,'hex')),transferId:Uint8Array.from(Buffer.from(checkpoint.transferId,'hex')),
   draftGeneration:BigInt(checkpoint.draftGeneration),authorGeneration:checkpoint.authorGeneration};
  const values=new Map(),storage={getItem:key=>values.get(key)??null,setItem:(key,value)=>values.set(key,value)};
  saveIPPublication({stage:'signing',authorKey:recordV2[1],authorGeneration:checkpoint.authorGeneration,draftId:recordV2[2],draftGeneration:resumeSignature.draftGeneration,
   operationId:Uint8Array.from(Buffer.from(checkpoint.operationId,'hex')),transferId:resumeSignature.transferId,hash:resumeSignature.hash,raw,signature:null,directoryKey:Buffer.from(receipt.delivery).toString('hex')},{storage});
  publicationSeed=[...values];
 }
}
if(args.includes('--verify-signature-fixture-only')){
 assert.ok(resumeSignature,'Exact signature fixture required');await new Promise(resolve=>server.close(resolve));
 console.log(JSON.stringify({verified:true,draft:resumeDraftId,revision:2,bytes:resumeSignature.raw.length,originalUnsignedInput:true}));process.exit(0);
}
const countSignatures=()=>hardware?.signCount??signCount,countConfirmations=()=>hardware?.confirmCount??confirmCount;
const hex=bytes=>Buffer.from(bytes).toString('hex');
async function usbReply(raw){const frame=new FrameDecoder().push(Uint8Array.from(raw))[0];assert.ok(frame);if(frame.type===FRAME.HELLO)return [...encodeFrame(FRAME.HELLO_ACK,frame.sequence,new Uint8Array([1,0,0,2,0,4,0,0,6,...new TextEncoder().encode('T-Deck'),4,...new TextEncoder().encode('test')]))];const request=decodeManagement(frame.payload),body=request.body;calls.push(request.code);let response;
 if(request.code===0)response=[1,512,320,1,[0,1,2,3,32,33,35,36,64,65,66,71,72,73,75,76],7];
 else if(request.code===1)response=info;
 else if(request.code===2)response=[...info,await author.sign(authorProofMessage(request.session,body[0],publicKey,7))];
 else if(request.code===3)response=[];
 else if(request.code===32){let transfer=transfers.get(hex(body[0]));if(!transfer){transfer={id:body[0],kind:body[1],total:body[2],hash:body[3],owner:request.session,bytes:[],sealed:false};transfers.set(hex(body[0]),transfer);}assert.ok(equal(transfer.owner,request.session));response=[transfer.id,320,transfer.bytes.length,transfer.sealed?1:0];}
 else if(request.code===33){const transfer=transfers.get(hex(body[0]));assert.equal(body[1],transfer.bytes.length);transfer.bytes.push(...body[2]);response=[transfer.id,320,transfer.bytes.length,0];}
 else if(request.code===35){const transfer=transfers.get(hex(body[0]));assert.equal(transfer.total,transfer.bytes.length);assert.ok(equal(await Identity.fullHash(Uint8Array.from(transfer.bytes)),transfer.hash));transfer.sealed=true;response=[transfer.id,320,transfer.bytes.length,1];}
 else if(request.code===36){transfers.delete(hex(body[0]));response=[];}
 else if(request.code===66){const transfer=transfers.get(hex(body[0]));assert.ok(transfer.sealed);const edit=decode(Uint8Array.from(transfer.bytes)),previous=drafts.get(hex(body[1]));assert.equal(previous?.value[3]??0,body[2]);assert.equal(body[3],7);const value=[previous?.value[0]??null,previous?.value[1]??null,edit[2],edit[3]];drafts.set(hex(body[1]),{id:body[1],value,revision:previous?.revision??0,confirmed:previous?.confirmed??0,localChanges:Boolean(previous?.revision)});catalog++;response=[body[0],body[1],edit[3]];}
 else if(request.code===64){const row=[...drafts.values()][body[0]];response=[catalog,body[0],drafts.size,row?[row.id,row.value[3],row.value[0],row.value[2][3],row.value[2][0],row.value[2][1],row.value[2][2],row.value[1],[row.revision,row.confirmed,row.localChanges?4:0]]:null];}
 else if(request.code===65){const row=drafts.get(hex(body[0])),raw=encode(row.value);response=[row.id,row.value[3],raw.length,body[1],raw.slice(body[1],body[1]+body[2])];}
 else if(request.code===71){const transfer=transfers.get(hex(body[0]));assert.ok(transfer.sealed&&transfer.kind===5&&equal(transfer.hash,body[1]));const draft=drafts.get(hex(body[5]));assert.equal(BigInt(draft.value[3]),BigInt(body[6]));const content=Uint8Array.from(transfer.bytes),signed=[content,await author.sign(Uint8Array.from([...new TextEncoder().encode('trailmate.geocache/sign/v1\0'),...content]))];await verifySigned(signed);const value={operation:body[4],hash:body[1],output:encode(signed)};signatures.set(hex(body[4]),value);signedVersions.push(signed);signCount++;response=[body[4],body[1],7,1,value.output.length];}
 else if(request.code===72){const task=signatures.get(hex(body[0]));response=[task.operation,task.hash,7,1,task.output.length];}
 else if(request.code===73){const task=signatures.get(hex(body[0]));response=[task.operation,body[1],task.output.length,task.hash,task.output.slice(body[1],body[1]+body[2])];}
 else if(request.code===75){const transfer=transfers.get(hex(body[0])),signed=decode(Uint8Array.from(transfer.bytes));assert.equal(transfer.kind,6);assert.ok(equal(transfer.hash,body[1]));const proof=decode(body[3]);await verifyPublicationReceipt([body[3],body[4]],signed,{publicKey:body[5],delivery:proof[1],epoch:body[6]});const verified=await verifySigned(signed),row=drafts.get(hex(verified.record[2]));row.value[0]=publicKey;row.value[1]=verified.summary[2];row.value[3]++;row.revision=row.confirmed=verified.record[3];row.localChanges=false;catalog++;confirmCount++;response=[body[0],body[1],7,1,verified.summary[0],verified.record[3],verified.summary[2],1];confirmations.set(hex(body[0]),response);}
 else if(request.code===76)response=confirmations.get(hex(body[0]));
 else assert.fail('Unexpected USB operation '+request.code);
 return [...encodeFrame(FRAME.MANAGEMENT_RESPONSE,frame.sequence,encodeManagement(request.session,request.request,0,response))];}
try{
 if(hardwarePort)hardware=new GeocachingHardwareExchange({port:hardwarePort,python:option('--serial-python'),fingerprint:option('--expected-author'),namePrefix:testName,resumeDraftId,resumeBaseHash,resumeSignature,calls,signedVersions,checkpointPath:path.join(work,'signature-recovery.json')});
 await fs.rm(path.join(work,'result.json'),{force:true});
 await fs.rm(path.join(state,'stop'),{force:true});const tcp=await freePort();service=start('directory',['tools/geocaching/browser_test_service.py','--state',state,'--port',String(tcp),'--empty','--startup-delay','0','--run-seconds','600']);const directory=await ready(service,'ready');seeds=[directory.discovery];bridge=start('bridge',['tools/geocaching/packet_bridge.py','--port','0','--tcp-port',String(tcp),'--origin',origin,'--run-seconds','600']);endpoint='ws://127.0.0.1:'+(await ready(bridge,'bridge_ready')).port;
 browser=await chromium.launch({channel:process.env.TRAIL_MATE_TEST_BROWSER_CHANNEL,headless:true});const context=await browser.newContext({viewport:{width:1440,height:960}});await context.exposeFunction('deviceReply',hardware?raw=>hardware.exchange(raw):usbReply);await context.addInitScript(()=>{let input;const port={async open(){this.readable=new ReadableStream({start(controller){input=controller;}});this.writable=new WritableStream({async write(bytes){input.enqueue(Uint8Array.from(await window.deviceReply([...bytes])));}});},async setSignals(){},async close(){}};Object.defineProperty(navigator,'serial',{configurable:true,value:{requestPort:async()=>port}});});await context.route('https://tile.openstreetmap.org/**',route=>route.fulfill({contentType:'image/svg+xml',body:'<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256"><rect width="256" height="256" fill="#dceae4"/></svg>'}));
 if(publicationSeed.length)await context.addInitScript(entries=>{if(location.protocol==='http:')for(const [key,value] of entries)if(localStorage.getItem(key)===null)localStorage.setItem(key,value);},publicationSeed);
 let page=await context.newPage();const errors=[];page.on('pageerror',error=>errors.push(String(error)));await page.goto(origin+'/trail-mate/geocaching/?lang=zh-Hans');await page.waitForFunction(()=>document.querySelector('#search')&&!document.querySelector('#search').disabled,{},{timeout:60000});await page.getByRole('button',{name:'草稿',exact:true}).click();await page.getByRole('button',{name:'连接设备',exact:true}).click();await page.getByText('USB 作者已验证',{exact:true}).waitFor();
 if(resumeDraftId){if(resumeConfirmed)await page.getByRole('button',{name:'我的藏宝点',exact:true}).click();await page.getByRole('button',{name:'刷新设备列表',exact:true}).click();await openTestDraft(resumeSignature?updatedName:testName);assert.ok(hardware.drafts.has(resumeDraftId));}
 else{await page.getByRole('button',{name:'新建草稿',exact:true}).click();await page.getByLabel('名称',{exact:true}).fill(testName);await page.getByLabel('正文',{exact:true}).fill('网页中文正文 <script>window.__geoNetworkInjection=true</script>\n'.repeat(25));await page.getByLabel('提示',{exact:true}).fill('&< >'.repeat(80));await page.getByLabel('纬度',{exact:true}).fill('31');await page.getByLabel('经度',{exact:true}).fill('121');await page.getByRole('button',{name:'保存草稿到设备',exact:true}).click();await page.getByText('草稿已保存到设备。刷新列表可更新发布状态。',{exact:true}).waitFor({timeout:45000});}
 async function publishAndSync(){
  try{
   await page.getByRole('button',{name:'刷新 IP 目录',exact:true}).click();
   await page.getByRole('button',{name:'检查并确认发布',exact:true}).click();
   assert.equal(await page.locator('dialog[open] script').count(),0);
   await page.locator('dialog[open]').getByRole('button',{name:'通过 IP 网络发布',exact:true}).click();
   await page.locator('.geo-publication-task').getByText('发布内容与目录签名证明已归档到设备。',{exact:true}).waitFor({timeout:60000});
  }catch(error){
   console.error('Publication workflow diagnostic',JSON.stringify({
    signatures:countSignatures(),confirmations:countConfirmations(),pageErrors:errors,
    status:await page.locator('.geo-publication-panel > p[role=status]').allTextContents(),
    task:await page.locator('.geo-publication-task > p[role=status]').allTextContents(),
    openDialogs:await page.locator('dialog[open]').count(),
   }));
   throw error;
  }
 }
 async function openTestDraft(name){for(let index=0;index<128;index++){await page.waitForFunction(()=>[...document.querySelectorAll('button')].some(button=>button.textContent==='刷新设备列表'&&!button.disabled),{},{timeout:45000});const row=page.locator('.geo-draft-row').filter({hasText:name});if(await row.count()){await row.click();await page.getByLabel('名称',{exact:true}).waitFor();return;}const more=page.getByRole('button',{name:'加载更多设备草稿',exact:true});if(!await more.count())throw Error('Isolated acceptance draft not present in current device catalogue');await more.click();}throw Error('Device catalogue acceptance search exceeded bounded page count');}
 if(resumeSignature){
  await page.locator('.geo-publication-task').getByRole('button',{name:geocachingIpPublicationText('zh-Hans').resume,exact:true}).click();
  await page.locator('.geo-publication-task').getByText('发布内容与目录签名证明已归档到设备。',{exact:true}).waitFor({timeout:90000});
 }else{
  if(!resumeConfirmed){await publishAndSync();assert.equal(countSignatures(),1);assert.equal(countConfirmations(),1);}
  await page.getByRole('button',{name:'我的藏宝点',exact:true}).click();await openTestDraft(testName);
  await page.getByLabel('名称',{exact:true}).fill(updatedName);await page.getByRole('button',{name:'保存草稿到设备',exact:true}).click();
  await page.getByText('草稿已保存到设备。刷新列表可更新发布状态。',{exact:true}).waitFor();await publishAndSync();
 }
 await openTestDraft(updatedName);await page.getByLabel('状态',{exact:true}).selectOption('2');
 await page.getByRole('button',{name:'保存草稿到设备',exact:true}).click();await page.getByText('草稿已保存到设备。刷新列表可更新发布状态。',{exact:true}).waitFor();
 await publishAndSync();assert.equal(countSignatures(),resumeConfirmed?2:3);assert.equal(countConfirmations(),resumeConfirmed?2:3);
 assert.equal((await verifySigned(signedVersions[2])).record[5],2);
 if(hardware)await page.locator('.geo-publication-panel').screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/geocaching-wio-network-archive.png'});
 else if(releaseDocs){
  await page.locator('.management-language select').selectOption('en');
  await page.locator('.geo-publication-task').getByText(geocachingIpPublicationText('en').confirmed,{exact:true}).waitFor();
  await page.locator('.geo-publication-task').screenshot({path:`${releaseOutput}/web-geocaching-maintenance.png`});
  await page.locator('.management-language select').selectOption('zh-Hans');
 }else await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/geocaching-real-network-archive.png',fullPage:true});
 await context.grantPermissions(['clipboard-read','clipboard-write'],{origin});const expectedId=(await verifySigned(signedVersions[2])).cacheId;await page.getByRole('button',{name:'复制藏宝点 ID',exact:true}).click();assert.equal(await page.evaluate(()=>navigator.clipboard.readText()),expectedId);
 const priorCalls=calls.length;for(const {id} of locales){await page.locator('.management-language select').selectOption(id);await page.locator('.geo-publication-task').getByText(geocachingIpPublicationText(id).confirmed,{exact:true}).waitFor();await page.locator('.geo-publication-task').getByText(geocachingDeviceSyncText(id).confirmed,{exact:true}).waitFor();await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth+1),false);assert.equal(calls.length,priorCalls);}await page.locator('.management-language select').selectOption('zh-Hans');await page.setViewportSize({width:1440,height:960});
 const native=spawnSync(python,['tools/geocaching/verify_browser_publication.py','--database',path.join(state,'service','directory.sqlite')],{input:JSON.stringify(signedVersions.map(item=>item.map(part=>[...part]))),windowsHide:true,maxBuffer:1048576});assert.equal(native.status,0,String(native.stderr));const nativeCheck=JSON.parse(native.stdout);assert.equal(nativeCheck.transportDiffersFromAuthor,true);
 await page.reload();await page.getByRole('button',{name:'我的藏宝点',exact:true}).click();await page.getByRole('button',{name:'连接设备',exact:true}).click();await page.getByText('USB 作者已验证',{exact:true}).waitFor();await page.locator('.geo-publication-task').getByText('发布内容与目录签名证明已归档到设备。',{exact:true}).waitFor();assert.equal(countSignatures(),resumeConfirmed?2:3);assert.equal(countConfirmations(),resumeConfirmed?2:3);assert.equal(await page.evaluate(()=>window.__geoNetworkInjection),undefined);assert.ok(signedVersions.every(item=>item[0].length>512));assert.ok(!calls.some(code=>[7,16,17,20,21,67,68,70].includes(code)));assert.deepEqual(errors,[]);await fs.writeFile(path.join(work,'result.json'),JSON.stringify({passed:true,actualPage:true,actualWorker:true,nativeNetwork:true,nativeWebSerial:false,usb:hardware?'physical Wio L2 via restricted browser serial API test bridge':'synthetic independent identity with real signatures',author:hardware?{fingerprint:hardware.author.fingerprint,generation:hardware.author.generation}:undefined,cacheId:expectedId,created:true,resumedVerifiedV1:resumeConfirmed,updated:true,archived:true,directoryProof:true,deviceConfirmation:true,refreshRecovery:true,signatures:countSignatures(),confirmations:countConfirmations(),rfRequests:0,configWrites:0,keyExports:0,locales:locales.length,mobileWidth:390,clipboardId:true,resourceSizedContent:true,nativeCheck},null,2));console.log('Actual Geocaching page + Worker + native network: create/update/archive/proof/device confirmation and refresh passed. USB: '+(hardware?'physical Wio L2':'synthetic'));
}finally{try{if(browser){const pages=browser.contexts().flatMap(context=>context.pages());for(const page of pages){await fs.writeFile(path.join(work,'page-state.json'),JSON.stringify({signatures:countSignatures(),confirmations:countConfirmations(),operations:calls,responses:hardware?.responses,diagnostics:hardware?.stderr,status:await page.locator('.geo-maintenance-list p').first().textContent().catch(()=>null),publication:await page.locator('.geo-publication-panel').textContent().catch(()=>null)},null,2));if(hardware)await page.locator('.geo-publication-panel').screenshot({path:path.join(work,'page.png')}).catch(()=>{});else await page.screenshot({path:path.join(work,'page.png'),fullPage:true}).catch(()=>{});}}}finally{await browser?.close();try{await hardware?.close();}finally{if(service&&service.child.exitCode===null){await fs.writeFile(path.join(state,'stop'),'stop');await Promise.race([service.done,sleep(15000)]);}for(const entry of children){if(entry.child.exitCode===null)entry.child.kill();await entry.done;await fs.writeFile(path.join(work,entry.label+'.log'),entry.output);}await new Promise(resolve=>server.close(resolve));}}}
