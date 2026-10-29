import React,{useEffect,useMemo,useRef,useState} from 'react';
import {Button} from 'animal-island-ui';
import L from 'leaflet';
import {createTrackMap} from './leaflet-map.js';
import {useManagementLocale} from '../../i18n/management-copy.jsx';
import {trackBrowseText} from '../../i18n/track-browse-labels.js';
import {regionDisplayGeometry} from '../../geocaching/src/country-boundaries.js';
import {trackId} from './shared-id.js';
import {SharedTrackRegions} from './shared-track-regions.jsx';
import {trackRegionText} from '../../i18n/track-region-labels.js';
import {trackFilterText} from '../../i18n/track-filter-labels.js';
import {trackOwnedText} from '../../i18n/track-owned-labels.js';
import {TrackSearchFilters} from './track-search-filters.jsx';
import {trackSearchText} from '../../i18n/track-search-labels.js';
import {validateTrackEditorMetadata} from './track-editor-metadata.js';
import {trackQueryContinuationText} from '../../i18n/track-query-continuation-labels.js';
import {trackStorageErrorText} from '../../i18n/track-storage-labels.js';
import {trackMultidirectoryText} from '../../i18n/track-multidirectory-labels.js';
const hex=value=>Array.from(value,byte=>byte.toString(16).padStart(2,'0')).join('');
export function SharedTrackBrowse({onQuery,onRegion,onOpen,disabled=false,canUseAuthor=true,authorKey=null,metadataSupported=false,metadataRegionSupported=false,directoryCount=1,sourceScope='',multipleMetadataSupported=false,multipleMetadataRegionSupported=false}){
 const locale=useManagementLocale(),text=useMemo(()=>trackBrowseText(locale),[locale]),number=new Intl.NumberFormat(locale);
 const regionText=useMemo(()=>trackRegionText(locale),[locale]);
 const filterText=useMemo(()=>trackFilterText(locale),[locale]),[filter,setFilter]=useState({mine:false,archived:false,activity:null,tags:[]});
 const ownedText=useMemo(()=>trackOwnedText(locale),[locale]),[allOwned,setAllOwned]=useState(false);
 const ownedMode=useRef(allOwned);ownedMode.current=allOwned;
 const [ownedPage,setOwnedPage]=useState(null);
 const [multiple,setMultiple]=useState(false),[aggregate,setAggregate]=useState(null),[paused,setPaused]=useState(false),multi=trackMultidirectoryText(locale);
 const [publicPage,setPublicPage]=useState(null),continuationText=trackQueryContinuationText(locale);
 const [selected,setSelected]=useState(null);const scope=useRef(null),regionLayer=useRef();scope.current=selected;
 const host=useRef(),map=useRef(),drawing=useRef(),query=useRef(onQuery),open=useRef(onOpen);query.current=onQuery;open.current=chooseResult;
 const [bounds,setBounds]=useState(null),[automatic,setAutomatic]=useState(false),[items,setItems]=useState([]),[loading,setLoading]=useState(false),[notice,setNotice]=useState({key:'intro'});
 const epoch=useRef(0),controller=useRef(),last=useRef('');
 useEffect(()=>{
  const view=createTrackMap(host.current);map.current=view;
  view.createPane('land');view.getPane('land').style.zIndex=200;
  const land=L.geoJSON(null,{pane:'land',renderer:L.canvas({pane:'land'}),interactive:false,style:{color:'#c5d4cb',weight:1,fillColor:'#f4efd8',fillOpacity:.85}}).addTo(view);drawing.current=L.featureGroup().addTo(view);
  let countries,cancelled=false;
  const move=()=>{const box=view.getBounds();setBounds([box.getSouth(),box.getWest(),box.getNorth(),box.getEast()]);if(!scope.current&&!ownedMode.current){epoch.current++;controller.current?.abort();setLoading(false);setPublicPage(null);setAggregate(null);setPaused(false);setItems([]);setNotice({key:'intro'});}if(countries){land.clearLayers();land.addData({...countries,features:countries.features.map(feature=>regionDisplayGeometry(feature,view.getCenter().lng))});}};
  fetch(new URL('../geocaching/data/countries.json',location.href)).then(response=>{if(!response.ok)throw Error('map');return response.json();}).then(data=>{if(!cancelled){countries=data;move();}}).catch(()=>{});
  view.on('moveend',move);move();
  return()=>{cancelled=true;epoch.current++;controller.current?.abort();view.remove();};
 },[]);
 useEffect(()=>{
  const view=map.current;if(!view)return;regionLayer.current?.remove();regionLayer.current=null;
  if(selected){
   const feature=regionDisplayGeometry({type:'Feature',properties:{},geometry:selected.geometry},view.getCenter().lng);
   const layer=L.geoJSON(feature,{interactive:false,style:{color:'#b96828',weight:3,fillOpacity:.06}}).addTo(view);regionLayer.current=layer;
   if(layer.getBounds().isValid())view.fitBounds(layer.getBounds(),{padding:[20,20],maxZoom:12});
  }
 },[selected]);
 function changeScope(next){epoch.current++;controller.current?.abort();setLoading(false);setItems([]);setAggregate(null);setPaused(false);setOwnedPage(null);setPublicPage(null);setAllOwned(false);last.current='';setNotice({key:'intro'});scope.current=next;setSelected(next);}
 const previousSources=useRef(sourceScope);useEffect(()=>{if(previousSources.current!==sourceScope){previousSources.current=sourceScope;changeScope(scope.current);}},[sourceScope]);
 function chooseResult(item){
  if(item.variants?.length>1){setNotice({key:'chooseVersion'});document.querySelector('[data-track-id="'+trackId(item.objectId)+'"]')?.scrollIntoView({block:'center'});return;}
  const version=item.variants?.[0]??item;onOpen?.({...version,sourceDirectory:version.sources?.[0]});
 }
 const previousAuthor=useRef(authorKey);
 useEffect(()=>{
  if(previousAuthor.current===authorKey)return;previousAuthor.current=authorKey;
  if(filter.mine||allOwned){epoch.current++;controller.current?.abort();setLoading(false);setItems([]);setAggregate(null);setPaused(false);setOwnedPage(null);setPublicPage(null);setAllOwned(false);setFilter(previous=>({...previous,mine:false}));last.current='';setNotice({key:'intro'});}
 },[authorKey]);
 useEffect(()=>{
  const layer=drawing.current;if(!layer)return;layer.clearLayers();
  for(const item of items)for(const version of item.variants??[item]){const [west,south,east,north]=version.bounds.map(value=>value/1e7);let right=east<west?east+360:east,left=west;const center=(left+right)/2,reference=map.current.getCenter().lng;const shift=360*Math.round((reference-center)/360);left+=shift;right+=shift;const color=item.multipleVersions||item.summaryDisagreement?'#b96828':'#478e79';
   const marker=L.circleMarker([(south+north)/2,(left+right)/2],{radius:7,color,fillOpacity:.65});
   const shapes=left===right&&south===north?[marker]:[L.rectangle([[south,left],[north,right]],{color,weight:2,fillOpacity:.12}),marker];
   for(const shape of shapes){const label=document.createElement('span');label.textContent=version.title;shape.bindTooltip(label);shape.on('click',()=>open.current?.(item));shape.addTo(layer);}
  }
 },[items,bounds,locale]);
 async function search(wholeOwner=false){
  if(disabled||loading||!bounds)return;
  const whole=wholeOwner===true;
  const retry=wholeOwner==='retry-sources',next=wholeOwner==='next'||retry&&allOwned;if(next&&!(retry?ownedPage?.retry:ownedPage?.cursor))return;
  const publicNext=wholeOwner==='public-next'||retry&&!allOwned;if(publicNext&&!(retry?publicPage?.retry:publicPage?.continuation))return;
  let selectedFilter;try{const labels=validateTrackEditorMetadata({activity:filter.activity??0,tags:filter.tags.filter(tag=>tag.trim().length)});selectedFilter={...filter,tags:labels.tags};}catch{last.current=JSON.stringify(bounds);setNotice({key:'error',code:'TRACK_QUERY_OPTIONS_INVALID'});return;}
  if((whole||next||filter.mine)&&!canUseAuthor)return;
  if(whole){scope.current=null;setSelected(null);setAutomatic(false);setAllOwned(true);setPublicPage(null);setFilter(previous=>({...previous,mine:true}));}else if(!next){setAllOwned(false);setOwnedPage(null);if(!publicNext)setPublicPage(null);}
  const token=++epoch.current,abort=new AbortController();controller.current=abort;last.current=JSON.stringify(bounds);setLoading(true);setPaused(false);setNotice({key:'reading'});let progress;
  const options={signal:abort.signal,filter:whole||next?{...selectedFilter,mine:true}:selectedFilter,ownedPage:whole||next,multiple,retryFailed:retry,continuation:next?(retry?{...ownedPage,continuation:ownedPage.retry}:ownedPage):publicNext?(retry?publicPage.retry:publicPage.continuation):undefined,onProgress:value=>{if(token!==epoch.current||abort.signal.aborted)return;progress=value;setItems(value.items);setAggregate(value);if(whole||next)setOwnedPage(value);else setPublicPage(value);}};
  try{const result=!whole&&!next&&selected?await onRegion({type:'query',region:selected},options):await query.current(whole||next?[-90,-180,90,180]:bounds,options);if(token!==epoch.current||abort.signal.aborted)return;if(!result){setNotice({key:'intro'});return;}setAggregate(result.aggregate?result:null);if(whole||next)setOwnedPage(result);else setPublicPage(result);if(result.aggregate||!publicNext||result.items.length||result.continuation)setItems(result.items);setNotice({key:publicNext&&!result.items.length&&!result.continuation&&!result.truncated?'finished':result.truncated?'limited':result.items.length?'results':'empty'});}
  catch(error){if(token===epoch.current){if(error.name==='AbortError'&&progress){setItems(progress.items);setAggregate(progress);if(whole||next)setOwnedPage(progress);else setPublicPage(progress);setPaused(true);setNotice({key:'intro'});}else{setItems([]);setAggregate(null);setOwnedPage(null);setPublicPage(null);setNotice({key:error.name==='AbortError'?'intro':'error',code:error.code});}}}
  finally{if(token===epoch.current)setLoading(false);}
 }
 useEffect(()=>{
  if(selected||!automatic||disabled||loading||!bounds||last.current===JSON.stringify(bounds))return;
  const timer=setTimeout(()=>search(),800);return()=>clearTimeout(timer);
 },[filter,selected,automatic,disabled,loading,bounds,sourceScope]);
 const storageError=trackStorageErrorText(locale,notice.code);
 return <section className="shared-track-browser" aria-label={text.title}>
  <h3>{text.title}</h3><p className="tracks-help">{text.intro}</p>
  {(directoryCount>1||multiple)&&<><label className="track-multidirectory-toggle"><input type="checkbox" checked={multiple} disabled={disabled||loading} onChange={event=>{changeScope(selected);setMultiple(event.target.checked);}}/>{multi.toggle} · <bdi>{number.format(directoryCount)}</bdi></label>{multiple&&<p className="tracks-help">{multi.intro}</p>}</>}
  <div className="tracks-actions">{['mine','archived'].map(key=><label key={key}><input type="checkbox" checked={filter[key]} disabled={key==='mine'&&!canUseAuthor} onChange={event=>{changeScope(selected);setFilter(previous=>({...previous,[key]:event.target.checked}));}}/>{filterText[key]}</label>)}</div>
  <TrackSearchFilters filter={filter} supported={(multiple?multipleMetadataSupported:metadataSupported)&&(!selected||(multiple?multipleMetadataRegionSupported:metadataRegionSupported))} disabled={disabled} onChange={next=>{changeScope(selected);setFilter(next);}}/>
  <Button disabled={disabled||loading||!bounds||!canUseAuthor} onClick={()=>search(true)}>{ownedText.all}</Button>
  {allOwned&&ownedPage?.cursor&&<Button disabled={disabled||loading} onClick={()=>search('next')}>{ownedText.more}</Button>}
  {onRegion&&<SharedTrackRegions onRead={onRegion} onScope={changeScope} selected={selected} disabled={disabled||loading}/>}
  {selected&&<p><strong><bdi>{selected.names[locale]??selected.name}</bdi></strong> · <code dir="ltr">{selected.id}</code></p>}
  <div ref={host} className="tracks-map shared-track-browse-map" aria-label={text.title}/>
  <div className="tracks-actions"><Button disabled={disabled||loading||!bounds} onClick={search}>{selected?regionText.search:text.search}</Button><label><input type="checkbox" checked={automatic} disabled={Boolean(selected)} onChange={event=>setAutomatic(event.target.checked)}/>{text.auto}</label>{loading&&<Button onClick={()=>controller.current?.abort()}>{text.cancel}</Button>}</div>
  {publicPage?.continuation&&!allOwned&&<Button disabled={disabled||loading} onClick={()=>search('public-next')}>{continuationText.next}</Button>}
  {(allOwned?ownedPage?.retry:publicPage?.retry)&&<Button disabled={disabled||loading} onClick={()=>search('retry-sources')}>{multi.retry}</Button>}
  {aggregate&&<section className="track-query-sources" aria-label={multi.sources}><h4>{multi.sources}</h4>{aggregate.sources.map(source=><p key={source.directory.key}><bdi>{source.directory.name}</bdi> · <code dir="ltr" title={hex(source.directory.delivery)}>{hex(source.directory.delivery).slice(0,8)}</code> · {source.error==='TRACK_MULTIDIRECTORY_REGION_MISMATCH'?multi.catalog:source.error&&/SNAPSHOT|CURSOR/.test(source.error)?multi.stale:source.error&&/UNSUPPORTED/.test(source.error)?multi.unsupported:multi[source.status]}</p>)}{paused&&<p role="status">{multi.paused}</p>}{aggregate.capacityReached?<p role="alert">{multi.budget}</p>:aggregate.partial&&<p role="status">{multi.partial}</p>}</section>}
  {publicPage&&!publicPage.truncated&&!allOwned&&['results','empty'].includes(notice.key)&&<p className="tracks-help">{continuationText.finished}</p>}
  <p role="status">{notice.key==='chooseVersion'?multi.choose:storageError??(notice.code==='TRACK_QUERY_OPTIONS_INVALID'?trackSearchText(locale).invalid:notice.code==='TRACK_QUERY_METADATA_UNSUPPORTED'?trackSearchText(locale).unsupported:['TRACK_QUERY_CURSOR_MISMATCH','TRACK_QUERY_SNAPSHOT_CHANGED'].includes(notice.code)?continuationText.changed:notice.key==='finished'?continuationText.finished:!allOwned&&publicPage?.continuation&&notice.key==='limited'?(items.length?continuationText.pending:continuationText.emptyPending):allOwned&&['results','limited'].includes(notice.key)?multiple&&notice.key==='results'?multi.owned:ownedText[notice.key]:text[notice.key])}{items.length>0&&<> · <bdi>{number.format(items.length)}</bdi></>}{!storageError&&notice.code&&!['TRACK_QUERY_OPTIONS_INVALID','TRACK_QUERY_METADATA_UNSUPPORTED','TRACK_QUERY_CURSOR_MISMATCH','TRACK_QUERY_SNAPSHOT_CHANGED'].includes(notice.code)&&<> · <bdi>{String(notice.code)}</bdi></>}</p><p className="tracks-help">{text.summary}</p>
  <ul className={'shared-track-results'+(aggregate?' track-aggregate-results':'')}>{items.map(item=><li key={trackId(item.objectId)} data-track-id={trackId(item.objectId)}><strong><bdi>{item.title}</bdi></strong><code dir="ltr">{trackId(item.objectId)}</code>{item.multipleVersions&&<p>{multi.versions}</p>}{item.summaryDisagreement&&<p>{multi.disagreement}</p>}{item.variants?item.variants.map((variant,index)=><article className="track-query-variant" key={hex(variant.revisionHash)+':'+index}><strong><bdi>{number.format(variant.revision)} · {variant.title}</bdi></strong><code dir="ltr">{hex(variant.revisionHash)}</code>{variant.sources.map(source=><Button key={source.key} disabled={disabled||loading} onClick={()=>onOpen?.({...variant,sourceDirectory:source})}>{multi.read} · <bdi>{source.name} · {hex(source.delivery).slice(0,8)}</bdi></Button>)}</article>):<Button disabled={disabled||loading} onClick={()=>open.current?.(item)}>{text.open}</Button>}</li>)}</ul>
 </section>;
}
