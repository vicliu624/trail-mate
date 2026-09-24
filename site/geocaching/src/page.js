import L from 'leaflet';
import {mountIslandShell} from './island-shell.jsx';
import './page.css';
import './theme.css';

mountIslandShell();
const chinese = navigator.language.startsWith('zh');
const tr = (en, zh) => chinese ? zh : en;
const labels = {
  title:tr('Geocaching','寻宝地图'),offline:tr('Loading…','加载中…'),source:tr('Source','源代码'),
  heading:tr('Find your next little adventure.','下一段小小的冒险，就在地图上。'),
  intro:tr('Find a cache, discover its story, and take the coordinates on your next trip.','寻找藏宝点，发现它的故事，带上坐标开启下一次旅程。'),
  noDirectory:tr('Loading the map service…','正在加载地图服务…'),
  show:tr('Show caches','显示藏宝点'),active:tr('Active','开放'),disabled:tr('Disabled','停用'),archived:tr('Archived','归档'),
  emptyCount:tr('Loaded 0 caches','已加载 0 个藏宝点'),start:tr('Preparing your map…','正在准备你的寻宝地图…'),
  more:tr('Load more','加载更多'),downloadSelected:tr('Download selected GPX','下载所选 GPX'),downloadPartial:tr('Download successful items only','仅下载成功的项目'),
  scope:tr('Results cover the directories reached in this session, not a global total.','结果来自本次连接到的目录，数量仅指已加载内容，不代表全网总数。'),
  search:tr('Search this area','搜索此区域'),download:tr('Download GPX','下载 GPX'),
};
document.documentElement.lang = chinese ? 'zh-Hans' : 'en';
for (const node of document.querySelectorAll('[data-label]')) node.textContent = labels[node.dataset.label];
const $ = id => document.getElementById(id);
const worker = new Worker(new URL('./reticulum-worker.js', import.meta.url), {type:'module'});
let nextId = 0, ready = false, rows = [], detailId = null, detailGeneration = 0, partial = null;
const pending = new Map(), selected = new Set();
const map = L.map('map', {worldCopyJump:true, minZoom:0}).fitWorld();
const tiles = L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {maxZoom:19, updateWhenIdle:true,
  keepBuffer:1, attribution:'&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'}).addTo(map);
const markers = L.layerGroup().addTo(map);
tiles.on('tileerror', () => { $('map-state').textContent = tr('Some map tiles are unavailable. Cache coordinates remain accessible.','部分底图暂不可用，仍可通过列表查看藏宝点坐标。'); });

function rpc(command, args = {}) {
  if (pending.size >= 8) return Promise.reject(Error(tr('Please wait for the current requests.','请等待当前请求完成。')));
  const id = ++nextId;
  return new Promise((resolve, reject) => { pending.set(id, {resolve,reject}); worker.postMessage({id,command,args}); });
}
const notice = message => { $('notice').textContent = message; };
const bounds = () => { const b = map.getBounds(); return [b.getSouth(), b.getWest(), b.getNorth(), b.getEast()]; };
const stateMask = () => [...document.querySelectorAll('input[name=state]:checked')].reduce((mask,input) => mask | Number(input.value), 0);

async function search() {
  const mask = stateMask();
  if (!mask) { notice(tr('Choose at least one cache state.','请至少选择一种状态。')); return; }
  selected.clear(); updateSelection();
  notice(tr('Querying public directories…','正在查询公共目录…'));
  try { await rpc('query', {bounds:bounds(),stateMask:mask}); }
  catch (error) { notice(error.message); }
}

function updateSelection() {
  $('selection-count').textContent = selected.size ? tr(`${selected.size} selected`, `已选 ${selected.size} 个`) : '';
  $('download-selected').disabled = !selected.size || !ready;
}

