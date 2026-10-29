import {chromium} from '../../site/node_modules/playwright/index.mjs';
import http from 'node:http';
import fs from 'node:fs/promises';
import path from 'node:path';
import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {FrameDecoder,encodeFrame,FRAME,CAPABILITY} from '../../site/device/src/hostlink.js';
import {decodeManagement,encodeManagement,OPERATION} from '../../site/device/src/management-protocol.js';
import {Identity} from '../../site/node_modules/@reticulum/core/src/index.js';
import {authorProofMessage} from '../../site/device/src/author-identity.js';
import {TmsDocument} from '../../site/device/src/tms-document.js';
import {encryptConfigBackup,decryptConfigBackup,decryptConfigBackupDetails,isEncryptedConfigBackup} from '../../site/device/src/config-backup.js';
import {locales} from '../../site/i18n/registry.js';
import {configBackupText} from '../../site/i18n/config-backup-labels.js';
import {configInputText} from '../../site/i18n/config-input-labels.js';
import {configMeaningText} from '../../site/i18n/config-meaning-labels.js';
import {configMqttRuntimeText} from '../../site/i18n/config-mqtt-runtime-labels.js';
import {meshCoreRegionOptions} from '../../site/device/src/meshcore-region-presets.js';
import {configCharacterText} from '../../site/i18n/config-character-labels.js';
import {configDurationText} from '../../site/i18n/config-duration-labels.js';
import {configModuleDurationText} from '../../site/i18n/config-module-duration-labels.js';
import {configPrecisionText} from '../../site/i18n/config-precision-labels.js';
import {configPositionRuntimeText} from '../../site/i18n/config-position-runtime-labels.js';
import {positionPrecisionOptions,formatPositionPrecision} from '../../site/device/src/config-position-precision.js';
import {configurationRadioTuning,formatConfigurationRadioTuning} from '../../site/device/src/config-radio-tuning.js';
import {configRadioTuningText} from '../../site/i18n/config-radio-tuning-labels.js';
import {configReticulumInterfaceText} from '../../site/i18n/config-reticulum-interface-labels.js';
import {configRadioValueText} from '../../site/i18n/config-radio-values-labels.js';
import {configNumericPurposeText} from '../../site/i18n/config-numeric-purpose-labels.js';
import {configRoutingValueText} from '../../site/i18n/config-routing-values-labels.js';
import {configurationFrequency,frequencyInputDisplay} from '../../site/device/src/config-frequency.js';
import {configAprsSymbolNames} from '../../site/i18n/config-aprs-symbol-labels.js';
import {configSchemaText} from '../../site/i18n/config-schema-labels.js';
import {configCollectionText} from '../../site/i18n/config-collection-labels.js';
import {deviceAuthorText} from '../../site/i18n/device-author-labels.js';
import {configActionText} from '../../site/i18n/config-action-labels.js';
import {configConnectionText} from '../../site/i18n/config-connection-labels.js';
import {configTransferText} from '../../site/i18n/config-transfer-labels.js';
import {configImportText} from '../../site/i18n/config-import-labels.js';
import {configNetworkFieldText} from '../../site/i18n/config-network-field-labels.js';
import {configCollectionIssueText} from '../../site/i18n/config-collection-issue-labels.js';
import {configBasicFieldText} from '../../site/i18n/config-basic-field-labels.js';
import {configSecretText} from '../../site/i18n/config-secret-labels.js';
import {configPositionText} from '../../site/i18n/config-position-labels.js';
import {configModuleText,configModuleFieldLabel} from '../../site/i18n/config-module-labels.js';
import {readMeshtasticModule} from '../../site/device/src/meshtastic-module-document.js';
import {readStructuredModule} from '../../site/device/src/meshtastic-module-model.js';
import {C6_CONFIG_FIELDS} from '../../site/device/src/config-c6-validation.js';
import {configC6Text} from '../../site/i18n/config-c6-labels.js';
import {configC6EffectiveText} from '../../site/i18n/config-c6-effective-labels.js';
import {configC6PasswordText} from '../../site/i18n/config-c6-password-labels.js';
import {configFinderText} from '../../site/i18n/config-finder-labels.js';
import {configReviewText} from '../../site/i18n/config-review-labels.js';
import {configMeshChannelText} from '../../site/i18n/config-mesh-channel-labels.js';
import {configReadbackText} from '../../site/i18n/config-readback-labels.js';
import {configRadioPolicyText} from '../../site/i18n/config-radio-policy-labels.js';
import {configTimezoneText} from '../../site/i18n/config-timezone-labels.js';
import {updateStructuredModuleField} from '../../site/device/src/meshtastic-module-model.js';
const root=process.cwd();
const releaseOutput=process.env.TRAIL_MATE_DOCS_OUTPUT??'docs/images/release-1.0.0';
await fs.mkdir(releaseOutput,{recursive:true});
// Node and Edge ship different ICU defaults for Arabic numbering. Compare
// translated copy/units exactly while accepting either native digit system.
const precisionCopy=value=>value.replace(/[٠-٩]/g,digit=>String('٠١٢٣٤٥٦٧٨٩'.indexOf(digit))).replaceAll('٫','.');
const server=http.createServer(async(req,res)=>{try{let pathname=decodeURIComponent(new URL(req.url,'http://localhost').pathname);if(pathname.endsWith('/'))pathname+='index.html';const filename=path.resolve(root,'.'+pathname);if(!filename.startsWith(root+path.sep))throw Error('Invalid path');const contents=await fs.readFile(filename);res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css','.png':'image/png','.svg':'image/svg+xml','.webp':'image/webp','.woff2':'font/woff2','.ttf':'font/ttf'})[path.extname(filename)]??'application/octet-stream');res.end(contents);}catch{res.writeHead(404).end();}});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const origin=`http://127.0.0.1:${server.address().port}`;
const browser=await chromium.launch({channel:process.env.TRAIL_MATE_TEST_BROWSER_CHANNEL,headless:true});
const page=await browser.newPage({viewport:{width:1440,height:1000}});
const errors=[];page.on('pageerror',e=>errors.push(String(e)));
const collectionLines=['rt.net.interface_count=u8:1','wifi.enabled=bool:1','wifi.profile_count=u8:2',
 'mc.channel.0.enabled=bool:1','mc.channel.0.name=str:Primary','mc.channel.1.enabled=bool:1',
 'mc.active_slot=u8:0',`mc.base.secondary_psk=b64:${Buffer.from(new Uint8Array(16).fill(11)).toString('base64')}`,`mc.channel.0.key=b64:${Buffer.from(new Uint8Array(16).fill(11)).toString('base64')}`,`mc.channel.1.key=b64:${Buffer.from(new Uint8Array(16).fill(22)).toString('base64')}`,
 'mc.send_profile=u8:1','mc.forward_profile=u8:1','rt.interface_policy=u8:0',
 'mc.region_preset=u8:0','mc.frequency_mhz=f32:869.525','mc.bandwidth_khz=f32:250','mc.spread_factor=u8:11','mc.coding_rate=u8:5','mc.base.tx_power=i8:20',
 'ui.timezone.profile=i32:1','ui.timezone.offset_minutes=i32:480',
 'wifi.profile.0.ssid=str:Home','wifi.profile.0.password=str:secret','wifi.profile.1.ssid=str:Camp','wifi.profile.1.password=str:',
 'wifi.profile.1.vendor=opaque:future','chat.auto_reply.text=b64:SGVsbG8=','mt_ble.mode=u8:1','mt_ble.fixed_pin=u32:123456','mc_ble.pin=u32:123456',
 'mt_module.format=str:pb-v1','mt_module.size=u16:2','mt_module.chunk_count=u8:3','mt_module.chunk.0=b64:QAE=','mt_module.chunk.1=b64:','mt_module.chunk.2=b64:'];
for(const [name,spec] of Object.entries(C6_CONFIG_FIELDS))collectionLines.push(`c6_companion.${name}=${spec.type}:${spec.type==='bool'?0:spec.type==='str'?'':spec.min}`);
collectionLines.push('chat.channel=u8:0','privacy.encrypt_mode=u8:1','chat.alerts.message=i32:1','chat.alerts.contact=i32:1','gps.strategy=u8:1','gps.alt_ref=u8:0','gps.init_profile=u8:2','gps.init_rxm_policy=u8:0','gps.init_gnss_policy=u8:0','gps.init_nmea_policy=u8:0','gps.init_baud=u32:9600','ui.display.brightness=u8:128','ui.audio.speaker_volume=u8:45');
collectionLines.push('gps.motion_sensor=u8:143','aprs.symbol_table=u8:47','aprs.symbol_code=u8:91');
collectionLines.push('mc_ble.telemetry_base=u8:0','mc_ble.telemetry_location=u8:1','mc_ble.telemetry_environment=u8:3','mc_ble.advert_location_policy=u8:255');
collectionLines.push('device.ble_enabled=bool:0','mc_ble.manual_add_contacts=bool:0');
collectionLines.push('channel.primary_has_module=bool:0','channel.secondary_has_module=bool:0','channel.primary_position_precision=u32:0','channel.secondary_position_precision=u32:32');
collectionLines.push('mc.base.bandwidth_khz=f32:125','rt.bandwidth_khz=f32:125','mc.rx_delay_base=f32:0','mc.airtime_factor=f32:1','mc.flood_max=u8:16','mt.frequency_offset_mhz=f32:0');
collectionLines.push('mt.override_frequency_mhz=f32:0','mc.base.frequency_offset_mhz=f32:0','mc.base.override_frequency_mhz=f32:0','rt.frequency_offset_mhz=f32:0','rt.override_frequency_mhz=f32:0','mt.tx_power=i8:0','rt.tx_power=i8:0','mt.channel_num=u16:0');
collectionLines.push('gps.init_probe_ms=u32:900','gps.interval_ms=u32:60000','gps.motion_idle_ms=u32:300000','rt.net.propagation.sync_interval_s=u32:900','aprs.tx_min_interval_s=u16:0','aprs.dedupe_window_s=u16:30','aprs.position_interval_s=u16:60','ui.screen.timeout_ms=u32:30000','map.track_interval=u8:99');
collectionLines.push('aprs.igate_ssid=u8:0','power.gauge.design_mah=u32:1500','power.gauge.full_mah=u32:1500','rt.net.propagation.max_messages_per_sync=u8:32','mt.mqtt.port=u16:1883','mc.mqtt.port=u16:1883');
collectionLines.push('policy.hop_limit=u8:2','policy.max_retries=u8:1','policy.max_channels=u8:3','mt.hop_limit=u8:2','mc.base.hop_limit=u8:2','rt.hop_limit=u8:2','mc.base.channel_num=u16:0','rt.channel_num=u16:0');
for(const prefix of ['mt','mc.base','rt'])for(const channel of ['primary','secondary'])collectionLines.push(`${prefix}.${channel}_channel_id=u32:0`);
for(let i=0;i<6;i++)for(const [field,value] of Object.entries({id:'str:'+(i===0?'lora':''),type:'u8:0',enabled:'bool:0',target_host:'str:',target_port:'u16:4242',group_id:'str:reticulum',discovery_port:'u16:29716',data_port:'u16:42671'}))collectionLines.push(`rt.net.interface.${i}.${field}=${value}`);
const data=new TextEncoder().encode('TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\ndevice.node_name=str:TestDevice\ndevice.short_name=str:TD\ngps.enabled=bool:1\ngps.mode=u8:1\ngps.sat_mask=u8:141\ngps.external_nmea_hz=u8:0\ngps.external_nmea_mask=u8:0\ngps.coord_format=u8:2\nmt.use_preset=bool:1\nmt.modem_preset=u8:0\nmt.bandwidth_khz=f32:125\nmt.spread_factor=u8:9\nmt.coding_rate=u8:5\nrt.wifi_host=str:example.org\nrt.wifi_port=u16:4242\n'+collectionLines.join('\n')+'\nEND\n');
const hash=new Uint8Array(await webcrypto.subtle.digest('SHA-256',data));
let expectedCommitHash=hash,commitCalls=0;
let managementCommands=0;
const dialogs=[];
let writeSupport=false,uploadId,uploadHash,uploadOwner,uploaded=[],uploadTotal=0,failNextChunk=false,resumed=false;
let authorSupport=false,badProof=false,badCapabilities=false;
let radioSupport=false,precisionSupport=false;
let readbackData;
let loseNextCommitReply=false;
const deviceIdentity=await Identity.generate(),authorKey=await deviceIdentity.getPublicKey();
const authorName=(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10);
const authorDestination=await Identity.truncatedHash(Uint8Array.from([...authorName,...deviceIdentity.identityHash]));
const authorInfo=[authorKey,deviceIdentity.identityHash,authorDestination,7];
await page.exposeFunction('deviceReply',async raw=>{
 const frame=new FrameDecoder().push(Uint8Array.from(raw))[0];assert.ok(frame);
 if(frame.type===FRAME.HELLO){const payload=new Uint8Array([1,0,0,2,0,4,0,0,6,...new TextEncoder().encode('T-Deck'),4,...new TextEncoder().encode('test')]);if(precisionSupport)new DataView(payload.buffer).setUint32(4,CAPABILITY.MANAGEMENT|CAPABILITY.CHANNEL_POSITION_PRECISION,true);return [...encodeFrame(FRAME.HELLO_ACK,frame.sequence,payload)];}
 assert.equal(frame.type,FRAME.MANAGEMENT_REQUEST);const request=decodeManagement(frame.payload);
 managementCommands++;
 let body;
 if(request.code===OPERATION.CAPABILITIES)body=badCapabilities?[]:[1,512,320,1,[...(writeSupport?[0,17,20,21,32,33,34,35,36]:[0,17]),...(authorSupport?[1,2]:[]),...(radioSupport?[7,8,9]:[])],7];
 else if(request.code===OPERATION.AUTHOR_INFO)body=authorInfo;
 else if(request.code===OPERATION.AUTHOR_PROVE){assert.equal(request.body[1],7);const signature=await deviceIdentity.sign(authorProofMessage(request.session,request.body[0],authorKey,7));if(badProof)signature[0]^=1;body=[...authorInfo,signature];}
 else if(request.code===OPERATION.RADIO_IDENTITY_INFO){assert.equal(request.body[0],7);body=[request.body[1],1,1,new Uint8Array(32).fill(9)];}
 else if(request.code===OPERATION.CONFIG_SNAPSHOT){const active=readbackData??data,currentHash=readbackData?new Uint8Array(await webcrypto.subtle.digest('SHA-256',active)):hash;expectedCommitHash=currentHash;body=[request.body[0],active.length,currentHash,active.slice(request.body[0],request.body[0]+request.body[1])];}
 else if(request.code===OPERATION.TRANSFER_BEGIN){
   if(uploadId){assert.deepEqual(request.body[0],uploadId);assert.deepEqual(request.session,uploadOwner);assert.deepEqual(request.body[3],uploadHash);resumed=true;}
   else{[uploadId,,uploadTotal,uploadHash]=request.body;uploadOwner=request.session;uploaded=[];}
   body=[uploadId,320,uploaded.length,0];
 }
 else if(request.code===OPERATION.TRANSFER_CHUNK){assert.equal(request.body[1],uploaded.length);uploaded.push(...request.body[2]);
   if(failNextChunk){failNextChunk=false;return [...encodeFrame(FRAME.MANAGEMENT_RESPONSE,frame.sequence,encodeManagement(request.session,request.request,6,[]))];}
   body=[uploadId,320,uploaded.length,0];
 }
 else if(request.code===OPERATION.TRANSFER_FINISH){assert.equal(uploaded.length,uploadTotal);assert.deepEqual(new Uint8Array(await webcrypto.subtle.digest('SHA-256',Uint8Array.from(uploaded))),uploadHash);body=[uploadId,320,uploaded.length,1];}
 else if(request.code===OPERATION.CONFIG_COMMIT){assert.deepEqual(request.body[1],expectedCommitHash);body=[uploadId,uploadHash,1];expectedCommitHash=uploadHash;commitCalls++;if(loseNextCommitReply){loseNextCommitReply=false;readbackData=Uint8Array.from(uploaded);return null;}}
 else if(request.code===OPERATION.TRANSFER_ABORT){assert.deepEqual(request.body[0],uploadId);assert.deepEqual(request.session,uploadOwner);uploadId=undefined;uploaded=[];body=[];}
 else throw Error('Unexpected device command');
 return [...encodeFrame(FRAME.MANAGEMENT_RESPONSE,frame.sequence,encodeManagement(request.session,request.request,0,body))];
});
await page.addInitScript(()=>{
 let input;
 const port={async open(){this.readable=new ReadableStream({start(c){input=c;}});this.writable=new WritableStream({async write(bytes){const reply=await window.deviceReply([...bytes]);if(reply===null)input.close();else input.enqueue(Uint8Array.from(reply));}});},async setSignals(){},async close(){}};
 window.testUnplug=()=>input.close();
 Object.defineProperty(navigator,'serial',{configurable:true,value:{async requestPort(){return port;}}});
});
try{
 normalFlow:{
 if(process.argv.includes('--normal-flow')||process.argv.includes('--release-docs')){writeSupport=true;authorSupport=true;}
 await page.goto(origin+'/site/device/?lang=zh-Hans');
 await page.getByRole('button',{name:'连接设备',exact:true}).click();
 await page.getByText(/已验证配置快照/).waitFor();
 await page.waitForFunction(()=>document.querySelector('.brand-logo')?.naturalWidth>0);
 if(process.argv.includes('--release-docs')){
  await page.locator('.management-language select').selectOption('en');
  await page.waitForFunction(()=>document.documentElement.lang==='en'&&document.querySelector('.site-primary a').textContent==='Device management');
  await page.locator('.device-nav button').nth(6).click();
  await page.setViewportSize({width:1440,height:1000});
  await page.evaluate(()=>scrollTo(0,0));
  const section=page.locator('[id="field-ui.display.brightness"]').locator('xpath=ancestor::*[contains(concat(" ",normalize-space(@class)," ")," device-section ")][1]');
  await section.screenshot({path:`${releaseOutput}/web-config-percent.png`});
  await page.evaluate(()=>scrollTo(0,0));
  await page.screenshot({path:`${releaseOutput}/web-device-management.png`});
  // Navigation must preserve edits when the user cancels the native leave prompt.
  const brightness=page.locator('[id="field-ui.display.brightness"]');
  await brightness.fill('35');
  let prompts=0;
  page.on('dialog',async dialog=>{assert.equal(dialog.type(),'beforeunload');prompts++;await dialog.dismiss();});
  await page.locator('.site-primary a').nth(1).click({noWaitAfter:true});
  await page.waitForFunction(()=>document.querySelector('[id="field-ui.display.brightness"]')?.value==='34.9');
  assert.equal(prompts,1);assert.ok(page.url().includes('/device/'));assert.equal(await brightness.inputValue(),'34.9');
  break normalFlow;
 }
 if(process.argv.includes('--normal-flow')){
  await page.locator('[id="field-device.node_name"]').fill('Trail group');
  await page.getByRole('button',{name:'无线与协议',exact:true}).click();await page.locator('[id="module-mqtt"]').click();
  const mqttCommands=managementCommands;
  for(const {id} of locales){await page.locator('.management-language select').selectOption(id);const copy=configMqttRuntimeText(id);await page.getByText(copy.module,{exact:true}).waitFor();await page.getByText(copy.device,{exact:true}).first().waitFor();await page.getByRole('button',{name:copy.settings,exact:true}).click();await page.waitForFunction(()=>document.activeElement.id==='field-mt.mqtt.port');assert.equal(managementCommands,mqttCommands);}
  await page.locator('.management-language select').selectOption('zh-Hans');
  await page.getByRole('button',{name:'显示与交互',exact:true}).click();
  const brightness=page.locator('[id="field-ui.display.brightness"]'),volume=page.locator('[id="field-ui.audio.speaker_volume"]');
  assert.equal(await brightness.inputValue(),'50.2');assert.equal(await brightness.getAttribute('max'),'100');
  assert.equal(await page.locator('[id="field-ui.display.brightness-help"]').textContent(),'请输入 0%–100% 范围内的数值。');
  assert.equal(await volume.inputValue(),'45');
  await brightness.fill('101');assert.equal(await brightness.getAttribute('aria-invalid'),'true');assert.equal(await page.getByRole('button',{name:'保存到设备',exact:true}).isDisabled(),true);
  await brightness.fill('35');await volume.fill('60');
  const review=page.locator('.device-review');await review.getByRole('cell',{name:'34.9%',exact:true}).waitFor();await review.getByRole('cell',{name:'60%',exact:true}).waitFor();
  const displayCard=brightness.locator('xpath=ancestor::*[contains(concat(" ",normalize-space(@class)," ")," device-section ")][1]');
  await displayCard.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/config-percent-zh-Hans.png'});
  const editCommands=managementCommands;
  for(const {id} of locales){await page.locator('.management-language select').selectOption(id);await page.setViewportSize({width:390,height:844});assert.equal(await brightness.inputValue(),'34.9');assert.equal(await volume.inputValue(),'60');assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1),true);assert.equal(managementCommands,editCommands);}
  await page.locator('.management-language select').selectOption('zh-Hans');await page.setViewportSize({width:1440,height:1000});
  await page.getByRole('button',{name:'GPS 与定位',exact:true}).click();
  const rawMask=page.locator('[id="field-gps.sat_mask"]');assert.equal(await rawMask.isVisible(),false);
  await page.getByRole('checkbox',{name:/GLONASS$/}).check();assert.equal(await rawMask.inputValue(),'143');assert.equal(await rawMask.isVisible(),false);
  await page.getByRole('button',{name:'保存到设备',exact:true}).click();
  const dialog=page.getByRole('dialog',{name:'保存到设备',exact:true});await dialog.getByRole('cell',{name:'34.9%',exact:true}).waitFor();await dialog.getByRole('cell',{name:'60%',exact:true}).waitFor();
  assert.equal(commitCalls,0);await dialog.getByRole('button',{name:'保存到设备',exact:true}).click();await page.getByText(/配置已保存，设备即将重启/).waitFor();
  const saved=new TmsDocument(Uint8Array.from(uploaded));assert.equal(saved.get('device.node_name').value,'Trail group');assert.equal(saved.get('ui.display.brightness').value,89);assert.equal(saved.get('ui.audio.speaker_volume').value,60);assert.equal(saved.get('gps.sat_mask').value,143);assert.equal(saved.get('privacy.encrypt_mode').value,1);assert.equal(commitCalls,1);
  readbackData=Uint8Array.from(uploaded);await page.getByRole('button',{name:'断开设备',exact:true}).click();await page.getByRole('button',{name:'连接设备',exact:true}).click();await page.getByText(configReadbackText('zh-Hans').matched,{exact:true}).waitFor();
  await page.getByRole('button',{name:'显示与交互',exact:true}).click();assert.equal(await brightness.inputValue(),'34.9');assert.equal(await volume.inputValue(),'60');await page.getByText('没有未保存编辑',{exact:true}).waitFor();
  break normalFlow;
 }
 const tuningCalls=managementCommands,tuningRows=[['mt.bandwidth_khz','f32',1],['mc.base.bandwidth_khz','f32',1],['mc.bandwidth_khz','f32',1],['rt.bandwidth_khz','f32',2],['mc.rx_delay_base','f32',1],['mc.airtime_factor','f32',1],['mc.flood_max','u8',1]],tuningSelector=key=>page.locator('[id="field-'+key+'"]');
 for(const {id} of locales){await page.locator('.management-language select').selectOption(id);for(const [key,type,category] of tuningRows){await page.locator('.device-nav button').nth(category).click();const spec=configurationRadioTuning({key,type},id);await tuningSelector(key).getByRole('option',{name:spec.custom,exact:true}).waitFor({state:'attached'});assert.deepEqual((await tuningSelector(key).locator('option').allTextContents()).map(precisionCopy),[...spec.options.map(option=>option.label),spec.custom].map(precisionCopy));assert.equal(await page.locator('[id="field-'+key+'-help"]').textContent(),spec.help);}await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth+1),false);assert.equal(managementCommands,tuningCalls);}
 await page.setViewportSize({width:1440,height:2200});await page.locator('.management-language select').selectOption('zh-Hans');await page.locator('.device-nav button').nth(1).click();
 const rx=tuningSelector('mc.rx_delay_base'),air=tuningSelector('mc.airtime_factor'),flood=tuningSelector('mc.flood_max'),band=tuningSelector('mc.bandwidth_khz');
 await air.selectOption('custom');const airInput=air.locator('..').locator('input[type="number"]');assert.equal(await airInput.getAttribute('step'),'any');await airInput.fill('2.5');await air.locator('xpath=ancestor::*[contains(concat(" ",normalize-space(@class)," ")," device-field ")][1]').locator('output').getByText('2.5×',{exact:true}).waitFor();await page.locator('.device-review').getByRole('cell',{name:'2.5×',exact:true}).waitFor();
 await rx.selectOption('4');await flood.selectOption('0');await flood.selectOption('custom');const floodInput=flood.locator('..').locator('input[type="number"]');await floodInput.fill('256');assert.equal(await floodInput.getAttribute('aria-invalid'),'true');await page.locator('.device-nav button').nth(0).click();await page.locator('.device-nav button').nth(1).click();assert.equal(await floodInput.inputValue(),'256');assert.equal(await flood.inputValue(),'custom');await floodInput.fill('255');await page.locator('.device-review').getByRole('cell',{name:'255 跳',exact:true}).waitFor();
 await band.selectOption('custom');const bandInput=band.locator('..').locator('input[type="number"]');assert.equal(await bandInput.getAttribute('step'),'any');await bandInput.fill('7.8');assert.equal(await bandInput.inputValue(),'7.8');await page.locator('.device-review').getByRole('cell',{name:'7.8 kHz',exact:true}).waitFor();assert.equal(await tuningSelector('mc.region_preset').inputValue(),'0');
 await bandInput.fill('3.5e38');assert.equal(await bandInput.getAttribute('aria-invalid'),'true');await page.locator('.device-nav button').nth(0).click();await page.locator('.device-nav button').nth(1).click();for(const {id} of locales){await page.locator('.management-language select').selectOption(id);assert.equal(await bandInput.inputValue(),'3.5e38');assert.equal(await bandInput.getAttribute('aria-invalid'),'true');assert.equal(managementCommands,tuningCalls);}await page.locator('.management-language select').selectOption('zh-Hans');await bandInput.fill('7.8');assert.equal(await bandInput.getAttribute('aria-invalid'),'false');
 const tuningCard=rx.locator('xpath=ancestor::*[contains(concat(" ",normalize-space(@class)," ")," device-section ")][1]');await tuningCard.evaluate(node=>node.scrollIntoView({block:'start'}));await page.evaluate(()=>scrollBy(0,-140));await tuningCard.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/config-radio-tuning-zh-Hans.png'});
 for(const {id} of locales){await page.locator('.management-language select').selectOption(id);assert.equal(await airInput.inputValue(),'2.5');assert.equal(await bandInput.inputValue(),'7.8');assert.equal(await floodInput.inputValue(),'255');assert.equal(await rx.inputValue(),'4');assert.ok(precisionCopy(await flood.locator('xpath=ancestor::*[contains(concat(" ",normalize-space(@class)," ")," device-field ")][1]').textContent()).includes(precisionCopy(configRadioTuningText(id).runtime.replace('{value}',formatConfigurationRadioTuning({key:'mc.flood_max',type:'u8',value:64},id)))));assert.equal(managementCommands,tuningCalls);}await page.locator('.management-language select').selectOption('zh-Hans');await rx.selectOption('0');await air.selectOption('1');await flood.selectOption('16');await band.selectOption('250');await page.getByText('没有未保存编辑',{exact:true}).waitFor();assert.equal(managementCommands,tuningCalls);await page.setViewportSize({width:1440,height:1000});await page.locator('.device-nav button').nth(0).click();
 for(const {id} of locales){await page.locator('.management-language select').selectOption(id);const text=configMeaningText(id);
  for(const category of [0,7]){await page.locator('.device-nav button').nth(category).click();assert.equal(await page.locator('[id^="field-mt_ble."],[id^="field-mc_ble."],[id="field-device.ble_enabled"]').count(),0);}
  const scopeFinder=page.locator('.device-finder');await scopeFinder.getByRole('searchbox').fill('mc_ble');await scopeFinder.getByText(configFinderText(id).empty,{exact:true}).waitFor();await scopeFinder.getByRole('searchbox').fill('');
  await page.locator('.device-nav button').nth(1).click();await page.waitForFunction(expected=>document.querySelector('[id="field-mc.region_preset"] option')?.textContent===expected,meshCoreRegionOptions(id)[0].label);assert.deepEqual(await page.locator('[id="field-mc.region_preset"] option').allTextContents(),meshCoreRegionOptions(id).map(row=>row.label));
  await page.locator('.device-nav button').nth(3).click();for(const [key,labels] of [['chat.channel',[text.primary,text.secondary]],['privacy.encrypt_mode',[text.off,text.psk,text.pki]],['chat.alerts.message',[text.off,text.on]],['chat.alerts.contact',[text.off,text.contacts,text.all]]]){const control=page.locator('[id="field-'+key+'"]');assert.equal(await control.evaluate(node=>node.tagName),'SELECT');assert.deepEqual(await control.locator('option').allTextContents(),labels);}
  await page.locator('.device-nav button').nth(4).click();assert.equal(await page.locator('[id="field-gps.strategy"]').evaluate(node=>node.tagName),'SELECT');assert.equal(await page.locator('[id="field-gps.init_baud"] option:checked').textContent(),'9600 baud');
  const durationText=configDurationText(id),interval=page.locator('[id="field-gps.interval_ms"]');assert.equal(await interval.inputValue(),'1');assert.deepEqual(await interval.locator('..').locator('select option').allTextContents(),[durationText.milliseconds,durationText.seconds,durationText.minutes,durationText.hours]);assert.equal(await interval.locator('..').locator('select').inputValue(),'60000');
  assert.equal(await page.locator('[id="field-gps.motion_sensor"] option:checked').textContent(),configCharacterText(id).wake);
  await page.locator('.device-nav button').nth(7).click();assert.equal(await page.locator('[id="field-aprs.symbol_table"]').inputValue(),'/');assert.equal(await page.locator('[id="field-aprs.symbol_code"]').inputValue(),'[');assert.ok((await page.locator('[id="field-aprs.symbol_code"]').locator('..').locator('select option:checked').textContent()).includes(configAprsSymbolNames(id)['[']));
  await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth+1),false);await page.setViewportSize({width:1440,height:1000});
  await page.locator('.device-nav button').nth(0).click();await page.locator('[id="field-device.short_name-help"]').getByText(text.help.shortName,{exact:true}).waitFor();
 }
 // Precision is protocol metadata; selecting/customizing it does not enable flags or issue USB commands.
 const precisionCommands=managementCommands,primaryPrecision=page.locator('[id="field-channel.primary_position_precision"]'),secondaryPrecision=page.locator('[id="field-channel.secondary_position_precision"]');
 for(const {id} of locales){await page.locator('.management-language select').selectOption(id);await page.locator('.device-nav button').nth(3).click();assert.deepEqual((await primaryPrecision.locator('option').allTextContents()).map(precisionCopy),[...positionPrecisionOptions(id).map(row=>row.label),configPrecisionText(id).custom].map(precisionCopy));assert.equal(await primaryPrecision.inputValue(),'0');assert.equal(await secondaryPrecision.inputValue(),'32');assert.ok((await page.locator('[id="field-channel.primary_position_precision-help"]').textContent()).includes(configPrecisionText(id).channelScope));await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth+1),false);}
 await page.setViewportSize({width:1440,height:1400});await page.locator('.management-language select').selectOption('zh-Hans');await primaryPrecision.selectOption('13');await page.locator('.device-review').getByRole('cell',{name:formatPositionPrecision(13,'zh-Hans'),exact:true}).waitFor();assert.equal(await page.locator('[id="field-channel.primary_has_module"]').isChecked(),false);await primaryPrecision.locator('xpath=ancestor::*[contains(concat(" ", normalize-space(@class), " "), " device-section ")][1]').evaluate(node=>node.scrollIntoView({block:'start'}));await page.evaluate(()=>scrollBy(0,-140));await primaryPrecision.locator('xpath=ancestor::*[contains(concat(" ", normalize-space(@class), " "), " device-section ")][1]').screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/config-position-precision-zh-Hans.png'});
 await primaryPrecision.selectOption('0');await primaryPrecision.selectOption('custom');const coreBits=primaryPrecision.locator('..').locator('input[type="number"]');await coreBits.fill('4294967296');assert.equal(await coreBits.getAttribute('aria-invalid'),'true');await page.locator('.device-nav button').nth(0).click();await page.locator('.device-nav button').nth(3).click();assert.equal(await primaryPrecision.inputValue(),'custom');assert.equal(await coreBits.inputValue(),'4294967296');
 for(const value of [22,33,4294967295]){await coreBits.fill(String(value));await page.locator('.device-review').getByRole('cell',{name:formatPositionPrecision(value,'zh-Hans'),exact:true}).waitFor();assert.equal(await page.locator('[id="field-channel.primary_has_module"]').isChecked(),false);}await primaryPrecision.selectOption('0');await page.getByText('没有未保存编辑',{exact:true}).waitFor();assert.equal(managementCommands,precisionCommands);
 await page.setViewportSize({width:1440,height:1000});await page.locator('.management-language select').selectOption('zh-Hans');await page.locator('.device-nav button').nth(4).click();
 const durationCommands=managementCommands,intervalInput=page.locator('[id="field-gps.interval_ms"]'),intervalBox=intervalInput.locator('xpath=ancestor::*[contains(concat(" ", normalize-space(@class), " "), " device-duration ")][1]'),intervalUnit=intervalBox.locator('select');
 await intervalUnit.selectOption('1000');assert.equal(await intervalInput.inputValue(),'60');await intervalUnit.selectOption('60000');assert.equal(await page.getByText('没有未保存编辑',{exact:true}).count(),1);
 await intervalInput.fill('1.');assert.equal(await intervalInput.inputValue(),'1.');await intervalInput.fill('1.5');assert.equal(await intervalBox.locator('output').textContent(),'当前时长：90,000 毫秒');await page.locator('.device-review').getByRole('cell',{name:'90 秒',exact:true}).waitFor();
 await intervalUnit.selectOption('1000');await intervalInput.fill('0.0001');assert.equal(await intervalInput.getAttribute('aria-invalid'),'true');assert.equal(await intervalUnit.isDisabled(),true);await page.locator('.device-nav button').nth(0).click();await page.locator('.device-nav button').nth(4).click();assert.equal(await intervalInput.inputValue(),'0.0001');assert.equal(await intervalUnit.inputValue(),'1000');
 await intervalInput.fill('1.234');assert.equal(await intervalBox.locator('output').textContent(),'当前时长：1,234 毫秒');await intervalUnit.selectOption('60000');await intervalBox.getByText(configDurationText('zh-Hans').approximate,{exact:true}).waitFor();
 assert.equal(await intervalBox.locator('output').textContent(),'当前时长：1,234 毫秒');await intervalUnit.selectOption('1000');assert.equal(await intervalInput.inputValue(),'1.234');await intervalInput.fill('4294967.295');assert.equal(await intervalBox.locator('output').textContent(),'当前时长：4,294,967,295 毫秒');await intervalInput.fill('4294967.296');await page.getByText('时长必须在 0–4,294,967,295 毫秒之间。',{exact:true}).waitFor();await intervalInput.fill('90');await intervalUnit.selectOption('60000');
 await intervalInput.locator('xpath=ancestor::*[contains(concat(" ", normalize-space(@class), " "), " device-section ")][1]').screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/config-duration-zh-Hans.png'});await intervalInput.fill('1');
 await page.locator('.device-nav button').nth(2).click();const syncInput=page.locator('[id="field-rt.net.propagation.sync_interval_s"]'),syncUnit=syncInput.locator('..').locator('select');await syncUnit.selectOption('1');await syncInput.fill('0');await page.getByText('时长必须在 60–86,400 秒之间。',{exact:true}).waitFor();await syncInput.fill('900');
 await page.locator('.device-nav button').nth(6).click();assert.equal(await page.locator('[id="field-ui.screen.timeout_ms"]').evaluate(node=>node.tagName),'SELECT');await page.locator('.device-nav button').nth(5).click();assert.equal(await page.locator('[id="field-map.track_interval"]').inputValue(),'99');assert.equal(managementCommands,durationCommands);
 await page.locator('.management-language select').selectOption('zh-Hans');await page.locator('.device-nav button').nth(7).click();
 const symbol=page.locator('[id="field-aprs.symbol_code"]'),table=page.locator('[id="field-aprs.symbol_table"]');await symbol.fill('汉');assert.equal(await symbol.getAttribute('aria-invalid'),'true');await symbol.fill('b');assert.equal(await symbol.inputValue(),'b');await table.fill('\\');assert.equal(await symbol.locator('..').locator('select option:checked').textContent(),'b');await table.fill('/');await symbol.evaluate(node=>node.closest('.device-section').scrollIntoView({block:'center'}));await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/config-aprs-characters-zh-Hans.png'});const symbolPanel=symbol.locator('..');await symbolPanel.locator('summary').click();const raw=symbolPanel.locator('input[type=number]');await raw.fill('255');assert.equal(await symbol.inputValue(),'0xFF');await raw.fill('256');assert.equal(await raw.inputValue(),'256');assert.equal(await raw.getAttribute('aria-invalid'),'true');await raw.fill('98');await symbol.fill('');assert.equal(await symbolPanel.locator('select').inputValue(),'0');await symbol.fill('[');await symbolPanel.locator('summary').click();
 await page.locator('.device-nav button').nth(4).click();const sensor=page.locator('[id="field-gps.motion_sensor"]'),sensorPanel=sensor.locator('..');assert.equal(await sensorPanel.locator('input').count(),0);await sensor.selectOption('custom');const sensorId=sensorPanel.locator('input');await sensorId.fill('42');await page.locator('.device-nav button').nth(0).click();await page.locator('.device-nav button').nth(4).click();assert.equal(await sensor.inputValue(),'custom');assert.equal(await sensorPanel.locator('input').inputValue(),'42');await sensor.selectOption('143');assert.equal(await sensorPanel.locator('input').count(),0);await sensor.scrollIntoViewIfNeeded();await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/config-motion-events-zh-Hans.png'});
 await page.locator('.management-language select').selectOption('zh-Hans');await page.locator('.device-nav button').nth(1).click();
 await page.locator('[id="field-mc.region_preset"]').selectOption('17');for(const [key,value] of [['mc.frequency_mhz','495.2'],['mc.bandwidth_khz','125'],['mc.spread_factor','9'],['mc.coding_rate','5'],['mc.base.tx_power','12']])assert.equal(await page.locator('[id="field-'+key+'"]').inputValue(),value);
 await page.locator('[id="field-mc.region_preset-help"]').scrollIntoViewIfNeeded();await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/config-meshcore-region-zh-Hans.png'});
 await page.locator('[id="field-mc.base.tx_power"]').selectOption('custom');await page.locator('[id="field-mc.base.tx_power"]').locator('..').locator('input').fill('8');assert.equal(await page.locator('[id="field-mc.region_preset"]').inputValue(),'17');await page.locator('[id="field-mc.frequency_mhz"]').fill('496.25');assert.equal(await page.locator('[id="field-mc.region_preset"]').inputValue(),'0');
 await page.locator('[id="field-mc.region_preset"]').selectOption('6');assert.equal(await page.locator('[id="field-mc.frequency_mhz"]').inputValue(),'869.525');await page.locator('[id="field-mc.region_preset"]').selectOption('0');assert.equal(await page.locator('[id="field-mc.frequency_mhz"]').inputValue(),'869.525');await page.locator('[id="field-mc.spread_factor"]').selectOption('11');await page.locator('[id="field-mc.base.tx_power"]').selectOption('20');
 // Native MHz storage has editable Hz/kHz units; changing units alone leaves
 // both config values and the modification review unchanged.
 const frequencyCommands=managementCommands,frequencyCommits=commitCalls,radioReview=page.locator('.device-review');
 for(const key of ['mt.frequency_offset_mhz','mt.override_frequency_mhz','mc.base.frequency_offset_mhz','mc.base.override_frequency_mhz','mc.frequency_mhz','rt.frequency_offset_mhz','rt.override_frequency_mhz']){
  await page.locator('.device-nav button').nth(key.startsWith('rt.')?2:1).click();const input=page.locator(`[id="field-${key}"]`),units=input.locator('..').locator('select'),before=await radioReview.allTextContents(),value=Number(await input.inputValue());
  assert.deepEqual(await units.locator('option').evaluateAll(nodes=>nodes.map(node=>node.value)),['MHz','kHz','Hz']);
  for(const unit of ['Hz','kHz','MHz']){await units.selectOption(unit);assert.equal(await input.inputValue(),frequencyInputDisplay(value,unit));assert.deepEqual(await radioReview.allTextContents(),before);}
 }
 await page.locator('.device-nav button').nth(1).click();
 const offset=page.locator('[id="field-mt.frequency_offset_mhz"]'),offsetUnits=offset.locator('..').locator('select');await offsetUnits.selectOption('kHz');await offset.fill('-12.5');
 assert.ok((await radioReview.textContent()).includes('-0.0125 MHz'));
 for(const locale of locales){await page.locator('.management-language select').selectOption(locale.id);const copy=configRadioValueText(locale.id);await page.locator('[id="field-mt.frequency_offset_mhz-help"]').getByText(copy.mtOffset,{exact:true}).waitFor();assert.equal(await offset.inputValue(),'-12.5');assert.equal(await offsetUnits.inputValue(),'kHz');await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);assert.equal(managementCommands,frequencyCommands);}
 await page.locator('.management-language select').selectOption('zh-Hans');await page.setViewportSize({width:1440,height:1000});await page.locator('.device-nav button').nth(0).click();await page.locator('.device-nav button').nth(1).click();assert.equal(await offsetUnits.inputValue(),'kHz');assert.equal(await offset.inputValue(),'-12.5');
 await offset.fill('invalid');
 for(const locale of locales){await page.locator('.management-language select').selectOption(locale.id);await page.getByText(configRadioValueText(locale.id).FREQUENCY_FORMAT,{exact:true}).waitFor();assert.equal(await offset.inputValue(),'invalid');assert.equal(await offsetUnits.isDisabled(),true);}
 await page.locator('.management-language select').selectOption('zh-Hans');await offset.fill('3.5e41');await page.getByText(configInputText('zh-Hans').CONFIG_FLOAT,{exact:true}).waitFor();await page.locator('.device-nav button').nth(0).click();await page.locator('.device-nav button').nth(1).click();assert.equal(await offset.inputValue(),'3.5e41');assert.equal(await offsetUnits.inputValue(),'kHz');await offset.fill('-12.5');
 const power=page.locator('[id="field-mc.base.tx_power"]');await power.selectOption('custom');const powerRaw=power.locator('..').locator('input');await powerRaw.fill('0');await power.locator('xpath=ancestor::div[contains(@class,"device-field")][1]').getByText('按运行限制换算：1 dBm',{exact:true}).waitFor();
 await page.setViewportSize({width:1440,height:2000});const frequencyPanel=offset.locator('xpath=ancestor::*[contains(concat(" ", normalize-space(@class), " "), " device-section ")][1]');await frequencyPanel.evaluate(node=>node.scrollIntoView({block:'start'}));await page.evaluate(()=>scrollBy(0,-140));await frequencyPanel.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/config-frequency-units-zh-Hans.png'});await page.setViewportSize({width:1440,height:1000});
 await powerRaw.fill('128');for(const locale of locales){await page.locator('.management-language select').selectOption(locale.id);assert.equal(await powerRaw.inputValue(),'128');assert.equal(await powerRaw.getAttribute('aria-invalid'),'true');}
 await page.locator('.management-language select').selectOption('zh-Hans');await power.selectOption('20');await offset.fill('0');await offsetUnits.selectOption('MHz');
 const channelNumber=page.locator('[id="field-mt.channel_num"]');await channelNumber.selectOption('custom');await channelNumber.locator('..').locator('input').fill('65535');await channelNumber.selectOption('0');
 const mtPower=page.locator('[id="field-mt.tx_power"]');assert.equal(await mtPower.locator('option:checked').textContent(),configRadioValueText('zh-Hans').mtAutoPower);await mtPower.selectOption('custom');await mtPower.locator('..').locator('input').fill('-9');await mtPower.selectOption('0');
 const mcFrequency=page.locator('[id="field-mc.frequency_mhz"]'),mcUnits=mcFrequency.locator('..').locator('select');await mcUnits.selectOption('Hz');await page.locator('[id="field-mc.region_preset"]').selectOption('17');assert.equal(await mcUnits.inputValue(),'Hz');assert.equal(await mcFrequency.inputValue(),'495200000');
 await mcFrequency.fill('496250000');assert.equal(await page.locator('[id="field-mc.region_preset"]').inputValue(),'0');await mcUnits.selectOption('MHz');await page.locator('[id="field-mc.region_preset"]').selectOption('6');await page.locator('[id="field-mc.region_preset"]').selectOption('0');
 await page.locator('.device-nav button').nth(2).click();
 const rtFrequency=page.locator('[id="field-rt.override_frequency_mhz"]'),rtUnits=rtFrequency.locator('..').locator('select'),rtPower=page.locator('[id="field-rt.tx_power"]');await rtUnits.selectOption('Hz');await rtFrequency.fill('869525000');await rtPower.selectOption('custom');await rtPower.locator('..').locator('input').fill('30');
 await rtPower.locator('xpath=ancestor::div[contains(@class,"device-field")][1]').getByText('按运行限制换算：22 dBm',{exact:true}).waitFor();
 await page.setViewportSize({width:1440,height:2000});const reticulumFrequencyPanel=rtFrequency.locator('xpath=ancestor::*[contains(concat(" ", normalize-space(@class), " "), " device-section ")][1]');await reticulumFrequencyPanel.evaluate(node=>node.scrollIntoView({block:'start'}));await page.evaluate(()=>scrollBy(0,-140));await reticulumFrequencyPanel.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/config-reticulum-frequency-zh-Hans.png'});await page.setViewportSize({width:1440,height:1000});
 await rtPower.selectOption('0');assert.equal(await rtPower.locator('option:checked').textContent(),configRadioValueText('zh-Hans').rtAutoPower);await rtFrequency.fill('0');await rtUnits.selectOption('MHz');
 await page.getByText('没有未保存编辑',{exact:true}).waitFor();assert.equal(managementCommands,frequencyCommands);assert.equal(commitCalls,frequencyCommits);
 const numericCommands=managementCommands,numericCommits=commitCalls,numericCopy=configNumericPurposeText('zh-Hans');await page.locator('.device-nav button').nth(6).click();const design=page.locator('[id="field-power.gauge.design_mah"]'),capacityPanel=design.locator('xpath=ancestor::*[contains(concat(" ",normalize-space(@class)," ")," device-section ")][1]');assert.equal(await design.getAttribute('min'),'1');assert.equal(await design.getAttribute('max'),'10000');await design.fill('10001');assert.equal(await design.getAttribute('aria-invalid'),'true');for(const {id} of locales){await page.locator('.management-language select').selectOption(id);assert.equal(await design.inputValue(),'10001');assert.equal(await design.getAttribute('aria-invalid'),'true');await page.getByText(configNumericPurposeText(id).capacityHelp,{exact:true}).first().waitFor();await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth+1),false);}await page.locator('.management-language select').selectOption('zh-Hans');await design.fill('2000');await page.locator('.device-review').getByRole('cell',{name:'2,000 mAh',exact:true}).waitFor();await page.setViewportSize({width:1440,height:1600});await capacityPanel.evaluate(node=>node.scrollIntoView({block:'start'}));await page.evaluate(()=>scrollBy(0,-140));await capacityPanel.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/config-battery-capacity-zh-Hans.png'});await design.fill('1500');
 await page.locator('.device-nav button').nth(7).click();const suffix=page.locator('[id="field-aprs.igate_ssid"]');assert.equal(await suffix.locator('option:checked').textContent(),numericCopy.noSuffix);await suffix.selectOption('custom');const suffixRaw=suffix.locator('..').locator('input');await suffixRaw.fill('255');await suffix.locator('xpath=ancestor::div[contains(@class,"device-field")][1]').getByText(numericCopy.ssidUnsupported,{exact:true}).waitFor();await page.locator('.device-review').getByRole('cell',{name:'呼号后缀 -255',exact:true}).waitFor();await suffixRaw.fill('256');assert.equal(await suffixRaw.getAttribute('aria-invalid'),'true');await suffix.selectOption('15');assert.equal(await suffix.locator('option:checked').textContent(),'呼号后缀 -15');for(const {id} of locales){await page.locator('.management-language select').selectOption(id);const expectedSuffix=configNumericPurposeText(id).suffix.replace('{value}',await page.evaluate(locale=>new Intl.NumberFormat(locale).format(15),id));await suffix.getByRole('option',{name:expectedSuffix,exact:true}).waitFor({state:'attached'});assert.equal(await suffix.locator('option:checked').textContent(),expectedSuffix);}await page.locator('.management-language select').selectOption('zh-Hans');await suffix.selectOption('0');
 await page.locator('.device-nav button').nth(2).click();const syncCount=page.locator('[id="field-rt.net.propagation.max_messages_per_sync"]'),syncPanel=syncCount.locator('xpath=ancestor::*[contains(concat(" ",normalize-space(@class)," ")," device-section ")][1]');await syncCount.selectOption('custom');const syncRaw=syncCount.locator('..').locator('input');await syncRaw.fill('7');await page.locator('.device-review').getByRole('cell',{name:'7 条消息／次同步',exact:true}).waitFor();await syncRaw.fill('65');assert.equal(await syncRaw.getAttribute('aria-invalid'),'true');await syncCount.selectOption('64');await page.setViewportSize({width:1440,height:1700});await syncPanel.evaluate(node=>node.scrollIntoView({block:'start'}));await page.evaluate(()=>scrollBy(0,-140));await syncPanel.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/config-propagation-count-zh-Hans.png'});await syncCount.selectOption('32');const gatewayPort=page.locator('[id="field-rt.wifi_port"]');await gatewayPort.fill('0');assert.equal(await gatewayPort.getAttribute('aria-invalid'),'true');await gatewayPort.fill('65535');await page.locator('.device-review').getByRole('cell',{name:'端口 65535',exact:true}).waitFor();await gatewayPort.fill('4242');for(const {id} of locales){await page.locator('.management-language select').selectOption(id);await page.getByText(configNumericPurposeText(id).gatewayHelp+' '+configNumericPurposeText(id).portHelp,{exact:true}).waitFor();assert.equal(await syncCount.inputValue(),'32');await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth+1),false);}await page.locator('.management-language select').selectOption('zh-Hans');await page.setViewportSize({width:1440,height:1000});await page.locator('.device-nav button').nth(1).click();const mqttPort=page.locator('[id="field-mt.mqtt.port"]');await mqttPort.fill('1884');await page.locator('.device-review').getByRole('cell',{name:'端口 1884',exact:true}).waitFor();await mqttPort.fill('1883');await page.getByText('没有未保存编辑',{exact:true}).waitFor();assert.equal(managementCommands,numericCommands);assert.equal(commitCalls,numericCommits);
 const routingCommands=managementCommands,routingCommits=commitCalls,routingCopy=configRoutingValueText('zh-Hans');await page.locator('.device-nav button').nth(1).click();const channelIdField=page.locator('[id="field-mt.primary_channel_id"]'),channelIdBase=channelIdField.locator('..').locator('select');assert.equal(await channelIdField.inputValue(),'0x00000000');await channelIdBase.selectOption('decimal');assert.equal(await channelIdField.inputValue(),'0');assert.equal(await page.locator('.device-review tbody tr').count(),0);await channelIdField.fill('4294967296');assert.equal(await channelIdField.getAttribute('aria-invalid'),'true');assert.equal(await channelIdBase.isDisabled(),true);await page.locator('.device-nav button').nth(0).click();await page.locator('.device-nav button').nth(1).click();for(const {id} of locales){await page.locator('.management-language select').selectOption(id);assert.equal(await channelIdField.inputValue(),'4294967296');assert.equal(await channelIdBase.inputValue(),'decimal');await page.getByText(configRoutingValueText(id).invalid,{exact:true}).waitFor();await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth+1),false);}await page.locator('.management-language select').selectOption('zh-Hans');await channelIdField.fill('4294967295');await channelIdBase.selectOption('hex');assert.equal(await channelIdField.inputValue(),'0xFFFFFFFF');await page.locator('.device-review').getByRole('cell',{name:'0xFFFFFFFF · 4,294,967,295',exact:true}).waitFor();const idPanel=channelIdField.locator('xpath=ancestor::div[contains(@class,"device-field")][1]');await page.setViewportSize({width:1440,height:1500});await idPanel.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/config-channel-id-zh-Hans.png'});await channelIdField.fill('0x00000000');
 for(const prefix of ['mt','mc.base','rt']){await page.locator('.device-nav button').nth(prefix==='rt'?2:1).click();for(const channel of ['primary','secondary']){const input=page.locator('[id="field-'+prefix+'.'+channel+'_channel_id"]'),base=input.locator('..').locator('select');await base.selectOption('hex');await input.fill('0x80000000');assert.equal(await input.getAttribute('aria-invalid'),'false');await base.selectOption('decimal');assert.equal(await input.inputValue(),'2147483648');await input.fill('0');await base.selectOption('hex');assert.equal(await input.inputValue(),'0x00000000');}}
 await page.locator('.device-nav button').nth(1).click();for(const key of ['policy.hop_limit','policy.max_retries','policy.max_channels','mt.hop_limit','mc.base.hop_limit']){const control=page.locator('[id="field-'+key+'"]');await control.selectOption('custom');const input=control.locator('..').locator('input');await input.fill('255');assert.equal(await input.getAttribute('aria-invalid'),'false');if(key==='mt.hop_limit')await control.locator('xpath=ancestor::div[contains(@class,"device-field")][1]').getByText(routingCopy.wire.replace('{value}','7'),{exact:true}).waitFor();await input.fill('256');assert.equal(await input.getAttribute('aria-invalid'),'true');await control.selectOption(key==='policy.max_retries'?'1':key==='policy.max_channels'?'3':'2');}const policyPanel=page.locator('[id="field-policy.max_retries"]').locator('xpath=ancestor::*[contains(concat(" ",normalize-space(@class)," ")," device-section ")][1]');await policyPanel.evaluate(node=>node.scrollIntoView({block:'start'}));await page.evaluate(()=>scrollBy(0,-140));await policyPanel.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/config-policy-counts-zh-Hans.png'});await page.locator('[id="field-mc.base.channel_num"]').fill('65535');await page.locator('.device-review').getByRole('cell',{name:'兼容信道编号 65,535',exact:true}).waitFor();await page.locator('[id="field-mc.base.channel_num"]').fill('0');await page.locator('.device-nav button').nth(2).click();const rtHops=page.locator('[id="field-rt.hop_limit"]');await rtHops.selectOption('custom');await rtHops.locator('..').locator('input').fill('255');await rtHops.selectOption('2');await page.locator('[id="field-rt.channel_num"]').fill('65535');await page.locator('[id="field-rt.channel_num"]').fill('0');await page.locator('.device-nav button').nth(6).click();const timezoneOffset=page.locator('[id="field-ui.timezone.offset_minutes"]');await timezoneOffset.fill('345');await page.locator('.device-review').getByRole('cell',{name:'UTC+05:45（保存 345 分钟）',exact:true}).waitFor();await timezoneOffset.fill('480');await page.setViewportSize({width:1440,height:1000});await page.getByText('没有未保存编辑',{exact:true}).waitFor();assert.equal(managementCommands,routingCommands);assert.equal(commitCalls,routingCommits);
 await page.locator('.management-language select').selectOption('zh-Hans');await page.locator('.device-nav button').nth(3).click();await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/config-meaning-chat-zh-Hans.png',fullPage:true});await page.locator('.device-nav button').nth(0).click();
 if(!process.argv.includes('--presentation-only')){
 const finderCommands=managementCommands,finder=page.locator('.device-finder');
 await page.locator('[id="field-device.node_name"]').fill('Search retained edit');
 const review=page.locator('.device-review');await review.getByRole('cell',{name:'TestDevice',exact:true}).waitFor();await review.getByRole('cell',{name:'Search retained edit',exact:true}).waitFor();
 await finder.getByRole('searchbox').fill('gps.mode');await finder.locator('button').filter({has:page.getByText('gps.mode',{exact:true})}).click();
 await page.waitForFunction(()=>document.activeElement?.id==='field-gps.mode');assert.equal(await page.locator('.device-nav button.selected').textContent(),'GPS 与定位');
 for(const locale of locales){await page.locator('.management-language select').selectOption(locale.id);const copy=configFinderText(locale.id);await finder.getByRole('searchbox',{name:copy.search,exact:true}).waitFor();assert.equal(await finder.getByRole('searchbox').inputValue(),'gps.mode');await review.getByRole('columnheader',{name:configReviewText(locale.id).before,exact:true}).waitFor();assert.equal(await review.getByRole('cell',{name:'Search retained edit',exact:true}).count(),1);}
 await page.locator('.management-language select').selectOption('zh-Hans');await finder.getByRole('searchbox').fill('device.node_name');
 await finder.locator('button').filter({has:page.getByText('device.node_name',{exact:true})}).click();assert.equal(await page.locator('[id="field-device.node_name"]').inputValue(),'Search retained edit');
 await finder.getByRole('searchbox').fill('wifi.profile.0.password');await finder.locator('button').filter({has:page.getByText('wifi.profile.0.password',{exact:true})}).click();
 await page.locator('[id="field-wifi.profile.0.password"]').fill('synthetic-review-secret');assert.equal(await review.getByRole('cell',{name:configReviewText('zh-Hans').secret,exact:true}).count(),2);assert.ok(!(await review.textContent()).includes('synthetic-review-secret'));
 await review.scrollIntoViewIfNeeded();await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-device-review.png'});
 await page.locator('[id="field-wifi.profile.0.password"]').fill('secret');await finder.getByRole('searchbox').fill('device.node_name');await finder.locator('button').filter({has:page.getByText('device.node_name',{exact:true})}).click();
 await finder.getByRole('searchbox').fill('');await finder.getByLabel(configFinderText('zh-Hans').changedOnly,{exact:true}).check();assert.equal(await finder.locator('.device-finder-results li').count(),1);
 await page.locator('[id="field-device.node_name"]').fill('TestDevice');await finder.getByText(configFinderText('zh-Hans').empty,{exact:true}).waitFor();
 await finder.getByLabel(configFinderText('zh-Hans').changedOnly,{exact:true}).uncheck();await finder.getByRole('searchbox').fill('wifi');
 await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-device-search.png'});
 await finder.getByRole('button',{name:configFinderText('zh-Hans').clear,exact:true}).click();assert.equal(managementCommands,finderCommands);
 // Absent module settings are discoverable, but a search never adds a module.
 await finder.getByRole('searchbox').fill('module:mqtt.address');await finder.locator('button').filter({has:page.getByText('module:mqtt.address',{exact:true})}).click();
 await page.waitForFunction(()=>document.activeElement?.id==='module-mqtt');assert.equal(await page.locator('#module-mqtt-address').count(),0);
 assert.equal(await page.locator('.device-nav button.selected').textContent(),'无线与协议');
 await finder.getByRole('button',{name:configFinderText('zh-Hans').clear,exact:true}).click();await page.locator('#module-mqtt').click();
 await page.getByRole('button',{name:'无线与协议',exact:true}).click();
 // Companion BLE fields must also stay out of structured module controls/search.
 const scopeCommands=managementCommands;
 await finder.getByRole('searchbox').fill('module:paxcounter.ble_threshold');await finder.getByText(configFinderText('zh-Hans').empty,{exact:true}).waitFor();
 await finder.getByRole('searchbox').fill('module:paxcounter.wifi_threshold');await finder.locator('button').filter({has:page.getByText('module:paxcounter.wifi_threshold',{exact:true})}).click();
 const paxModule=page.locator('#module-paxcounter').locator('..');await paxModule.getByRole('button',{name:configModuleText('zh-Hans').addModule,exact:true}).click();
 for(const locale of locales){await page.locator('.management-language select').selectOption(locale.id);assert.equal(await page.locator('#module-paxcounter-ble_threshold').count(),0);assert.equal(await page.locator('#module-paxcounter-wifi_threshold').count(),1);await finder.getByRole('searchbox').fill('module:paxcounter.ble_threshold');await finder.getByText(configFinderText(locale.id).empty,{exact:true}).waitFor();}
 await page.locator('.management-language select').selectOption('zh-Hans');await finder.getByRole('button',{name:configFinderText('zh-Hans').clear,exact:true}).click();await paxModule.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/config-module-scope-zh-Hans.png'});
 page.once('dialog',dialog=>dialog.accept());await paxModule.getByRole('button',{name:configModuleText('zh-Hans').remove,exact:true}).click();assert.equal(managementCommands,scopeCommands);
 const moduleCommands=managementCommands;
 const mqttModule=page.locator('details').filter({has:page.locator('summary').filter({hasText:/^MQTT ·/})});
 await mqttModule.locator(':scope > summary').click();await mqttModule.getByRole('button',{name:'添加模块配置',exact:true}).click();
 await page.locator('#module-mqtt-enabled').check();await page.locator('#module-mqtt-address').fill('browser.example');
 const mapSettings=page.locator('#module-mqtt-map_report_settings').locator('..');await mapSettings.locator(':scope > summary').click();if(!await page.locator('#module-mqtt-map_report_settings-position_precision').count())await mapSettings.getByRole('button',{name:configModuleText('zh-Hans').addModule,exact:true}).click();
 const mapPrecision=page.locator('#module-mqtt-map_report_settings-position_precision'),mapBits=mapPrecision.locator('..').locator('input[type="number"]');
 for(const {id} of locales){await page.locator('.management-language select').selectOption(id);assert.deepEqual((await mapPrecision.locator('option').allTextContents()).map(precisionCopy),[...positionPrecisionOptions(id,{map:true}).map(row=>row.label),configPrecisionText(id).custom].map(precisionCopy));assert.equal(await mapPrecision.inputValue(),'0');await mapSettings.getByText(configPrecisionText(id).meaning+' '+configPrecisionText(id).mapScope,{exact:true}).waitFor();await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth+1),false);}
 await page.setViewportSize({width:1440,height:1000});await page.locator('.management-language select').selectOption('zh-Hans');await mapPrecision.selectOption('custom');await mapBits.fill('4294967296');await page.locator('#module-mqtt-map_report_settings-position_precision-error').waitFor();await page.getByRole('button',{name:'设备与身份',exact:true}).click();await page.getByRole('button',{name:'无线与协议',exact:true}).click();await mqttModule.locator(':scope > summary').click();await mapSettings.locator(':scope > summary').click();assert.equal(await mapPrecision.inputValue(),'custom');assert.equal(await mapBits.inputValue(),'4294967296');for(const value of [22,33,4294967295]){await mapBits.fill(String(value));await page.locator('.device-review').getByRole('cell',{name:formatPositionPrecision(value,'zh-Hans',{map:true}),exact:true}).waitFor();}await mapPrecision.selectOption('32');assert.equal(await page.locator('#module-mqtt-map_report_settings-should_report_location').isChecked(),false);assert.equal(await page.locator('#module-mqtt-map_reporting_enabled').isChecked(),false);await mapPrecision.selectOption('0');
 await page.getByRole('button',{name:'设备与身份',exact:true}).click();await finder.getByRole('searchbox').fill('MQTT 地址');
 await finder.locator('button').filter({has:page.getByText('module:mqtt.address',{exact:true})}).click();await page.waitForFunction(()=>document.activeElement?.id==='module-mqtt-address');
 assert.equal(await page.locator('#module-mqtt-address').inputValue(),'browser.example');
 await finder.getByLabel(configFinderText('zh-Hans').changedOnly,{exact:true}).check();assert.equal(await finder.locator('.device-finder-results li').count(),1);
 await finder.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-device-module-search.png'});
 await finder.locator('button').filter({has:page.getByText('module:mqtt.address',{exact:true})}).click();await page.waitForFunction(()=>document.activeElement?.id==='module-mqtt-address');
 await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-device-module-search-target.png'});
 for(const locale of locales){await page.locator('.management-language select').selectOption(locale.id);await finder.getByRole('searchbox').fill('module:mqtt.address');
  await finder.locator('button').filter({has:page.getByText('module:mqtt.address',{exact:true})}).click();await page.waitForFunction(()=>document.activeElement?.id==='module-mqtt-address');}
 await page.locator('.management-language select').selectOption('zh-Hans');await finder.getByRole('button',{name:configFinderText('zh-Hans').clear,exact:true}).click();
 const modulePassword=page.locator('#module-mqtt-password');assert.equal(await modulePassword.getAttribute('type'),'password');
 await modulePassword.fill('search-secret-fixture');await finder.getByRole('searchbox').fill('search-secret-fixture');await finder.getByText(configFinderText('zh-Hans').empty,{exact:true}).waitFor();
 await finder.getByRole('button',{name:configFinderText('zh-Hans').clear,exact:true}).click();
 await modulePassword.fill('界'.repeat(11));await page.getByText(/MODULE_FIELD_STRING/).waitFor();
 assert.equal(await page.getByRole('button',{name:'导出加密备份',exact:true}).isDisabled(),true);
 await page.getByRole('button',{name:'设备与身份',exact:true}).click();await page.getByRole('button',{name:'无线与协议',exact:true}).click();
 await mqttModule.locator(':scope > summary').click();assert.equal(await modulePassword.inputValue(),'界'.repeat(11));await page.getByText(/MODULE_FIELD_STRING/).waitFor();
 await modulePassword.fill('界'.repeat(10));assert.equal(await page.getByRole('button',{name:'导出加密备份',exact:true}).isDisabled(),false);
 page.once('dialog',dialog=>dialog.accept());await mqttModule.getByRole('button',{name:'删除条目',exact:true}).first().click();
 const remoteModule=page.locator('details').filter({has:page.locator('summary').filter({hasText:/^远程硬件 ·/})});
 await remoteModule.locator('summary').click();await remoteModule.getByRole('button',{name:'添加模块配置',exact:true}).click();
 for(let index=0;index<4;index++)await remoteModule.getByRole('button',{name:'添加 GPIO 条目',exact:true}).click();
 assert.equal(await remoteModule.getByRole('button',{name:'添加 GPIO 条目',exact:true}).isDisabled(),true);
 await page.locator('#module-remote_hardware-available_pins-0-gpio_pin').fill('13');
 await page.locator('#module-remote_hardware-available_pins-0-name').fill('Relay');
 await page.locator('#module-remote_hardware-available_pins-0-type').selectOption('2');
 assert.equal(await page.locator('#module-remote_hardware-available_pins-0-name').inputValue(),'Relay');
 page.once('dialog',dialog=>dialog.accept());await remoteModule.getByRole('button',{name:'删除条目',exact:true}).first().click();
 await page.getByText('没有未保存编辑',{exact:true}).waitFor();assert.equal(managementCommands,moduleCommands);
 const notificationModule=page.locator('details').filter({has:page.locator('summary').filter({hasText:/^外部通知 ·/})});
 await notificationModule.locator(':scope > summary').click();await notificationModule.getByRole('button',{name:'添加模块配置',exact:true}).click();
 await page.locator('#module-external_notification-output').fill('13');await page.locator('#module-external_notification-output_ms').fill('1234');await page.locator('#module-external_notification-active').check();
 const moduleOutput=page.locator('#module-external_notification-output_ms'),moduleOutputUnit=moduleOutput.locator('..').locator('select'),beforeUnit=await review.textContent();
 await moduleOutputUnit.selectOption('1000');assert.equal(await moduleOutput.inputValue(),'1.234');assert.equal(await review.textContent(),beforeUnit);await moduleOutput.fill('1.5');await notificationModule.getByText('当前时长：1,500 毫秒',{exact:true}).waitFor();
 const nag=page.locator('#module-external_notification-nag_timeout'),nagUnit=nag.locator('..').locator('select');await nagUnit.selectOption('3600');await nag.fill('20');const nagError=page.locator('#module-external_notification-nag_timeout-error');await nagError.waitFor();assert.equal(await nag.inputValue(),'20');assert.equal(await nagUnit.isDisabled(),true);
 for(const locale of locales){await page.locator('.management-language select').selectOption(locale.id);assert.ok((await nagError.textContent()).includes(configDurationText(locale.id).seconds));assert.equal(await nag.inputValue(),'20');assert.equal(await nagUnit.inputValue(),'3600');await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth+1),false);}
 await page.setViewportSize({width:1440,height:1000});await page.locator('.management-language select').selectOption('zh-Hans');await page.getByRole('button',{name:'设备与身份',exact:true}).click();await page.getByRole('button',{name:'无线与协议',exact:true}).click();await notificationModule.locator(':scope > summary').click();assert.equal(await nag.inputValue(),'20');assert.equal(await nagUnit.inputValue(),'3600');await nag.fill('1.5');await nagError.waitFor({state:'hidden'});await notificationModule.getByText('当前时长：5,400 秒',{exact:true}).waitFor();await moduleOutputUnit.selectOption('1000');await page.setViewportSize({width:1440,height:1600});await notificationModule.evaluate(node=>node.scrollIntoView({block:'start'}));await page.evaluate(()=>scrollBy(0,-140));await notificationModule.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/config-module-duration-zh-Hans.png'});await page.setViewportSize({width:1440,height:1000});
 await nag.fill('0');await nagUnit.selectOption('1');await moduleOutputUnit.selectOption('1');await moduleOutput.fill('1234');
 await page.locator('#module-external_notification-use_pwm').check();
 for(const field of ['output','output_ms','active'])assert.equal(await page.locator(`#module-external_notification-${field}`).isDisabled(),true);
 assert.equal(await page.locator('#module-external_notification-output').inputValue(),'13');assert.equal(await page.locator('#module-external_notification-output_ms').inputValue(),'1234');assert.equal(await page.locator('#module-external_notification-active').isChecked(),true);
 await page.locator('#module-external_notification-use_pwm').uncheck();for(const field of ['output','output_ms','active'])assert.equal(await page.locator(`#module-external_notification-${field}`).isDisabled(),false);
 assert.equal(await page.locator('#module-external_notification-output_ms').inputValue(),'1234');
 page.once('dialog',dialog=>dialog.accept());await notificationModule.getByRole('button',{name:'删除条目',exact:true}).first().click();
 await page.getByText('没有未保存编辑',{exact:true}).waitFor();assert.equal(managementCommands,moduleCommands);
 const storeModule=page.locator('#module-store_forward').locator('..');await storeModule.locator(':scope > summary').click();await storeModule.getByRole('button',{name:configModuleText('zh-Hans').addModule,exact:true}).click();const windowInput=page.locator('#module-store_forward-history_return_window'),windowUnit=windowInput.locator('..').locator('select');assert.deepEqual(await windowUnit.locator('option').evaluateAll(options=>options.map(option=>option.value)),['1','60']);await windowUnit.selectOption('60');await windowInput.fill('1.5');await storeModule.getByText('当前时长：90 分钟',{exact:true}).waitFor();await windowUnit.selectOption('1');await windowInput.fill('1.5');await page.locator('#module-store_forward-history_return_window-error').waitFor();await windowInput.fill('90');page.once('dialog',dialog=>dialog.accept());await storeModule.getByRole('button',{name:configModuleText('zh-Hans').remove,exact:true}).click();
 const rangeModule=page.locator('#module-range_test').locator('..');await rangeModule.locator(':scope > summary').click();await rangeModule.getByRole('button',{name:configModuleText('zh-Hans').addModule,exact:true}).click();await page.getByLabel(configModuleDurationText('zh-Hans').senderInterval,{exact:true}).waitFor();await rangeModule.getByText(configModuleDurationText('zh-Hans').senderOff,{exact:true}).waitFor();page.once('dialog',dialog=>dialog.accept());await rangeModule.getByRole('button',{name:configModuleText('zh-Hans').remove,exact:true}).click();await page.getByText('没有未保存编辑',{exact:true}).waitFor();assert.equal(managementCommands,moduleCommands);
 await page.getByRole('button',{name:'设备与身份',exact:true}).click();
 assert.equal(await page.locator('input[value="TestDevice"]').count(),1);
 await page.locator('input[value="TestDevice"]').fill('Mountain');
 await page.getByText('1 项未保存编辑').waitFor();
 await page.locator('#field-device\\.node_name').fill('TestDevice');
 await page.getByText('没有未保存编辑',{exact:true}).waitFor();
 await page.locator('#field-device\\.node_name').fill('Mountain');
 assert.equal(await page.getByRole('button',{name:'保存到设备',exact:true}).isDisabled(),true);
 await page.getByRole('button',{name:'频道与消息',exact:true}).click();const autoReply=page.locator('[id="field-chat.auto_reply.text"]'),textCommands=managementCommands;
 await page.getByRole('button',{name:'无线与协议',exact:true}).click();
 const primaryEnabled=page.locator('[id="field-mc.channel.0.enabled"]'),secondaryEnabled=page.locator('[id="field-mc.channel.1.enabled"]');
 for(const locale of locales){
  await page.locator('.management-language select').selectOption(locale.id);
  await page.getByText(configMeshChannelText(locale.id).meshCorePrimaryHelp,{exact:true}).waitFor();
  assert.equal(await primaryEnabled.isChecked(),true);assert.equal(await primaryEnabled.isDisabled(),true);
  assert.equal(await secondaryEnabled.isDisabled(),false);
  assert.equal(await page.locator('[id="field-mc.channel.0.enabled-help"]').textContent(),configMeshChannelText(locale.id).meshCorePrimaryHelp);
  const channelText=configMeshChannelText(locale.id),activeSlot=page.locator('[id="field-mc.active_slot"]'),mirror=page.locator('[id="field-mc.base.secondary_psk"]');assert.equal(await mirror.isDisabled(),true);assert.equal(await page.locator('[id="field-mc.base.secondary_psk-help"]').textContent(),channelText.meshCoreMirrorHelp);assert.equal(await activeSlot.evaluate(element=>element.tagName),'SELECT');const browserSlots=await page.evaluate(({id,slots})=>{const number=new Intl.NumberFormat(id);return slots.map(({value,label})=>({value,label:label.replace(/[0-9\u0660-\u0669]+$/,number.format(value+1))}));},{id:locale.id,slots:channelText.meshCoreSlots});assert.deepEqual(await activeSlot.locator('option').evaluateAll(options=>options.map(option=>({value:Number(option.value),label:option.textContent}))),browserSlots);
  for(const [key,property,values] of [['mc.send_profile','meshCoreSendProfiles',['0','1','2']],['mc.forward_profile','meshCoreForwardProfiles',['0','1']]]){
   const selector=page.locator(`[id="field-${key}"]`);assert.equal(await selector.evaluate(element=>element.tagName),'SELECT');
   assert.deepEqual(await selector.locator('option').evaluateAll(options=>options.map(option=>option.value)),values);
   assert.deepEqual(await selector.locator('option').allTextContents(),configRadioPolicyText(locale.id)[property]);assert.equal(await selector.inputValue(),'1');
  }
 }
 await page.locator('.management-language select').selectOption('zh-Hans');
 const mirrorInput=page.locator('[id="field-mc.base.secondary_psk"]'),actualKey=page.locator('[id="field-mc.channel.0.key"]'),slotInput=page.locator('[id="field-mc.active_slot"]'),replacedKey=Buffer.from(new Uint8Array(16).fill(33)).toString('base64');await actualKey.fill(replacedKey);await page.waitForFunction(expected=>document.getElementById('field-mc.base.secondary_psk').value===expected,replacedKey);await slotInput.selectOption('1');await page.waitForFunction(expected=>document.getElementById('field-mc.base.secondary_psk').value===expected,Buffer.from(new Uint8Array(16).fill(22)).toString('base64'));await slotInput.scrollIntoViewIfNeeded();await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/config-meshcore-mirror-desktop.png'});await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth+1),false);await slotInput.scrollIntoViewIfNeeded();await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/config-meshcore-mirror-mobile.png'});await page.setViewportSize({width:1440,height:1000});await slotInput.selectOption('0');assert.equal(await mirrorInput.inputValue(),replacedKey);await actualKey.fill(Buffer.from(new Uint8Array(16).fill(11)).toString('base64'));await page.waitForFunction(expected=>document.getElementById('field-mc.base.secondary_psk').value===expected,Buffer.from(new Uint8Array(16).fill(11)).toString('base64'));
 await secondaryEnabled.uncheck();assert.equal(await secondaryEnabled.isChecked(),false);await secondaryEnabled.check();
 assert.equal(await page.locator('[id="field-mc.channel.0.name"]').isDisabled(),false);
 await page.locator('[id="field-mc.send_profile"]').selectOption('2');await page.locator('[id="field-mc.send_profile"]').selectOption('1');
 await page.getByRole('button',{name:'Reticulum',exact:true}).click();
 for(const locale of locales){
  await page.locator('.management-language select').selectOption(locale.id);const text=configRadioPolicyText(locale.id);
  await page.getByText(text.reticulumPolicyHelp,{exact:true}).waitFor();
  const selector=page.locator('[id="field-rt.interface_policy"]');assert.equal(await selector.evaluate(element=>element.tagName),'SELECT');
  assert.deepEqual(await selector.locator('option').allTextContents(),text.reticulumPolicies);assert.equal(await selector.inputValue(),'0');assert.equal(managementCommands,textCommands);
 }
 await page.locator('.management-language select').selectOption('zh-Hans');
 await page.getByRole('button',{name:'频道与消息',exact:true}).click();
 assert.equal(await autoReply.evaluate(element=>element.tagName),'TEXTAREA');assert.equal(await autoReply.inputValue(),'Hello');
 await autoReply.fill('你好 🌍\nReply later');assert.equal(await autoReply.inputValue(),'你好 🌍\nReply later');
 await autoReply.fill('界'.repeat(58));await page.getByText(/UTF-8 内容不能超过 171/).waitFor();
 await autoReply.fill('Hello');assert.equal(managementCommands,textCommands);
 await page.getByRole('button',{name:'网络与外设',exact:true}).click();assert.equal(await page.locator('[id^="field-mt_ble."],[id^="field-mc_ble."]').count(),0);assert.equal(managementCommands,textCommands);
 const c6Password=page.locator('[id="field-c6_companion.wifi_ap_password"]'),c6Channel=page.locator('[id="field-c6_companion.wifi_ap_channel"]');
 assert.equal(await c6Password.getAttribute('type'),'password');await c6Password.fill('山'.repeat(22));await page.getByText(/UTF-8 内容不能超过 63/).waitFor();await c6Password.fill('');
 await c6Channel.fill('0');await page.getByText(/请输入 1–14 范围内的数值/).waitFor();await c6Channel.fill('1');
 const c6Commands=managementCommands;
 await page.locator('[id="field-c6_companion.wifi_ap_enabled"]').check();
 await page.locator('[id="field-c6_companion.wifi_ap_ssid"]').fill('Camp AP');await c6Password.fill('short');
 await page.locator('[id="field-c6_companion.wifi_sta_ssid"]').fill('Camp');
 await page.setViewportSize({width:390,height:844});
 for(const locale of locales){await page.locator('.management-language select').selectOption(locale.id);const copy=configC6Text(locale.id);for(const key of Object.keys(C6_CONFIG_FIELDS))await page.getByLabel(`${copy.c6_companion} · ${copy[key]}`,{exact:true}).waitFor();await page.locator('.device-c6-effective').getByText(configC6EffectiveText(locale.id),{exact:true}).waitFor();await page.locator('.device-error[role="alert"]').filter({hasText:configC6PasswordText(locale.id)}).waitFor();assert.equal(await page.locator('[id="field-c6_companion.wifi_enabled"]').isChecked(),false);assert.equal(await page.locator('[id="field-c6_companion.wifi_sta_enabled"]').isChecked(),false);assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);assert.equal(managementCommands,c6Commands);}
 await c6Password.fill('12345678');await page.locator('.device-error[role="alert"]').waitFor({state:'hidden'});
 await page.locator('[id="field-c6_companion.wifi_ap_enabled"]').uncheck();await page.locator('[id="field-c6_companion.wifi_ap_ssid"]').fill('');await c6Password.fill('');
 await page.locator('[id="field-c6_companion.wifi_sta_ssid"]').fill('');await page.locator('.device-c6-effective').waitFor({state:'hidden'});
 await page.locator('.management-language select').selectOption('zh-Hans');await page.setViewportSize({width:1440,height:1000});
 await page.getByRole('button',{name:'无线与协议',exact:true}).click();const presetToggle=page.locator('[id="field-mt.use_preset"]'),customBandwidth=page.locator('[id="field-mt.bandwidth_khz"]'),presetChoice=page.locator('[id="field-mt.modem_preset"]');assert.equal(await customBandwidth.isDisabled(),true);assert.equal(await presetChoice.isDisabled(),false);await presetToggle.uncheck();assert.equal(await customBandwidth.isDisabled(),false);assert.equal(await customBandwidth.inputValue(),'125');assert.equal(await presetChoice.isDisabled(),true);await presetToggle.check();assert.equal(await customBandwidth.isDisabled(),true);assert.equal(await customBandwidth.inputValue(),'125');
 await page.getByRole('button',{name:'显示与交互',exact:true}).click();
 const timezoneProfile=page.locator('[id="field-ui.timezone.profile"]'),timezoneOffset=page.locator('[id="field-ui.timezone.offset_minutes"]'),timezoneCommands=managementCommands;
 assert.equal(await timezoneProfile.evaluate(element=>element.tagName),'SELECT');await timezoneProfile.selectOption('-1');await timezoneOffset.fill('330');
 await page.setViewportSize({width:390,height:844});
 for(const {id} of locales){await page.locator('.management-language select').selectOption(id);const copy=configTimezoneText(id);assert.deepEqual(await timezoneProfile.locator('option').evaluateAll(options=>options.map(option=>({value:Number(option.value),label:option.textContent}))),copy.timezoneProfiles);await page.getByText(copy.timezoneProfileHelp,{exact:true}).waitFor();await page.getByText(copy.timezoneOffsetHelp,{exact:true}).waitFor();assert.equal(await timezoneProfile.inputValue(),'-1');assert.equal(await timezoneOffset.inputValue(),'330');assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);assert.equal(managementCommands,timezoneCommands);}
 await page.locator('.management-language select').selectOption('zh-Hans');await page.setViewportSize({width:1440,height:1000});await timezoneProfile.selectOption('1');await timezoneOffset.fill('480');
 await page.getByRole('button',{name:'GPS 与定位',exact:true}).click();
 assert.equal(await page.locator('[id="field-gps.enabled"]').isChecked(),true);
 const nmeaHz=page.locator('[id="field-gps.external_nmea_hz"]'),nmeaSentences=page.locator('[id="field-gps.external_nmea_mask"]');assert.equal(await nmeaHz.evaluate(element=>element.tagName),'SELECT');assert.equal(await nmeaSentences.evaluate(element=>element.tagName),'SELECT');await nmeaHz.selectOption('5');await nmeaSentences.selectOption('2');assert.equal(await nmeaSentences.locator('option:checked').textContent(),'GGA + RMC');await nmeaHz.selectOption('0');await nmeaSentences.selectOption('0');
 const gpsMask=page.locator('[id="field-gps.sat_mask"]');assert.equal(await gpsMask.inputValue(),'141');await page.getByRole('checkbox',{name:/GLONASS$/}).check();assert.equal(await gpsMask.inputValue(),'143');await page.getByRole('checkbox',{name:/BeiDou$/}).uncheck();assert.equal(await gpsMask.inputValue(),'135');await gpsMask.locator('xpath=ancestor::details[1]').locator('summary').click();await gpsMask.fill('141');assert.equal(await page.getByRole('checkbox',{name:/GLONASS$/}).isChecked(),false);assert.equal(await page.getByRole('checkbox',{name:/BeiDou$/}).isChecked(),true);
 const gpsMode=page.locator('[id="field-gps.mode"]'),gpsFormat=page.locator('[id="field-gps.coord_format"]'),positionCommands=managementCommands;
 assert.equal(await gpsMode.evaluate(element=>element.tagName),'SELECT');assert.equal(await gpsMode.inputValue(),'1');assert.equal(await gpsFormat.inputValue(),'2');
 for(const locale of locales){await page.locator('.management-language select').selectOption(locale.id);await gpsMode.getByRole('option',{name:configPositionText(locale.id).powerSave,exact:true}).waitFor({state:'attached'});assert.equal(await gpsMode.inputValue(),'1');assert.equal(managementCommands,positionCommands);}await page.locator('.management-language select').selectOption('zh-Hans');
 await page.getByRole('button',{name:'断开设备',exact:true}).click();
 await page.getByText(/未保存编辑保留/).waitFor();
 await page.getByRole('button',{name:'连接设备',exact:true}).click();
 await page.getByText(/设备配置版本未变化/).waitFor();
 const reconnectCommands=managementCommands;
 for(const locale of locales){await page.locator('.management-language select').selectOption(locale.id);await page.getByText(configConnectionText(locale.id).sameVersion,{exact:true}).waitFor();assert.equal(managementCommands,reconnectCommands);}
 await page.locator('.management-language select').selectOption('zh-Hans');
 await page.getByRole('button',{name:'设备与身份',exact:true}).click();
 assert.equal(await page.locator('#field-device\\.node_name').inputValue(),'Mountain');
 // Negotiate a newer firmware, then disconnect and reconnect an older one.
 badCapabilities=false;badProof=false;precisionSupport=true;
 await page.locator('.management-language select').selectOption('zh-Hans');
 if(await page.getByRole('button',{name:'断开设备',exact:true}).count())await page.getByRole('button',{name:'断开设备',exact:true}).click();
 await page.getByRole('button',{name:'连接设备',exact:true}).click();await page.getByRole('button',{name:'重新读取',exact:true}).click({trial:true});
 const runtimeCommands=managementCommands;
 for(const {id} of locales){await page.locator('.management-language select').selectOption(id);await page.locator('.device-nav button').nth(3).click();for(const channel of ['primary','secondary'])assert.ok((await page.locator('[id="field-channel.'+channel+'_position_precision-help"]').textContent()).includes(configPositionRuntimeText(id)));await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth+1),false);assert.equal(managementCommands,runtimeCommands);}
 await page.locator('.management-language select').selectOption('zh-Hans');await page.setViewportSize({width:1440,height:1400});const runtimeField=page.locator('[id="field-channel.primary_position_precision"]');await runtimeField.locator('xpath=ancestor::*[contains(concat(" ",normalize-space(@class)," ")," device-section ")][1]').screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/config-position-runtime-zh-Hans.png'});
 await page.getByRole('button',{name:'断开设备',exact:true}).click();assert.ok((await page.locator('[id="field-channel.primary_position_precision-help"]').textContent()).includes(configPrecisionText('zh-Hans').channelScope));
 precisionSupport=false;await page.getByRole('button',{name:'连接设备',exact:true}).click();await page.getByRole('button',{name:'重新读取',exact:true}).click({trial:true});await page.locator('.device-nav button').nth(3).click();assert.ok((await page.locator('[id="field-channel.primary_position_precision-help"]').textContent()).includes(configPrecisionText('zh-Hans').channelScope));
 assert.deepEqual(errors,[]);
 await page.setViewportSize({width:390,height:844});
 assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
 writeSupport=true;await page.setViewportSize({width:1440,height:1000});await page.reload();
 await page.getByRole('button',{name:'连接设备',exact:true}).click();await page.getByText(/已验证配置快照/).waitFor();
 await page.getByRole('button',{name:'Reticulum',exact:true}).click();
 await page.locator('#field-rt\\.wifi_port').fill('0');
 const invalidCommands=managementCommands,invalidCommits=commitCalls;
 const portField=page.locator('[id="field-rt.wifi_port"]'),portError=page.locator('[id="field-rt.wifi_port-error"]');
 for(const locale of locales){
   const text=configInputText(locale.id);
   await page.locator('.management-language select').selectOption(locale.id);
   const expectedRange=await page.evaluate(({locale,template})=>{const numbers=new Intl.NumberFormat(locale);return template.replace('{min}',numbers.format(1)).replace('{max}',numbers.format(65535));},{locale:locale.id,template:text.CONFIG_RANGE});
   await page.getByText(expectedRange,{exact:true}).waitFor();
   assert.equal(await portField.inputValue(),'0');assert.equal(await portField.getAttribute('aria-invalid'),'true');
   assert.ok((await portField.getAttribute('aria-describedby')).split(' ').includes('field-rt.wifi_port-error'));
   assert.equal(await portField.getAttribute('dir'),'ltr');assert.equal(await portError.count(),1);
   assert.equal(managementCommands,invalidCommands);assert.equal(commitCalls,invalidCommits);
 }
 await page.locator('.management-language select').selectOption('zh-Hans');
 await portField.fill('1.5');await page.getByText(configInputText('zh-Hans').CONFIG_INTEGER,{exact:true}).waitFor();
 assert.equal(await page.getByRole('button',{name:'保存到设备',exact:true}).isDisabled(),true);
 await portField.fill('0');
 await page.getByRole('button',{name:'设备与身份',exact:true}).click();
 await page.locator('#field-device\\.node_name').fill('SavedDevice');
 assert.equal(await page.getByRole('button',{name:'保存到设备',exact:true}).isDisabled(),true);
 await page.getByRole('button',{name:'Reticulum',exact:true}).click();
 assert.equal(await page.locator('#field-rt\\.wifi_port').inputValue(),'0');
 await page.locator('#field-rt\\.wifi_port').fill('4242');
 assert.equal(await portField.getAttribute('aria-invalid'),'false');assert.equal(await portError.count(),0);
 await page.getByRole('button',{name:'网络与外设',exact:true}).click();
 const c6WriteCommands=managementCommands,c6WriteCommits=commitCalls;
 await page.locator('[id="field-c6_companion.wifi_sta_ssid"]').fill('Camp');await page.locator('[id="field-c6_companion.wifi_ap_enabled"]').check();
 assert.equal(await page.getByRole('button',{name:'保存到设备',exact:true}).isDisabled(),true);
 assert.equal(await page.getByRole('button',{name:'导出加密备份',exact:true}).isDisabled(),true);
 await page.locator('[id="field-c6_companion.wifi_ap_ssid"]').fill('Camp AP');await page.locator('[id="field-c6_companion.wifi_ap_password"]').fill('short');
 for(const locale of locales){await page.locator('.management-language select').selectOption(locale.id);const copy=configActionText(locale.id);assert.equal(await page.getByRole('button',{name:copy.save,exact:true}).isDisabled(),true);assert.equal(await page.getByRole('button',{name:copy.encryptedExport,exact:true}).isDisabled(),true);assert.equal(await page.getByRole('button',{name:copy.plainExport,exact:true,includeHidden:true}).isDisabled(),true);assert.equal(managementCommands,c6WriteCommands);assert.equal(commitCalls,c6WriteCommits);}
 await page.locator('.management-language select').selectOption('zh-Hans');await page.locator('[id="field-c6_companion.wifi_ap_password"]').fill('12345678');
 assert.equal(await page.getByRole('button',{name:'保存到设备',exact:true}).isDisabled(),false);assert.equal(await page.getByRole('button',{name:'导出加密备份',exact:true}).isDisabled(),false);
 await page.locator('[id="field-c6_companion.wifi_ap_enabled"]').uncheck();await page.locator('[id="field-c6_companion.wifi_ap_ssid"]').fill('');await page.locator('[id="field-c6_companion.wifi_ap_password"]').fill('');await page.locator('[id="field-c6_companion.wifi_sta_ssid"]').fill('');
 await page.getByRole('button',{name:'设备与身份',exact:true}).click();
 page.on('dialog',dialog=>{dialogs.push(dialog.message());return dialog.accept();});
 failNextChunk=true;
 await page.getByRole('button',{name:'保存到设备',exact:true}).click();
 const previewDialog=page.getByRole('dialog',{name:'保存到设备',exact:true});await previewDialog.waitFor();
 assert.ok((await previewDialog.textContent()).includes('SavedDevice'));
 const beforeReviewLanguage=managementCommands;
 for(const locale of locales){await page.getByRole('dialog').locator('.management-language select').selectOption(locale.id);const review=page.getByRole('dialog',{name:configActionText(locale.id).save,exact:true});await review.waitFor();assert.ok((await review.textContent()).includes('SavedDevice'));assert.equal(managementCommands,beforeReviewLanguage);}
 await page.getByRole('dialog').locator('.management-language select').selectOption('zh-Hans');
 const beforeApproval=managementCommands;await previewDialog.getByRole('button',{name:'取消',exact:true}).click();assert.equal(managementCommands,beforeApproval);
 await page.getByRole('button',{name:'保存到设备',exact:true}).click();
 await page.getByRole('dialog',{name:'保存到设备',exact:true}).getByRole('button',{name:'保存到设备',exact:true}).click();
 await page.getByText(/保存未确认：/).waitFor();
 assert.equal(await page.locator('#field-device\\.node_name').isDisabled(),true);
 assert.equal(await page.getByRole('button',{name:'重新读取',exact:true}).isDisabled(),true);
 const pendingCommands=managementCommands,pendingCommitCalls=commitCalls,pendingId=Buffer.from(uploadId).toString('hex');
 for(const locale of locales){const text=configActionText(locale.id);await page.locator('.management-language select').selectOption(locale.id);await page.getByText(text.pending,{exact:true}).waitFor();assert.ok((await page.locator('.device-notice').textContent()).startsWith(configTransferText(locale.id).saveUnconfirmed.split('{diagnostic}')[0]));assert.equal(await page.getByRole('button',{name:text.read,exact:true}).isDisabled(),true);assert.equal(await page.getByRole('button',{name:text.retry,exact:true}).isDisabled(),false);assert.equal(await page.getByRole('button',{name:text.cancelUpload,exact:true}).isDisabled(),false);assert.equal(await page.locator('#field-device\\.node_name').inputValue(),'SavedDevice');assert.equal(await page.locator('#field-device\\.node_name').isDisabled(),true);assert.equal(Buffer.from(uploadId).toString('hex'),pendingId);assert.equal(managementCommands,pendingCommands);assert.equal(commitCalls,pendingCommitCalls);}
 await page.locator('.management-language select').selectOption('zh-Hans');
 await page.getByRole('button',{name:'断开设备',exact:true}).click();
 await page.getByRole('button',{name:'连接设备',exact:true}).click();
 await page.getByText(/设备配置版本未变化/).waitFor();
 await page.getByRole('button',{name:'重试并确认保存',exact:true}).click();
 await page.getByRole('dialog',{name:'保存到设备',exact:true}).getByRole('button',{name:'保存到设备',exact:true}).click();
 await page.getByText(/配置已保存，设备即将重启/).waitFor();
 assert.equal(resumed,true);
 assert.ok(new TextDecoder().decode(Uint8Array.from(uploaded)).includes('device.node_name=str:SavedDevice'));
 // A second failed upload can be explicitly discarded without losing edits.
 uploadId=undefined;failNextChunk=true;await page.locator('#field-device\\.node_name').fill('SecondDraft');
 await page.getByRole('button',{name:'保存到设备',exact:true}).click();await page.getByRole('dialog',{name:'保存到设备',exact:true}).getByRole('button',{name:'保存到设备',exact:true}).click();await page.getByText(/保存未确认：/).waitFor();
 await page.getByRole('button',{name:'取消未提交上传',exact:true}).click();await page.getByText(/未提交上传已取消/).waitFor();
 assert.equal(await page.locator('#field-device\\.node_name').isDisabled(),false);
 assert.equal(await page.locator('#field-device\\.node_name').inputValue(),'SecondDraft');
 await page.getByRole('button',{name:'重新读取',exact:true}).click();await page.getByText(/已验证配置快照/).waitFor();
 assert.equal(await page.locator('#field-device\\.node_name').inputValue(),'TestDevice');
 authorSupport=true;radioSupport=true;await page.reload();
 await page.getByRole('button',{name:'连接设备',exact:true}).click();await page.getByText('设备作者签名已验证。',{exact:true}).waitFor();
 await page.getByText(/已验证配置快照/).waitFor();
 const radioPanel=page.locator('.device-radio-identities');await radioPanel.getByRole('heading',{name:'无线协议身份',exact:true}).waitFor();
 await radioPanel.getByRole('button',{name:'读取公钥',exact:true}).click();await radioPanel.getByText('无线密钥代次: 1',{exact:true}).waitFor();
 await page.evaluate(()=>scrollTo(0,0));await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/images/radio-identities-in-device-page.png',fullPage:true});
 assert.equal(await page.locator('.device-author code').textContent(),Buffer.from(await Identity.fullHash(authorKey)).toString('hex'));
 const verifiedCommands=managementCommands,verifiedFingerprint=await page.locator('.device-author code').textContent();
 for(const locale of locales){const text=deviceAuthorText(locale.id);await page.locator('.management-language select').selectOption(locale.id);await page.getByText(text.verified,{exact:true}).waitFor();await page.getByRole('button',{name:text.reverify,exact:true}).waitFor();assert.equal(await page.locator('.device-author code').textContent(),verifiedFingerprint);assert.equal(await page.locator('[data-author-verified]').getAttribute('data-author-verified'),'true');assert.equal(await page.locator('.device-author code').getAttribute('dir'),'ltr');assert.equal(managementCommands,verifiedCommands);}
 await page.locator('.management-language select').selectOption('zh-Hans');
 await page.setViewportSize({width:390,height:844});
 assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
 await page.getByRole('button',{name:'断开设备',exact:true}).click();
 assert.equal(await page.locator('[data-author-verified]').getAttribute('data-author-verified'),'false');
 assert.equal(await page.locator('.device-author').count(),0);
 badProof=true;await page.reload();
 await page.getByRole('button',{name:'连接设备',exact:true}).click();await page.getByText(/作者身份未验证：设备作者证明验签失败/).waitFor();
 await page.getByText(/已验证配置快照/).waitFor();
 assert.equal(await page.locator('.device-author').count(),0);
 badProof=false;await page.getByRole('button',{name:'重新验证设备作者',exact:true}).click();
 await page.getByText('设备作者签名已验证。',{exact:true}).waitFor();
 assert.equal(await page.locator('.device-author').count(),1);
 // Hold an actual successful signature result, unplug, and fail the next proof.
 // Releasing the old verification must not restore the old connection's author.
 await page.evaluate(()=>{const verify=crypto.subtle.verify.bind(crypto.subtle);let hold=true;crypto.subtle.verify=async(...args)=>{const result=await verify(...args);if(hold){hold=false;window.authorVerifyWaiting=true;await new Promise(resolve=>{window.finishAuthorVerification=resolve;});window.authorVerifyReleased=true;}return result;};});
 await page.getByRole('button',{name:'重新验证设备作者',exact:true}).click();
 await page.waitForFunction(()=>window.authorVerifyWaiting===true);
 await page.evaluate(()=>window.testUnplug());await page.getByText('设备已断开，作者授权已结束。',{exact:true}).waitFor();
 assert.equal(await page.locator('.device-author').count(),0);
 badProof=true;await page.getByRole('button',{name:'连接设备',exact:true}).click();
 await page.getByText(/作者身份未验证：设备作者证明验签失败/).waitFor();await page.getByText(/已验证配置快照/).waitFor();
 assert.equal(await page.locator('[data-author-verified]').getAttribute('data-author-verified'),'false');
 const afterReconnectCommands=managementCommands;
 await page.evaluate(()=>window.finishAuthorVerification());await page.waitForFunction(()=>window.authorVerifyReleased===true);
 await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
 assert.equal(await page.locator('.device-author').count(),0,'late success must not restore revoked author');
 await page.getByText(/作者身份未验证：设备作者证明验签失败/).waitFor();assert.equal(managementCommands,afterReconnectCommands);
 badProof=false;await page.getByRole('button',{name:'重新验证设备作者',exact:true}).click();await page.getByText('设备作者签名已验证。',{exact:true}).waitFor();
 await page.setViewportSize({width:1440,height:1000});
 await page.getByRole('button',{name:'Reticulum',exact:true}).click();
 await page.getByRole('button',{name:'添加 TCP 接口',exact:true}).click();
 assert.equal(await page.getByRole('button',{name:'保存到设备',exact:true}).isDisabled(),true);
 await page.locator('[id="field-rt.net.interface.1.target_host"]').fill('gateway.example.org');
 await page.getByRole('region',{name:'Reticulum 接口 2',exact:true}).getByRole('button',{name:'添加接口访问控制',exact:true}).click();
 await page.locator('[id="field-rt.net.interface.1.network_name"]').fill('Private');
 assert.equal(await page.getByRole('button',{name:'保存到设备',exact:true}).isDisabled(),true);
 const invalidAccessCommands=managementCommands,invalidAccessCommits=commitCalls;
 for(const locale of locales){await page.locator('.management-language select').selectOption(locale.id);await page.getByText(configCollectionIssueText(locale.id).COLLECTION_IFAC_INVALID,{exact:false}).waitFor();assert.equal(await page.getByRole('button',{name:configActionText(locale.id).save,exact:true}).isDisabled(),true);assert.equal(await page.locator('[id="field-rt.net.interface.1.network_name"]').inputValue(),'Private');assert.equal(await page.locator('[id="field-rt.net.interface.1.ifac_size"]').inputValue(),'0');assert.equal(managementCommands,invalidAccessCommands);assert.equal(commitCalls,invalidAccessCommits);}
 await page.locator('.management-language select').selectOption('zh-Hans');
 const accessLength=page.locator('[id="field-rt.net.interface.1.ifac_size"]'),accessRow=page.locator('.device-field').filter({has:accessLength});
 await accessLength.selectOption('custom');await accessRow.locator('input').fill('9');
 const invalidBitsCommands=managementCommands;
 for(const locale of locales){await page.locator('.management-language select').selectOption(locale.id);await page.getByText(configReticulumInterfaceText(locale.id).CONFIG_IFAC_BITS,{exact:true}).waitFor();assert.equal(await accessRow.locator('input').inputValue(),'9');assert.equal(await accessRow.locator('input').getAttribute('step'),'8');assert.equal(managementCommands,invalidBitsCommands);}
 await page.locator('.management-language select').selectOption('zh-Hans');await page.getByRole('button',{name:'设备与身份',exact:true}).click();await page.getByRole('button',{name:'Reticulum',exact:true}).click();assert.equal(await accessRow.locator('input').inputValue(),'9');
 await accessRow.locator('input').fill('24');assert.equal(await page.getByRole('button',{name:'保存到设备',exact:true}).isDisabled(),false);
 await accessLength.selectOption('64');
 await page.locator('[id="field-rt.net.interface.1.passphrase"]').fill('private-password');
 assert.equal(await page.locator('[id="field-rt.net.interface.1.passphrase"]').getAttribute('type'),'password');
 const interfaceCommands=managementCommands,interfaceCommits=commitCalls;
 const interfaceValues=await page.locator('.device-collection input,.device-collection select').evaluateAll(nodes=>nodes.map(node=>node.value));
 for(const locale of locales){
   const text=configSchemaText(locale.id);await page.locator('.management-language select').selectOption(locale.id);
   await page.locator('[id="field-rt.net.interface.1.target_host-help"]').getByText(text.tcpHostHelp,{exact:true}).waitFor();
   const collection=configCollectionText(locale.id),index=await page.evaluate(locale=>new Intl.NumberFormat(locale).format(2),locale.id);
   await page.getByRole('region',{name:`${collection.interfaces} ${index}`,exact:true}).getByRole('button',{name:collection.removeAccess,exact:true}).waitFor();
   await page.getByRole('button',{name:collection.addTcp,exact:true}).waitFor();
   const selector=page.locator('[id="field-rt.net.interface.1.type"]');
   assert.equal(await selector.inputValue(),'2');assert.deepEqual(await selector.locator('option').evaluateAll(nodes=>nodes.map(node=>node.value)),['0','1','2']);
   assert.equal(await selector.locator('option[value="2"]').textContent(),text.tcpClient);
   const fields=configNetworkFieldText(locale.id);
   assert.ok((await page.locator('label[for="field-rt.net.interface.1.target_port"]').textContent()).endsWith(` · ${fields.target_port}`));
   assert.ok((await page.locator('label[for="field-rt.net.interface.1.passphrase"]').textContent()).endsWith(` · ${fields.passphrase}`));
   const accessText=configReticulumInterfaceText(locale.id);await page.getByText(accessText.bitsHelp,{exact:true}).waitFor();await page.locator('[id="field-rt.net.interface.1.target_port-help"]').getByText(accessText.tcpPortHelp,{exact:true}).waitFor();await page.getByText(accessText.accessScope,{exact:true}).first().waitFor();
   assert.deepEqual(await page.locator('.device-collection input,.device-collection select').evaluateAll(nodes=>nodes.map(node=>node.value)),interfaceValues);
   assert.equal(managementCommands,interfaceCommands);assert.equal(commitCalls,interfaceCommits);
 }
 await page.locator('.management-language select').selectOption('zh-Hans');
 await page.evaluate(()=>scrollTo(0,0));await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-device-interfaces.png',fullPage:true});
 await page.getByRole('region',{name:'Reticulum 接口 2',exact:true}).screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-device-ifac-length.png'});
 await page.getByRole('region',{name:'Reticulum 接口 2',exact:true}).getByRole('button',{name:'移除接口访问控制',exact:true}).click();
 await page.locator('[id="field-rt.net.interface.1.type"]').selectOption('1');
 const autoRow=page.getByRole('region',{name:'Reticulum 接口 2',exact:true});
 assert.equal(await autoRow.locator('[id="field-rt.net.interface.1.target_host"]').isVisible(),false);assert.equal(await autoRow.locator('[id="field-rt.net.interface.1.target_host"]').inputValue(),'gateway.example.org');
 for(const field of ['group_id','discovery_port','data_port'])assert.equal(await autoRow.locator(`[id="field-rt.net.interface.1.${field}"]`).isVisible(),true);
 await page.locator('[id="field-rt.net.interface.1.group_id"]').fill('web-discovery');await page.locator('[id="field-rt.net.interface.1.discovery_port"]').fill('29717');await page.locator('[id="field-rt.net.interface.1.data_port"]').fill('42672');
 const autoCommands=managementCommands;
 for(const locale of locales){await page.locator('.management-language select').selectOption(locale.id);const copy=configReticulumInterfaceText(locale.id);for(const [field,key] of [['group_id','groupHelp'],['discovery_port','discoveryHelp'],['data_port','dataHelp']])await page.locator(`[id="field-rt.net.interface.1.${field}-help"]`).getByText(copy[key],{exact:true}).waitFor();assert.equal(await page.locator('[id="field-rt.net.interface.1.target_host"]').inputValue(),'gateway.example.org');assert.equal(await page.locator('[id="field-rt.net.interface.1.group_id"]').inputValue(),'web-discovery');assert.equal(managementCommands,autoCommands);await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);await page.setViewportSize({width:1440,height:1000});}
 await page.locator('.management-language select').selectOption('zh-Hans');await autoRow.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-device-auto-ports.png'});
 await page.locator('[id="field-rt.net.interface.1.type"]').selectOption('2');assert.equal(await page.locator('[id="field-rt.net.interface.1.target_host"]').isVisible(),true);assert.equal(await page.locator('[id="field-rt.net.interface.1.group_id"]').isVisible(),false);assert.equal(await page.locator('[id="field-rt.net.interface.1.group_id"]').inputValue(),'web-discovery');
 await page.getByRole('region',{name:'Reticulum 接口 2',exact:true}).getByRole('button',{name:'上移',exact:true}).click();
 assert.equal(await page.locator('[id="field-rt.net.interface.0.target_host"]').inputValue(),'gateway.example.org');
 await page.getByRole('region',{name:'Reticulum 接口 1',exact:true}).getByRole('button',{name:'删除条目',exact:true}).click();
 assert.equal(await page.locator('[id="field-rt.net.interface.0.id"]').inputValue(),'lora');
 assert.equal(await page.locator('[id="field-rt.net.interface.0.passphrase"]').count(),0);
 await page.getByRole('button',{name:'添加 TCP 接口',exact:true}).click();
 await page.locator('[id="field-rt.net.interface.1.target_host"]').fill('gateway.example.org');
 await page.getByRole('button',{name:'网络与外设',exact:true}).click();
 const secret=page.locator('[id="field-wifi.profile.0.password"]'),secretRow=page.locator('.device-field').filter({has:secret}),secretValue=await secret.inputValue(),secretCommands=managementCommands;
 assert.equal(await secret.getAttribute('type'),'password');assert.equal(await secret.getAttribute('autocomplete'),'new-password');assert.equal(await secret.getAttribute('spellcheck'),'false');
 await secretRow.getByRole('button',{name:'查看内容',exact:true}).click();assert.equal(await secret.getAttribute('type'),'text');assert.equal(await secret.inputValue(),secretValue);
 for(const locale of locales){await page.locator('.management-language select').selectOption(locale.id);await secretRow.getByRole('button',{name:configSecretText(locale.id).hide,exact:true}).waitFor();assert.equal(await secret.inputValue(),secretValue);assert.equal(managementCommands,secretCommands);}
 await page.locator('.management-language select').selectOption('zh-Hans');await secretRow.getByRole('button',{name:'隐藏内容',exact:true}).click();assert.equal(await secret.getAttribute('type'),'password');assert.equal(managementCommands,secretCommands);
 await page.getByRole('button',{name:'添加 Wi-Fi',exact:true}).click();
 assert.equal(await page.getByRole('button',{name:'保存到设备',exact:true}).isDisabled(),true);
 await page.locator('[id="field-wifi.profile.2.ssid"]').fill('Camp');
 assert.equal(await page.getByRole('button',{name:'保存到设备',exact:true}).isDisabled(),true);
 await page.locator('[id="field-wifi.profile.2.ssid"]').fill('TrailNet');
 await page.getByRole('region',{name:'已保存 Wi-Fi 3',exact:true}).getByRole('button',{name:'上移',exact:true}).click();
 await page.getByRole('region',{name:'已保存 Wi-Fi 1',exact:true}).getByRole('button',{name:'删除条目',exact:true}).click();
 assert.equal(await page.locator('[id="field-wifi.profile.0.ssid"]').inputValue(),'TrailNet');
 assert.equal(await page.locator('[id="field-wifi.profile.1.ssid"]').inputValue(),'Camp');
 await page.evaluate(()=>scrollTo(0,0));await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-device-wifi.png',fullPage:true});
 await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
 await page.getByRole('button',{name:'保存到设备',exact:true}).click();await page.getByRole('dialog',{name:'保存到设备',exact:true}).getByRole('button',{name:'保存到设备',exact:true}).click();await page.getByText(/配置已保存，设备即将重启/).waitFor();
 const saved=new TmsDocument(Uint8Array.from(uploaded));
 assert.equal(saved.get('rt.net.interface_count').value,2);assert.equal(saved.get('rt.net.interface.1.target_host').value,'gateway.example.org');
 assert.equal(saved.get('wifi.profile_count').value,2);assert.equal(saved.get('wifi.profile.0.ssid').value,'TrailNet');
 assert.equal(saved.get('wifi.profile.1.vendor').raw,'future');assert.throws(()=>saved.get('wifi.profile.2.ssid'));
 assert.equal(saved.get('rt.net.interface.5.target_port').value,4242);assert.throws(()=>saved.get('rt.net.interface.1.passphrase'));
 // A blank access form is a firmware-format no-op, even after cancel/review in
 // another language. No transfer, commit or restart is sent for this operation.
 await page.getByRole('button',{name:'Reticulum',exact:true}).click();
 await page.getByRole('region',{name:'Reticulum 接口 2',exact:true}).getByRole('button',{name:'添加接口访问控制',exact:true}).click();
 const blankAccessCommands=managementCommands,blankAccessCommits=commitCalls;
 await page.getByRole('button',{name:'保存到设备',exact:true}).click();await page.getByRole('dialog').getByText(configReticulumInterfaceText('zh-Hans').emptyAccessReview,{exact:true}).waitFor();
 await page.getByRole('dialog').getByRole('button',{name:'取消',exact:true}).click();assert.equal(await page.locator('[id="field-rt.net.interface.1.ifac_size"]').inputValue(),'0');
 await page.getByRole('button',{name:'保存到设备',exact:true}).click();
 for(const locale of locales){await page.getByRole('dialog').locator('.management-language select').selectOption(locale.id);const copy=configReticulumInterfaceText(locale.id);await page.getByRole('dialog',{name:copy.tidy,exact:true}).getByText(copy.emptyAccessReview,{exact:true}).waitFor();assert.equal(managementCommands,blankAccessCommands);}
 await page.getByRole('dialog').locator('.management-language select').selectOption('zh-Hans');await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-device-ifac-no-write.png'});await page.getByRole('dialog').getByRole('button',{name:configReticulumInterfaceText('zh-Hans').tidy,exact:true}).click();
 await page.getByText(configReticulumInterfaceText('zh-Hans').emptyAccessNormalized,{exact:true}).waitFor();assert.equal(await page.locator('[id="field-rt.net.interface.1.ifac_size"]').count(),0);assert.equal(managementCommands,blankAccessCommands);assert.equal(commitCalls,blankAccessCommits);
 // With another real edit, upload only the canonical bytes and verify readback.
 await page.getByRole('region',{name:'Reticulum 接口 2',exact:true}).getByRole('button',{name:'添加接口访问控制',exact:true}).click();
 await page.locator('[id="field-rt.net.interface.1.target_port"]').fill('4243');uploadId=undefined;
 await page.setViewportSize({width:1440,height:1000});await page.getByRole('button',{name:'保存到设备',exact:true}).click();await page.getByRole('dialog').getByText(configReticulumInterfaceText('zh-Hans').normalized,{exact:true}).waitFor();
 await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-device-ifac-save-review.png'});
 await page.getByRole('dialog').getByRole('button',{name:'保存到设备',exact:true}).click();await page.getByText(/配置已保存，设备即将重启/).waitFor();
 const normalizedUpload=new TmsDocument(Uint8Array.from(uploaded));assert.equal(normalizedUpload.get('rt.net.interface.1.target_port').value,4243);assert.throws(()=>normalizedUpload.get('rt.net.interface.1.ifac_size'));
 assert.equal(await page.locator('[id="field-rt.net.interface.1.ifac_size"]').count(),0);readbackData=Uint8Array.from(uploaded);
 await page.getByRole('button',{name:'重新读取',exact:true}).click();await page.getByText(configReadbackText('zh-Hans').matched,{exact:true}).waitFor();readbackData=undefined;
 await page.getByRole('button',{name:'网络与外设',exact:true}).click();
 const restoreText=new TextDecoder().decode(data).replace('document.kind=enum:working','document.kind=enum:backup')
  .replace('wifi.profile_count=u8:2','wifi.profile_count=u8:3')
  .replace('END\n','wifi.profile.2.ssid=str:BackupNet\nwifi.profile.2.password=str:restored-password\nEND\n');
 const commitsBeforeImport=commitCalls;
 await page.locator('input[type=file]').setInputFiles({name:'invalid.tms',mimeType:'text/plain',buffer:Buffer.from(restoreText.replace('str:BackupNet','str:Camp'))});
 await page.getByText(/备份未导入：/).waitFor();
 assert.equal(await page.locator('[id="field-wifi.profile.0.ssid"]').inputValue(),'TrailNet');assert.equal(commitCalls,commitsBeforeImport);
 await page.locator('input[type=file]').setInputFiles({name:'restore.tms',mimeType:'text/plain',buffer:Buffer.from(restoreText)});
 await page.getByText(/备份已导入编辑区/).waitFor();assert.equal(commitCalls,commitsBeforeImport);
 const importedCommands=managementCommands,importedValues=await page.locator('.device-collection input,.device-collection select').evaluateAll(nodes=>nodes.map(node=>node.value));
 for(const locale of locales){await page.locator('.management-language select').selectOption(locale.id);await page.getByText(configImportText(locale.id).imported.split('{skipped}')[0],{exact:false}).waitFor();assert.ok((await page.locator('label[for="field-wifi.profile.0.ssid"]').textContent()).endsWith(` · ${configBasicFieldText(locale.id).ssid}`));assert.deepEqual(await page.locator('.device-collection input,.device-collection select').evaluateAll(nodes=>nodes.map(node=>node.value)),importedValues);assert.equal(managementCommands,importedCommands);assert.equal(commitCalls,commitsBeforeImport);}
 await page.locator('.management-language select').selectOption('zh-Hans');
 assert.ok(dialogs.at(-1).includes('项新增')&&dialogs.at(-1).includes('项删除')&&dialogs.at(-1).includes('口令或密钥'));
 assert.equal(await page.locator('[id="field-wifi.profile.2.ssid"]').inputValue(),'BackupNet');
 assert.equal(await page.locator('[id="field-wifi.profile.0.ssid"]').inputValue(),'Home');
 await page.getByRole('button',{name:'Reticulum',exact:true}).click();assert.equal(await page.locator('.device-collection-row').count(),1);
 assert.equal(await page.locator('[id="field-rt.net.interface.0.id"]').inputValue(),'lora');
 uploadId=undefined;
 await page.getByRole('button',{name:'保存到设备',exact:true}).click();await page.getByRole('dialog',{name:'保存到设备',exact:true}).getByRole('button',{name:'保存到设备',exact:true}).click();await page.getByText(/配置已保存，设备即将重启/).waitFor();
 const restored=new TmsDocument(Uint8Array.from(uploaded));assert.equal(commitCalls,commitsBeforeImport+1);
 assert.equal(restored.get('wifi.profile_count').value,3);assert.equal(restored.get('wifi.profile.2.password').value,'restored-password');
 assert.equal(restored.get('wifi.profile.1.vendor').raw,'future');assert.equal(restored.get('rt.net.interface_count').value,1);
 assert.equal(restored.get('rt.net.interface.1.id').value,'');assert.equal(restored.get('rt.net.interface.5.target_port').value,4242);
 const commandsBeforeBackup=managementCommands,commitsBeforeEncrypted=commitCalls,backupPassword='browser backup password 123';
 await page.setViewportSize({width:1440,height:1000});
 await page.getByRole('button',{name:'导出加密备份',exact:true}).click();
 await page.getByRole('dialog',{name:'导出加密配置备份',exact:true}).waitFor();
 await page.getByLabel('备份口令',{exact:true}).fill(backupPassword);
 await page.getByLabel('再次输入备份口令',{exact:true}).fill('different password 123');
 assert.equal(await page.getByRole('button',{name:'加密并下载',exact:true}).isDisabled(),true);
 await page.keyboard.press('Escape');assert.equal(await page.getByRole('dialog').count(),0);
 await page.waitForFunction(()=>document.activeElement?.textContent==='导出加密备份');
 await page.getByRole('button',{name:'导出加密备份',exact:true}).click();assert.equal(await page.getByLabel('备份口令',{exact:true}).inputValue(),'');
 await page.getByLabel('备份口令',{exact:true}).fill(backupPassword);await page.getByLabel('再次输入备份口令',{exact:true}).fill(backupPassword);
 for(const locale of locales){const text=configBackupText(locale.id);await page.getByRole('dialog').locator('.management-language select').selectOption(locale.id);await page.getByRole('dialog',{name:text.exportTitle,exact:true}).waitFor();assert.equal(await page.getByLabel(text.password,{exact:true}).inputValue(),backupPassword);assert.equal(await page.getByLabel(text.confirmation,{exact:true}).inputValue(),backupPassword);assert.equal(managementCommands,commandsBeforeBackup);assert.equal(commitCalls,commitsBeforeEncrypted);}
 await page.getByRole('dialog').locator('.management-language select').selectOption('zh-Hans');
 await page.waitForFunction(()=>[...document.querySelectorAll('button')].find(button=>button.textContent==='加密并下载')?.disabled===false);
 assert.equal(await page.getByRole('button',{name:'加密并下载',exact:true}).isDisabled(),false);
 await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-device-encrypted-backup.png'});
 await page.evaluate(()=>{const original=crypto.subtle.encrypt.bind(crypto.subtle);crypto.subtle.encrypt=async(...args)=>{window.encryptionWaiting=true;await new Promise(resolve=>window.finishEncryption=resolve);window.encryptionWaiting=false;crypto.subtle.encrypt=original;return original(...args);};});
 const downloadPromise=page.waitForEvent('download');await page.getByLabel('再次输入备份口令',{exact:true}).press('Enter');
 await page.waitForFunction(()=>window.encryptionWaiting===true);
 for(const locale of locales){const text=configBackupText(locale.id);await page.getByRole('dialog').locator('.management-language select').selectOption(locale.id);await page.getByText(text.working,{exact:true}).waitFor();assert.equal(await page.getByRole('button',{name:text.encrypt,exact:true}).isDisabled(),true);assert.equal(await page.getByLabel(text.password,{exact:true}).inputValue(),backupPassword);assert.equal(managementCommands,commandsBeforeBackup);assert.equal(commitCalls,commitsBeforeEncrypted);}
 await page.keyboard.press('Escape');assert.equal(await page.getByRole('dialog').count(),1);
 await page.getByRole('dialog').locator('.management-language select').selectOption('zh-Hans');await page.evaluate(()=>finishEncryption());
 const download=await downloadPromise;assert.equal(download.suggestedFilename(),'trail-mate-settings.tmbak');
 const encrypted=await fs.readFile(await download.path());assert.equal(isEncryptedConfigBackup(encrypted),true);
 const decrypted=await decryptConfigBackup(encrypted,backupPassword,{cryptoProvider:webcrypto});
 assert.deepEqual(decrypted.serialize({kind:'working'}),restored.serialize({kind:'working'}));
 const provenance=(await decryptConfigBackupDetails(encrypted,backupPassword,{cryptoProvider:webcrypto})).source;
 assert.equal(provenance.model,'T-Deck');assert.equal(provenance.firmware,'test');assert.equal(provenance.authorGeneration,7);
 assert.equal(provenance.authorFingerprint,Buffer.from(await Identity.fullHash(authorKey)).toString('hex'));
 await page.getByRole('button',{name:'网络与外设',exact:true}).click();await page.locator('[id="field-wifi.profile.0.ssid"]').fill('Edited');
 await page.locator('input[type=file]').setInputFiles({name:'encrypted.tmbak',mimeType:'application/octet-stream',buffer:encrypted});
 await page.getByRole('dialog',{name:'解密配置备份',exact:true}).waitFor();
 await page.getByLabel('备份口令',{exact:true}).fill('incorrect backup password');await page.getByRole('button',{name:'解密并检查导入',exact:true}).click();
 await page.getByText('备份口令不正确或文件已损坏',{exact:true}).waitFor();assert.equal(await page.locator('[id="field-wifi.profile.0.ssid"]').inputValue(),'Edited');
 await page.getByRole('dialog').locator('.management-language select').selectOption('en');await page.getByText(configBackupText('en').authenticationError,{exact:true}).waitFor();assert.equal(await page.getByLabel(configBackupText('en').password,{exact:true}).inputValue(),'incorrect backup password');
 await page.getByRole('dialog').locator('.management-language select').selectOption('zh-Hans');
 await page.getByLabel('备份口令',{exact:true}).fill(backupPassword);await page.getByRole('button',{name:'解密并检查导入',exact:true}).click();
 await page.getByText('检查备份来源',{exact:true}).waitFor();
 assert.equal(await page.locator('[id="field-wifi.profile.0.ssid"]').inputValue(),'Edited');
 assert.equal(await page.locator('.device-backup-fingerprint').textContent(),provenance.authorFingerprint);
 for(const locale of locales){const text=configBackupText(locale.id);await page.getByRole('dialog').locator('.management-language select').selectOption(locale.id);await page.getByText(text.sourceTitle,{exact:true}).waitFor();assert.equal(await page.locator('.device-backup-fingerprint').textContent(),provenance.authorFingerprint);assert.equal(await page.locator('dialog time').getAttribute('datetime'),provenance.createdAt);assert.equal(await page.locator('[id="field-wifi.profile.0.ssid"]').inputValue(),'Edited');assert.equal(managementCommands,commandsBeforeBackup);assert.equal(commitCalls,commitsBeforeEncrypted);}
 await page.getByRole('dialog').locator('.management-language select').selectOption('zh-Hans');
 await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-device-backup-source.png'});
 await page.getByRole('button',{name:'继续检查导入',exact:true}).click();
 await page.getByText(/备份已导入编辑区/).waitFor();await page.getByRole('dialog').waitFor({state:'detached'});
 assert.equal(await page.locator('[id="field-wifi.profile.0.ssid"]').inputValue(),'Home');
 const foreign=await encryptConfigBackup(restored,backupPassword,{cryptoProvider:webcrypto,source:{...provenance,authorGeneration:8}});
 await page.locator('[id="field-wifi.profile.0.ssid"]').fill('KeepEdit');
 await page.locator('input[type=file]').setInputFiles({name:'other-generation.tmbak',mimeType:'application/octet-stream',buffer:Buffer.from(foreign)});
 await page.getByLabel('备份口令',{exact:true}).fill(backupPassword);await page.getByRole('button',{name:'解密并检查导入',exact:true}).click();
 await page.getByText('备份记录的作者或身份代次与当前设备不同，请检查恢复内容。',{exact:true}).waitFor();
 await page.getByRole('dialog').getByRole('button',{name:'取消',exact:true}).click();
 assert.equal(await page.locator('[id="field-wifi.profile.0.ssid"]').inputValue(),'KeepEdit');
 assert.equal(managementCommands,commandsBeforeBackup);assert.equal(commitCalls,commitsBeforeEncrypted);
 assert.equal((await page.evaluate(()=>JSON.stringify(Object.values(localStorage)))).includes(backupPassword),false);
 await page.getByRole('button',{name:'导出加密备份',exact:true}).click();await page.setViewportSize({width:390,height:844});
 assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);await page.keyboard.press('Escape');
 await page.setViewportSize({width:1440,height:1000});await page.getByRole('button',{name:'无线与协议',exact:true}).click();
 await mqttModule.locator(':scope > summary').click();
 await mqttModule.getByRole('button',{name:'添加模块配置',exact:true}).click();
 await page.locator('#module-mqtt-enabled').check();await page.locator('#module-mqtt-address').fill('saved.example');
 await page.locator('#module-mqtt-password').fill('synthetic-module-password');
 const serialModule=page.locator('details').filter({has:page.locator('summary').filter({hasText:/^串口 ·/})});
 await serialModule.locator(':scope > summary').click();await serialModule.getByRole('button',{name:'添加模块配置',exact:true}).click();
 await page.locator('#module-serial-mode').selectOption('3');
 const beforeModuleLocales=managementCommands;
 for(const locale of locales){
  await page.locator('.management-language select').selectOption(locale.id);await page.getByRole('heading',{name:configModuleText(locale.id).title,exact:true}).waitFor();
  assert.equal(await page.getByLabel(configModuleFieldLabel(['mqtt','address'],locale.id),{exact:true}).inputValue(),'saved.example');
  const moduleText=configModuleText(locale.id);
  for(const name of ['address','username','password','root'])assert.ok((await page.locator(`[id="module-mqtt-${name}-help"]`).textContent()).includes(moduleText.blankMqttValue));
  assert.equal(await mqttModule.locator(':scope > p').filter({hasText:moduleText.restoredPresence}).textContent(),moduleText.restoredPresence);
  assert.equal(await page.locator('#module-mqtt-map_report_settings').locator('..').locator(':scope > p').filter({hasText:moduleText.restoredPresence}).textContent(),moduleText.restoredPresence);
  const serialMode=page.getByLabel(configModuleFieldLabel(['serial','mode'],locale.id),{exact:true});assert.equal(await serialMode.inputValue(),'3');
  assert.equal(await serialMode.getByRole('option',{name:configModuleFieldLabel(['serial','mode','TEXTMSG'],locale.id,'option'),exact:true}).getAttribute('value'),'3');
  await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
  await page.setViewportSize({width:1440,height:1000});assert.equal(managementCommands,beforeModuleLocales);
 }
 await page.locator('.management-language select').selectOption('zh-Hans');
 await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-device-modules.png'});
 uploadId=undefined;const beforeModuleCommit=commitCalls;
 await page.getByRole('button',{name:'保存到设备',exact:true}).click();
 const moduleReview=page.getByRole('dialog',{name:'保存到设备',exact:true});
 assert.ok((await moduleReview.textContent()).includes('saved.example'));assert.ok(!(await moduleReview.textContent()).includes('synthetic-module-password'));
 assert.equal(await moduleReview.locator('code').filter({hasText:'module.mqtt.address'}).count(),1);
 await moduleReview.getByRole('button',{name:'保存到设备',exact:true}).click();await page.getByText(/配置已保存，设备即将重启/).waitFor();
 assert.equal(commitCalls,beforeModuleCommit+1);
 const savedModule=readStructuredModule(readMeshtasticModule(new TmsDocument(Uint8Array.from(uploaded))));
 const savedMqtt=savedModule.fields.find(field=>field.name==='mqtt').value;
 assert.equal(savedMqtt.fields.find(field=>field.name==='enabled').value,true);
 assert.equal(savedMqtt.fields.find(field=>field.name==='address').value,'saved.example');
 assert.equal(savedMqtt.fields.find(field=>field.name==='password').value,'synthetic-module-password');
 assert.equal(savedModule.fields.find(field=>field.name==='version').value,1);
 assert.equal(savedModule.fields.find(field=>field.name==='serial').value.fields.find(field=>field.name==='mode').value,3);
 await page.getByText(configReadbackText('zh-Hans').pending,{exact:true}).waitFor();
 readbackData=Uint8Array.from(uploaded);
 const nativeFloat=new TmsDocument(readbackData);nativeFloat.set('mc.frequency_mhz',869.525024);readbackData=nativeFloat.serialize();
 await page.getByRole('button',{name:'断开设备',exact:true}).click();await page.getByRole('button',{name:'连接设备',exact:true}).click();
 await page.getByText(configReadbackText('zh-Hans').matched,{exact:true}).waitFor();
 const canonicalCheckpoint=JSON.parse(await page.evaluate(()=>localStorage.getItem('trailmate.config.readback.v1')));assert.equal(canonicalCheckpoint.sha256,Buffer.from(await webcrypto.subtle.digest('SHA-256',readbackData)).toString('hex'));
 const readbackCommands=managementCommands;
 for(const locale of locales){await page.locator('.management-language select').selectOption(locale.id);await page.getByText(configReadbackText(locale.id).matched,{exact:true}).waitFor();assert.equal(managementCommands,readbackCommands);}
 await page.locator('.management-language select').selectOption('zh-Hans');
 let normalized=updateStructuredModuleField(new TmsDocument(readbackData),['mqtt','address'],'device-normalized.example');
 normalized=updateStructuredModuleField(normalized,['mqtt','password'],'replacement-sensitive');readbackData=normalized.serialize();
 await page.getByRole('button',{name:'重新读取',exact:true}).click();await page.getByText(configReadbackText('zh-Hans').mismatch,{exact:true}).waitFor();
 const readbackRegion=page.getByRole('region',{name:configReadbackText('zh-Hans').title,exact:true});
 assert.ok((await readbackRegion.textContent()).includes('device-normalized.example'));
 assert.ok(!(await readbackRegion.textContent()).includes('synthetic-module-password'));assert.ok(!(await readbackRegion.textContent()).includes('replacement-sensitive'));
 for(const locale of locales){
  await page.locator('.management-language select').selectOption(locale.id);const text=configReadbackText(locale.id);
  await page.getByText(text.mismatch,{exact:true}).waitFor();
  const region=page.getByRole('region',{name:text.title,exact:true});await region.getByRole('columnheader',{name:text.before,exact:true}).waitFor();
  assert.ok(!(await region.textContent()).includes('replacement-sensitive'));
  await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
  await page.setViewportSize({width:1440,height:1000});
 }
 await page.locator('.management-language select').selectOption('zh-Hans');await readbackRegion.scrollIntoViewIfNeeded();
 await page.screenshot({path:'docs/locales/zh-Hans/web-outdoor-management/assets/implemented-device-readback.png'});
 badProof=true;await page.getByRole('button',{name:'断开设备',exact:true}).click();await page.getByRole('button',{name:'连接设备',exact:true}).click();
 await page.getByText(configReadbackText('zh-Hans').unverified,{exact:true}).waitFor();
 assert.equal(await page.getByRole('heading',{name:configReadbackText('zh-Hans').differences,exact:true}).count(),0);badProof=false;
 await page.getByRole('button',{name:'断开设备',exact:true}).click();await page.getByRole('button',{name:'连接设备',exact:true}).click();
 await page.getByText(configReadbackText('zh-Hans').mismatch,{exact:true}).waitFor();
 await page.getByRole('button',{name:'设备与身份',exact:true}).click();await page.locator('[id="field-device.node_name"]').fill('RefreshLostReply');
 uploadId=undefined;loseNextCommitReply=true;
 await page.getByRole('button',{name:'保存到设备',exact:true}).click();await page.getByRole('dialog',{name:'保存到设备',exact:true}).getByRole('button',{name:'保存到设备',exact:true}).click();
 await page.getByRole('button',{name:'连接设备',exact:true}).waitFor();const lostCommitCount=commitCalls;
 const storedCheckpoint=await page.evaluate(()=>localStorage.getItem('trailmate.config.readback.v1'));
 assert.ok(storedCheckpoint&&!storedCheckpoint.includes('RefreshLostReply')&&!storedCheckpoint.includes('replacement-sensitive'));
 const lostExpected=new TmsDocument(readbackData),nativeLines=lostExpected.entries().reverse().map(record=>`${record.key}=${record.type}:${record.type==='f32'?String(Math.fround(record.value)):record.raw}`);
 readbackData=new TextEncoder().encode('TMSET7\n'+nativeLines.join('\n')+'\nEND\n');assert.notEqual(Buffer.from(await webcrypto.subtle.digest('SHA-256',readbackData)).toString('hex'),JSON.parse(storedCheckpoint).sha256);
 assert.equal(JSON.parse(storedCheckpoint).version,2);
 await page.reload();await page.getByText(configReadbackText('zh-Hans').pending,{exact:true}).waitFor();
 await page.getByRole('button',{name:'连接设备',exact:true}).click();await page.getByText(configReadbackText('zh-Hans').matched,{exact:true}).waitFor();assert.equal(commitCalls,lostCommitCount);
 readbackData=new TmsDocument(readbackData);readbackData.set('device.node_name','ChangedAfterRefresh');readbackData=readbackData.serialize();
 await page.getByRole('button',{name:'重新读取',exact:true}).click();await page.getByText(configReadbackText('zh-Hans').mismatchSummary,{exact:true}).waitFor();
 for(const locale of locales){await page.locator('.management-language select').selectOption(locale.id);await page.getByText(configReadbackText(locale.id).mismatchSummary,{exact:true}).waitFor();assert.equal(commitCalls,lostCommitCount);}
 await page.locator('.management-language select').selectOption('zh-Hans');
 await page.evaluate(()=>{const original=Storage.prototype.setItem;Storage.prototype.setItem=function(key,value){if(key==='trailmate.config.readback.v1')throw new DOMException('Blocked','SecurityError');return original.call(this,key,value);};});
 await page.locator('[id="field-device.node_name"]').fill('StorageFallback');uploadId=undefined;
 await page.getByRole('button',{name:'保存到设备',exact:true}).click();await page.getByRole('dialog',{name:'保存到设备',exact:true}).getByRole('button',{name:'保存到设备',exact:true}).click();
 await page.getByText(/配置已保存，设备即将重启/).waitFor();await page.getByText(configReadbackText('zh-Hans').storageUnavailable,{exact:true}).waitFor();
 assert.equal(commitCalls,lostCommitCount+1);readbackData=Uint8Array.from(uploaded);
 await page.getByRole('button',{name:'重新读取',exact:true}).click();await page.getByText(configReadbackText('zh-Hans').matched,{exact:true}).waitFor();
 await page.getByText(configReadbackText('zh-Hans').storageUnavailable,{exact:true}).waitFor();
 badCapabilities=true;await page.getByRole('button',{name:'断开设备',exact:true}).click();await page.getByRole('button',{name:'连接设备',exact:true}).click();
 const failedConnection=configConnectionText('zh-Hans');await page.getByText(failedConnection.connectionFailed.replace('{diagnostic}',failedConnection.capabilitiesInvalid),{exact:true}).waitFor();
 assert.equal(await page.locator('.device-author').count(),0);const failureCommands=managementCommands;
 for(const locale of locales){const text=configConnectionText(locale.id);await page.locator('.management-language select').selectOption(locale.id);await page.getByText(text.connectionFailed.replace('{diagnostic}',text.capabilitiesInvalid),{exact:true}).waitFor();assert.equal(await page.locator('.device-author').count(),0);assert.equal(managementCommands,failureCommands);}
 }
 }
 assert.deepEqual(errors,[]);
 console.log(process.argv.includes('--release-docs')?'Current English device captures and cancelled navigation preserving unsaved settings passed; synthetic USB only.':process.argv.includes('--normal-flow')?'Device config normal flow passed: connect, edit, readable review, save, reconnect/readback; percent controls, satellite choices and 11 locales. Simulated serial, no hardware.':process.argv.includes('--presentation-only')?'Device config presentation: 11 locales, no companion BLE configuration, exact duration units, character bytes, custom motion IDs and presets passed.':'Device page verified config recovery and actual author proof verification, rejection, disconnect revocation and narrow-screen layout.');
}finally{await browser.close();server.close();}
