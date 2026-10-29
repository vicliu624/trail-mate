import React,{useState,useRef,useEffect} from 'react';
import {createPortal} from 'react-dom';
import {Button,Card} from 'animal-island-ui';
import {HostlinkConnection} from '../../device/src/hostlink.js';
import {ManagementSession,OPERATION} from '../../device/src/management-protocol.js';
import {proveDeviceAuthor} from '../../device/src/author-identity.js';
import {DeviceDraftReader} from './device-draft-reader.js';
import {DraftEditor,draftEditorForm,draftEditorValue} from './draft-editor.jsx';
import {equal} from './protocol.js';
import {IPPublicationPanel as PublicationPanel} from './ip-publication-panel.jsx';
import {loadPublicationCheckpoint} from './publication-journal.js';
import {deviceUsbOwner} from './device-usb-owner.js';
import {loadIPPublication} from './ip-publication-journal.js';
import {recoverSignatureOwner,recoverConfirmationOwner} from './device-signature-recovery.js';
import {loadDraftSave,retainDraftSave,clearDraftSave,assertDraftSaveRetained,withDraftSaveLock,draftSaveSnapshotMatches} from './device-draft-save-journal.js';
import {geocachingSaveRecoveryText,geocachingSaveRecoveryError} from '../../i18n/geocaching-save-recovery-labels.js';
import './maintenance.css';
import {useGeocachingMaintenanceText} from '../../i18n/management-copy.jsx';
import {geocachingMaintenanceStatusText} from '../../i18n/geocaching-maintenance-status.js';
import {geocachingConnectionText} from '../../i18n/geocaching-connection-labels.js';
import {geocachingDraftActionText} from '../../i18n/geocaching-draft-action-labels.js';
import {geocachingTransferText} from '../../i18n/geocaching-transfer-labels.js';
import {geocachingMaintenanceErrorText,geocachingMaintenanceFailure} from '../../i18n/geocaching-maintenance-errors.js';

const hex=value=>Array.from(value,b=>b.toString(16).padStart(2,'0')).join('');