function renderRows() {
  $('result-count').textContent = tr(`Loaded ${rows.length} caches`, `已加载 ${rows.length} 个藏宝点`);
  const items = rows.map(row => {
    const item = document.createElement('li'), select = document.createElement('input'), button = document.createElement('button');
    select.type = 'checkbox'; select.checked = selected.has(row.id); select.disabled = row.conflict;
    select.setAttribute('aria-label', tr(`Select ${row.summary[6]}`, `选择 ${row.summary[6]}`));
    select.onchange = () => {
      if (select.checked && selected.size >= 20) { select.checked = false; notice(tr('Select up to 20 caches.','最多选择 20 个藏宝点。')); return; }
      if (select.checked) selected.add(row.id); else selected.delete(row.id);
      updateSelection();
    };
    button.className = 'result'; button.textContent = row.summary[6];
    const subtitle = document.createElement('small');
    subtitle.textContent = row.conflict ? tr('Sources disagree · download unavailable','来源版本矛盾 · 暂不能下载') :
      `${[labels.active,labels.disabled,labels.archived][row.summary[3]]} · D ${row.summary[7]/2} / T ${row.summary[8]/2} · v${row.summary[1]}`;
    button.append(subtitle); button.onclick = () => openDetail(row);
    item.append(select,button); return item;
  });
  $('results').replaceChildren(...items); renderMarkers();
}

function renderMarkers() {
  markers.clearLayers();
  const cells = new Map(), center = map.getCenter().lng;
  for (const row of rows) {
    const lat = row.summary[4]/1e7, rawLon = row.summary[5]/1e7;
    const lon = rawLon + 360 * Math.round((center - rawLon)/360);
    const pixel = map.latLngToContainerPoint([lat,lon]), key = `${Math.floor(pixel.x/50)},${Math.floor(pixel.y/50)}`;
    if (!cells.has(key)) cells.set(key, []);
    cells.get(key).push({row,lat,lon});
  }
  for (const cluster of cells.values()) {
    const {row,lat,lon} = cluster[0], multi = cluster.length > 1;
    const icon = L.divIcon({className:`cache-marker ${multi?'cluster':row.conflict?'conflict':row.summary[3]?'inactive':''}`,
      html:multi?String(cluster.length):'◆',iconSize:multi?[36,36]:[28,28],iconAnchor:multi?[18,18]:[14,14]});
    const marker = L.marker([lat,lon],{icon,title:multi?`${cluster.length} caches`:row.summary[6]}).addTo(markers);
    marker.on('click', () => multi ? map.setView([lat,lon],Math.min(19,map.getZoom()+2)) : openDetail(row));
  }
}

async function openDetail(row) {
  const generation = ++detailGeneration; detailId = row.id;
  $('detail').hidden = false; $('detail-name').textContent = row.summary[6];
  $('detail-coordinates').textContent = `${(row.summary[4]/1e7).toFixed(7)}, ${(row.summary[5]/1e7).toFixed(7)}`;
  $('detail-content').replaceChildren(); $('download-one').disabled = true;
  $('detail-state').textContent = tr('VERIFYING DETAILS','正在获取并验证详情');
  try {
    const item = await rpc('get',{cacheId:row.id});
    if (generation !== detailGeneration) return;
    $('detail-state').textContent = item.isCurrent ? tr('AUTHOR SIGNATURE VERIFIED','作者签名已验证') : tr('VERIFIED HISTORICAL VERSION','已验证的历史版本');
    const description = document.createElement('p'); description.textContent = item.record[9];
    const hint = document.createElement('details'), summary = document.createElement('summary'), text = document.createElement('p');
    summary.textContent = tr('Show hint','展开提示'); text.textContent = item.record[10] || tr('No hint provided.','未提供提示。'); hint.append(summary,text);
    const metadata = document.createElement('p'); metadata.className = 'fingerprint';
    metadata.textContent = `${tr('Author','作者')}: ${item.authorHash}\n${tr('Version','版本')}: ${item.record[3]}\n` +
      `${tr('Verified at','验证时间')}: ${new Date(item.checkedAt).toLocaleString()}\n${item.sourceName}`;
    $('detail-content').append(description,hint,metadata); $('download-one').disabled = false;
  } catch (error) { if (generation === detailGeneration) $('detail-state').textContent = error.message; }
}

function saveGpx(gpx, filename) {
  const url = URL.createObjectURL(new Blob([gpx],{type:'application/gpx+xml;charset=utf-8'}));
  const link = document.createElement('a'); link.href=url; link.download=filename; link.click();
  setTimeout(() => URL.revokeObjectURL(url),10000);
}

