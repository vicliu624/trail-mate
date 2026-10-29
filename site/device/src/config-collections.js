import {TmsDocument} from './tms-document.js';
import {configurationField,validateConfigurationField} from './config-schema.js';
const utf8=new TextEncoder();
export const CONFIG_COLLECTIONS=Object.freeze({
 interfaces:{title:'Reticulum 接口',count:'rt.net.interface_count',prefix:'rt.net.interface.',capacity:6,fixed:true,
  fields:{id:'str',type:'u8',enabled:'bool',target_host:'str',target_port:'u16',group_id:'str',discovery_port:'u16',data_port:'u16'}},
 wifi:{title:'已保存 Wi-Fi',count:'wifi.profile_count',prefix:'wifi.profile.',capacity:10,fixed:false,fields:{ssid:'str',password:'str'}}
});
const access={network_name:'str',passphrase:'str',ifac_size:'u16'};
function specFor(name){const spec=CONFIG_COLLECTIONS[name];if(!spec)throw Error('未知配置集合');return spec;}
function typed(field,type,value){
 const doc=new TmsDocument(utf8.encode(`TMSET7\nschema.version=u16:7\ndocument.kind=enum:working\n${field}=${type}:${type==='str'?'':0}\nEND\n`));
 doc.set(field,value);return doc.get(field);
}
export function configurationChanges(candidate,baseline){
 const before=new Map(baseline?.entries().map(r=>[r.key,r])??[]),after=new Map(candidate.entries().map(r=>[r.key,r]));
 return new Set([...new Set([...before.keys(),...after.keys()])].filter(key=>{
  const a=before.get(key),b=after.get(key);if(!a||!b||a.type!==b.type)return true;
  if(a.value===undefined||b.value===undefined)return a.raw!==b.raw;
  if(a.value instanceof Uint8Array&&b.value instanceof Uint8Array)return a.value.length!==b.value.length||a.value.some((v,i)=>v!==b.value[i]);
  // TMSET7 f32 values persist as IEEE-754 single precision. Firmware emission
  // can have different decimal digits while representing the same stored bits.
  if(a.type==='f32')return Math.fround(a.value)!==Math.fround(b.value);
  return a.value!==b.value;
 }));
}
export function readConfigCollection(document,name){
 const spec=specFor(name),entries=document.entries(),count=entries.find(r=>r.key===spec.count);
 if(!count)return undefined;
 if(count.type!=='u8'||!Number.isInteger(count.value)||count.value<0||count.value>spec.capacity)throw Error(`${spec.title}数量无效`);
 const rows=Array.from({length:spec.fixed?spec.capacity:count.value},()=>[]);
 for(const record of entries){
  if(!record.key.startsWith(spec.prefix))continue;
  const tail=record.key.slice(spec.prefix.length),match=/^(0|[1-9][0-9]*)\.(.+)$/.exec(tail);
  if(!match||Number(match[1])>=rows.length)throw Error(`${spec.title}存在范围外字段，保留原文并先修复数量`);
  rows[Number(match[1])].push({...record,field:match[2]});
 }
 for(const row of rows){
  for(const [field,type] of Object.entries(spec.fields))if(!row.some(r=>r.field===field&&r.type===type))throw Error(`${spec.title}字段不完整或类型不匹配`);
  const extra=row.filter(r=>Object.hasOwn(access,r.field));
  if(name==='interfaces'&&extra.length&&(extra.length!==3||extra.some(r=>r.type!==access[r.field])))throw Error('接口访问控制字段不完整');
 }
 return {name,...spec,count:count.value,rows};
}
export function changeConfigCollection(document,name,action,index,type=2){
 const spec=specFor(name),collection=readConfigCollection(document,name);if(!collection)throw Error('设备未提供此集合');
 const rows=collection.rows.map(row=>row.map(r=>({...r})));let count=collection.count;
 const defaults=()=>name==='wifi'?{ssid:'',password:''}:{id:'',type:0,enabled:false,target_host:'',target_port:4242,group_id:'reticulum',discovery_port:29716,data_port:42671};
 const rowFrom=values=>Object.entries(spec.fields).map(([field,kind])=>({...typed('value',kind,values[field]),field}));
 if(action==='add'){
  if(count>=spec.capacity)throw Error(`${spec.title}已达到容量上限`);
  const values=defaults();
  if(name==='interfaces'){
   if(![0,1,2].includes(type))throw Error('接口类型无效');
   const limits=[1,1,3],used=rows.slice(0,count).filter(row=>row.find(r=>r.field==='type').value===type).length;
   if(used>=limits[type])throw Error('此接口类型已达到设备上限');
   const ids=new Set(rows.slice(0,count).map(row=>row.find(r=>r.field==='id').value));let n=1;
   while(ids.has(`web-${n}`))n++;values.id=`web-${n}`;values.type=type;
  }
  const next=rowFrom(values);
  if(spec.fixed){next.push(...rows[count].filter(r=>!Object.hasOwn(spec.fields,r.field)&&!Object.hasOwn(access,r.field)));rows[count]=next;}
  else rows.push(next);count++;
 }else{
  if(!Number.isInteger(index)||index<0||index>=count)throw Error('请选择有效集合条目');
  if(action==='remove'){rows.splice(index,1);if(spec.fixed)rows.splice(count-1,0,rowFrom(defaults()));count--;}
  else if(action==='up'||action==='down'){
   const other=index+(action==='up'?-1:1);if(other<0||other>=count)throw Error('条目已位于边界');
   [rows[index],rows[other]]=[rows[other],rows[index]];
  }else if(action==='access'||action==='remove-access'){
   if(name!=='interfaces'||(action==='access'&&![1,2].includes(rows[index].find(r=>r.field==='type').value)))throw Error('接口访问控制仅支持 TCP 或 AutoInterface');
   rows[index]=rows[index].filter(r=>!Object.hasOwn(access,r.field));
   if(action==='access')for(const [field,kind] of Object.entries(access))rows[index].push({...typed('value',kind,kind==='str'?'':0),field});
  }else throw Error('未知集合操作');
 }
 const lines=['TMSET7'];
 for(const record of document.entries()){
  if(record.key.startsWith(spec.prefix))continue;
  lines.push(record.key===spec.count?`${spec.count}=u8:${count}`:`${record.key}=${record.type}:${record.raw}`);
 }
 rows.forEach((row,i)=>row.forEach(r=>lines.push(`${spec.prefix}${i}.${r.field}=${r.type}:${r.raw}`)));
 lines.push('END','');return new TmsDocument(utf8.encode(lines.join('\n')));
}
export function configCollectionIssues(document){
 const issues=[],add=(key,message,code='COLLECTION_INVALID',params={})=>{if(!issues.some(issue=>issue.key===key))issues.push({key,message,code,params});};
 for(const name of Object.keys(CONFIG_COLLECTIONS)){
  let collection;try{collection=readConfigCollection(document,name);}catch(error){add(CONFIG_COLLECTIONS[name].count,error.message);continue;}
  if(!collection)continue;
  collection.rows.forEach((row,index)=>{
   for(const record of row){
    const max=configurationField(record).maxBytes;
    if(max&&typeof record.value==='string'&&utf8.encode(record.value).length>max)add(record.key,`UTF-8 内容不能超过 ${max} 字节`,'CONFIG_UTF8_LIMIT',{max});
    if(name==='interfaces'&&record.field==='type'&&![0,1,2].includes(record.value))add(record.key,'不支持的接口类型','COLLECTION_TYPE_UNSUPPORTED');
    if(name==='interfaces'&&index>=collection.count&&Object.hasOwn(access,record.field))add(record.key,'未启用接口槽不能包含访问控制字段','COLLECTION_INACTIVE_ACCESS');
   }
  });
  const unique=new Set(),types=[0,0,0];
  collection.rows.slice(0,collection.count).forEach((row,index)=>{
   const value=field=>row.find(r=>r.field===field)?.value,key=field=>`${collection.prefix}${index}.${field}`;
   for(const record of row){
    if(record.value===undefined)continue;
    if(name==='interfaces'&&((value('type')!==2&&['target_host','target_port'].includes(record.field))||
       (value('type')!==1&&['group_id','discovery_port','data_port'].includes(record.field))))continue;
    try{validateConfigurationField(record,record.value);}catch(error){add(record.key,error.message,error.code,error.params);}
   }
   const identity=value(name==='wifi'?'ssid':'id'),identityField=name==='wifi'?'ssid':'id';
   if(!identity)add(key(identityField),name==='wifi'?'请输入网络名称（SSID）':'请输入接口稳定标识',name==='wifi'?'COLLECTION_SSID_REQUIRED':'COLLECTION_ID_REQUIRED');
   else if(unique.has(identity))add(key(identityField),'同一集合内不能重复','COLLECTION_DUPLICATE');else unique.add(identity);
   if(name==='interfaces'){
    const type=value('type');if(![0,1,2].includes(type))add(key('type'),'不支持的接口类型','COLLECTION_TYPE_UNSUPPORTED');
    else if(++types[type]>[1,1,3][type])add(key('type'),'此接口类型超过设备上限','COLLECTION_TYPE_LIMIT');
    if(type===2&&!value('target_host'))add(key('target_host'),'TCP 接口需要主机名或 IP 地址','COLLECTION_TCP_HOST_REQUIRED');
    if(type===1&&!value('group_id'))add(key('group_id'),'AutoInterface 需要接口组','COLLECTION_AUTO_GROUP_REQUIRED');
    const enabled=Boolean(value('network_name')||value('passphrase')),size=value('ifac_size')??0;
    if(enabled&&(![1,2].includes(type)||size<8||size>512||size%8))add(key('ifac_size'),'访问控制仅支持 TCP 或 AutoInterface；IFAC 长度须为 8–512 位且为 8 的倍数','COLLECTION_IFAC_INVALID');
    if(type===1&&value('discovery_port')===65535)add(key('discovery_port'),'发现端口必须为相邻控制端口保留空间','CONFIG_RANGE');
    if(type===1&&[value('discovery_port'),value('discovery_port')+1].includes(value('data_port')))add(key('data_port'),'数据端口不能与发现或控制端口冲突','CONFIG_RANGE');
    if(!enabled&&size!==0)add(key('ifac_size'),'未设置网络名称或口令时 IFAC 长度须为 0','COLLECTION_IFAC_ZERO');
   }
  });
 }
 return issues;
}

