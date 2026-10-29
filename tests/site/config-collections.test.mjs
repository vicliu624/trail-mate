import test from 'node:test';
import assert from 'node:assert/strict';
import {TmsDocument} from '../../site/device/src/tms-document.js';
import {readConfigCollection,changeConfigCollection,configurationChanges,configCollectionIssues} from '../../site/device/src/config-collections.js';
const utf8=new TextEncoder();
function fixture(){
 const lines=['TMSET7','schema.version=u16:7','document.kind=enum:working','device.node_name=str:Preserved','extension.vendor=opaque:untouched',
 'rt.net.interface_count=u8:2','wifi.profile_count=u8:2','wifi.profile.0.ssid=str:Home','wifi.profile.0.password=str:secret',
 'wifi.profile.1.ssid=str:Camp','wifi.profile.1.password=str:','wifi.profile.1.vendor=opaque:future'];
 for(let i=0;i<6;i++){
  const values={id:['lora','auto'][i]??'',type:i===1?'u8:1':'u8:0',enabled:'bool:0',target_host:'str:',target_port:'u16:4242',group_id:'str:reticulum',discovery_port:'u16:29716',data_port:'u16:42671'};
  values.id='str:'+values.id;
  for(const [field,value] of Object.entries(values))lines.push(`rt.net.interface.${i}.${field}=${value}`);
 }
 lines.push('rt.net.interface.2.vendor=opaque:inactive-future','END','');return new TmsDocument(utf8.encode(lines.join('\n')));
}
test('Wi-Fi removal compacts rows, keeps extensions with their row and detects deleted fields',()=>{
 const before=fixture(),next=changeConfigCollection(before,'wifi','remove',0),collection=readConfigCollection(next,'wifi');
 assert.equal(collection.count,1);assert.equal(next.get('wifi.profile.0.ssid').value,'Camp');
 assert.equal(next.get('wifi.profile.0.vendor').raw,'future');assert.equal(next.get('extension.vendor').raw,'untouched');
 assert.equal(next.get('device.node_name').value,'Preserved');assert.throws(()=>next.get('wifi.profile.1.ssid'));
 const changes=configurationChanges(next,before);assert.ok(changes.has('wifi.profile.1.ssid'));assert.ok(changes.has('wifi.profile.0.vendor'));
 assert.equal(before.get('wifi.profile_count').value,2);
});
test('adding TCP retains six complete slots and preserves inactive extensions',()=>{
 const next=changeConfigCollection(fixture(),'interfaces','add',undefined,2),collection=readConfigCollection(next,'interfaces');
 assert.equal(collection.count,3);assert.equal(collection.rows.length,6);assert.equal(next.get('rt.net.interface.2.type').value,2);
 assert.equal(next.get('rt.net.interface.2.vendor').raw,'inactive-future');
 assert.ok(configCollectionIssues(next).some(issue=>issue.key==='rt.net.interface.2.target_host'));
 next.set('rt.net.interface.2.target_host','example.org');assert.deepEqual(configCollectionIssues(next),[]);
});
test('fixed-slot removal resets the new inactive slot and moving interfaces preserves IDs',()=>{
 const before=fixture(),moved=changeConfigCollection(before,'interfaces','down',0);
 assert.equal(moved.get('rt.net.interface.0.id').value,'auto');assert.equal(moved.get('rt.net.interface.1.id').value,'lora');
 const removed=changeConfigCollection(moved,'interfaces','remove',0);
 assert.equal(removed.get('rt.net.interface_count').value,1);assert.equal(removed.get('rt.net.interface.0.id').value,'lora');
 assert.equal(removed.get('rt.net.interface.1.id').value,'');assert.equal(readConfigCollection(removed,'interfaces').rows.length,6);
 assert.equal(removed.get('rt.net.interface.2.vendor').raw,'inactive-future');
});
test('IFAC adds a complete optional triple, enforces combinations and removes it without leaking credentials',()=>{
 let next=changeConfigCollection(fixture(),'interfaces','add',undefined,2);next.set('rt.net.interface.2.target_host','example.org');
 next=changeConfigCollection(next,'interfaces','access',2);assert.equal(next.get('rt.net.interface.2.ifac_size').value,0);
 next.set('rt.net.interface.2.network_name','Private');assert.ok(configCollectionIssues(next).some(issue=>issue.key.endsWith('.ifac_size')));
 next.set('rt.net.interface.2.ifac_size',64);assert.deepEqual(configCollectionIssues(next),[]);
 next.set('rt.net.interface.2.passphrase','secret');const cleared=changeConfigCollection(next,'interfaces','remove-access',2);
 assert.throws(()=>cleared.get('rt.net.interface.2.passphrase'));assert.equal(cleared.get('rt.net.interface.2.vendor').raw,'inactive-future');
 assert.deepEqual(configCollectionIssues(cleared),[]);assert.throws(()=>changeConfigCollection(fixture(),'interfaces','access',0));
});
test('collection capacities and type limits reject operations without mutating the source',()=>{
 let next=fixture();assert.throws(()=>changeConfigCollection(next,'interfaces','add',undefined,0));assert.throws(()=>changeConfigCollection(next,'interfaces','add',undefined,1));
 for(let i=0;i<3;i++)next=changeConfigCollection(next,'interfaces','add',undefined,2);
 assert.throws(()=>changeConfigCollection(next,'interfaces','add',undefined,2));
 let wifi=fixture();for(let i=2;i<10;i++)wifi=changeConfigCollection(wifi,'wifi','add');
 assert.equal(readConfigCollection(wifi,'wifi').count,10);assert.throws(()=>changeConfigCollection(wifi,'wifi','add'));
 assert.throws(()=>changeConfigCollection(fixture(),'wifi','remove',9));assert.throws(()=>changeConfigCollection(fixture(),'wifi','up',0));
});
test('new blank Wi-Fi rows block submission and duplicate identifiers are rejected',()=>{
 const next=changeConfigCollection(fixture(),'wifi','add');assert.ok(configCollectionIssues(next).some(issue=>issue.key==='wifi.profile.2.ssid'&&issue.code==='COLLECTION_SSID_REQUIRED'));
 next.set('wifi.profile.2.ssid','Home');assert.ok(configCollectionIssues(next).some(issue=>issue.message.includes('重复')&&issue.code==='COLLECTION_DUPLICATE'));
 next.set('wifi.profile.2.ssid','Trail');assert.deepEqual(configCollectionIssues(next),[]);
});

test('AutoInterface accepts complete access control and rejects discovery/data port conflicts',()=>{
 const next=changeConfigCollection(fixture(),'interfaces','access',1);
 next.set('rt.net.interface.1.network_name','Private LAN');next.set('rt.net.interface.1.ifac_size',128);
 assert.deepEqual(configCollectionIssues(next),[]);
 next.set('rt.net.interface.1.discovery_port',65535);
 assert(configCollectionIssues(next).some(issue=>issue.key==='rt.net.interface.1.discovery_port'&&issue.code==='CONFIG_RANGE'));
 next.set('rt.net.interface.1.discovery_port',29716);next.set('rt.net.interface.1.data_port',29717);
 assert(configCollectionIssues(next).some(issue=>issue.key==='rt.net.interface.1.data_port'));
 next.set('rt.net.interface.1.data_port',42671);assert.deepEqual(configCollectionIssues(next),[]);
});
