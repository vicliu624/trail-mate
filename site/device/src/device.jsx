import React,{useState,useRef,useEffect} from 'react';
import {createRoot} from 'react-dom/client';
import {Background,Button,Card,Title,Input} from 'animal-island-ui';
import 'animal-island-ui/style';
import '../../styles.css';
import './device.css';
import {SiteHeader} from '../../site-header.jsx';
import {HostlinkConnection} from './hostlink.js';
import {canEditInterfaceAccess} from './config-interface-capabilities.js';
import {ManagementSession,OPERATION} from './management-protocol.js';
import {readConfigSnapshot} from './config-snapshot.js';
import {saveConfigDocument} from './config-save.js';
import {prepareConfigurationSave} from './config-save-preparation.js';
import {configurationContentHash} from './config-content-hash.js';
import {configReticulumInterfaceText} from '../../i18n/config-reticulum-interface-labels.js';
import {setConfigurationRadioInput} from './config-radio-input.js';
import {equal} from '../../geocaching/src/protocol.js';
import {configurationField,validateConfigurationField} from './config-schema.js';
import {configurationCharacter,encodeConfigurationCharacter} from './config-character.js';
import {formatPositionPrecision} from './config-position-precision.js';
import {configurationFloatInput,formatConfigurationRadioTuning} from './config-radio-tuning.js';
import {prepareConfigImport} from './config-import.js';
import {TmsDocument} from './tms-document.js';
import {CONFIG_COLLECTIONS,readConfigCollection,changeConfigCollection,configCollectionIssues,configurationChanges} from './config-collections.js';
import {proveDeviceAuthor} from './author-identity.js';
import {BackupDialog} from './backup-dialog.jsx';
import {isEncryptedConfigBackup,ENCRYPTED_BACKUP_MAX_BYTES} from './config-backup.js';
import {ManagementPageProvider,initializeManagementPage,useManagementLanguage} from '../../i18n/management-language.jsx';
import {useManagementText} from '../../i18n/management-copy.jsx';
import {configInputText} from '../../i18n/config-input-labels.js';
import {configCollectionText} from '../../i18n/config-collection-labels.js';
import {deviceAuthorText} from '../../i18n/device-author-labels.js';
import {configActionText} from '../../i18n/config-action-labels.js';
import {formatConfigConnectionNotice} from '../../i18n/config-connection-labels.js';
import {configTransferText} from '../../i18n/config-transfer-labels.js';
import {configImportText} from '../../i18n/config-import-labels.js';
import {configOnboardingText} from '../../i18n/config-onboarding-labels.js';
import {configurationPresentation} from './config-field-presentation.js';
import {formatConfigCollectionIssue} from '../../i18n/config-collection-issue-labels.js';
import {configSecretText} from '../../i18n/config-secret-labels.js';
import {configurationFieldAvailable} from './config-dependencies.js';
import {configBinaryText} from '../../i18n/config-binary-labels.js';
import {decodeConfigurationBinary,isGroupHash} from './config-binary-input.js';
import {configGroupHashHelp} from '../../i18n/config-group-hash-labels.js';
import {IdentityBackupExport} from './identity-backup-dialog.jsx';
import {IdentityRestore} from './identity-restore-dialog.jsx';
import {RadioIdentityTools} from './radio-identity-tools.jsx';
import {configurationTextBlobValue,encodeConfigurationTextBlob} from './config-text-blob.js';
import {ConfigurationDurationInput} from './config-duration-input.jsx';
import {ConfigurationFrequencyInput} from './config-frequency-input.jsx';
import {ConfigurationChannelIdInput} from './config-channel-id-input.jsx';import {configRoutingValueText} from '../../i18n/config-routing-values-labels.js';
import {formatConfigurationRadioValue} from './config-radio-values.js';
import {configRadioValueText} from '../../i18n/config-radio-values-labels.js';
import {configDurationText} from '../../i18n/config-duration-labels.js';
import {isWebConfigurationField} from './config-web-scope.js';
import {sharedWifiOverridesC6} from './config-c6-effective.js';
import {configC6EffectiveText} from '../../i18n/config-c6-effective-labels.js';
import {configC6Issues} from './config-c6-issues.js';
import {MeshtasticModuleEditor} from './meshtastic-module-editor.jsx';
import {configModuleText,configModuleFieldLabel} from '../../i18n/config-module-labels.js';
import {ConfigFinder} from './config-finder.jsx';
import {configurationModuleSearchRows} from './config-module-search.js';
import {ConfigReview} from './config-review.jsx';
import {ConfigSaveReview} from './config-save-review.jsx';
import {configurationSections} from './config-sections.js';
import {configurationMaskOptions,toggleConfigurationMask} from './config-mask.js';
import {configMaskHelp} from '../../i18n/config-mask-labels.js';
import {createConfigurationReadback,checkConfigurationReadback} from './config-readback.js';
import {configReadbackText} from '../../i18n/config-readback-labels.js';
import {configMqttRuntimeText} from '../../i18n/config-mqtt-runtime-labels.js';
import {saveConfigurationReadbackCheckpoint,loadConfigurationReadbackCheckpoint,confirmConfigurationReadbackCheckpoint} from './config-readback-checkpoint.js';

