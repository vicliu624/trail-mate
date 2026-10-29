import React,{useEffect,useMemo,useRef,useState} from 'react';
import {createRoot} from 'react-dom/client';
import {Background,Button,Card,Title} from 'animal-island-ui';
import L from 'leaflet';
import {createTrackMap} from './leaflet-map.js';
import 'animal-island-ui/style';
import 'leaflet/dist/leaflet.css';
import '../../styles.css';
import './tracks.css';
import {parseKml,descriptionContent,KML_LIMIT} from './kml.js';
import {summarizeKml} from './kml-summary.js';
import {editKmlMetadata,editKmlFeature,addKmlWaypoint,removeKmlFeature,editKmlCoordinates,kmlGeometryEditors} from './kml-edit.js';
import {listTrackDrafts,readTrackDraft,saveTrackDraft,removeTrackDraft} from './draft-store.js';
import {verifyTrackManifest} from './manifest.js';
import {regionDisplayGeometry} from '../../geocaching/src/country-boundaries.js';
import {createKmlStyleResolver} from './kml-style.js';
import {createKmlBalloonPopup} from './kml-balloon-popup.js';
import {collectScreenOverlays} from './kml-screen-overlay.js';
import {addScreenOverlayLayers} from './kml-screen-overlay-layer.js';
import {formatScreenOverlayIssues} from '../../i18n/kml-screen-labels.js';
import {KmlScreenOverlayEditor} from './kml-screen-overlay-editor.jsx';
import {KmlPhotoOverlayEditor} from './kml-photo-overlay-editor.jsx';
import {collectPhotoOverlays} from './kml-photo-preview.js';
import {KmlPhotoBrowser,addPhotoMarkers} from './kml-photo-viewer.jsx';
import {kmlOriginalText} from '../../i18n/kml-original-labels.js';
import {recordingOriginalText} from '../../i18n/recording-original-labels.js';
import {originalFormat} from './original-format.js';
import {convertRecording} from './recording-convert.js';
import {convertGpx} from './gpx-convert.js';
import {DeviceRecords} from './device-records.jsx';
import {ManagementPageProvider,initializeManagementPage,useManagementLanguage} from '../../i18n/management-language.jsx';
import {SiteHeader} from '../../site-header.jsx';
import {useKmlWorkspaceText,useKmlActionText} from '../../i18n/management-copy.jsx';
import {trackDraftLockText} from '../../i18n/track-draft-lock-labels.js';
import {KmlElevationProfile} from './kml-elevation-profile.jsx';
import {KmlTimeline} from './kml-timeline.jsx';
import {KmlTimeFilter,useKmlTimeFilter} from './kml-time-filter.jsx';
import {kmlTimeMatches,kmlTimeFeatureVisible,filterKmlTimeGeometry,kmlProfilePointVisible} from './kml-time.js';
import {kmlTimelineText} from '../../i18n/kml-timeline-labels.js';
import {configElevationText} from '../../i18n/kml-elevation-labels.js';
import {KmlLineOperations} from './kml-line-operations.jsx';
import {transformKmlLine} from './kml-line-operations.js';
import {transformKmlTrack} from './kml-track-operations.js';
import {transformKmlSegments} from './kml-segment-operations.js';
import {KmlSimplify} from './kml-simplify.jsx';
import {simplifyKmlGeometry} from './kml-simplify.js';
import {kmlSimplifyText} from '../../i18n/kml-simplify-labels.js';
import {TrackDraftDetails as TrackAttachments} from './track-draft-details.jsx';
import {MAX_TRACK_ARCHIVE_BYTES,readTrackPackageArchive} from './package-archive.js';
import {trackArchiveText} from '../../i18n/track-archive-labels.js';
import {KmlArchiveImport} from './kml-archive-import.jsx';
import {kmlArchiveText} from '../../i18n/kml-archive-labels.js';
import {DraftBackupControls} from './draft-backup-controls.jsx';
import {useKmlIconResources} from './kml-icon-resources.js';
import {kmlMapIcon} from './kml-map-icon.js';
import {collectGroundOverlays} from './kml-ground-overlays.js';
import {addGroundOverlayLayers} from './kml-ground-overlay-layer.js';
import {formatGroundOverlayIssues} from '../../i18n/kml-ground-overlay-labels.js';
import {KmlGroundOverlayEditor} from './kml-ground-overlay-editor.jsx';
import {KmlStyleEditor} from './kml-style-editor.jsx';
import {KmlTimeEditor} from './kml-time-editor.jsx';
import {KmlRegionEditor} from './kml-region-editor.jsx';
import {KmlRegionPreview,useKmlRegionPreview,fitKmlRegionSource} from './kml-region-preview.jsx';import {kmlRegionAppearance,kmlRegionProfileOpacity,kmlRegionGeometryVisible} from './kml-region.js';
const actionError=error=>({key:({KML_COORDINATE_LINE:'coordinateLine',KML_COORDINATE_RANGE:'coordinateRange',KML_FILE_TOO_LARGE:'fileTooLarge',KML_FILE_TYPE:'fileType',DRAFT_CONFLICT:'conflict',DRAFT_CORRUPT:'corrupt',DRAFT_FULL:'full',DRAFT_NAME_INVALID:'nameInvalid',RECORDING_BINARY_TRUNCATED:'binaryTruncated'}[error.code]??'error'),code:error.code});
function coordinates(points,reference){let previous=reference??points[0]?.longitude??0;return points.map(point=>{let x=point.longitude;while(x-previous>180)x-=360;while(x-previous< -180)x+=360;previous=x;return [point.latitude,x];});}
function Preview({model,draft,selected,onSelect,onPoint,picking,coordinateBounds,profilePoint,operationPreview,styleState='highlight'}){
 const {locale,text}=useKmlWorkspaceText(),unnamed=useRef([]);
 const host=useRef(),map=useRef(),drawing=useRef(),point=useRef(onPoint),pick=useRef(picking),select=useRef(onSelect);
 const groundOverlays=useMemo(()=>collectGroundOverlays(model),[model]),screenOverlays=useMemo(()=>collectScreenOverlays(model),[model]),photoOverlays=useMemo(()=>collectPhotoOverlays(model),[model]),scopeOverlays=useMemo(()=>({overlays:[...groundOverlays.overlays,...screenOverlays.overlays.map(overlay=>({...overlay,index:'screen:'+overlay.index})),...photoOverlays.overlays.map(overlay=>({...overlay,index:'photo:'+overlay.index}))]}),[groundOverlays,screenOverlays,photoOverlays]);
 const [photoIndex,setPhotoIndex]=useState(null);useEffect(()=>setPhotoIndex(null),[model]);
 const [photoTiles,setPhotoTiles]=useState(null);
 const photoReferences=useMemo(()=>{const row=photoOverlays.overlays.find(photo=>photo.index===photoIndex);return !row||row.invalid?[]:row.pyramid?(photoTiles?.index===row.index?photoTiles.refs:[]):row.href?[row.href]:[];},[photoOverlays,photoIndex,photoTiles]);
 const temporal=useKmlTimeFilter(model,scopeOverlays),fittedModel=useRef();
 const iconResources=useKmlIconResources({draft,model,groundOverlays:groundOverlays.overlays,screenOverlays:screenOverlays.overlays,extraReferences:photoReferences});
 const resolveStyle=useMemo(()=>model?createKmlStyleResolver(model,{iconResources,locale}):null,[model,iconResources,locale]);
 const [styleIssues,setStyleIssues]=useState([]);
 const profileMarker=useRef();
 const operationDrawing=useRef();
 point.current=onPoint;pick.current=picking;select.current=onSelect;
 useEffect(()=>{
  const view=createTrackMap(host.current);map.current=view;
  view.createPane('land');view.getPane('land').style.zIndex=200;
  const land=L.geoJSON(null,{pane:'land',renderer:L.canvas({pane:'land'}),interactive:false,style:{color:'#c5d4cb',weight:1,fillColor:'#f4efd8',fillOpacity:.85}}).addTo(view);
  drawing.current=L.featureGroup().addTo(view);operationDrawing.current=L.featureGroup().addTo(view);let countries,cancelled=false;
  const redraw=()=>{if(cancelled||!countries)return;land.clearLayers();land.addData({...countries,features:countries.features.map(feature=>regionDisplayGeometry(feature,view.getCenter().lng))});};
  fetch(new URL('../geocaching/data/countries.json',location.href)).then(response=>{if(!response.ok)throw Error('map');return response.json();}).then(data=>{if(!cancelled){countries=data;redraw();}}).catch(()=>{});
  view.on('moveend',redraw);view.on('click',event=>{if(pick.current){pick.current=false;point.current({latitude:Number(event.latlng.lat.toFixed(7)),longitude:Number((((event.latlng.lng+180)%360+360)%360-180).toFixed(7))});}});
  return()=>{cancelled=true;view.off('moveend',redraw);view.remove();};
 },[]);
 const regions=useKmlRegionPreview(model,scopeOverlays,map),regionVisible=temporal.scopes.features.filter((feature,index)=>kmlTimeFeatureVisible(feature,temporal.range)&&feature.geometries.some(value=>kmlRegionGeometryVisible(filterKmlTimeGeometry(value,temporal.range),regions.opacities[index]))).length;
 const visiblePhotos=useMemo(()=>photoOverlays.overlays.filter(row=>row.visible&&kmlTimeMatches(temporal.scopes.overlays.get('photo:'+row.index),temporal.range)&&regions.overlayOpacity('photo:'+row.index)>0).map(row=>({...row,source:row,opacity:(row.opacity??1)*regions.overlayOpacity('photo:'+row.index)})),[photoOverlays,temporal.scopes,temporal.range,regions.scopes,regions.viewport,regions.enabled]);
 useEffect(()=>{
  const layer=drawing.current;if(!layer)return;layer.clearLayers();unnamed.current=[];if(!model){setStyleIssues([]);return;}
  if(regions.scopes.declared.length)fitKmlRegionSource(map.current,model,{bounds:coordinateBounds},{overlays:[...groundOverlays.overlays,...photoOverlays.overlays.filter(row=>row.point&&row.visible).map(row=>({...row,corners:[row.point]}))]},fittedModel);
  const reference=regions.scopes.declared.length?map.current.getCenter().lng:coordinateBounds?(coordinateBounds.west+coordinateBounds.east+(coordinateBounds.crossesDateLine?360:0))/2:undefined;
  const photoIssues=addPhotoMarkers(layer,visiblePhotos,setPhotoIndex,locale,reference,model,iconResources);
  const indices=new Map(model.features.map((feature,i)=>[feature,i]));
  const styles=model.features.map((feature,index)=>resolveStyle(index,{highlight:selected===index&&styleState==='highlight'}));
  const overlayIssues=addGroundOverlayLayers(layer,{...groundOverlays,overlays:groundOverlays.overlays.filter(overlay=>kmlTimeMatches(temporal.scopes.overlays.get(overlay.index),temporal.range)&&regions.overlayOpacity(overlay.index)>0).map(overlay=>({...overlay,opacity:overlay.opacity*regions.overlayOpacity(overlay.index)}))},iconResources);
  const screenIssues=addScreenOverlayLayers(layer,{overlays:screenOverlays.overlays.filter(overlay=>kmlTimeMatches(temporal.scopes.overlays.get('screen:'+overlay.index),temporal.range)&&regions.overlayOpacity('screen:'+overlay.index)>0).map(overlay=>({...overlay,opacity:overlay.opacity*regions.overlayOpacity('screen:'+overlay.index)}))},iconResources);
  setStyleIssues([...new Set([...styles.flatMap(style=>style.issues),...photoIssues,...formatGroundOverlayIssues([...groundOverlays.issues.filter(issue=>issue.code!=='scope'),...overlayIssues],locale),...formatScreenOverlayIssues([...screenOverlays.issues,...screenIssues],locale)])]);
  function geometry(value,index){
   if(value.geometries){value.geometries.forEach(child=>geometry(child,index));return;}
   const opacity=regions.opacities[index];if(value.type==='Point'&&opacity<=.5)return;const style=kmlRegionAppearance(styles[index],opacity);let item;
   if(value.rings)item=L.polygon(value.rings.map(ring=>coordinates(ring.points,reference)),style.polygon);
   else if(value.points?.length){const icon=value.type==='Point'&&kmlMapIcon(style);item=value.type==='Point'?(icon?L.marker(coordinates(value.points,reference)[0],{icon}):L.circleMarker(coordinates(value.points,reference)[0],style.point)):L.polyline(coordinates(value.points,reference),style.line);}
   if(item){if(style.label.scale>0){const label=window.document.createElement('span');label.textContent=model.features[index].name||text.unnamedFeature;if(!model.features[index].name)unnamed.current.push(label);label.style.color=style.label.color;label.style.opacity=String(style.label.opacity);label.style.fontSize=String(12*style.label.scale)+'px';item.bindTooltip(label);}item.on('click',event=>{select.current(index);map.current.closePopup();const content=createKmlBalloonPopup(model,index,resolveStyle(index,{highlight:styleState==='highlight'}).sourceStyle.balloon,locale);if(content)L.popup({maxWidth:360,maxHeight:360}).setLatLng(event.latlng).setContent(content).openOn(map.current);});item.addTo(layer);}
  }
  function walk(node,visible=true){visible&&=node.visible!==false;if(node.children)node.children.forEach(child=>walk(child,visible));else if(visible&&node.geometries&&regions.opacities[indices.get(node)]>0&&kmlTimeFeatureVisible(temporal.scopes.features[indices.get(node)],temporal.range))node.geometries.forEach(value=>{const filtered=filterKmlTimeGeometry(value,temporal.range);if(filtered)geometry(filtered,indices.get(node));});}
  model.tree.forEach(node=>walk(node));host.current.dataset.regionFeatureCount=String(regionVisible);host.current.dataset.timeFeatureCount=String(temporal.scopes.features.filter(feature=>kmlTimeFeatureVisible(feature,temporal.range)).length);const bounds=layer.getBounds();if(bounds.isValid()&&fittedModel.current!==model){host.current.dataset.longitudeSpan=String(bounds.getEast()-bounds.getWest());map.current.fitBounds(bounds,{padding:[30,30],maxZoom:16});fittedModel.current=model;}
 },[model,selected,coordinateBounds,resolveStyle,groundOverlays,screenOverlays,photoOverlays,visiblePhotos,iconResources,locale,styleState,temporal.scopes,temporal.range,regions.scopes,regions.opacities,regions.viewport,regions.enabled]);
 useEffect(()=>{for(const label of unnamed.current)label.textContent=text.unnamedFeature;},[text.unnamedFeature]);
 useEffect(()=>{map.current?.closePopup();},[model,styleState,locale]);
 useEffect(()=>{const layer=operationDrawing.current;if(!layer)return;layer.clearLayers();delete host.current.dataset.previewPoints;if(operationPreview&&model){const reference=coordinateBounds?(coordinateBounds.west+coordinateBounds.east+(coordinateBounds.crossesDateLine?360:0))/2:undefined;L.polyline(coordinates(operationPreview.points,reference),{color:'#8c64ac',weight:4,dashArray:'8 8',interactive:false}).addTo(layer);host.current.dataset.previewPoints=String(operationPreview.after);}},[operationPreview,model,coordinateBounds]);
 useEffect(()=>{profileMarker.current?.remove();profileMarker.current=null;if(profilePoint&&model&&map.current&&kmlRegionProfileOpacity(profilePoint,regions)>0&&kmlProfilePointVisible(profilePoint,temporal.scopes,temporal.range)){const value=profilePoint.point,longitude=value.longitude+360*Math.round((map.current.getCenter().lng-value.longitude)/360);profileMarker.current=L.circleMarker([value.latitude,longitude],{radius:7,color:'#a57444',fillOpacity:1}).addTo(map.current);host.current.dataset.profileLatitude=String(value.latitude);host.current.dataset.profileLongitude=String(value.longitude);}else{delete host.current.dataset.profileLatitude;delete host.current.dataset.profileLongitude;}},[profilePoint,model,temporal.scopes,temporal.range,regions.opacities]);
 return <div><div id="tracks-workspace-map" ref={host} className={'tracks-map'+(picking?' picking':'')} aria-label={text.map}/><small>{text.mapCredit}{model&&profilePoint&&<> · {Number.isFinite(profilePoint.time)?kmlTimelineText(locale).title:configElevationText(locale).title}</>}</small><KmlTimeFilter {...temporal} visibleCount={regionVisible}/><KmlRegionPreview {...regions}/><KmlPhotoBrowser photos={visiblePhotos} selected={photoIndex} onSelect={setPhotoIndex} resources={iconResources} issues={photoOverlays.issues} onReferences={setPhotoTiles}/>{operationPreview&&<p className="tracks-help">{kmlSimplifyText(locale).mapHelp}</p>}{styleIssues.length>0&&<div className="tracks-help" role="status">{styleIssues.map(issue=><p key={issue}>{issue}</p>)}</div>}</div>;
}
function App(){
 const language=useManagementLanguage();
 const {locale,text}=useKmlWorkspaceText(),number=new Intl.NumberFormat(locale);
 const {text:actions}=useKmlActionText();
 const fixed=(value,digits)=>new Intl.NumberFormat(locale,{minimumFractionDigits:digits,maximumFractionDigits:digits}).format(value);
 const format=(key,values)=>text[key].replace(/\{(\w+)\}/g,(token,name)=>String(values[name]??token));
 const file=useRef(),history=useRef([]),geometrySelection=useRef(null);
 const [sharedDetail,setSharedDetail]=useState(null);
 const [profilePoint,setProfilePoint]=useState(null);
 const [operationPreview,setOperationPreview]=useState(null);
 const [attachmentDirty,setAttachmentDirty]=useState(false),[attachmentBusy,setAttachmentBusy]=useState(false);
 const [overlayPending,setOverlayPending]=useState(false);
 const [stylePending,setStylePending]=useState(false),[styleState,setStyleState]=useState('highlight');
 const [timePending,setTimePending]=useState(false);
 const [regionPending,setRegionPending]=useState(false);
 const [screenPending,setScreenPending]=useState(false);
 const [photoPending,setPhotoPending]=useState(false);
 const editorPending=overlayPending||stylePending||timePending||regionPending||screenPending||photoPending;
 const [rows,setRows]=useState([]),[current,setCurrent]=useState(),[model,setModel]=useState(),[dirty,setDirty]=useState(false),[busy,setBusy]=useState(false),[message,setMessage]=useState({key:'initial'}),[selected,setSelected]=useState(0),[picking,setPicking]=useState(false);
 const [title,setTitle]=useState(''),[name,setName]=useState(''),[description,setDescription]=useState(''),[visible,setVisible]=useState(true),[geometry,setGeometry]=useState(0),[coordinateText,setCoordinateText]=useState('');
 const editors=useMemo(()=>model?.features[selected]?kmlGeometryEditors(model.source,selected):[],[model,selected]),summary=useMemo(()=>model?summarizeKml(model):null,[model]);
 const item=model?.features[selected];
 const featurePending=Boolean(item&&(name!==item.name||description!==item.description||visible!==item.visible||editors[geometry]&&coordinateText!==editors[geometry].coordinates));
 const pending=featurePending||editorPending;
 async function refresh(){try{setRows(await listTrackDrafts());}catch(error){setMessage(actionError(error));}}
 useEffect(()=>{refresh();},[]);
 useEffect(()=>{const protect=event=>{if(dirty||pending||operationPreview||attachmentDirty||attachmentBusy){event.preventDefault();event.returnValue='';}};window.addEventListener('beforeunload',protect);return()=>window.removeEventListener('beforeunload',protect);},[dirty,pending,operationPreview,attachmentDirty,attachmentBusy]);
 useEffect(()=>{const path=geometrySelection.current;geometrySelection.current=null;const found=path?editors.findIndex(editor=>JSON.stringify(editor.path)===JSON.stringify(path)):0,index=Math.max(0,found);setName(item?.name??'');setDescription(item?.description??'');setVisible(item?.visible??true);setGeometry(index);setCoordinateText(editors[index]?.coordinates??'');},[model,selected]);
 function load(row,source){const parsed=parseKml(source);history.current=[];setCurrent(row);setModel(parsed);setTitle(row.name);setSelected(0);setDirty(false);setPicking(false);}
 function selectFeature(index){if(busy||editorPending||index===selected)return;if(featurePending&&!confirm(actions.switchFeature))return;setSelected(index);}
 async function choose(row){if(attachmentBusy)return;if((dirty||pending||attachmentDirty)&&!confirm(actions.openDraft))return;setBusy(true);try{const loaded=await readTrackDraft(row.id);load(loaded,loaded.source);setMessage({key:'opened'});}catch(error){setMessage(actionError(error));}finally{setBusy(false);}}
 async function importFile(value,{packageSource}={}){if(!value||busy||attachmentBusy)return false;if((dirty||pending||attachmentDirty)&&!confirm(actions.importFile))return false;setBusy(true);try{
  const extension=value.name.split('.').at(-1).toLowerCase();if(value.size>(extension==='zip'?MAX_TRACK_ARCHIVE_BYTES:KML_LIMIT.bytes))throw Object.assign(Error('轨迹文件超过容量限制'),{code:extension==='zip'?'TRACK_ARCHIVE_SIZE':'KML_FILE_TOO_LARGE'});const input=new Uint8Array(await value.arrayBuffer());
  if(extension==='zip'){const restored=await readTrackPackageArchive(input);parseKml(restored.payload.kml);const saved=await saveTrackDraft(restored.payload.kml,{name:Array.from(restored.verified.record[9]).slice(0,32).join('')||actions.unnamedTrack,filename:'track.kml',packageSource:restored.payload,originalImport:{source:restored.payload.kml,filename:'track.kml'}});const loaded=await readTrackDraft(saved.id);load(loaded,loaded.source);await refresh();setMessage({key:'imported'});return true;}
  const recording=['csv','bin','gpx'].includes(extension);
  if(!recording&&extension!=='kml')throw Object.assign(Error('请选择 KML、GPX、设备 CSV 或设备 BIN 记录'),{code:'KML_FILE_TYPE'});
  const baseName=Array.from(value.name.replace(/\.(kml|gpx|csv|bin)$/i,'')).slice(0,32).join('')||actions.unnamedTrack;
  const converted=extension==='gpx'?convertGpx(input,{title:baseName}):recording?convertRecording(input,{format:extension==='csv'?'csv':'binary',title:baseName}):null;
  const source=converted?.kml??input,parsed=parseKml(source);
  const sharedName=packageSource?(await verifyTrackManifest(packageSource.signed)).record[9]:null;
  const name=Array.from(sharedName||parsed.tree[0]?.name||baseName).slice(0,32).join('')||actions.unnamedTrack;
  const saved=await saveTrackDraft(source,{name,filename:recording?baseName+'.kml':value.name,packageSource,originalImport:{source:input,filename:value.name,format:recording?extension:'kml'}});const loaded=await readTrackDraft(saved.id);load(loaded,loaded.source);await refresh();setMessage({key:recording?'converted':'imported'});
  return true;
 }catch(error){setMessage(error.code==='TRACK_ARCHIVE_INVALID'?{key:'archiveFailed'}:error.code==='TRACK_ARCHIVE_SIZE'?{key:'archiveSize'}:actionError(error));return false;}finally{setBusy(false);}}
 function mutate(work){try{const result=work(candidateSource()),next=result.source??result,parsed=parseKml(next);geometrySelection.current=result.geometryPath??editors[geometry]?.path??null;history.current.push(model.source);while(history.current.length>5||history.current.reduce((size,bytes)=>size+bytes.length,0)>16*1024*1024)history.current.shift();setModel(parsed);setSelected(Math.min(selected,Math.max(0,parsed.features.length-1)));setDirty(true);setMessage({key:'edited'});return {ok:true};}catch(error){setMessage(actionError(error));return {ok:false,error};}}
 function pointsFromText(){return coordinateText.trim().split(/\n+/).map(line=>{const parts=line.trim().split(',');if(parts.length<2||parts.length>3||parts.some(part=>!part.trim()))throw Object.assign(Error('每行需要经度、纬度和可选高度'),{code:'KML_COORDINATE_LINE'});return {longitude:Number(parts[0]),latitude:Number(parts[1]),altitude:parts.length===3?Number(parts[2]):null};});}
 function candidateSource(){let source=model.source;if(item&&(name!==item.name||description!==item.description||visible!==item.visible))source=editKmlFeature(source,selected,{name,description,visible});if(editors[geometry]&&coordinateText!==editors[geometry].coordinates)source=editKmlCoordinates(source,selected,editors[geometry].path,pointsFromText());return title!==current.name?editKmlMetadata(source,{name:title}):source;}
 async function save(){if(!model||!current||busy||editorPending)return;setBusy(true);setPicking(false);try{const source=candidateSource();const saved=await saveTrackDraft(source,{id:current.id,filename:current.filename,name:title,expectedVersion:current.version});const parsed=parseKml(source);geometrySelection.current=editors[geometry]?.path??null;setCurrent(saved);setModel(parsed);setDirty(false);await refresh();setMessage({key:'saved'});}catch(error){setMessage(actionError(error));}finally{setBusy(false);}}
 async function remove(){if(!current||!confirm(actions.deleteDraft))return;setBusy(true);try{await removeTrackDraft(current.id,current.version);setCurrent(undefined);setModel(undefined);setDirty(false);await refresh();setMessage({key:'deleted'});}catch(error){setMessage(actionError(error));}finally{setBusy(false);}}
 function download(){try{const url=URL.createObjectURL(new Blob([candidateSource()],{type:'application/vnd.google-earth.kml+xml'})),anchor=window.document.createElement('a');anchor.href=url;anchor.download=current.filename;anchor.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}catch(error){setMessage(actionError(error));}}
 async function downloadArchiveOriginal(){try{const saved=await readTrackDraft(current.id);if(!saved.originalSource)throw Error('Original missing');const url=URL.createObjectURL(new Blob([saved.originalSource],{type:originalFormat(saved.originalFormat).mime})),anchor=window.document.createElement('a');anchor.href=url;anchor.download='original-'+saved.archiveMember.split(/[\\/]/).at(-1);anchor.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}catch(error){setMessage(actionError(error));}}
 function applyCoordinates(){mutate(source=>editKmlCoordinates(source,selected,editors[geometry].path,pointsFromText()));}
 return <><Background type="sprinkles" aria-hidden="true"/><div className="page-shell tracks-shell">
  <SiteHeader page="tracks"/>
  <div className="content-shell"><aside className="sidebar"><Title color="app-teal" size="middle">{text.mine}</Title><p>{format('drafts',{count:number.format(rows.length)})}</p><Button disabled={busy} onClick={()=>{file.current.value='';file.current.click();}}>{text.import}</Button><input ref={file} hidden type="file" accept=".zip,.kml,.gpx,.csv,.bin,application/vnd.google-earth.kml+xml" onChange={event=>importFile(event.target.files[0])}/><p className="tracks-help">{text.importHelp}</p><p className="tracks-help">{trackArchiveText(locale).importHint}</p>
    <div className="tracks-actions"><KmlArchiveImport disabled={busy||dirty||pending||attachmentDirty||attachmentBusy||Boolean(operationPreview)} onSaved={async saved=>{const loaded=await readTrackDraft(saved.id);load(loaded,loaded.source);await refresh();setMessage({key:'imported'});}}/></div>
    <DraftBackupControls draft={current} disabled={busy||dirty||pending||attachmentDirty||attachmentBusy||Boolean(operationPreview)} onBusy={setBusy} onRestored={async saved=>{const loaded=await readTrackDraft(saved.id);load(loaded,loaded.source);await refresh();setMessage({key:'imported'});}}/>
    <div className="tracks-list">{rows.map(row=><button className={'tracks-row'+(current?.id===row.id?' selected':'')} key={row.id} disabled={busy||attachmentBusy} onClick={()=>choose(row)}><strong><bdi>{row.name}</bdi></strong><small><bdi>{fixed(row.bytes/1024,1)} KiB</bdi> · {format('localVersion',{version:number.format(row.version)})}</small></button>)}</div><p className="tracks-help">{text.draftHelp}</p><DeviceRecords onImport={importFile} onOpenDraft={async row=>{if(busy)return;await choose(row);await refresh();}} disabled={busy||dirty||attachmentDirty||attachmentBusy||pending||Boolean(operationPreview)} draft={current} draftSource={dirty||pending||operationPreview||attachmentDirty||attachmentBusy?null:model?.source} onSharedDetail={setSharedDetail}/></aside>
   <main className="main-content"><Title color="app-teal" size="middle">{current?.name??text.maintenance}</Title><p role="status" className="tracks-notice">{message.code==='TRACK_IP_DRAFT_LOCKED'?trackDraftLockText(locale):<>{message.key==='archiveFailed'?trackArchiveText(locale).failed:message.key==='archiveSize'?trackArchiveText(locale).limit:actions[message.key]}{message.key==='error'&&message.code!=null&&<> · <bdi>{String(message.code)}</bdi></>}</>}</p>
    <div id="tracks-shared-detail"/>
    {(model||!sharedDetail)&&<Preview model={model} draft={current} coordinateBounds={summary?.bounds} selected={selected} onSelect={selectFeature} styleState={styleState} picking={!busy&&!pending&&!attachmentDirty&&!attachmentBusy&&picking} profilePoint={profilePoint} operationPreview={operationPreview} onPoint={point=>{mutate(source=>addKmlWaypoint(source,{...point,name:format('waypoint',{number:number.format(model.features.length+1)})}));setPicking(false);}}/>}
    {model?.features.some(feature=>feature.extendedData.some(data=>data.name==='trailmate.recording.profile'))&&<Card><h3>{text.conversionTitle}</h3><p>{text.conversionHelp}</p>{model.features.flatMap((feature,index)=>feature.extendedData.filter(data=>data.name==='trailmate.recording.warnings').map((data,i)=><p key={`${index}-${i}`} style={{whiteSpace:'pre-line'}}>{data.value}</p>))}</Card>}
    {model&&<><div className="tracks-stats"><span>{format('points',{count:number.format(summary.pointCount),segments:number.format(summary.segments)})}</span><span>{format('distance',{value:summary.distanceM===null?text.unknown:fixed(summary.distanceM/1000,2)+' km'})}</span><span>{format('ascent',{value:summary.ascentM===null?text.unknown:fixed(summary.ascentM,0)+' m'})}</span><span>{format('duration',{value:summary.durationS===null?text.unknown:fixed(summary.durationS,0)+' s'})}</span></div>
     <KmlElevationProfile model={model} onPoint={setProfilePoint}/>
     <KmlTimeline model={model} onPoint={setProfilePoint}/>
     <TrackAttachments draft={current} model={model} disabled={busy||dirty||pending||Boolean(operationPreview)} onDirty={setAttachmentDirty} onBusy={setAttachmentBusy} onSaved={async saved=>{setCurrent(saved);setAttachmentDirty(false);await refresh();setMessage({key:'saved'});}}/>
     {current.originalBytes!==undefined?<><div className="tracks-actions"><Button disabled={busy||attachmentBusy} onClick={downloadArchiveOriginal}>{current.originalFormat?recordingOriginalText(locale).download:kmlArchiveText(locale).original}</Button><small><bdi>{current.archiveMember} · {current.originalBytes<1024?number.format(current.originalBytes)+' B':fixed(current.originalBytes/1024,1)+' KiB'}</bdi></small></div><p className="tracks-help">{current.originalFormat?recordingOriginalText(locale).help:kmlOriginalText(locale).help}</p></>:<p className="tracks-help">{kmlOriginalText(locale).legacy}</p>}
     <div className="tracks-actions"><Button disabled={busy||attachmentDirty||attachmentBusy||editorPending||!dirty&&!pending} type="primary" onClick={save}>{text.save}</Button><Button disabled={busy||attachmentDirty||attachmentBusy||editorPending} onClick={download}>{dirty||pending?text.downloadEdited:text.download}</Button><Button disabled={busy||attachmentDirty||attachmentBusy||editorPending||!history.current.length} onClick={()=>{const source=history.current.pop();setModel(parseKml(source));setDirty(true);}}>{text.undo}</Button><Button disabled={busy||attachmentDirty||attachmentBusy||pending} onClick={()=>setPicking(!picking)}>{picking?text.cancelPicking:text.pickWaypoint}</Button><Button disabled={busy||attachmentDirty||attachmentBusy} onClick={remove}>{text.remove}</Button></div>
     {picking&&<p>{text.pickingHelp}</p>}
     <KmlGroundOverlayEditor key={'overlay:'+current.id} model={model} draft={current} bounds={summary?.bounds} disabled={busy||featurePending||stylePending||timePending||regionPending||screenPending||photoPending||attachmentDirty||attachmentBusy||Boolean(operationPreview)} onApply={mutate} onDirty={setOverlayPending}/>
     <KmlScreenOverlayEditor key={'screen:'+current.id} model={model} draft={current} disabled={busy||featurePending||overlayPending||stylePending||timePending||regionPending||photoPending||attachmentDirty||attachmentBusy||Boolean(operationPreview)} onApply={mutate} onDirty={setScreenPending}/>
     <KmlPhotoOverlayEditor key={'photo:'+current.id} model={model} draft={current} bounds={summary?.bounds} disabled={busy||featurePending||overlayPending||stylePending||timePending||regionPending||screenPending||attachmentDirty||attachmentBusy||Boolean(operationPreview)} onApply={mutate} onDirty={setPhotoPending}/>
     <KmlStyleEditor key={'style:'+current.id} model={model} draft={current} index={selected} onSelect={selectFeature} disabled={busy||featurePending||overlayPending||timePending||regionPending||screenPending||photoPending||attachmentDirty||attachmentBusy||Boolean(operationPreview)} onApply={mutate} onDirty={setStylePending} onState={setStyleState}/>
     <KmlTimeEditor key={'time:'+current.id} model={model} index={selected} onSelect={selectFeature} disabled={busy||featurePending||overlayPending||stylePending||regionPending||screenPending||photoPending||attachmentDirty||attachmentBusy||Boolean(operationPreview)} onApply={mutate} onDirty={setTimePending}/>
     <KmlRegionEditor key={'region:'+current.id} model={model} index={selected} bounds={summary?.bounds} onSelect={selectFeature} disabled={busy||featurePending||overlayPending||stylePending||timePending||screenPending||photoPending||attachmentDirty||attachmentBusy||Boolean(operationPreview)} onApply={mutate} onDirty={setRegionPending}/>
     <fieldset disabled={attachmentDirty||attachmentBusy||editorPending} className="tracks-edit-lock"><Card className="tracks-editor"><h2>{text.editor}</h2><label>{text.title}<input value={title} disabled={busy} onChange={event=>{setTitle(event.target.value);setDirty(true);}}/></label><Button disabled={busy} onClick={()=>mutate(source=>editKmlMetadata(source,{name:title}))}>{text.applyTitle}</Button>
      <label>{text.feature}<select aria-label={text.feature} disabled={busy} value={selected} onChange={event=>selectFeature(Number(event.target.value))}>{model.features.map((feature,index)=><option key={index} value={index}>{feature.path.filter(Boolean).join(' / ')} / {feature.name||text.unnamedFeature} · {number.format(index+1)}</option>)}</select></label>
      {item&&<><div className="tracks-fields"><label>{text.name}<input value={name} disabled={busy} onChange={event=>setName(event.target.value)}/></label><label>{text.visible}<input type="checkbox" checked={visible} disabled={busy} onChange={event=>setVisible(event.target.checked)}/></label></div><label>{text.description}<textarea aria-label={text.description} rows={4} value={description} disabled={busy} onChange={event=>setDescription(event.target.value)}/></label><div className="tracks-actions">
<Button disabled={busy} onClick={()=>mutate(source=>editKmlFeature(source,selected,{name,description,visible}))}>{text.applyFeature}</Button><Button disabled={busy} onClick={()=>{if(confirm(actions.deleteFeature))mutate(source=>removeKmlFeature(source,selected));}}>{text.removeFeature}</Button></div>
       <div className="tracks-description" dangerouslySetInnerHTML={{__html:descriptionContent(item.description).html}}/>
       {editors.length>0&&<><label>{text.geometry}<select aria-label={text.geometry} disabled={busy} value={geometry} onChange={event=>{if(coordinateText!==editors[geometry].coordinates&&!confirm(actions.switchGeometry))return;const index=Number(event.target.value);setGeometry(index);setCoordinateText(editors[index].coordinates);}}>{editors.map((value,index)=><option key={index} value={index}>{number.format(index+1)} · {value.type}</option>)}</select></label><label>{text.coordinates}<textarea dir="ltr" aria-label={text.coordinates} rows={6} value={coordinateText} disabled={busy} onChange={event=>setCoordinateText(event.target.value)}/></label><p className="tracks-help">{text.coordinatesHelp}</p><Button disabled={busy} onClick={applyCoordinates}>{text.applyCoordinates}</Button></>}
       <p className="tracks-help">{format('style',{value:item.styleUrl||text.default})} · {format('timestamp',{value:item.timestamp||text.none})} · {format('extensions',{count:number.format(item.extendedData.length)})}</p>
       {editors[geometry]&&<KmlLineOperations editor={editors[geometry]} disabled={busy||pending} onAction={operation=>mutate(source=>(['earlier','later','merge'].includes(operation.action)?transformKmlSegments:editors[geometry].type==='gx:Track'?transformKmlTrack:transformKmlLine)(source,selected,editors[geometry].path,operation))}/>}
       {editors[geometry]&&<KmlSimplify source={model.source} index={selected} editor={editors[geometry]} disabled={busy||pending} onPreview={setOperationPreview} onApply={tolerance=>mutate(source=>simplifyKmlGeometry(source,selected,editors[geometry].path,tolerance))}/>}
      </>}
     </Card></fieldset>{model.issues.length>0&&<Card><h3>{text.retainedTitle}</h3>{model.issues.map((issue,index)=><p key={index}>{issue.element??issue.type} {issue.name??issue.id??''}</p>)}<p>{text.externalHelp}</p></Card>}
    </>}
   </main></div></div></>;
}
await initializeManagementPage();
createRoot(window.document.getElementById('tracks-app')).render(<ManagementPageProvider><App/></ManagementPageProvider>);
