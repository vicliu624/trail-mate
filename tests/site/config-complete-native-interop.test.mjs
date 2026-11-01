import test from 'node:test';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import path from 'node:path';
import {TmsDocument} from '../../site/device/src/tms-document.js';
import {configurationField,validateConfigurationField} from '../../site/device/src/config-schema.js';
import {setConfigurationValue} from '../../site/device/src/config-editor-values.js';
import {setConfigurationRadioInput} from '../../site/device/src/config-radio-input.js';
import {MESHCORE_REGION_PRESETS,MESHCORE_PRESET_FIELDS} from '../../site/device/src/meshcore-region-presets.js';
import {configurationChanges,changeConfigCollection} from '../../site/device/src/config-collections.js';
import {readMeshtasticModule,replaceMeshtasticModule} from '../../site/device/src/meshtastic-module-document.js';
import {readStructuredModule,updateStructuredModuleField,editStructuredModuleRow,setStructuredModulePresence} from '../../site/device/src/meshtastic-module-model.js';
import {MODULE_SCHEMA} from '../../site/device/src/meshtastic-module-schema.js';
import {patchSingularProtobufField,protobufVarintField} from '../../site/device/src/module-protobuf-wire.js';
import {moduleNormalizationRule} from '../../site/device/src/meshtastic-module-normalization.js';
import {parseDurationInput} from '../../site/device/src/config-duration.js';
import {moduleDuration,validateModuleDuration} from '../../site/device/src/config-module-duration.js';
import {isWebConfigurationField} from '../../site/device/src/config-web-scope.js';
import {prepareConfigImport} from '../../site/device/src/config-import.js';
import {prepareConfigurationSave} from '../../site/device/src/config-save-preparation.js';
import {configurationContentHash} from '../../site/device/src/config-content-hash.js';
import {parseFrequencyInput} from '../../site/device/src/config-frequency.js';
import {parseConfigurationChannelId,configurationChannelIdInput} from '../../site/device/src/config-channel-id.js';

