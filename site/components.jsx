import React, {useState, useEffect} from 'react';
import {createRoot} from 'react-dom/client';
import {Select, Background, BackTop, Title, Icon} from 'animal-island-ui';
import 'animal-island-ui/style';
import {locales} from './i18n/registry.js';
function useWebsiteLabels() {
  const [labels,setLabels]=useState({manufacturer:'Manufacturer',device:'Device',all:'All',language:'Language',backTop:'Back to top',protocol:'Sample protocol',previewDevice:'Preview device'});
  useEffect(()=>{
    let dispose,active=true;
    const url=new URL('../i18n/component-labels.js',import.meta.url);
    import(url.href).then(module=>module.watchComponentLabels(value=>{if(active)setLabels(value);})).then(unsubscribe=>{if(active)dispose=unsubscribe;else unsubscribe();});
    return ()=>{active=false;dispose?.();};
  },[]);
  return labels;
}
import {devices as simulatorDevices} from './simulator/devices/registry.js';

function SimulatorDevicePicker({element}) {
  const labels=useWebsiteLabels();
  const [brand,setBrand]=useState(simulatorDevices[element.value]?.manufacturer||'lilygo');
  const [device,setDevice]=useState(element.value);
  const ready=Object.values(simulatorDevices).filter(item=>item.manufacturer===brand);
  const options=ready.map(item=>({key:item.id,label:item.name}));
  return <span className="simulator-device-picker">
    <Select options={[{key:'lilygo',label:'LILYGO'},{key:'seeed',label:'Seeed Studio'}]} value={brand} aria-label={labels.manufacturer} onChange={value=>{
      const next=Object.values(simulatorDevices).find(item=>item.manufacturer===value);
      setBrand(value);setDevice(next.id);
      element.value=next.id;element.dispatchEvent(new Event('change',{bubbles:true}));
    }}/>
    <Select options={options} value={device} aria-label={labels.device} onChange={value=>{
      setDevice(value);
      if(simulatorDevices[value]){element.value=value;element.dispatchEvent(new Event('change',{bubbles:true}));}
    }}/>
  </span>;
}

const navigationIcons = {
  './geocaching/':'icon-map',
  '#overview':'page', '#explorer':'icon-variant', '#capabilities':'icon-design',
  '#features':'icon-map', '#languages':'icon-encyclopedia',
  '#webflasher':'icon-diy', '#docs':'page',
};
const brandHost=document.querySelector('#device-brand-selector');
if(brandHost) {
  function BrandSelector() {
    const labels=useWebsiteLabels();
    const [brand,setBrand]=useState('all');
    useEffect(()=>{
      document.querySelectorAll('.device-support-card[data-brand]').forEach(card=>{card.hidden=brand!=='all'&&card.dataset.brand!==brand;});
    },[brand]);
    const options=[{key:'all',label:labels.all},{key:'lilygo',label:'LILYGO'},{key:'seeed',label:'Seeed Studio'}];
    return <Select options={options} value={brand} aria-label={labels.manufacturer} onChange={value=>{
      setBrand(value);
    }}/>;
  }
  createRoot(brandHost).render(<BrandSelector/>);
}
for (const link of document.querySelectorAll('.side-nav > a')) {
  const name=navigationIcons[link.getAttribute('href')];
  if(!name)continue;
  const decoration=document.createElement('span');
  decoration.className='navigation-icon';
  decoration.setAttribute('aria-hidden','true');
  // Sibling placement survives the legacy translator replacing link textContent.
  link.before(decoration);
  createRoot(decoration).render(<Icon name={name} size={22}/>);
}
for (const [selector,name] of [
  ['.product-principles article:nth-child(1)','page'],
  ['.product-principles article:nth-child(2)','icon-chat'],
  ['.product-principles article:nth-child(3)','icon-map'],
  ['.product-principles article:nth-child(4)','location'],
  ['#prepare','icon-map'],['#webflasher .board-card','icon-diy'],
]) {
  const parent=document.querySelector(selector);
  if(!parent)continue;
  const decoration=document.createElement('span');
  decoration.className='section-icon-badge';
  decoration.setAttribute('aria-hidden','true');
  parent.prepend(decoration);
  createRoot(decoration).render(<Icon name={name} size={28}/>);
}