// Restore a complete collection only when the device advertises its count and
// shape. Device extensions stay attached to their stable interface ID / SSID.
export function restoreConfigCollection(current,backup,name){
 const spec=specFor(name),target=readConfigCollection(current,name),source=readConfigCollection(backup,name);
 if(!target||!source)throw Error('设备或备份未提供完整集合');
 const identity=name==='wifi'?'ssid':'id',known={...spec.fields,...(name==='interfaces'?access:{})},skipped=[];
 const rows=source.rows.map((row,index)=>{
  const id=row.find(r=>r.field===identity)?.value;
  const original=index<source.count?target.rows.slice(0,target.count).find(r=>r.find(v=>v.field===identity)?.value===id):
    index>=target.count?target.rows[index]:undefined;
  const extensions=new Map((original??[]).filter(r=>!Object.hasOwn(known,r.field)).map(r=>[r.field,r]));
  const restored=row.filter(r=>Object.hasOwn(known,r.field));
  for(const record of row.filter(r=>!Object.hasOwn(known,r.field))){
   const previous=extensions.get(record.field);
   if(!previous){skipped.push({key:record.key,reason:'此条目在设备上未提供该扩展字段'});continue;}
   if(previous.type!==record.type)throw Object.assign(Error('集合扩展字段类型与设备不一致'),{field:record.key});
   if(previous.value===undefined||record.value===undefined){skipped.push({key:record.key,reason:'不支持编辑的扩展字段保留设备值'});continue;}
   validateConfigurationField(record,record.value);extensions.set(record.field,record);
  }
  return [...restored,...extensions.values()];
 });
 const lines=['TMSET7'];
 for(const record of current.entries()){
  if(record.key.startsWith(spec.prefix))continue;
  lines.push(record.key===spec.count?`${spec.count}=u8:${source.count}`:`${record.key}=${record.type}:${record.raw}`);
 }
 rows.forEach((row,index)=>row.forEach(record=>lines.push(`${spec.prefix}${index}.${record.field}=${record.type}:${record.raw}`)));
 lines.push('END','');const document=new TmsDocument(utf8.encode(lines.join('\n')));
 const issues=configCollectionIssues(document).filter(issue=>issue.key===spec.count||issue.key.startsWith(spec.prefix));
 if(issues.length)throw Object.assign(Error(issues.map(issue=>`${issue.key}：${issue.message}`).join('；')),{field:issues[0].key});
 return {document,skipped};
}