const executable=path.resolve('.codex-build/hostlink-codec/native/tms_complete_interop_cli'+(process.platform==='win32'?'.exe':''));
function run(args,input){const r=spawnSync(executable,args,{input,maxBuffer:32*1024*1024,windowsHide:true});if(r.error)throw r.error;assert.equal(r.status,0,String(r.stderr));return r.stdout;}
const baseline=new Uint8Array(run(['--emit']));
function fresh(){return new TmsDocument(baseline);}
function batch(documents){
 const frames=documents.map(document=>{const bytes=document instanceof TmsDocument?document.serialize():document;const header=Buffer.alloc(4);header.writeUInt32LE(bytes.length);return Buffer.concat([header,bytes]);});
 const output=run(['--batch'],Buffer.concat(frames)),results=[];
 for(let offset=0;offset<output.length;){assert.ok(offset+5<=output.length);const status=output[offset],size=output.readUInt32LE(offset+1);offset+=5;assert.ok(offset+size<=output.length);results.push({status,bytes:new Uint8Array(output.subarray(offset,offset+size))});offset+=size;}
 assert.equal(results.length,documents.length);return results;
}
test('radio tuning choices preserve custom float and unsigned values through actual native storage',()=>{
 const cases=[];for(const key of ['mt.bandwidth_khz','mc.base.bandwidth_khz','mc.bandwidth_khz','rt.bandwidth_khz'])for(const value of [7.8,125,250,500,600]){const document=fresh();setConfigurationValue(document,key,value);cases.push({key,value:Math.fround(value),document});}
 for(const [key,values] of [['mc.rx_delay_base',[-1,0,1,2.5,20.5]],['mc.airtime_factor',[-1,0,1,2.5,9.5]],['mc.flood_max',[0,1,16,64,255]]])for(const value of values){const document=fresh();setConfigurationValue(document,key,value);cases.push({key,value,document});}
 const replies=batch(cases.map(row=>row.document));for(let i=0;i<cases.length;i++){assert.equal(replies[i].status,0,cases[i].key+' '+cases[i].value);const restored=new TmsDocument(replies[i].bytes),actual=restored.get(cases[i].key);assert.equal(actual.type==='f32'?Math.fround(actual.value):actual.value,cases[i].value);assert.deepEqual([...configurationChanges(restored,new TmsDocument(cases[i].document.serialize()))],[]);}
});
test('channel and MQTT precision keep protocol values and raw u32 compatibility values through native storage',()=>{
 const path=['mqtt','map_report_settings','position_precision'],cases=[];
 for(const value of [0,1,13,22,32,33,4294967295]){
  for(const key of ['channel.primary_position_precision','channel.secondary_position_precision']){const document=fresh();setConfigurationValue(document,key,value);cases.push({document,key,value});}
  const document=updateStructuredModuleField(fresh(),path,value);cases.push({document,path,value});
 }
 const results=batch(cases.map(row=>row.document));for(let i=0;i<cases.length;i++){const row=cases[i];assert.equal(results[i].status,0,JSON.stringify([row.key??row.path,row.value]));const restored=new TmsDocument(results[i].bytes);if(row.key){assert.equal(restored.get(row.key).value,row.value);assert.deepEqual([...configurationChanges(restored,row.document)],[]);}else{const read=document=>row.path.reduce((model,key)=>model.fields.find(field=>field.name===key).value,readStructuredModule(readMeshtasticModule(document)));assert.equal(read(restored),row.value);}for(const key of ['channel.primary_has_module','channel.secondary_has_module'])assert.equal(restored.get(key).value,row.document.get(key).value);}
});
test('all module duration units preserve exact generated integer values through native save/readback',()=>{
 const fields=[];function walk(message,path=[]){for(const field of MODULE_SCHEMA.messages[message].fields){const next=[...path,field.name];if(field.type==='MESSAGE')walk(field.message,next);else{const spec=moduleDuration(next,field);if(spec){const unit=spec.units.find(unit=>unit.factor>1),value=parseDurationInput('1.5',unit.factor);validateModuleDuration(field,value);fields.push({path:next,value});}}}}walk(MODULE_SCHEMA.root);assert.equal(fields.length,15);
 const candidates=fields.map(({path,value})=>updateStructuredModuleField(fresh(),path,value)),results=batch(candidates);
 for(let i=0;i<fields.length;i++){assert.equal(results[i].status,0,fields[i].path.join('.'));const module=readStructuredModule(readMeshtasticModule(new TmsDocument(results[i].bytes)));const value=fields[i].path.reduce((current,key)=>current.fields.find(field=>field.name===key).value,module);assert.equal(value,fields[i].value,fields[i].path.join('.'));}
});
test('backup module restore preserves excluded BLE compatibility data through actual native save/readback',()=>{
 let current=updateStructuredModuleField(fresh(),['paxcounter','ble_threshold'],-80);current=updateStructuredModuleField(current,['paxcounter','wifi_threshold'],-70);
 const backup=new TmsDocument(updateStructuredModuleField(updateStructuredModuleField(current,['paxcounter','ble_threshold'],-50),['paxcounter','wifi_threshold'],-60).serialize({kind:'backup'}));
 const imported=prepareConfigImport(current,backup);assert.deepEqual(imported.errors,[]);const [reply]=batch([imported.document]);assert.equal(reply.status,0);const restored=readStructuredModule(readMeshtasticModule(new TmsDocument(reply.bytes))).fields.find(field=>field.name==='paxcounter').value;
 assert.equal(restored.fields.find(field=>field.name==='ble_threshold').value,-80);assert.equal(restored.fields.find(field=>field.name==='wifi_threshold').value,-60);
});
test('permitted backup edits retain excluded companion BLE fields through the actual native save/readback',()=>{
 const current=fresh(),backup=new TmsDocument(current.serialize({kind:'backup'})),excluded=current.entries().filter(row=>!isWebConfigurationField(row.key));assert.ok(excluded.length>=9);
 for(const row of excluded)backup.set(row.key,row.type==='bool'?!row.value:row.value===0?1:0);backup.set('device.node_name','Web scope test');
 const imported=prepareConfigImport(current,backup);assert.deepEqual(imported.errors,[]);assert.deepEqual(imported.changed.map(row=>row.key),['device.node_name']);
 const [reply]=batch([imported.document]);assert.equal(reply.status,0);const restored=new TmsDocument(reply.bytes);assert.equal(restored.get('device.node_name').value,'Web scope test');for(const row of excluded)assert.deepEqual(restored.get(row.key).value,row.value,row.key);assert.deepEqual([...configurationChanges(restored,imported.document)],[]);
});
test('duration unit input preserves exact stored integers through native save/readback and rejects invalid sync intervals',()=>{
 const inputs=[['gps.interval_ms','1.5',60000,90000],['gps.interval_ms','1.234',1000,1234],['gps.interval_ms','4294967.295',1000,4294967295],['gps.init_probe_ms','0.9',1000,900],['gps.motion_idle_ms','5.5',60000,330000],['rt.net.propagation.sync_interval_s','1',60,60],['rt.net.propagation.sync_interval_s','24',3600,86400],['aprs.tx_min_interval_s','1.5',60,90],['aprs.dedupe_window_s','0.25',60,15],['aprs.position_interval_s','18',3600,64800]];
 const documents=inputs.map(([key,raw,factor,value])=>{const document=fresh(),parsed=parseDurationInput(raw,factor);assert.equal(parsed,value);setConfigurationValue(document,key,parsed);return document;}),results=batch(documents);
 for(let i=0;i<inputs.length;i++){assert.equal(results[i].status,0);const restored=new TmsDocument(results[i].bytes);assert.equal(restored.get(inputs[i][0]).value,inputs[i][3]);assert.deepEqual([...configurationChanges(restored,documents[i])],[]);}
 const invalid=[0,59,86401].map(value=>{const document=fresh();document.set('rt.net.propagation.sync_interval_s',value);return document;});for(const result of batch(invalid))assert.notEqual(result.status,0);
});
function alternate(record){
 const spec=configurationField(record),value=record.value;
 if(spec.readOnly||record.key.startsWith('mt_module.'))return null;
 if(record.key==='mt_ble.fixed_pin'||record.key==='mc_ble.pin')return 123456;
 if(record.key==='rt.net.propagation.sync_interval_s')return 120;
 if(spec.options)return spec.options.find(option=>option.value!==value)?.value??null;
 if(record.type==='bool')return !value;
 if(record.type==='str')return /(?:host|ssid|\.id)$/.test(record.key)?'fixture-other.invalid':'测试 %&/?';
 if(record.type==='b64'){
  if(['ui.ime.enabled','ui.timezone.tzdef','chat.auto_reply.text'].includes(record.key))return new TextEncoder().encode(record.key==='ui.timezone.tzdef'?'UTC0':'测试消息');
  return Uint8Array.from({length:value.length||16},(_,i)=>(i*13+71)%256);
 }
 if(record.type==='f32')return Math.fround(value===1.25?2.5:1.25);
 if(spec.range)return [1,0,2,spec.range[0],spec.range[0]+1].find(candidate=>candidate!==value&&candidate>=spec.range[0]&&candidate<=spec.range[1]);
 throw Error('Missing alternate: '+record.key);
}