const groups=['设备与身份','无线与协议','Reticulum','频道与消息','GPS 与定位','轨迹与地图','显示与交互','网络与外设','其他设置'];
function group(key){
 if(key.startsWith('device.'))return groups[0];
 if(/^(mt\.|mc\.|mt_module\.|protocol\.|policy\.)/.test(key))return groups[1];
 if(key.startsWith('rt.'))return groups[2];
 if(/^(channel\.|chat\.|privacy\.)/.test(key))return groups[3];
 if(key.startsWith('gps.'))return groups[4];
 if(/^(map\.|route\.)/.test(key))return groups[5];
 if(/^(ui\.|power\.|locale\.|ime\.|timezone\.)/.test(key))return groups[6];
 if(/^(wifi\.|ble\.|mt_ble\.|mc_ble\.|cellular\.|aprs\.|network\.|c6_companion\.)/.test(key))return groups[7];
 return groups[8];
}
function display(r){return r.value instanceof Uint8Array?btoa(Array.from(r.value,b=>String.fromCharCode(b)).join('')):r.type==='f32'&&typeof r.value==='number'?configurationFloatInput(r.value):String(r.value??r.raw);}
function Field({record,document,onEdit,onValidity,errorData,disabled,preferredFrequencyUnit,onFrequencyUnitChange,preferredIdBase,onIdBaseChange,firmwareCapabilities=0}){
 const {locale,text}=useManagementText(),inputText={...configInputText(locale),...configBinaryText(locale),...configDurationText(locale),...configReticulumInterfaceText(locale),...configRadioValueText(locale),numberRequired:text.numberRequired,base64Required:text.base64Required},numbers=new Intl.NumberFormat(locale);
 const [error,setError]=useState(errorData);
 const [customInput,setCustomInput]=useState(false);
 const [revealed,setRevealed]=useState(false),secretText=configSecretText(locale);
 const schema=configurationField(record,configurationPresentation(record.key,locale),document,{firmwareCapabilities}),id=`field-${record.key}`;
 const errorText=error?(error.code==='CONFIG_CHANNEL_ID'?configRoutingValueText(locale).invalid:error.code==='CONFIG_CHARACTER'?schema.characterText.invalid:Object.hasOwn(inputText,error.code)?inputText[error.code].replace(/\{(\w+)\}/g,(_,name)=>typeof error.params[name]==='number'?numbers.format(error.params[name]):String(error.params[name])):`${inputText.invalid} · ${error.message}`):'';
 const accessibility={'aria-invalid':Boolean(error),'aria-describedby':`${id}-help${error?` ${id}-error`:''}`};
 const invalid=(failure,raw)=>{const details={message:failure.message,code:failure.code,params:failure.params??{},raw};setError(details);onValidity(record.key,false,details);};
 const edit=(value,raw)=>{try{validateConfigurationField(record,value);onEdit(record.key,value);setError(undefined);onValidity(record.key,true);}catch(e){invalid(e,raw??display({...record,value}));}};
 const known=record.value!==undefined,locked=disabled||!known||schema.readOnly;
 const blobText=configurationTextBlobValue(record);
 let control;
 if(blobText!==undefined)control=<textarea id={id} {...accessibility} rows={4} defaultValue={errorData?.raw??blobText} disabled={locked} onChange={e=>{try{edit(encodeConfigurationTextBlob(e.target.value),e.target.value);}catch(failure){invalid(failure,e.target.value);}}}/>;
 else if(record.type==='bool')control=<label className="device-toggle"><input id={id} {...accessibility} type="checkbox" defaultChecked={record.value} disabled={locked} onChange={e=>edit(e.target.checked)}/>{text.enabled}</label>;
 else if(schema.options)control=<select id={id} {...accessibility} value={record.value} disabled={locked} onChange={e=>edit(Number(e.target.value))}>{!schema.options.some(option=>option.value===record.value)&&<option value={record.value} disabled>{inputText.unknownOption.replace('{value}',numbers.format(record.value))}</option>}{schema.options.map(option=><option key={option.value} value={option.value}>{option.label}</option>)}</select>;
 else control=<Input id={id} {...accessibility} status={error?'error':undefined} dir={/^(u8|u16|u32|i8|i32|f32|b64)$/.test(record.type)?'ltr':undefined} type={schema.secret?(revealed?'text':'password'):/^(u8|u16|u32|i8|i32|f32)$/.test(record.type)?'number':'text'} autoComplete={schema.secret?'new-password':undefined} spellCheck={schema.secret?false:undefined} min={schema.range?.[0]} max={schema.range?.[1]} step={record.type==='f32'?'any':'1'} value={schema.readOnly?display(record):undefined} defaultValue={schema.readOnly?undefined:errorData?.raw??display(record)} disabled={locked} onChange={e=>{
   let value=e.target.value;
   if(/^(u8|u16|u32|i8|i32|f32)$/.test(record.type)){if(!value.trim()){invalid({code:'numberRequired',message:text.numberRequired},value);return;}value=Number(value);}
   if(record.type==='b64'){try{value=decodeConfigurationBinary(record.key,value);}catch{invalid({code:'base64Required',message:text.base64Required},value);return;}}
   edit(value);
 }}/>
 const mask=configurationMaskOptions(record.key);
 if(schema.duration&&known)control=<ConfigurationDurationInput record={record} schema={schema} error={error} onEdit={edit} onInvalid={invalid} disabled={locked} id={id} accessibility={accessibility} locale={locale}/>;
 if(schema.frequency&&known)control=<ConfigurationFrequencyInput record={record} schema={schema} error={error} onEdit={edit} onInvalid={invalid} disabled={locked} id={id} accessibility={accessibility} locale={locale} preferredUnit={preferredFrequencyUnit} onUnitChange={onFrequencyUnitChange}/>;
 if(schema.numericPurpose?.kind==='channelId'&&known)control=<ConfigurationChannelIdInput record={record} schema={schema} error={error} onEdit={edit} onInvalid={invalid} disabled={locked} id={id} accessibility={accessibility} locale={locale} preferredBase={preferredIdBase} onBaseChange={onIdBaseChange}/>;
 if(schema.allowCustom&&known&&!schema.character){const custom=customInput||Boolean(error)||!schema.options.some(option=>option.value===record.value);control=<div><select id={id} {...accessibility} value={custom?'custom':record.value} disabled={locked} onChange={event=>{setCustomInput(event.target.value==='custom');if(event.target.value!=='custom')edit(Number(event.target.value));}}>{schema.options.map(option=><option key={option.value} value={option.value}>{option.label}</option>)}<option value="custom">{schema.customLabel}</option></select>{custom&&<label>{schema.customLabel}<input aria-label={schema.label+' · '+schema.customLabel} {...accessibility} type="number" min={schema.range?.[0]} max={schema.range?.[1]} step={schema.step??(record.type==='f32'?'any':'1')} value={error?.raw??(record.type==='f32'?configurationFloatInput(record.value):record.value)} disabled={locked} onChange={event=>{if(!event.target.value.trim())invalid({code:'numberRequired',message:text.numberRequired},event.target.value);else edit(Number(event.target.value),event.target.value);}}/></label>}</div>;}
 if(schema.character&&known)control=<div><input id={id} {...accessibility} dir="ltr" type="text" autoComplete="off" spellCheck={false} value={error?.code==='CONFIG_CHARACTER'?error.raw:configurationCharacter(record.value)} disabled={locked} onChange={event=>{try{edit(encodeConfigurationCharacter(event.target.value),event.target.value);}catch(failure){invalid(failure,event.target.value);}}}/><select aria-label={schema.label+' · '+schema.characterText.character} {...accessibility} value={schema.options.some(option=>option.value===record.value)?record.value:'custom'} disabled={locked} onChange={event=>{if(event.target.value!=='custom')edit(Number(event.target.value));}}>{schema.options.map(option=><option key={option.value} value={option.value}>{option.label}</option>)}<option value="custom">{schema.customLabel}</option></select><details><summary>{schema.characterText.byte}</summary><input aria-label={schema.label+' · '+schema.characterText.byte} {...accessibility} type="number" min={0} max={255} step={1} value={error&&error.code!=='CONFIG_CHARACTER'?error.raw:record.value} disabled={locked} onChange={event=>{if(!event.target.value.trim())invalid({code:'numberRequired',message:text.numberRequired},event.target.value);else edit(Number(event.target.value),event.target.value);}}/></details></div>;
 if(schema.percentScale&&known){const percent=Number((record.value*100/schema.percentScale).toFixed(1));control=<div className="device-percent"><input type="range" aria-label={schema.label} min={0} max={schema.percentScale} step={1} value={record.value} disabled={locked} onChange={event=>edit(Number(event.target.value))}/><div className="device-percent-entry"><input id={id} {...accessibility} dir="ltr" aria-label={schema.label+' (%)'} type="number" min={0} max={100} step={0.1} value={error?.raw??percent} disabled={locked} onChange={event=>{const raw=event.target.value,value=Number(raw);if(!raw.trim())invalid({code:'numberRequired',message:text.numberRequired},raw);else if(!Number.isFinite(value)||value<0||value>100)invalid({code:'CONFIG_RANGE',params:{min:0,max:100},message:inputText.CONFIG_RANGE},raw);else edit(Math.round(value*schema.percentScale/100),raw);}}/><span aria-hidden="true">%</span></div></div>;}
 if(schema.numericPurpose&&known)control=<div className="device-numeric-purpose">{control}{!error&&<><output>{schema.numericPurpose.format(record.value)}</output>{schema.numericPurpose.note&&<small>{schema.numericPurpose.note}</small>}</>}</div>;
 if(mask&&known)control=React.cloneElement(control,{value:error?.raw??record.value,defaultValue:undefined});
 if(mask&&known)control=<fieldset disabled={locked} style={{minWidth:0,margin:0,border:0,padding:0}}><div className="device-fields">{mask.map(option=><label key={option.value} className="device-toggle"><input type="checkbox" aria-label={`${schema.label} · ${option.label}`} checked={Boolean(record.value&option.value)} onChange={event=>edit(toggleConfigurationMask(record.value,option.value,event.target.checked))}/>{option.label}</label>)}</div><small>{configMaskHelp(locale)}</small><details className="device-mask-advanced" open={Boolean(error)}><summary>{configMaskHelp(locale,'advanced')}</summary>{control}</details></fieldset>;
 return <div className="device-field"><label htmlFor={id}>{schema.label}</label>{control}{schema.secret&&known&&record.type!=='bool'&&!schema.options&&<Button aria-controls={id} aria-pressed={revealed} onClick={()=>setRevealed(value=>!value)}>{revealed?secretText.hide:secretText.show}</Button>}<small id={`${id}-help`}>{schema.percentScale?inputText.CONFIG_RANGE.replace('{min}',numbers.format(0)+'%').replace('{max}',numbers.format(100)+'%'):isGroupHash(record.key)?configGroupHashHelp(locale):schema.help??(record.type==='b64'&&blobText===undefined?text.binaryConfig:!known?text.unknownField:'')}</small>{schema.radioTuning&&known&&!error&&<><output>{formatConfigurationRadioTuning(record,locale)}</output>{schema.runtimeTuningNote&&<small>{schema.runtimeTuningNote}</small>}</>}{schema.radioValue&&known&&!error&&<><output>{formatConfigurationRadioValue(record,locale)}</output>{schema.radioValue.runtimeNote&&<small>{schema.radioValue.runtimeNote}</small>}</>}{schema.positionPrecision&&!error&&!schema.options.some(option=>option.value===record.value)&&<small>{formatPositionPrecision(record.value,locale)}</small>}{error&&<p id={`${id}-error`} role="alert" className="device-error">{errorText}</p>}</div>;
}
function App(){
 const frequencyUnits=useRef(new Map());
 const channelIdBases=useRef(new Map());
 const language=useManagementLanguage();
 const collectionText=configCollectionText(language.locale),numbers=new Intl.NumberFormat(language.locale);
 const authorText=deviceAuthorText(language.locale);
 const actionText=configActionText(language.locale),actionCount=(key,count)=>actionText[key].replace('{count}',numbers.format(count));
 const noticeText={...configTransferText(language.locale),...configImportText(language.locale),...configReticulumInterfaceText(language.locale)};
 const onboardingText=configOnboardingText(language.locale);
 const translatedGroups=['deviceIdentity','radioProtocols','reticulum','channelsMessages','gpsLocation','tracksMaps','displayInteraction','networkPeripherals','otherSettings'].map(key=>language.text[key]);
 const connection=useRef(),session=useRef(),generation=useRef(0),baseHash=useRef(),baseline=useRef(),transaction=useRef(),importInput=useRef(),sessionId=useRef();
 const authorVerification=useRef();
 sessionId.current??=crypto.getRandomValues(new Uint8Array(16));
 const [state,setState]=useState('disconnected'),[noticeState,setNotice]=useState({key:'intro'}),[info,setInfo]=useState(),[document,setDocument]=useState(),[selected,setSelected]=useState(groups[0]),[changes,setChanges]=useState(new Set()),[operations,setOperations]=useState([]),[busy,setBusy]=useState(false),[invalidFields,setInvalidFields]=useState(new Map()),[formVersion,setFormVersion]=useState(0);
 const notice=formatConfigConnectionNotice(noticeState,language.locale,noticeText);
 const support='serial' in navigator;
 const [pendingTransfer,setPendingTransfer]=useState(false),[snapshotConflict,setSnapshotConflict]=useState(false);
 const [saveReview,setSaveReview]=useState(null);
 const [author,setAuthor]=useState(),[authorStatus,setAuthorStatus]=useState({key:'unverified'});
 const authorStatusMessage=authorText[authorStatus.key].replace('{diagnostic}',()=>authorStatus.diagnostic??'');
 const [backupOperation,setBackupOperation]=useState();
 const [jump,setJump]=useState();
 const [initialReadback]=useState(()=>{try{return loadConfigurationReadbackCheckpoint(localStorage);}catch{return undefined;}});
 const savedReadback=useRef(initialReadback);
 const [readback,setReadback]=useState(initialReadback?{status:'pending'}:undefined);
 const readbackText=configReadbackText(language.locale);
 useEffect(()=>{
  if(!jump)return;
  const control=window.document.getElementById(jump.targetId??`field-${jump.key}`);
  if(!control){setJump(undefined);return;}
  for(let parent=control.parentElement;parent;parent=parent.parentElement)if(parent.tagName==='DETAILS')parent.open=true;
  control.scrollIntoView({block:'center'});control.focus({preventScroll:true});
  setJump(undefined);
 },[jump,selected,formVersion]);
 useEffect(()=>{
   const protect=event=>{if(changes.size||invalidFields.size||pendingTransfer){event.preventDefault();event.returnValue='';}};
   window.addEventListener('beforeunload',protect);return()=>window.removeEventListener('beforeunload',protect);
 },[changes,invalidFields,pendingTransfer]);
 async function disconnect(){generation.current++;authorVerification.current?.abort();session.current?.close();session.current=undefined;await connection.current?.close();connection.current=undefined;setState('disconnected');setOperations([]);setAuthor(undefined);setAuthorStatus({key:'revoked'});setNotice({key:'disconnected'});}
 async function read({preserveEdits=false,authorProof=author}={}){const token=generation.current;setBusy(true);if(savedReadback.current)setReadback({status:'pending',persistent:savedReadback.current.persistent});try{const snapshot=await readConfigSnapshot(session.current,{onProgress:p=>{if(token===generation.current)setNotice({key:'reading',values:{offset:p.offset,total:p.total}});}});if(token!==generation.current)return;
   if(savedReadback.current){const check=savedReadback.current,contentSha256=check.version===2?await configurationContentHash(snapshot.document):undefined;if(token!==generation.current)return;const result=checkConfigurationReadback(check,snapshot.document,authorProof,snapshot.sha256,contentSha256);if(check.persistent&&result.status==='matched'){try{confirmConfigurationReadbackCheckpoint(localStorage,check,result,snapshot.sha256);}catch{check.persistent=false;}}setReadback({...result,actual:snapshot.document,persistent:check.persistent});}
   if(preserveEdits&&document&&(changes.size||invalidFields.size||transaction.current)){
     const same=equal(baseHash.current,snapshot.sha256);setSnapshotConflict(!same);
     setNotice({key:same?'sameVersion':'changedVersion'});return;
   }
   setDocument(snapshot.document);baseline.current=new TmsDocument(snapshot.document.originalBytes);setInvalidFields(new Map());setFormVersion(v=>v+1);baseHash.current=snapshot.sha256;setChanges(new Set());setSnapshotConflict(false);setNotice({key:'snapshotVerified',values:{count:snapshot.document.size}});
 }catch(e){if(token===generation.current)setNotice({key:'readFailed',diagnostic:e.message});}finally{if(token===generation.current)setBusy(false);}}
 async function save(approved){
   if(!document||!baseHash.current||busy||state!=='ready'||invalidFields.size||configCollectionIssues(document).length||configC6Issues(document).length||(snapshotConflict&&!transaction.current))return;
   let prepared,inputSource;try{inputSource=document.serialize({kind:'working'});prepared=prepareConfigurationSave(document);}catch(error){setNotice({key:'notUploaded',diagnostic:error.message});return;}
   const {source,omitted}=prepared;
   if(!approved){setSaveReview({source:source.slice(),inputSource:inputSource.slice(),omitted,noWrite:omitted.length>0&&!transaction.current&&!configurationChanges(prepared.document,baseline.current).size,epoch:generation.current,baseHash:baseHash.current.slice(),document:prepared.document,baseline:new TmsDocument(baseline.current.originalBytes)});return;}
   setSaveReview(null);
   if(approved.epoch!==generation.current||!equal(approved.source,source)||!equal(approved.inputSource,inputSource)||!equal(approved.baseHash,baseHash.current))return;
   if(omitted.length&&!transaction.current&&!configurationChanges(prepared.document,baseline.current).size){setDocument(prepared.document);setFormVersion(value=>value+1);setChanges(new Set());setNotice({key:'emptyAccessNormalized'});return;}
   if(transaction.current&&!equal(transaction.current.source,source)){setNotice({key:'previousUnconfirmed'});return;}
   transaction.current??={id:crypto.getRandomValues(new Uint8Array(16)),source};
   setPendingTransfer(true);const token=generation.current;
   setBusy(true);
   try{const result=await saveConfigDocument(session.current,prepared.document,baseHash.current,{id:transaction.current.id,beforeCommit:async receipt=>{
     if(token!==generation.current)throw Error('CONFIG_CONNECTION_CHANGED');
     if(!equal(receipt.source,source))throw Error('CONFIG_APPROVED_SOURCE_CHANGED');
     const check=createConfigurationReadback(new TmsDocument(source),author);
     check.contentSha256=await configurationContentHash(check.expected);
     if(token!==generation.current)throw Error('CONFIG_CONNECTION_CHANGED');
     check.transferId=receipt.id.slice();
     try{saveConfigurationReadbackCheckpoint(localStorage,check,receipt.sha256,receipt.id);check.persistent=true;}
     catch{check.persistent=false;}
     savedReadback.current=check;setReadback({status:'pending',persistent:check.persistent});
   },onProgress:p=>{if(token===generation.current)setNotice(p.sealed?{key:'uploadVerified'}:{key:'uploading',values:{offset:p.offset,total:p.total}});}});
     if(token!==generation.current)return;
     setDocument(new TmsDocument(source));setFormVersion(value=>value+1);setChanges(new Set());transaction.current=undefined;setPendingTransfer(false);baseHash.current=result.sha256;baseline.current=new TmsDocument(source);setSnapshotConflict(false);
     setNotice({key:result.restartPending?'savedRestart':'savedConfirmed'});
     setReadback({status:'pending',persistent:savedReadback.current?.persistent});
   }catch(e){if(token===generation.current)setNotice({key:'saveUnconfirmed',diagnostic:e.message});}finally{if(token===generation.current)setBusy(false);}
 }
 async function abortTransfer(){
   if(!transaction.current||busy||state!=='ready')return;
   if(!confirm(actionText.abortConfirm))return;
   const token=generation.current;setBusy(true);
   try{const reply=await session.current.request(OPERATION.TRANSFER_ABORT,[transaction.current.id]);if(!Array.isArray(reply)||reply.length)throw Error('CONFIG_CANCEL_RECEIPT_INVALID');if(token!==generation.current)return;transaction.current=undefined;setPendingTransfer(false);setNotice({key:'uploadCancelled'});}
   catch(error){if(token===generation.current)setNotice({key:'cancelUnconfirmed',...(error.message==='CONFIG_CANCEL_RECEIPT_INVALID'?{diagnosticKey:'cancelReceiptInvalid'}:{diagnostic:error.message})});}
   finally{if(token===generation.current)setBusy(false);}
 }
 async function verifyAuthor(s=session.current,token=generation.current){
   authorVerification.current?.abort();const check=new AbortController();authorVerification.current=check;
   setAuthor(undefined);setAuthorStatus({key:'verifying'});
   try{const proven=await proveDeviceAuthor(s,{signal:check.signal});if(token===generation.current&&!check.signal.aborted){setAuthor(proven);setAuthorStatus({key:'verified'});return proven;}}
   catch(error){if(token===generation.current)setAuthorStatus({key:'failed',diagnostic:error.message});}
   finally{if(authorVerification.current===check)authorVerification.current=undefined;}
 }
 async function connect(){
   if(busy)return;
   // requestPort stays in the click's user gesture, before opening a connection.
   let port;try{port=await navigator.serial.requestPort();}catch(e){setNotice(e.name==='NotFoundError'?{key:'noDevice'}:{key:'portFailed',diagnostic:e.message});return;}
   setBusy(true);if(savedReadback.current)setReadback({status:'pending',persistent:savedReadback.current.persistent});const token=++generation.current;let activeConnection;
   try{const c=new HostlinkConnection(port);activeConnection=c;connection.current=c;c.subscribe(e=>{if(token!==generation.current)return;if(e.type==='state'){setState(e.state);if(e.state==='disconnected'){generation.current++;authorVerification.current?.abort();setAuthor(undefined);setOperations([]);setBusy(false);setAuthorStatus({key:'revoked'});setNotice({key:'disconnected'});}}});const hello=await c.open();if(token!==generation.current)return;setInfo(hello);const s=new ManagementSession(c,{sessionId:sessionId.current});session.current=s;const caps=await s.request(OPERATION.CAPABILITIES,[]);if(token!==generation.current)return;
     if(!Array.isArray(caps)||caps.length!==6||!Array.isArray(caps[4]))throw Error('CONFIG_CAPABILITIES_INVALID');
     setOperations(caps[4]);setAuthor(undefined);
     let connectedAuthor;
     if(caps[4].includes(OPERATION.AUTHOR_INFO)&&caps[4].includes(OPERATION.AUTHOR_PROVE)){
       connectedAuthor=await verifyAuthor(s,token);
     }else setAuthorStatus({key:'unsupported'});
     if(token!==generation.current)return;await read({preserveEdits:true,authorProof:connectedAuthor??null});
   }catch(e){if(token===generation.current||connection.current===activeConnection){session.current?.close();await activeConnection?.close();if(connection.current===activeConnection){setNotice({key:'connectionFailed',...(e.message==='CONFIG_CAPABILITIES_INVALID'?{diagnosticKey:'capabilitiesInvalid'}:{diagnostic:e.message})});setState('disconnected');setBusy(false);}}}finally{if(token===generation.current)setBusy(false);}
 }
 function editedKeys(candidate){return configurationChanges(candidate,baseline.current);}
 function edit(key,value){const changed=setConfigurationRadioInput(document,key,value);setChanges(editedKeys(document));if(key==='mc.region_preset'){setInvalidFields(previous=>{const next=new Map(previous);for(const field of changed)next.delete(field);return next;});setFormVersion(v=>v+1);}}
 function changeCollection(name,action,index,type){
   if(busy||pendingTransfer||invalidFields.size)return;
   if(action==='remove'&&!confirm(collectionText.confirmRemove))return;
   try{const next=changeConfigCollection(document,name,action,index,type);setDocument(next);setChanges(editedKeys(next));setFormVersion(v=>v+1);setNotice({key:'collectionChanged'});}
   catch(error){setNotice({key:'collectionFailed',diagnostic:error.message});}
 }
 async function importBackup(file){
   if(!file||!document||busy||transaction.current)return;
   setBusy(true);
   try{
     if(file.size>ENCRYPTED_BACKUP_MAX_BYTES)throw Error('CONFIG_BACKUP_TOO_LARGE');
     const bytes=new Uint8Array(await file.arrayBuffer());
     if(isEncryptedConfigBackup(bytes)){setBackupOperation({mode:'import',bytes});return;}
     applyBackup(new TmsDocument(bytes));
   }catch(error){setNotice({key:'importFailed',...(error.message==='CONFIG_BACKUP_TOO_LARGE'?{diagnosticKey:'backupTooLarge'}:{diagnostic:error.message})});}
   finally{setBusy(false);}
 }
 function applyBackup(backup){
   const prepared=prepareConfigImport(document,backup);
   if(prepared.errors.length)throw Error(prepared.errors.map(e=>`${e.key}: ${e.reason}`).join('；'));
   if(!confirm(formatConfigConnectionNotice({key:'importConfirm',values:{count:prepared.changed.length,added:prepared.changed.filter(item=>item.added).length,removed:prepared.changed.filter(item=>item.removed).length,secrets:prepared.changed.filter(item=>item.secret).length,skipped:prepared.skipped.length}},language.locale,noticeText)))return;
   setDocument(prepared.document);setFormVersion(v=>v+1);setInvalidFields(new Map());setChanges(editedKeys(prepared.document));
   setNotice({key:'imported',values:{skipped:prepared.skipped.length}});
 }
 function downloadBytes(bytes,name,type){const url=URL.createObjectURL(new Blob([bytes],{type}));const a=window.document.createElement('a');a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}
 function downloadBackup(){if(!document)return;if(!confirm(actionText.plainConfirm))return;downloadBytes(document.serialize({kind:'backup'}),'trail-mate-settings.tms','text/plain');}
 const collections=document?Object.keys(CONFIG_COLLECTIONS).flatMap(name=>{try{const value=readConfigCollection(document,name);return value&&group(CONFIG_COLLECTIONS[name].count)===selected?[value]:[];}catch{return [];}}):[];
 const issues=document?[...configCollectionIssues(document),...configC6Issues(document)]:[];
 const moduleVisible=selected===groups[1]&&document?.entries().some(record=>record.key==='mt_module.format');
 const moduleRecords=moduleVisible?document.entries().filter(record=>record.key.startsWith('mt_module.')):[];
 const records=document?.entries().filter(r=>isWebConfigurationField(r.key)&&group(r.key)===selected&&!(moduleVisible&&r.key.startsWith('mt_module.'))&&!collections.some(c=>r.key===CONFIG_COLLECTIONS[c.name].count||r.key.startsWith(c.prefix)))??[];
  const firmwareCapabilities=state==='ready'&&operations.includes(OPERATION.CONFIG_SNAPSHOT)?info?.capabilities??0:0;
  const renderField=r=><Field firmwareCapabilities={firmwareCapabilities} key={r.key+'-'+formVersion} record={r} document={document} onEdit={edit} errorData={invalidFields.get(r.key)} preferredFrequencyUnit={frequencyUnits.current.get(r.key)} onFrequencyUnitChange={unit=>frequencyUnits.current.set(r.key,unit)} preferredIdBase={channelIdBases.current.get(r.key)} onIdBaseChange={base=>channelIdBases.current.set(r.key,base)} onValidity={(key,valid,details)=>setInvalidFields(previous=>{const next=new Map(previous);valid?next.delete(key):next.set(key,details);return next;})} disabled={busy||pendingTransfer||!configurationFieldAvailable(document,r.key)}/>;
 const moduleSearchRows=document?configurationModuleSearchRows(document,baseline.current,{label:(path,kind)=>configModuleFieldLabel(path,language.locale,kind),text:configModuleText(language.locale),group:groups[1],groupLabel:translatedGroups[1],index:value=>numbers.format(value)}):[];
 const searchRows=document?[...document.entries().filter(record=>isWebConfigurationField(record.key)&&!(moduleSearchRows.length&&record.key.startsWith('mt_module.'))&&!Object.values(CONFIG_COLLECTIONS).some(collection=>collection.count===record.key)).map(record=>{const presentation=configurationField(record,configurationPresentation(record.key,language.locale),document,{firmwareCapabilities}),category=group(record.key);return{key:record.key,label:presentation.label,help:presentation.help,group:category,groupLabel:translatedGroups[groups.indexOf(category)]};}),...moduleSearchRows]:[];
 const searchChanges=new Set([...changes,...invalidFields.keys(),...moduleSearchRows.filter(row=>row.changed).map(row=>row.key)]);
 return <><Background type="sprinkles" aria-hidden="true"/>
 <div className="page-shell device-shell">
   <SiteHeader page="device" data-author-verified={author?.verified===true&&authorStatus.key==='verified'?'true':'false'}>
       <span className="device-status" role="status">{state==='ready'?<><bdi>{info?.model}</bdi> · {language.text.connected}</>:state==='connecting'?language.text.connecting:state==='closing'?language.text.disconnecting:language.text.disconnected}</span>
       <Button onClick={state==='ready'?disconnect:connect} loading={state==='connecting'} disabled={busy||!support} type="primary">{state==='ready'?language.text.disconnect:language.text.connect}</Button>
   </SiteHeader>
   <div className="content-shell">
     <aside className="sidebar"><p className="sidebar-label">{language.deviceTitle}</p><nav className="device-nav">{groups.map((name,index)=><button key={name} className={selected===name?'selected':''} onClick={()=>setSelected(name)}>{translatedGroups[index]}</button>)}</nav><p className="device-footnote" role="status">{authorStatusMessage}</p>{author&&<div className="device-author"><span>{authorText.fingerprint}</span><code dir="ltr">{author.fingerprint}</code><span>{authorText.generation.replace('{generation}',numbers.format(author.generation))}</span></div>}{operations.includes(OPERATION.AUTHOR_INFO)&&operations.includes(OPERATION.AUTHOR_PROVE)&&<Button disabled={busy||state!=='ready'} onClick={async()=>{const token=generation.current;setBusy(true);try{await verifyAuthor();}finally{if(token===generation.current)setBusy(false);}}}>{authorText.reverify}</Button>}<p className="device-footnote">{authorText.usbNotProof}</p></aside>
     <main className="main-content"><Title color="app-teal" size="middle">{translatedGroups[groups.indexOf(selected)]}</Title><p role="status" className="device-notice">{notice}</p>
       {readback&&<section aria-label={readbackText.title}><Card><h2>{readbackText.title}</h2><p role="status">{readbackText[readback.status]}</p>{readback.persistent===false&&<p>{readbackText.storageUnavailable}</p>}</Card>{readback.status==='mismatch'&&<ConfigReview document={readback.actual} baseline={savedReadback.current.expected} locale={language.locale} labels={{title:readbackText.differences,before:readbackText.before,after:readbackText.after}}/>}</section>}
       {!support&&<p role="alert">{onboardingText.noSerial}</p>}
       <IdentityRestore session={state==='ready'?session.current:null} operations={operations} visible={selected===groups[0]} disabled={busy||pendingTransfer||Boolean(backupOperation)} acquire={()=>{if(busy||pendingTransfer||state!=='ready')return null;setBusy(true);return {epoch:generation.current};}} release={lease=>{if(lease.epoch===generation.current)setBusy(false);}}/>
       <RadioIdentityTools session={state==='ready'?session.current:null} operations={operations} visible={selected===groups[0]} disabled={busy||pendingTransfer||Boolean(backupOperation)} acquire={()=>{if(busy||pendingTransfer||state!=='ready')return null;setBusy(true);return {epoch:generation.current};}} release={lease=>{if(lease.epoch===generation.current)setBusy(false);}} onFile={(bytes,name)=>downloadBytes(bytes,name,'application/octet-stream')}/>
       {state==='ready'&&selected===groups[0]&&<IdentityBackupExport session={session.current} operations={operations} disabled={busy||pendingTransfer||Boolean(backupOperation)} acquire={()=>{if(busy||pendingTransfer||state!=='ready')return null;setBusy(true);return {epoch:generation.current};}} release={lease=>{if(lease.epoch===generation.current)setBusy(false);}} onFile={bytes=>downloadBytes(bytes,'trail-mate-identity.tmid','application/octet-stream')}/>}
       {!document?<Card color="app-yellow"><h2>{onboardingText.startTitle}</h2><p>{onboardingText.startHelp}</p><p>{onboardingText.capabilities}</p></Card>:<>
         <ConfigFinder rows={searchRows} changes={searchChanges} locale={language.locale} onSelect={row=>{setSelected(row.group);setJump({key:row.key,targetId:row.targetId});}}/>
         {selected===groups[7]&&sharedWifiOverridesC6(document)&&<p className="device-c6-effective device-notice" role="status">{configC6EffectiveText(language.locale)}</p>}
         {configurationSections(records,language.locale).map(section=><Card className="device-section" key={section.key}><h2>{section.label} <small>({numbers.format(section.records.length)})</small></h2>{/^(mt|mc)\.mqtt$/.test(section.key)&&<p className="device-footnote">{configMqttRuntimeText(language.locale).device}</p>}<div className="device-fields">{section.records.map(renderField)}</div></Card>)}
         {!records.length&&!collections.length&&!moduleVisible&&<Card><p>{onboardingText.empty}</p></Card>}
         {moduleVisible&&<>
           <MeshtasticModuleEditor key={formVersion} document={document} errors={invalidFields} text={{...configModuleText(language.locale),enabled:language.text.enabled}} label={(path,kind)=>configModuleFieldLabel(path,language.locale,kind)} disabled={busy||pendingTransfer} hasInvalid={invalidFields.size>0} onNativeMqtt={document.entries().some(row=>row.key.startsWith('mt.mqtt.'))?()=>setJump({key:document.entries().find(row=>row.key.startsWith('mt.mqtt.')).key}):undefined} onChange={next=>{setDocument(next);setChanges(editedKeys(next));}} onValidity={(key,valid,details)=>setInvalidFields(previous=>{const next=new Map(previous);valid?next.delete(key):next.set(key,details);return next;})}/>
           <Card><details className="device-collection-extra"><summary>{configModuleText(language.locale).raw}</summary><div className="device-fields">{moduleRecords.map(renderField)}</div></details></Card>
         </>}
         {collections.map(collection=><Card className="device-collection" key={collection.name}>
           <h2>{collectionText[collection.name]}</h2><p>{collectionText.count.replace('{count}',numbers.format(collection.count)).replace('{capacity}',numbers.format(collection.capacity))}</p>
           {collection.rows.slice(0,collection.count).map((row,index)=>{
             const type=row.find(r=>r.field==='type')?.value;
             const specific=['target_host','target_port','group_id','discovery_port','data_port'];
             const deferred=collection.name==='interfaces'?row.filter(r=>specific.includes(r.field)&&!(type===2?['target_host','target_port']:type===1?['group_id','discovery_port','data_port']:[]).includes(r.field)):[];
             return <section className="device-collection-row" key={index+'-'+formVersion} aria-label={`${collectionText[collection.name]} ${numbers.format(index+1)}`}>
             <h3>{collectionText[collection.name]} {numbers.format(index+1)}</h3><div className="device-fields">{row.filter(r=>!deferred.includes(r)).map(renderField)}</div>
             {deferred.length>0&&<details className="device-collection-extra" open={deferred.some(r=>invalidFields.has(r.key))||undefined}><summary>{collectionText.retained}</summary><div className="device-fields">{deferred.map(renderField)}</div></details>}
             <div className="device-collection-actions"><Button disabled={busy||pendingTransfer||invalidFields.size>0||index===0} onClick={()=>changeCollection(collection.name,'up',index)}>{collectionText.up}</Button><Button disabled={busy||pendingTransfer||invalidFields.size>0||index===collection.count-1} onClick={()=>changeCollection(collection.name,'down',index)}>{collectionText.down}</Button><Button disabled={busy||pendingTransfer||invalidFields.size>0} onClick={()=>changeCollection(collection.name,'remove',index)}>{collectionText.remove}</Button>
               {collection.name==='interfaces'&&canEditInterfaceAccess(row.find(r=>r.field==='type')?.value,info?.capabilities??0,row.some(r=>r.field==='ifac_size'))&&<Button disabled={busy||pendingTransfer||invalidFields.size>0} onClick={()=>changeCollection(collection.name,row.some(r=>r.field==='ifac_size')?'remove-access':'access',index)}>{row.some(r=>r.field==='ifac_size')?collectionText.removeAccess:collectionText.addAccess}</Button>}
             </div>
           </section>;})}
           <div className="device-collection-actions">{(collection.name==='interfaces'?[[0,collectionText.addLoRa],[1,collectionText.addAuto],[2,collectionText.addTcp]]:[[0,collectionText.addWifi]]).map(([type,label])=><Button key={type} disabled={busy||pendingTransfer||invalidFields.size>0||collection.count>=collection.capacity} onClick={()=>changeCollection(collection.name,'add',undefined,type)}>{label}</Button>)}</div>
         </Card>)}
         {issues.length>0&&<div role="alert" className="device-error">{issues.map(issue=><p key={issue.key} title={issue.key}>{configurationField({key:issue.key,type:'str'},configurationPresentation(issue.key,language.locale)).label} · {formatConfigCollectionIssue(issue,language.locale)}</p>)}</div>}
         <ConfigReview document={document} baseline={baseline.current} locale={language.locale}/>
         <div className="device-savebar"><span>{changes.size?actionCount('changed',changes.size):actionText.unchanged}</span>
           <Button disabled={busy||state!=='ready'||pendingTransfer} onClick={()=>{if(!(changes.size||invalidFields.size)||confirm(actionText.discardRead))read();}}>{actionText.read}</Button>
           <Button disabled={busy||pendingTransfer} onClick={()=>{if(importInput.current){importInput.current.value='';importInput.current.click();}}}>{actionText.import}</Button>
           <input ref={importInput} hidden type="file" accept=".tms,.tmbak,text/plain,application/octet-stream" onChange={event=>importBackup(event.target.files[0])}/>
           <Button disabled={busy||invalidFields.size>0||issues.length>0} onClick={()=>setBackupOperation({mode:'export',document:new TmsDocument(document.serialize({kind:'backup'})),source:{createdAt:new Date().toISOString(),model:info?.model??null,firmware:info?.firmware??null,authorFingerprint:state==='ready'&&author?.verified?author.fingerprint:null,authorGeneration:state==='ready'&&author?.verified?author.generation:null}})}>{actionText.encryptedExport}</Button>
           <details><summary>{actionText.plainCompatibility}</summary><Button disabled={busy||invalidFields.size>0||issues.length>0} onClick={downloadBackup}>{actionText.plainExport}</Button></details>
           {pendingTransfer&&<Button disabled={busy||state!=='ready'||!operations.includes(OPERATION.TRANSFER_ABORT)} onClick={abortTransfer}>{actionText.cancelUpload}</Button>}
           <Button type="primary" disabled={busy||invalidFields.size>0||issues.length>0||(!changes.size&&!pendingTransfer)||(snapshotConflict&&!pendingTransfer)||state!=='ready'||!operations.includes(OPERATION.CONFIG_COMMIT)} onClick={()=>save()}>{pendingTransfer?actionText.retry:actionText.save}</Button>
         </div>
         {pendingTransfer&&<p className="device-footnote">{actionText.pending}</p>}
         {!operations.includes(OPERATION.CONFIG_COMMIT)&&<p className="device-footnote">{actionText.unsupported}</p>}
       </>}
     </main>
     {saveReview&&<ConfigSaveReview review={saveReview} locale={language.locale} actions={actionText} help={actionCount('saveConfirm',configurationChanges(saveReview.document,saveReview.baseline).size)} onClose={()=>setSaveReview(null)} onConfirm={()=>save(saveReview)}/>}
   </div>
   {backupOperation&&<BackupDialog operation={backupOperation} currentAuthor={state==='ready'?author:undefined} onBusy={setBusy} onClose={()=>setBackupOperation(undefined)} onResult={result=>{
     if(backupOperation.mode==='import')applyBackup(result);
     else{downloadBytes(result,'trail-mate-settings.tmbak','application/octet-stream');setNotice({key:'encryptedDownloaded'});}
   }}/>}
 </div></>;
}
await initializeManagementPage();
createRoot(document.getElementById('device-app')).render(<ManagementPageProvider><App/></ManagementPageProvider>);
