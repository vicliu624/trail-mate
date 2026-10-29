import React,{useEffect,useMemo,useRef,useState} from 'react';
import L from 'leaflet';
import {createTrackMap} from './leaflet-map.js';
import {parseKml} from './kml.js';
import {summarizeKml} from './kml-summary.js';
import {KmlContentDetails} from './kml-content-details.jsx';
import {createKmlStyleResolver} from './kml-style.js';
import {createKmlBalloonPopup} from './kml-balloon-popup.js';
import {collectScreenOverlays} from './kml-screen-overlay.js';
import {addScreenOverlayLayers} from './kml-screen-overlay-layer.js';
import {formatScreenOverlayIssues} from '../../i18n/kml-screen-labels.js';
import {collectPhotoOverlays} from './kml-photo-preview.js';
import {KmlPhotoBrowser,addPhotoMarkers} from './kml-photo-viewer.jsx';
import {regionDisplayGeometry} from '../../geocaching/src/country-boundaries.js';
import {useKmlWorkspaceText} from '../../i18n/management-copy.jsx';
import {KmlElevationProfile} from './kml-elevation-profile.jsx';
import {KmlTimeline} from './kml-timeline.jsx';
import {KmlTimeFilter,useKmlTimeFilter} from './kml-time-filter.jsx';
import {kmlTimeMatches,kmlTimeFeatureVisible,filterKmlTimeGeometry,kmlProfilePointVisible} from './kml-time.js';
import {kmlTimelineText} from '../../i18n/kml-timeline-labels.js';
import {configElevationText} from '../../i18n/kml-elevation-labels.js';
import {useKmlIconResources} from './kml-icon-resources.js';
import {kmlMapIcon} from './kml-map-icon.js';
import {collectGroundOverlays} from './kml-ground-overlays.js';
import {addGroundOverlayLayers} from './kml-ground-overlay-layer.js';
import {formatGroundOverlayIssues} from '../../i18n/kml-ground-overlay-labels.js';
import {KmlRegionPreview,useKmlRegionPreview,fitKmlRegionSource} from './kml-region-preview.jsx';import {kmlRegionAppearance,kmlRegionProfileOpacity,kmlRegionGeometryVisible} from './kml-region.js';
export function SharedTrackPreview({detail,packet}){
 const {locale,text}=useKmlWorkspaceText(),host=useRef(),map=useRef(),drawing=useRef();
 const [profilePoint,setProfilePoint]=useState(null),profileMarker=useRef();
 const model=useMemo(()=>{try{return detail.source?parseKml(detail.source):null;}catch{return null;}},[detail.source]);
 const summary=useMemo(()=>model?summarizeKml(model):null,[model]);
 const groundOverlays=useMemo(()=>collectGroundOverlays(model),[model]);
 const screenOverlays=useMemo(()=>collectScreenOverlays(model),[model]),photoOverlays=useMemo(()=>collectPhotoOverlays(model),[model]),scopeOverlays=useMemo(()=>({overlays:[...groundOverlays.overlays,...screenOverlays.overlays.map(overlay=>({...overlay,index:'screen:'+overlay.index})),...photoOverlays.overlays.map(overlay=>({...overlay,index:'photo:'+overlay.index}))]}),[groundOverlays,screenOverlays,photoOverlays]);
 const [photoIndex,setPhotoIndex]=useState(null);useEffect(()=>setPhotoIndex(null),[model]);
 const [photoTiles,setPhotoTiles]=useState(null);
 const photoReferences=useMemo(()=>{const row=photoOverlays.overlays.find(photo=>photo.index===photoIndex);return !row||row.invalid?[]:row.pyramid?(photoTiles?.index===row.index?photoTiles.refs:[]):row.href?[row.href]:[];},[photoOverlays,photoIndex,photoTiles]);
 const temporal=useKmlTimeFilter(model,scopeOverlays),fittedModel=useRef();
 const iconResources=useKmlIconResources({packet,model,groundOverlays:groundOverlays.overlays,screenOverlays:screenOverlays.overlays,extraReferences:photoReferences});
 const [styleIssues,setStyleIssues]=useState([]);
 const number=new Intl.NumberFormat(locale),fixed=(value,digits)=>new Intl.NumberFormat(locale,{maximumFractionDigits:digits}).format(value);
 const format=(key,values)=>text[key].replace(/\{(\w+)\}/g,(_,name)=>values[name]);
 useEffect(()=>{
  const view=createTrackMap(host.current);map.current=view;
  view.createPane('land');view.getPane('land').style.zIndex=200;
  const land=L.geoJSON(null,{pane:'land',renderer:L.canvas({pane:'land'}),interactive:false,style:{color:'#c5d4cb',weight:1,fillColor:'#f4efd8',fillOpacity:.85}}).addTo(view);
  drawing.current=L.featureGroup().addTo(view);let countries,cancelled=false;
  const redraw=()=>{if(cancelled||!countries)return;land.clearLayers();land.addData({...countries,features:countries.features.map(feature=>regionDisplayGeometry(feature,view.getCenter().lng))});};
  fetch(new URL('../geocaching/data/countries.json',location.href)).then(response=>{if(!response.ok)throw Error('map');return response.json();}).then(data=>{if(!cancelled){countries=data;redraw();}}).catch(()=>{});
  view.on('moveend',redraw);
  return()=>{cancelled=true;view.off('moveend',redraw);view.remove();};
 },[]);
 const regions=useKmlRegionPreview(model,scopeOverlays,map),regionVisible=temporal.scopes.features.filter((feature,index)=>kmlTimeFeatureVisible(feature,temporal.range)&&feature.geometries.some(value=>kmlRegionGeometryVisible(filterKmlTimeGeometry(value,temporal.range),regions.opacities[index]))).length;
 const visiblePhotos=useMemo(()=>photoOverlays.overlays.filter(row=>row.visible&&kmlTimeMatches(temporal.scopes.overlays.get('photo:'+row.index),temporal.range)&&regions.overlayOpacity('photo:'+row.index)>0).map(row=>({...row,source:row,opacity:(row.opacity??1)*regions.overlayOpacity('photo:'+row.index)})),[photoOverlays,temporal.scopes,temporal.range,regions.scopes,regions.viewport,regions.enabled]);
 useEffect(()=>{
  const layer=drawing.current;if(!layer)return;layer.clearLayers();setStyleIssues([]);
  const [west,south,east,north]=detail.record[11].map(value=>value/1e7),right=east<west?east+360:east,reference=(west+right)/2;
  if(!model){L.rectangle([[south,west],[north,right]],{color:'#478e79',weight:2,fillOpacity:.08}).addTo(layer);}
  else{
   if(regions.scopes.declared.length)fitKmlRegionSource(map.current,model,summary,{overlays:[...groundOverlays.overlays,...photoOverlays.overlays.filter(row=>row.point&&row.visible).map(row=>({...row,corners:[row.point]}))]},fittedModel);
   const photoIssues=addPhotoMarkers(layer,visiblePhotos,setPhotoIndex,locale,regions.scopes.declared.length?map.current.getCenter().lng:reference,model,iconResources);
   const style=createKmlStyleResolver(model,{iconResources,locale}),indices=new Map(model.features.map((feature,index)=>[feature,index])),appearances=model.features.map((feature,index)=>style(index));
   const overlayIssues=addGroundOverlayLayers(layer,{...groundOverlays,overlays:groundOverlays.overlays.filter(overlay=>kmlTimeMatches(temporal.scopes.overlays.get(overlay.index),temporal.range)&&regions.overlayOpacity(overlay.index)>0).map(overlay=>({...overlay,opacity:overlay.opacity*regions.overlayOpacity(overlay.index)}))},iconResources);
   const screenIssues=addScreenOverlayLayers(layer,{overlays:screenOverlays.overlays.filter(overlay=>kmlTimeMatches(temporal.scopes.overlays.get('screen:'+overlay.index),temporal.range)&&regions.overlayOpacity('screen:'+overlay.index)>0).map(overlay=>({...overlay,opacity:overlay.opacity*regions.overlayOpacity('screen:'+overlay.index)}))},iconResources);
   setStyleIssues([...new Set([...appearances.flatMap(appearance=>appearance.issues),...photoIssues,...formatGroundOverlayIssues([...groundOverlays.issues.filter(issue=>issue.code!=='scope'),...overlayIssues],locale),...formatScreenOverlayIssues([...screenOverlays.issues,...screenIssues],locale)])]);
   const points=values=>{let previous=regions.scopes.declared.length?map.current.getCenter().lng:reference;return values.map(point=>{let longitude=point.longitude;while(longitude-previous>180)longitude-=360;while(longitude-previous< -180)longitude+=360;previous=longitude;return [point.latitude,longitude];});};
   const geometry=(value,index)=>{
    if(value.geometries){value.geometries.forEach(child=>geometry(child,index));return;}
    const opacity=regions.opacities[index];if(value.type==='Point'&&opacity<=.5)return;const appearance=kmlRegionAppearance(appearances[index],opacity);let shape;
    if(value.rings)shape=L.polygon(value.rings.map(ring=>points(ring.points)),appearance.polygon);
    else if(value.points?.length){const icon=value.type==='Point'&&kmlMapIcon(appearance);shape=value.type==='Point'?(icon?L.marker(points(value.points)[0],{icon}):L.circleMarker(points(value.points)[0],appearance.point)):L.polyline(points(value.points),appearance.line);}
    if(shape){shape.on('click',event=>{map.current.closePopup();const content=createKmlBalloonPopup(model,index,appearances[index].sourceStyle.balloon,locale);if(content)L.popup({maxWidth:360,maxHeight:360}).setLatLng(event.latlng).setContent(content).openOn(map.current);});shape.addTo(layer);}
   };
   const walk=(node,visible=true)=>{visible&&=node.visible!==false;if(node.children)node.children.forEach(child=>walk(child,visible));else if(visible&&node.geometries&&regions.opacities[indices.get(node)]>0&&kmlTimeFeatureVisible(temporal.scopes.features[indices.get(node)],temporal.range))node.geometries.forEach(value=>{const filtered=filterKmlTimeGeometry(value,temporal.range);if(filtered)geometry(filtered,indices.get(node));});};
   model.tree.forEach(node=>walk(node));
  }
  host.current.dataset.regionFeatureCount=String(regionVisible);host.current.dataset.timeFeatureCount=String(temporal.scopes.features.filter(feature=>kmlTimeFeatureVisible(feature,temporal.range)).length);const bounds=layer.getBounds(),key=model??detail;if(bounds.isValid()&&fittedModel.current!==key){map.current.fitBounds(bounds,{padding:[30,30],maxZoom:16});fittedModel.current=key;}
 },[detail,model,iconResources,groundOverlays,screenOverlays,photoOverlays,visiblePhotos,locale,temporal.scopes,temporal.range,regions.scopes,regions.opacities,regions.viewport,regions.enabled]);
 useEffect(()=>{profileMarker.current?.remove();profileMarker.current=null;if(profilePoint&&model&&map.current&&kmlRegionProfileOpacity(profilePoint,regions)>0&&kmlProfilePointVisible(profilePoint,temporal.scopes,temporal.range)){const point=profilePoint.point,longitude=point.longitude+360*Math.round((map.current.getCenter().lng-point.longitude)/360);profileMarker.current=L.circleMarker([point.latitude,longitude],{radius:7,color:'#a57444',fillOpacity:1}).addTo(map.current);host.current.dataset.profileLatitude=String(point.latitude);host.current.dataset.profileLongitude=String(point.longitude);}else{delete host.current.dataset.profileLatitude;delete host.current.dataset.profileLongitude;}},[profilePoint,model,temporal.scopes,temporal.range,regions.opacities]);
 useEffect(()=>{map.current?.closePopup();},[detail,model,locale]);
 return <div>
  {summary&&<div className="tracks-stats shared-track-statistics">
   <span>{format('points',{count:number.format(summary.pointCount),segments:number.format(summary.segments)})}</span>
   <span>{format('distance',{value:summary.distanceM===null?text.unknown:fixed(summary.distanceM/1000,2)+' km'})}</span>
   <span>{format('ascent',{value:summary.ascentM===null?text.unknown:fixed(summary.ascentM,0)+' m'})}</span>
   <span>{format('duration',{value:summary.durationS===null?text.unknown:fixed(summary.durationS,0)+' s'})}</span>
  </div>}
  <div ref={host} className="tracks-map shared-track-map" aria-label={text.map}/><small>{text.mapCredit}{model&&profilePoint&&<> · {Number.isFinite(profilePoint.time)?kmlTimelineText(locale).title:configElevationText(locale).title}</>}</small>
  <KmlTimeFilter {...temporal} visibleCount={regionVisible}/><KmlRegionPreview {...regions}/>
  <KmlPhotoBrowser photos={visiblePhotos} selected={photoIndex} onSelect={setPhotoIndex} resources={iconResources} issues={photoOverlays.issues} onReferences={setPhotoTiles}/>
  {styleIssues.length>0&&<div className="tracks-help" role="status">{styleIssues.map(issue=><p key={issue}>{issue}</p>)}</div>}
  {model&&<><KmlElevationProfile model={model} onPoint={setProfilePoint}/><KmlTimeline model={model} onPoint={setProfilePoint}/><KmlContentDetails model={model}/></>}
 </div>;
}