test('motion event IDs and APRS character bytes survive the actual native save/readback',()=>{
 const cases=[];for(const key of ['gps.motion_sensor','aprs.symbol_table','aprs.symbol_code'])for(const value of [0,47,55,77,91,92,98,143,255]){const document=fresh();setConfigurationValue(document,key,value);cases.push({key,value,document});}const results=batch(cases.map(row=>row.document));for(let i=0;i<cases.length;i++){assert.equal(results[i].status,0);const restored=new TmsDocument(results[i].bytes);assert.equal(restored.get(cases[i].key).value,cases[i].value);assert.deepEqual([...configurationChanges(restored,cases[i].document)],[]);}
});

test('all MeshCore region presets survive production validation, persistence and readback',()=>{
 const cases=MESHCORE_REGION_PRESETS.map(preset=>{const document=fresh();setConfigurationRadioInput(document,'mc.region_preset',preset.id);return {preset,document};});
 const replies=batch(cases.map(row=>row.document));for(let i=0;i<cases.length;i++){assert.equal(replies[i].status,0);const restored=new TmsDocument(replies[i].bytes),{preset,document}=cases[i];assert.equal(restored.get('mc.region_preset').value,preset.id);for(const [key,name] of Object.entries(MESHCORE_PRESET_FIELDS)){const actual=restored.get(key);assert.equal(actual.type==='f32'?Math.fround(actual.value):actual.value,actual.type==='f32'?Math.fround(preset[name]):preset[name]);}for(const row of document.entries().filter(row=>row.key!=='mc.region_preset'&&!Object.hasOwn(MESHCORE_PRESET_FIELDS,row.key)))assert.deepEqual(restored.get(row.key).value,row.value,row.key);}
 for(let i=0;i<cases.length;i++)assert.deepEqual([...configurationChanges(new TmsDocument(replies[i].bytes),cases[i].document)],[]);
});
function coupled(document,key,value){
 if(key==='rt.net.propagation.automatic_node'&&!value)document.set('rt.net.propagation.node_hash',new Uint8Array(16).fill(71));
 if(key==='rt.net.propagation.node_hash')document.set('rt.net.propagation.automatic_node',false);
 const type=/^rt\.net\.interface\.(\d+)\.type$/.exec(key);
 if(type&&Number(type[1])<document.get('rt.net.interface_count').value){
  const previous=document.get(key).value;
  const other=document.entries().find(r=>/^rt\.net\.interface\.\d+\.type$/.test(r.key)&&r.key!==key&&Number(r.key.split('.')[3])<document.get('rt.net.interface_count').value&&r.value===value);
  if(other){setConfigurationValue(document,other.key,previous);const host=other.key.replace(/type$/,'target_host');if(previous===2&&!document.get(host).value)setConfigurationValue(document,host,'fixture-swapped.invalid');}
  const host=key.replace(/type$/,'target_host');if(value===2&&!document.get(host).value)setConfigurationValue(document,host,'fixture-swapped.invalid');
 }
 setConfigurationValue(document,key,value);
}