async function download(cacheIds) {
  notice(tr('Fetching and verifying signed objects…','正在获取并验证签名原文…'));
  try {
    const result = await rpc('download',{cacheIds});
    const filename = cacheIds.length===1?`${cacheIds[0]}.gpx`:'trail-mate-geocaches.gpx';
    if (result.failures.length) {
      partial = result.gpx ? {gpx:result.gpx,filename} : null;
      $('download-partial').hidden = !partial;
      notice(tr(`${result.succeeded} ready; ${result.failures.length} failed.`,`${result.succeeded} 个已准备，${result.failures.length} 个失败。`) +
        '\n' + result.failures.map(f => `${f.cacheId.slice(0,12)}: ${f.message}`).join('\n'));
    } else {
      saveGpx(result.gpx,filename); notice(tr('GPX prepared and handed to your browser for saving.','GPX 已生成，已交给浏览器保存。'));
    }
  } catch (error) { notice(error.message); }
}

worker.onmessage = ({data}) => {
  if (data.id) {
    const action=pending.get(data.id); if (!action) return; pending.delete(data.id);
    if (data.error) action.reject(Error(data.error)); else action.resolve(data.result);
    return;
  }
  const event=data.event;
  if (event.type==='status') {
    $('connection-state').textContent = event.state==='disconnected'?tr('Disconnected','连接已断开'):tr('Discovering directories…','正在发现目录…');
    if (event.state==='disconnected') { ready=false; $('search').disabled=true; $('connection-state').classList.remove('ready'); updateSelection(); }
  } else if (event.type==='directory') {
    const first = !ready; ready=true; $('search').disabled=false;
    $('connection-state').textContent=tr('Ready to explore','可以开始探索'); $('connection-state').classList.add('ready');
    $('directory-status').textContent=tr(`${event.count} verified public directories`, `已验证 ${event.count} 个公共目录`);
    if (first) search();
  } else if (event.type==='results') {
    rows=event.rows; $('more').hidden=!event.more; renderRows();
    notice(event.limited?tr('500-item display limit reached. Narrow the area to explore more.','已达到 500 项显示上限，请缩小区域继续探索。'):
      tr('Loaded from live directory responses. Open a cache to verify its details.','已加载目录实时响应。打开藏宝点以验证完整详情。'));
  } else if (event.type==='source-error') notice(`${event.name}: ${event.message}`);
};
worker.onerror = () => { notice(tr('The map service stopped. Refresh the page to try again.','地图服务已停止，请刷新页面重试。')); ready=false; $('search').disabled=true; };
async function startService() {
  try {
    const response = await fetch(new URL('../network.json', import.meta.url), {cache:'no-store'});
    if (!response.ok) throw Error('Missing deployment configuration');
    const config = await response.json();
    const url = new URL(config.endpoint);
    if (url.protocol !== 'wss:' && !(url.protocol === 'ws:' && ['localhost','127.0.0.1','[::1]'].includes(url.hostname))) throw Error('Invalid deployment endpoint');
    await rpc('connect',{url:url.href,discoverySeeds:config.discoverySeeds});
  } catch {
    $('connection-state').textContent = tr('Service unavailable','服务暂不可用');
    $('directory-status').textContent = tr('Please try again later.','请稍后再试。');
    notice(tr('The map service is temporarily unavailable. No setup is needed on your side.','寻宝地图服务暂时不可用，你无需进行任何网络设置。'));
  }
}
startService();
$('search').onclick=search;
$('more').onclick=async () => { $('more').disabled=true; try { await rpc('more'); } catch(error) {notice(error.message);} finally {$('more').disabled=false;} };
$('download-one').onclick=() => detailId && download([detailId]);
$('download-selected').onclick=() => download([...selected]);
$('download-partial').onclick=() => {if(partial)saveGpx(partial.gpx,partial.filename);partial=null;$('download-partial').hidden=true;};
$('close-detail').onclick=() => {++detailGeneration;$('detail').hidden=true;};
$('locate').onclick=() => navigator.geolocation ? navigator.geolocation.getCurrentPosition(position => map.setView([position.coords.latitude,position.coords.longitude],13),
  error=>notice(error.message),{timeout:10000}) : notice(tr('Location is unavailable.','当前浏览器不支持定位。'));
map.on('moveend',renderMarkers);
window.addEventListener('pagehide',()=>worker.terminate());
window.addEventListener('keydown',event=>{if(event.key==='Escape')$('close-detail').click();});