// Preserve each existing heading as the localized source and mirror it in Title.
for (const [selector, icon, color] of [
  ['#webflasher h2','icon-diy','app-teal'],
  ['#languages h2','icon-encyclopedia','app-blue'],
  ['#design-purpose h2','icon-design','app-green'],
  ['#field-calculator h2','icon-map','app-yellow'],
]) {
  const heading=document.querySelector(selector);
  if(!heading)continue;
  const host=document.createElement('div');
  host.className='island-section-title';
  heading.after(host);
  const root=createRoot(host);
  const render=()=>root.render(<Title color={color} size="middle"><span role="heading" aria-level="2"><Icon name={icon} size={24} aria-hidden="true"/> {heading.textContent}</span></Title>);
  heading.hidden=true;
  render();
  new MutationObserver(render).observe(heading,{childList:true,characterData:true,subtree:true});
}

const decorationHost = document.createElement('div');
decorationHost.id = 'island-decoration';
document.body.prepend(decorationHost);
createRoot(decorationHost).render(<Background type="sprinkles" className="page-island-background" aria-hidden="true"/>);
const backTopHost = document.createElement('div');
backTopHost.id = 'island-backtop';
document.body.append(backTopHost);
function LocalizedBackTop() {
  const labels=useWebsiteLabels();
  useEffect(()=>{
    const sync=()=>{
      backTopHost.querySelector('[role="button"]')?.setAttribute('aria-label',labels.backTop);
      backTopHost.querySelector('img')?.setAttribute('alt',labels.backTop);
    };
    sync();
    const observer=new MutationObserver(sync);
    observer.observe(backTopHost,{childList:true,subtree:true});
    return ()=>observer.disconnect();
  },[labels.backTop]);
  return <BackTop visibilityHeight={400} style={{right:16,bottom:20}}/>;
}
createRoot(backTopHost).render(<LocalizedBackTop/>);

const original = document.querySelector('#install-target');
const languageElement=document.querySelector('#site-language');
if(languageElement) {
  function LanguagePicker() {
    const labels=useWebsiteLabels();
    const [value,setValue]=useState(document.documentElement.lang||'en');
    useEffect(()=>{
      const sync=()=>setValue(document.documentElement.lang||'en');
      const observer=new MutationObserver(sync);
      observer.observe(document.documentElement,{attributes:true,attributeFilter:['lang']});
      sync();
      return ()=>observer.disconnect();
    },[]);
    return <Select options={locales.map(locale=>({key:locale.id,label:locale.name}))} value={value} aria-label={labels.language} onChange={key=>{
      languageElement.value=key;languageElement.dispatchEvent(new Event('change',{bubbles:true}));
    }}/>;
  }
  const host=document.createElement('div');languageElement.after(host);
  createRoot(host).render(<LanguagePicker/>);languageElement.hidden=true;
}
// The native explorer is mounted asynchronously, after the WASM manifest loads.
function PreviewDeviceSelect({element}) {
  const labels=useWebsiteLabels();
  const [value,setValue]=useState(element.value);
  useEffect(()=>{
    const sync=()=>setValue(element.value);
    element.addEventListener('change',sync);
    return ()=>element.removeEventListener('change',sync);
  },[element]);
  const options=Array.from(element.options,option=>({key:option.value,label:option.textContent}));
  return <Select options={options} value={value} aria-label={element.id==='native-protocol'?labels.protocol:labels.previewDevice} onChange={key=>{
    element.value=key;setValue(key);element.dispatchEvent(new Event('change',{bubbles:true}));
  }}/>;
}
const previewObserver=new MutationObserver(()=>{
  const elements=['native-device','native-protocol'].map(id=>document.getElementById(id));
  if(elements.some(element=>!element))return;
  previewObserver.disconnect();
  for(const element of elements) {
  const host=document.createElement('span');
  host.className='island-preview-select';
  element.after(host);
  createRoot(host).render(element.id==='native-device'?<SimulatorDevicePicker element={element}/>:<PreviewDeviceSelect element={element}/>);
  element.hidden=true;
  }
});
previewObserver.observe(document.querySelector('#explorer'),{childList:true,subtree:true});
if (original) {
  const host = document.createElement('div');
  host.className = 'island-install-select';
  original.after(host);
  const options = Array.from(original.options, option => ({key:option.value,label:option.textContent}));
  function InstallSelect() {
    const [value,setValue] = useState(original.value);
    return <Select options={options} value={value} aria-labelledby="install-target-label" onChange={key=>{
      original.value=key;
      setValue(key);
      original.dispatchEvent(new Event('change',{bubbles:true}));
    }}/>;
  }
  createRoot(host).render(<InstallSelect/>);
  original.hidden=true;
}