test('all ordinary fields survive complete production config validation, application, NVS reads and re-emission',()=>{
 const cases=[];
 for(const record of fresh().entries()){
  const value=alternate(record);if(value===null)continue;
  validateConfigurationField(record,value);const document=fresh();coupled(document,record.key,value);cases.push({key:record.key,value,document});
 }
 assert.ok(cases.length>290);
 const replies=batch(cases.map(x=>x.document)),failures=[];
 for(let i=0;i<cases.length;i++){
  const item=cases[i],reply=replies[i];if(reply.status!==0){failures.push(item.key+': native rejection '+reply.status);continue;}
  const restored=new TmsDocument(reply.bytes);try{assert.deepEqual(restored.get(item.key).value,item.value);assert.deepEqual([...configurationChanges(restored,item.document)],[]);}catch{failures.push(item.key+': changed during save/readback '+[...configurationChanges(restored,item.document)].join(','));}
 }
 assert.deepEqual(failures,[]);
 console.log(`Complete native configuration: ${fresh().size} baseline records; ${cases.length} scalar fields through production Settings/BLE/Wi-Fi/storage. This includes retained compatibility fields outside the web UI. NVS and runtime are desktop fixtures.`);
});

function cleanModel(model){return {message:model.message,unknown:model.unknown,fields:model.fields.map(field=>({name:field.name,value:field.type==='MESSAGE'?(Array.isArray(field.value)?field.value.map(cleanModel):field.value?cleanModel(field.value):undefined):field.value}))};}
function moduleLeaves(message=MODULE_SCHEMA.root,prefix=[]){return MODULE_SCHEMA.messages[message].fields.flatMap(field=>field.readOnly||field.cardinality==='REPEATED'?[]:field.type==='MESSAGE'?moduleLeaves(field.message,[...prefix,field.name]):[{path:[...prefix,field.name],field}]);}
function moduleValue(model,names){for(const name of names)model=model?.fields.find(field=>field.name===name)?.value;return model;}
test('every structured singular module field survives actual nanopb decode, Preferences storage and encode',()=>{
 const source=readStructuredModule(readMeshtasticModule(fresh())),cases=[];
 for(const {path,field} of moduleLeaves()){
  const before=moduleValue(source,path);let value;
  if(field.type==='BOOL')value=!before;
  else if(field.type==='STRING')value='测试值';
  else if(field.type==='UENUM')value=field.options.find(option=>option.value!==before)?.value??before;
  else value=field.type==='INT32'?-80:before===1?2:1;
  const document=updateStructuredModuleField(fresh(),path,value);cases.push({path,value,document});
 }
 const replies=batch(cases.map(x=>x.document)),failures=[];
 for(let i=0;i<cases.length;i++){
  const item=cases[i],reply=replies[i];if(reply.status!==0){failures.push(item.path.join('.')+': native rejection '+reply.status);continue;}
  const restored=new TmsDocument(reply.bytes),actual=readStructuredModule(readMeshtasticModule(restored));
  try{assert.deepEqual(moduleValue(actual,item.path),item.value);assert.deepEqual(cleanModel(actual),cleanModel(readStructuredModule(readMeshtasticModule(item.document))));assert.deepEqual([...configurationChanges(restored,item.document)].filter(key=>!key.startsWith('mt_module.')),[]);}catch{failures.push(item.path.join('.')+': module or other configuration changed');}
 }
 assert.deepEqual(failures,[]);console.log(`Structured module save/readback: ${cases.length} singular fields.`);
});