export function Maintenance(){
  const {locale,text}=useGeocachingMaintenanceText();
  const numbers=new Intl.NumberFormat(locale),label=(key,values={})=>text[key].replace(/\{(\w+)\}/g,(_,name)=>numbers.format(values[name]));
  const recoveryText=geocachingSaveRecoveryText(locale),recoveryFailure=error=>{const key=geocachingSaveRecoveryError(error);return key?{key:'recovery',recovery:key}:geocachingMaintenanceFailure(error);};
  const statusText={...geocachingMaintenanceStatusText(locale),...geocachingConnectionText(locale),...geocachingDraftActionText(locale),...geocachingTransferText(locale),...geocachingMaintenanceErrorText(locale)},statusValue=value=>value.key==='recovery'?recoveryText[value.recovery]:statusText[value.key].replace(/\{(\w+)\}/g,(_,key)=>numbers.format(value.values[key]))+(value.detailKey?` · ${statusText[value.detailKey]}`:value.detail?` · ${value.detail}`:'');
  const [mode,setMode]=useState('browse'),[statusState,setStatus]=useState({key:'connectIntro'});
  const [connected,setConnected]=useState(false),[author,setAuthor]=useState(),[busy,setBusy]=useState(false),[rows,setRows]=useState([]),[page,setPage]=useState(),[detail,setDetail]=useState(),[targets,setTargets]=useState();
  const [form,setForm]=useState(),[dirty,setDirty]=useState(false),[pending,setPending]=useState(false),[operations,setOperations]=useState([]),[picking,setPicking]=useState(false);
  const [publicationLocked,setPublicationLocked]=useState(false);
  const [directories,setDirectories]=useState([]),[directoryTarget,setDirectoryTarget]=useState(''),[directoryState,setDirectoryStatus]=useState({key:'directoryUnread'});
  const status=statusValue(statusState),directoryStatus=statusValue(directoryState);
  const transport=useRef(),session=useRef(),reader=useRef(),epoch=useRef(0),transaction=useRef(),baseline=useRef(),dirtyRef=useRef(false),sessionId=useRef(),editorOwner=useRef();
  if(!sessionId.current)sessionId.current=crypto.getRandomValues(new Uint8Array(16));dirtyRef.current=dirty;
  useEffect(()=>{const protect=event=>{if(dirty||pending){event.preventDefault();event.returnValue='';}};window.addEventListener('beforeunload',protect);return()=>window.removeEventListener('beforeunload',protect);},[dirty,pending]);
  useEffect(()=>{setTargets({header:document.getElementById('geocaching-connection-controls'),map:document.querySelector('.map-panel')});return()=>{epoch.current++;reader.current?.cancel();session.current?.close();transport.current?.close();};},[]);
  useEffect(()=>{document.body.dataset.geoMode=mode;return()=>{delete document.body.dataset.geoMode;};},[mode]);
  useEffect(()=>{
    const active=picking&&detail?.editable&&!busy&&!pending&&mode!=='browse';
    document.body.dataset.geoPick=active?'true':'false';
    const select=event=>{if(!active)return;const {latitude,longitude}=event.detail??{};if(!Number.isFinite(latitude)||!Number.isFinite(longitude)||Math.abs(latitude)>90)return;
      const lon=((Math.round(longitude*1e7)+1800000000)%3600000000+3600000000)%3600000000-1800000000;
      setForm(previous=>({...previous,latitude:String(Math.round(latitude*1e7)/1e7),longitude:String(lon/1e7)}));setDirty(true);setPicking(false);};
    window.addEventListener('trailmate:geo-coordinate',select);return()=>{window.removeEventListener('trailmate:geo-coordinate',select);delete document.body.dataset.geoPick;};
  },[picking,detail?.editable,busy,pending,mode]);
  useEffect(()=>{let coordinate=null;if(mode!=='browse'&&detail&&form){try{const value=draftEditorValue({...form,name:'',description:'',hint:''});if(value.latitudeE7!==null)coordinate={latitude:value.latitudeE7/1e7,longitude:value.longitudeE7/1e7};}catch{}}
    window.dispatchEvent(new CustomEvent('trailmate:geo-preview',{detail:coordinate}));
  },[form?.latitude,form?.longitude,detail?.draft.id,mode]);
  async function disconnect(){epoch.current++;reader.current?.cancel();session.current?.close();await transport.current?.close();reader.current=session.current=transport.current=undefined;setConnected(false);setAuthor(undefined);setDirectories([]);setDirectoryTarget('');setDirectoryStatus({key:'directoryDisconnected'});setRows([]);setPage(undefined);if(!transaction.current&&!dirtyRef.current)setDetail(undefined);setBusy(false);setStatus({key:'disconnectedEdits'});}
  async function connect(){
    let port;try{port=await navigator.serial.requestPort();}catch(error){setStatus(error.name==='NotFoundError'?{key:'noDevice'}:geocachingMaintenanceFailure(error));return;}
    const token=++epoch.current;setBusy(true);
    try{
      const connection=new HostlinkConnection(port);transport.current=connection;
      connection.subscribe(event=>{if(token!==epoch.current)return;if(event.type==='state'&&event.state==='disconnected'){epoch.current++;reader.current?.cancel();setConnected(false);setAuthor(undefined);setDirectories([]);setDirectoryTarget('');setDirectoryStatus({key:'directoryDisconnected'});setRows([]);setPage(undefined);if(!transaction.current&&!dirtyRef.current)setDetail(undefined);setBusy(false);setStatus({key:'disconnectedVerify'});}});
      await connection.open();let management=new ManagementSession(connection,{sessionId:sessionId.current});session.current=management;
      const caps=await management.request(OPERATION.CAPABILITIES,[]);
      if(!Array.isArray(caps)||caps.length!==6||!Array.isArray(caps[4])||![1,2,3,64,65].every(op=>caps[4].includes(op)))throw Error('GEO_FIRMWARE_UNSUPPORTED');
      let identity=await proveDeviceAuthor(management);if(token!==epoch.current)return;
      // A retained operation owns its original correlation session, even when
      // another device was used in this browser between connections.
      const retained=loadPublicationCheckpoint(identity.publicKey),retainedSave=loadDraftSave(identity.publicKey);
      const legacyActive=retained&&['prepared','submitting','admitted','stopping','failed','unknown'].includes(retained.stage);
      if(retainedSave&&legacyActive&&!equal(retainedSave.owner,retained.owner))throw Object.assign(Error('GEO_SAVE_OWNER_CONFLICT'),{code:'GEO_SAVE_OWNER_CONFLICT'});
      const publication=loadIPPublication(identity.publicKey),recoveryOptions={capabilities:connection.hello.capabilities,operations:caps[4]};
      const signatureOwner=await recoverSignatureOwner(management,publication,identity,recoveryOptions)??await recoverConfirmationOwner(management,publication,identity,recoveryOptions);
      const legacyOwner=retainedSave?.owner??retained?.owner;
      if(signatureOwner&&legacyOwner&&!equal(signatureOwner,legacyOwner))throw Object.assign(Error('GEO_SAVE_OWNER_CONFLICT'),{code:'GEO_SAVE_OWNER_CONFLICT'});
      const retainedOwner=await deviceUsbOwner(identity,{preferred:signatureOwner??legacyOwner});
      if(!equal(retainedOwner,sessionId.current)){
        management.close();sessionId.current=retainedOwner.slice();management=new ManagementSession(connection,{sessionId:sessionId.current});session.current=management;
        const verified=await proveDeviceAuthor(management);if(!equal(verified.publicKey,identity.publicKey))throw Error('GEO_RECOVERY_AUTHOR_CHANGED');identity=verified;
      }
      if(token!==epoch.current)return;
      reader.current=new DeviceDraftReader(management);setOperations(caps[4]);setAuthor(identity);setDirectoryStatus({key:'directoryConnected'});setConnected(true);
      if(retainedSave){transaction.current=retainedSave;editorOwner.current=identity.publicKey.slice();const draft={...retainedSave.fields,id:retainedSave.id.slice(),generation:retainedSave.expected,authorKey:retainedSave.authorKey.slice(),baseHash:null};baseline.current=draft;setDetail({draft,editable:true});setForm(draftEditorForm(draft));setDirty(true);setPending(true);setPicking(false);setMode('drafts');setStatus({key:'recovery',recovery:retainedSave.authorGeneration===identity.generation?'restored':'identityChanged'});}
      else{const resolvedElsewhere=transaction.current&&equal(transaction.current.authorKey,identity.publicKey);if(resolvedElsewhere){transaction.current=undefined;setPending(false);setDirty(true);}setDetail(current=>current?{...current,editable:Boolean(editorOwner.current&&equal(editorOwner.current,identity.publicKey)&&(current.draft.authorKey===null||equal(current.draft.authorKey,identity.publicKey)))}:current);setStatus(resolvedElsewhere?{key:'recovery',recovery:'stale'}:{key:'authorVerified'});}
    }catch(error){if(token===epoch.current){
      const closing=transport.current,failure=recoveryFailure(error);epoch.current++;
      session.current?.close();await closing?.close();
      if(transport.current===closing){reader.current=session.current=transport.current=undefined;setConnected(false);setAuthor(undefined);setBusy(false);setStatus(failure);}
    }}
    finally{if(token===epoch.current)setBusy(false);}
  }
  async function load(append=false){
    if(pending)return;
    if(!append&&dirty&&!confirm(statusText.discardRefresh))return;
    if(!reader.current||!author||busy)return;const token=epoch.current;setBusy(true);setStatus({key:'reading'});
    try{
      const next=await reader.current.list(author,append?{offset:page.nextOffset,catalogGeneration:page.catalogGeneration}:{ });if(token!==epoch.current)return;
      setRows(previous=>append?[...previous,...next.rows]:next.rows);setPage(next);if(!append){setDetail(undefined);setDirty(false);}
      setStatus({key:'localRecords'});
    }catch(error){if(token===epoch.current)setStatus(geocachingMaintenanceFailure(error));}
    finally{if(token===epoch.current)setBusy(false);}
  }
  async function loadDirectories(){
    if(!connected||!reader.current||busy||pending)return;const token=epoch.current;setBusy(true);
    setDirectoryStatus({key:'directoryWaiting'});
    try{const available=await reader.current.directories({waitForReadyTarget:true,retryMs:500});if(token!==epoch.current)return;
      setDirectories(available);setDirectoryTarget(previous=>available.some(row=>row.capabilitiesVerified&&hex(row.delivery)===previous)?previous:hex(available.find(row=>row.capabilitiesVerified).delivery));
      setDirectoryStatus({key:'directoryObserved'});
    }catch(error){if(token===epoch.current)setDirectoryStatus(geocachingMaintenanceFailure(error));}
    finally{if(token===epoch.current)setBusy(false);}
  }
  async function open(row){
    if(pending)return;
    if(dirty&&!confirm(statusText.discardOpen))return;
    if(!reader.current||!author||busy)return;const token=epoch.current;setBusy(true);
    try{const result=await reader.current.read(row.id,author);if(token!==epoch.current)return;editorOwner.current=author.publicKey.slice();setDetail(result);setForm(draftEditorForm(result.draft));baseline.current=result.draft;setDirty(false);setStatus({key:result.editable?'draftLoaded':'previousReadOnly'});}
    catch(error){if(token===epoch.current)setStatus(geocachingMaintenanceFailure(error));}
    finally{if(token===epoch.current)setBusy(false);}
  }
  function edit(key,value){const next={...form,[key]:value};setForm(next);try{setDirty(JSON.stringify(draftEditorValue(next))!==JSON.stringify(draftEditorValue(draftEditorForm(baseline.current))));}catch{setDirty(true);}}
  function closeDraft(){if(pending)return;if(dirty&&!confirm(statusText.discardClose))return;setDetail(undefined);setDirty(false);setPicking(false);}
  function createDraft(){
    if(!connected||!author||busy||pending)return;
    if(dirty&&!confirm(statusText.discardNew))return;
    const draft={id:crypto.getRandomValues(new Uint8Array(16)),generation:0,authorKey:null,baseHash:null,name:'',description:'',hint:'',state:0,
      latitudeE7:null,longitudeE7:null,difficultyX2:2,terrainX2:2,containerSize:0};
    editorOwner.current=author.publicKey.slice();baseline.current=draft;setDetail({draft,editable:true});setForm(draftEditorForm(draft));setDirty(true);setMode('drafts');setPicking(false);
    setStatus({key:'draftNew'});
  }
  async function save(fields){
    if(!connected||!author||!detail?.editable||busy)return;
    setPicking(false);
    if(transaction.current&&(transaction.current.authorGeneration!==author.generation||!equal(transaction.current.authorKey,author.publicKey))){setStatus({key:'saveAuthorChanged'});return;}
    const proposed=transaction.current??{transferId:crypto.getRandomValues(new Uint8Array(16)),fields,id:detail.draft.id.slice(),expected:detail.draft.generation,authorKey:author.publicKey.slice(),authorGeneration:author.generation,owner:session.current.id};
    const token=epoch.current;setBusy(true);
    try{
      await withDraftSaveLock(author.publicKey,async()=>{
        if(token!==epoch.current)return;
        const tx=transaction.current?assertDraftSaveRetained(proposed):retainDraftSave(proposed);transaction.current=tx;setPending(true);
        const saved=await reader.current.save(tx.id,tx.fields,tx.expected,author,{transferId:tx.transferId,onProgress:p=>{if(token===epoch.current)setStatus(p.phase==='committing'?{key:'saveWaiting'}:{key:'uploading',values:{offset:p.offset,total:p.total}});}});
        if(token!==epoch.current)return;clearDraftSave(tx);
        const next={...detail.draft,...tx.fields,generation:saved.generation};setDetail({...detail,draft:next});setForm(draftEditorForm(next));baseline.current=next;setDirty(false);setPending(false);transaction.current=undefined;setPage(undefined);
        setRows(previous=>previous.some(row=>equal(row.id,next.id))?previous.map(row=>equal(row.id,next.id)?{...row,...tx.fields,generation:saved.generation}:row):[...previous,{...next,editable:true,publication:{latestRevision:0,confirmedRevision:0,pending:false,stopped:false,localChanges:false,baseRetained:false}}]);
        setStatus({key:'saved'});
      });
    }catch(error){if(token===epoch.current){const known=geocachingSaveRecoveryError(error);setStatus(known?recoveryFailure(error):{key:'saveUnconfirmed',detail:error.message});}}
    finally{if(token===epoch.current)setBusy(false);}
  }
  async function abort(){
    if(!transaction.current||busy||!connected)return;const token=epoch.current;setBusy(true);
    const tx=transaction.current;
    try{await withDraftSaveLock(tx.authorKey,async()=>{if(!author?.verified||!equal(author.publicKey,tx.authorKey)||author.generation!==tx.authorGeneration){setStatus({key:'recovery',recovery:'identityChanged'});return;}assertDraftSaveRetained(tx);const reply=await session.current.request(OPERATION.TRANSFER_ABORT,[tx.transferId]);if(!Array.isArray(reply)||reply.length)throw Error('GEO_CANCEL_RECEIPT_INVALID');if(token!==epoch.current)return;clearDraftSave(tx);transaction.current=undefined;setPending(false);setStatus({key:'uploadCancelled'});});}
    catch(error){if(token===epoch.current)setStatus(geocachingSaveRecoveryError(error)?recoveryFailure(error):{key:'cancelUnconfirmed',...(error.message==='GEO_CANCEL_RECEIPT_INVALID'?{detailKey:'cancelReceiptInvalid'}:{detail:error.message})});}
    finally{if(token===epoch.current)setBusy(false);}
  }
  async function querySave(){
    const tx=transaction.current;if(!tx||busy||!connected||!author?.verified||!equal(author.publicKey,tx.authorKey))return;
    const token=epoch.current;setBusy(true);setStatus({key:'recovery',recovery:'querying'});
    try{await withDraftSaveLock(tx.authorKey,async()=>{assertDraftSaveRetained(tx);
      let snapshot;try{snapshot=await reader.current.read(tx.id,author);}catch(error){if(error.code===10){if(token===epoch.current)setStatus({key:'recovery',recovery:'notConfirmed'});return;}throw error;}
      if(token!==epoch.current)return;if(!draftSaveSnapshotMatches(tx,snapshot,author)){setStatus({key:'recovery',recovery:'notConfirmed'});return;}
      clearDraftSave(tx);transaction.current=undefined;setDetail(snapshot);setForm(draftEditorForm(snapshot.draft));baseline.current=snapshot.draft;setDirty(false);setPending(false);setPage(undefined);setStatus({key:'recovery',recovery:'confirmed'});
    });}catch(error){if(token===epoch.current)setStatus(recoveryFailure(error));}finally{if(token===epoch.current)setBusy(false);}
  }
  const visible=rows.filter(row=>mode==='owned'?row.publication.latestRevision>0:mode==='drafts'?row.publication.latestRevision===0||row.publication.localChanges:true);
  return <>
    <nav className="geo-maintenance-tabs" aria-label={text.modes}>
      {[['browse',text.browse],['owned',text.owned],['drafts',text.drafts]].map(([key,label])=><button key={key} aria-pressed={mode===key} onClick={()=>{setMode(key);setPicking(false);if(key==='browse'&&!dirty&&!pending)setDetail(undefined);}}>{label}</button>)}
    </nav>
    {targets?.header&&createPortal(<div className="geo-usb"><span role="status">{connected?text.usbVerified:text.usbDisconnected}</span><Button type="primary" disabled={busy||!('serial' in navigator)} onClick={connected?disconnect:connect}>{connected?text.disconnect:text.connect}</Button></div>,targets.header)}
    {mode!=='browse'&&<section className="geo-maintenance-list">
      <Card color="app-yellow"><h2>{mode==='owned'?text.ownedTitle:text.draftsTitle}</h2><p>{status}</p>{author&&<><label>{text.fingerprint}</label><code className="geo-fingerprint" dir="ltr">{author.fingerprint}</code></>}</Card>
      {!('serial' in navigator)&&<p role="alert">{text.noSerial}</p>}
      <div className="geo-maintenance-actions"><Button disabled={!connected||busy||pending} onClick={()=>load()}>{text.refresh}</Button><Button disabled={!connected||busy||pending||publicationLocked||!operations.includes(OPERATION.CACHE_DRAFT_PUT)} onClick={createDraft}>{text.new}</Button>{busy&&<Button onClick={()=>reader.current?.cancel()}>{pending||publicationLocked?text.pause:text.cancelRead}</Button>}</div>
      {pending&&<div className="geo-maintenance-actions"><Button disabled={!connected||busy||!author?.verified||!equal(author.publicKey,transaction.current?.authorKey)} onClick={querySave}>{recoveryText.query}</Button></div>}
      <PublicationPanel reader={reader} author={author} connected={connected} operations={operations} detail={detail} dirty={dirty||pending} directory={directories.find(row=>hex(row.delivery)===directoryTarget)} busy={busy||pending} epoch={epoch} owner={sessionId} onBusy={setBusy} onLock={setPublicationLocked} onConfirmed={id=>{if(detail&&!dirty&&equal(detail.draft.id,id)){setDetail(undefined);setDirty(false);setPicking(false);}setRows([]);setPage(undefined);}} onCatalog={next=>{setRows(next.rows);setPage(next);}}/>
      <p role="status">{label('loaded',{count:rows.length})}{page&&label('known',{count:page.knownCount})}</p>
      <ol className="results">{visible.map(row=><li key={hex(row.id)}><button className="geo-draft-row" disabled={busy||!connected||pending} onClick={()=>open(row)}><strong><bdi>{row.name||text.untitled}</bdi></strong><span>{row.editable?text.currentAuthor:text.previousAuthor}</span><small>{label('recordVersions',{generation:row.generation,revision:row.publication.confirmedRevision})}</small>{row.publication.pending&&<small>{text.publicationPending}</small>}</button></li>)}</ol>
      {page?.hasMore&&<Button disabled={busy||!connected} onClick={()=>load(true)}>{text.more}</Button>}
      {page&&!visible.length&&<p>{text.empty}</p>}
    </section>}
    {targets?.map&&mode!=='browse'&&detail&&form&&createPortal(<Card className="geo-draft-detail"><Button disabled={busy||pending} onClick={closeDraft}>{text.close}</Button><h2><bdi>{detail.draft.name||text.untitled}</bdi></h2><code dir="ltr">{hex(detail.draft.id)}</code><Button disabled={!detail.editable||busy||pending||publicationLocked} onClick={()=>setPicking(value=>!value)}>{picking?text.cancelPicking:text.chooseCoordinate}</Button>{picking&&<p role="status">{text.clickMap}</p>}{publicationLocked&&<p role="status">{text.publicationLocked}</p>}<DraftEditor value={form} onChange={edit} editable={detail.editable&&!publicationLocked} busy={busy} pending={pending} generation={detail.draft.generation} canSave={!publicationLocked&&connected&&detail.editable&&(!pending||transaction.current?.authorGeneration===author?.generation)&&[32,33,35,36,66].every(op=>operations.includes(op))&&(dirty||pending)} canAbort={connected&&detail.editable&&transaction.current?.authorGeneration===author?.generation} onSave={save} onAbort={abort}/></Card>,targets.map)}
  </>;
}