test('dynamic Wi-Fi, Reticulum IFAC and repeated module rows survive complete save/readback',()=>{
 const cases=[];
 let wifi=fresh();for(let i=2;i<10;i++){wifi=changeConfigCollection(wifi,'wifi','add');setConfigurationValue(wifi,`wifi.profile.${i}.ssid`,'fixture-wifi-'+i);setConfigurationValue(wifi,`wifi.profile.${i}.password`,'fixture-password-'+i);}cases.push(wifi);
 for(let i=9;i>=0;i--)wifi=changeConfigCollection(wifi,'wifi','remove',i);cases.push(wifi);
 let access=changeConfigCollection(fresh(),'interfaces','access',2);setConfigurationValue(access,'rt.net.interface.2.network_name','fixture-network');setConfigurationValue(access,'rt.net.interface.2.passphrase','fixture-passphrase');setConfigurationValue(access,'rt.net.interface.2.ifac_size',512);cases.push(access);
 access=changeConfigCollection(access,'interfaces','remove-access',2);cases.push(access);
 let rows=fresh();for(let i=0;i<4;i++)rows=editStructuredModuleRow(rows,['remote_hardware','available_pins'],i,{action:'insert',values:{gpio_pin:i+10,name:'Pin '+i,type:2}});cases.push(rows);
 rows=editStructuredModuleRow(rows,['remote_hardware','available_pins'],1,{action:'update',values:{name:'Updated'}});cases.push(rows);
 rows=editStructuredModuleRow(rows,['remote_hardware','available_pins'],0,{action:'remove'});cases.push(rows);
 const replies=batch(cases);replies.forEach((reply,i)=>{assert.equal(reply.status,0,`case ${i}`);const restored=new TmsDocument(reply.bytes);assert.deepEqual([...configurationChanges(restored,cases[i])].filter(key=>!key.startsWith('mt_module.')),[]);assert.deepEqual(cleanModel(readStructuredModule(readMeshtasticModule(restored))),cleanModel(readStructuredModule(readMeshtasticModule(cases[i]))));});
});

function optionalPaths(message=MODULE_SCHEMA.root,prefix=[]){return MODULE_SCHEMA.messages[message].fields.flatMap(field=>field.type==='MESSAGE'&&field.cardinality==='OPTIONAL'?[[...prefix,field.name],...optionalPaths(field.message,[...prefix,field.name])]:[]);}
test('optional module presence follows actual firmware normalization and browser explanations',()=>{
 const cases=[];
 for(const names of optionalPaths()){
  const added=setStructuredModulePresence(fresh(),names,true),removed=setStructuredModulePresence(added,names,false);
  // repairLegacyMqttConfig recreates the missing section and blank strings;
  // it retains decoded scalar zeros rather than copying the factory section.
  let expected=moduleNormalizationRule(names)==='restoredPresence'?added:removed;
  if(names.length===1&&names[0]==='mqtt')expected=updateStructuredModuleField(added,['mqtt','encryption_enabled'],false);
  cases.push({document:added,expected:added},{document:removed,expected});
 }
 for(const name of ['address','username','password','root']){
  assert.equal(moduleNormalizationRule(['mqtt',name]),'blankMqttValue');
  cases.push({document:updateStructuredModuleField(fresh(),['mqtt',name],''),expected:fresh()});
 }
 const replies=batch(cases.map(item=>item.document));replies.forEach((reply,i)=>{assert.equal(reply.status,0,`presence case ${i}`);const restored=new TmsDocument(reply.bytes);assert.deepEqual(cleanModel(readStructuredModule(readMeshtasticModule(restored))),cleanModel(readStructuredModule(readMeshtasticModule(cases[i].expected))));assert.deepEqual([...configurationChanges(restored,cases[i].document)].filter(key=>!key.startsWith('mt_module.')),[]);});
});

test('complete firmware rejects missing platform blocks, wrong types, invalid BLE and module data in validation pass',()=>{
 const source=new TextDecoder().decode(baseline),inputs=[];
 for(const key of ['ui.screen.timeout_ms','chat.auto_reply.text','wifi.profile_count','rt.net.version','mt_ble.mode','mc_ble.pin','mt_module.chunk.0'])inputs.push(new TextEncoder().encode(source.split('\n').filter(line=>!line.startsWith(key+'=')).join('\n')));
 for(const prefix of ['rt.net.','mt_ble.','mc_ble.','mt_module.'])inputs.push(new TextEncoder().encode(source.split('\n').filter(line=>!line.startsWith(prefix)).join('\n')));
 for(const [key,value] of [['mt_ble.fixed_pin',1000000],['mc_ble.pin',1],['mt_module.size',0],['mt_module.chunk_count',2],['rt.net.interface.2.target_port',0]]){const document=fresh();document.set(key,value);inputs.push(document.serialize());}
 inputs.push(new TextEncoder().encode(source.replace('ui.haptics.vibration_enabled=bool:','ui.haptics.vibration_enabled=u8:')));
 inputs.push(replaceMeshtasticModule(fresh(),patchSingularProtobufField(readMeshtasticModule(fresh()),[8],protobufVarintField(8,2))).serialize());
 for(const reply of batch(inputs)){assert.equal(reply.status,1);assert.equal(reply.bytes.length,0);}
});

test('disabled IFAC normalization matches the actual native writer and preserves configured access',async()=>{
 const empty=changeConfigCollection(fresh(),'interfaces','access',2),normalized=prepareConfigurationSave(empty),inputs=[empty,normalized.document];
 const prefix='rt.net.interface.2.';
 for(const [name,passphrase,bits] of [['Private','',8],['','secret',24],['Private','secret',64],['Private','secret',512]]){const document=changeConfigCollection(fresh(),'interfaces','access',2);document.set(prefix+'network_name',name);document.set(prefix+'passphrase',passphrase);document.set(prefix+'ifac_size',bits);inputs.push(prepareConfigurationSave(document).document);}
 const results=batch(inputs);assert.equal(normalized.omitted.length,3);assert.ok(configurationChanges(empty,normalized.document).size===3);
 for(let index=0;index<results.length;index++){assert.equal(results[index].status,0);const actual=new TmsDocument(results[index].bytes),expected=index===0?normalized.document:inputs[index];assert.deepEqual([...configurationChanges(actual,expected)],[]);if(index<2)for(const field of ['network_name','passphrase','ifac_size'])assert.throws(()=>actual.get(prefix+field));else for(const field of ['network_name','passphrase','ifac_size'])assert.deepEqual(actual.get(prefix+field).value,expected.get(prefix+field).value);}
 const hash=await configurationContentHash(normalized.document);assert.equal(await configurationContentHash(new TmsDocument(results[0].bytes)),hash);assert.equal(await configurationContentHash(new TmsDocument(results[1].bytes)),hash);
});

test('frequency units and signed power custom values retain native storage through actual complete save/readback',()=>{
 const cases=[];
 for(const key of ['mt.frequency_offset_mhz','mt.override_frequency_mhz','mc.base.frequency_offset_mhz','mc.base.override_frequency_mhz','mc.frequency_mhz','rt.frequency_offset_mhz','rt.override_frequency_mhz'])for(const [raw,unit] of [['12.5','kHz'],['-12.5','kHz'],['869525000','Hz'],['0','MHz'],['1.234567e-6','MHz']]){const document=fresh(),value=parseFrequencyInput(raw,unit);setConfigurationRadioInput(document,key,value);cases.push({document,key,value});}
 for(const key of ['mt.tx_power','mc.base.tx_power','rt.tx_power'])for(const value of [-128,-9,0,1,22,30,127]){const document=fresh();setConfigurationRadioInput(document,key,value);cases.push({document,key,value});}
 for(const value of [0,1,8,65535]){const document=fresh();setConfigurationRadioInput(document,'mt.channel_num',value);cases.push({document,key:'mt.channel_num',value});}
 const results=batch(cases.map(row=>row.document));
 for(let index=0;index<cases.length;index++){const {document,key,value}=cases[index];assert.equal(results[index].status,0,key);const actual=new TmsDocument(results[index].bytes);assert.equal(actual.get(key).type,document.get(key).type);assert.equal(document.get(key).type==='f32'?Math.fround(actual.get(key).value):actual.get(key).value,document.get(key).type==='f32'?Math.fround(value):value,key);assert.deepEqual([...configurationChanges(actual,document)],[]);}
 assert.equal(cases.length,60);
});

test('numeric purpose controls retain capacities, message counts, suffix and all interface ports in native storage',()=>{
 const cases=[],ports=['mt.mqtt.port','mc.mqtt.port','rt.wifi_port',...Array.from({length:6},(_,index)=>['target_port','discovery_port','data_port'].map(field=>`rt.net.interface.${index}.${field}`)).flat()];
 for(const [keys,values] of [[ports,[1,4242,65535]],[['power.gauge.design_mah','power.gauge.full_mah'],[1,1500,10000]],[['rt.net.propagation.max_messages_per_sync'],[1,32,64]],[['aprs.igate_ssid'],[0,15,255]]])for(const key of keys)for(const candidate of values){const value=key.endsWith('.discovery_port')&&candidate===65535?65534:candidate;const document=fresh();validateConfigurationField(document.get(key),value);setConfigurationRadioInput(document,key,value);cases.push({document,key,value});}
 const replies=batch(cases.map(row=>row.document));for(let index=0;index<cases.length;index++){const {document,key,value}=cases[index];assert.equal(replies[index].status,0,key);const actual=new TmsDocument(replies[index].bytes);assert.equal(actual.get(key).value,value,key);assert.equal(actual.get(key).type,document.get(key).type);assert.deepEqual([...configurationChanges(actual,document)],[]);}assert.equal(cases.length,75);
 for(const value of [0,65535]){const document=fresh();document.set('rt.net.interface.1.discovery_port',value);assert.notEqual(batch([document])[0].status,0,'AutoInterface must reject discovery port '+value);}
});

test('remaining routing and unsigned hexadecimal identifier controls survive native save/readback',()=>{
 const cases=[];for(const prefix of ['mt','mc.base','rt'])for(const channel of ['primary','secondary'])for(const value of [0,1,0x80000000,0xffffffff])for(const base of ['decimal','hex']){const key=prefix+'.'+channel+'_channel_id',document=fresh();setConfigurationRadioInput(document,key,parseConfigurationChannelId(configurationChannelIdInput(value,base),base));cases.push({document,key,value});}
 for(const [keys,values] of [[['policy.hop_limit','policy.max_retries','policy.max_channels','mt.hop_limit','mc.base.hop_limit','rt.hop_limit'],[0,7,255]],[['mc.base.channel_num','rt.channel_num'],[0,1,65535]],[['ui.timezone.offset_minutes'],[-720,-1,0,345,840]]])for(const key of keys)for(const value of values){const document=fresh();validateConfigurationField(document.get(key),value);setConfigurationRadioInput(document,key,value);cases.push({document,key,value});}
 const replies=batch(cases.map(row=>row.document));for(let index=0;index<cases.length;index++){const {document,key,value}=cases[index];assert.equal(replies[index].status,0,key);const actual=new TmsDocument(replies[index].bytes);assert.equal(actual.get(key).value,value,key);assert.equal(actual.get(key).type,document.get(key).type);assert.deepEqual([...configurationChanges(actual,document)],[]);}assert.equal(cases.length,77);
});
