import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';

for(const device of ['pager','tdeck','wio-tracker-l2']) {
  const base=new URL(`../../site/assets/native/${device}`,import.meta.url);
  const {default:create}=await import(`${base}.js`);
  const bytes=await readFile(new URL(`${base}.data`));
  const runtime=await create({wasmBinary:await readFile(new URL(`${base}.wasm`)),getPreloadedPackage(){return bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength);},print(){},printErr(){}});
  runtime._native_init(device==='pager'?0:1);
  const tick=()=>{for(let i=0;i<8;i++)runtime._native_tick(50);};
  const snapshot=()=>JSON.parse(runtime.UTF8ToString(runtime._native_snapshot()));
  const click=text=>{
    if(['Tracks','Routes','Nav'].includes(text)&&!snapshot().some(item=>item.button&&item.text===text)) {
      const menu=snapshot().find(item=>item.button&&item.text==='Menu');
      assert.ok(menu,`${device}: category menu remains available`);runtime._native_activate(menu.id);tick();
    }
    const button=snapshot().find(item=>item.button&&item.text===text);assert.ok(button,`${device}: missing ${text}`);runtime._native_activate(button.id);tick();
  };
  const tracks=()=>{click('Tracks');click('View');};
  runtime._native_open_page(14);tick();
  assert.ok(snapshot().some(item=>item.text==='No tracks yet'));
  assert.ok(!snapshot().some(item=>item.text==='Back'));
  assert.ok(!snapshot().some(item=>item.button&&['Start recording','New navigation'].includes(item.text)));
  click('Tracks');
  assert.ok(['View','New','Current'].every(text=>snapshot().some(item=>item.button&&item.text===text)));
  click('New');
  assert.ok(snapshot().some(item=>item.text==='Recording'));
  const stop=snapshot().find(item=>item.button&&item.text==='Stop recording');
  const menu=snapshot().find(item=>item.button&&item.text==='Menu');
  assert.ok(stop&&menu&&stop.x+stop.w<=menu.x,'Stop recording leaves room for the right-aligned category menu');
  // The recorder includes process and sequence tokens to avoid same-second collisions.
  assert.ok(snapshot().some(item=>!item.button&&/^track-\d+-[0-9a-f]{8}-[0-9a-f]{16}$/.test(item.text)),`${device}: new recording shows its collision-safe title without the file extension`);
  if(device!=='pager')assert.ok(snapshot().some(item=>item.text==='GPX | Recording'),`${device}: recording card shows file type/status`);
  else {
    const format=snapshot().find(item=>item.text==='GPX | Recording');
    const counter=snapshot().find(item=>item.text==='1 / 1');
    if(format&&counter)assert.ok(format.y+format.h<=counter.y,'Pager format row leaves room for paging after recovering the full content width');
  }
  click('Routes');
  assert.ok(!snapshot().some(item=>['Recording','Stop recording'].includes(item.text)),'Route mode has its own status and actions');
  tracks();
  assert.ok(snapshot().some(item=>item.text==='Recording'),'Changing tabs does not stop recording');
  click('Stop recording');
  assert.ok(snapshot().some(item=>item.text==='Not recording'));
  click('Routes');
  assert.ok(!snapshot().some(item=>['Start recording','Stop recording','Back'].includes(item.text)));
  const routeCard=snapshot().find(item=>item.text==='Sample route');
  assert.ok(routeCard,`${device}: KML document name is shown in the card`);
  assert.ok(!snapshot().some(item=>item.text.includes('sample-route.kml')),`${device}: internal filename is not repeated`);
  click('Actions');
  assert.ok(snapshot().some(item=>item.text==='Route Actions'),`${device}: explicit card action opens the existing route menu`);
  const dialogClose=snapshot().findLast(item=>item.button&&item.text==='\uf00d');
  assert.ok(dialogClose&&dialogClose.w===dialogClose.h&&dialogClose.h<=24,`${device}: card actions use a compact title-bar close`);
  assert.ok(!snapshot().some(item=>item.button&&item.text==='Cancel'),`${device}: title-bar close avoids a duplicate cancel row`);
  const lastAction=snapshot().find(item=>item.button&&item.text==='Delete');
  assert.ok(lastAction&&lastAction.y+lastAction.h-dialogClose.y<140,`${device}: action list fits its contents`);
  runtime._native_key('X'.charCodeAt(0));tick();
  assert.ok(!snapshot().some(item=>item.text==='Route Actions'),`${device}: uppercase X closes from the focused action`);
  const routeCounter=snapshot().find(item=>item.text==='1 / 2');
  assert.ok(routeCounter,`${device}: card shows the selected position`);
  click('\uf054');
  const nextCard=snapshot().find(item=>item.text==='Second sample trail');
  assert.equal(nextCard?.id,routeCard.id,`${device}: flipping reuses the route card`);
  assert.ok(snapshot().some(item=>item.text==='2 / 2'));
  tracks();
  click('Routes');
  assert.equal(snapshot().find(item=>item.text==='Second sample trail')?.id,routeCard.id,
    `${device}: category switching preserves the selected file and card widget`);
  const previous=snapshot().find(item=>item.button&&item.text==='\uf053'&&item.y>50);
  assert.ok(previous,`${device}: previous-card navigation is available`);
  runtime._native_activate(previous.id);tick();
  assert.equal(snapshot().find(item=>item.text==='Sample route')?.id,routeCard.id);
  tracks();
  assert.ok(!snapshot().some(item=>item.button&&item.text==='Start recording'));
  click('\uf053');
  assert.equal(runtime._native_page_value(),10,'Top-bar back remains available without a list Back row');
  runtime._native_open_page(15);tick();
  const closeDialog=(keyboard=false)=>{
    const close=snapshot().findLast(item=>item.button&&item.text==='\uf00d');
    assert.ok(close&&close.w===close.h&&close.h<=24,`${device}: modal has a compact square close`);
    const x=close.x+Math.floor(close.w/2),y=close.y+Math.floor(close.h/2);
    if(keyboard) {runtime._native_key('x'.charCodeAt(0));tick();}
    else {
      runtime._native_pointer(x,y,1);runtime._native_tick(50);
      runtime._native_pointer(x,y,0);tick();
    }
    assert.ok(!snapshot().some(item=>item.button&&item.text==='\uf00d'),`${device}: touch closes the topmost modal`);
  };
  click('Help');
  assert.ok(snapshot().some(item=>item.text==='Map Help'));
  closeDialog();
  for(const section of ['Navigation','Tracks','Contours']) {
    click('Menu');click(section);
    assert.ok(snapshot().some(item=>!item.button&&item.text===section),`${device}: stable map entry opens ${section} management`);
    if(section!=='Contours') {
      assert.ok(snapshot().some(item=>item.button&&item.text==='Unload'));
      click('Load');
      assert.ok(snapshot().some(item=>item.text===(section==='Navigation'?'Load navigation':'Load track')));
    }
    // Checkbox text is drawn by the checkbox widget itself and is not a label
    // in native_snapshot. The shared title and real pointer-close remain visible.
    closeDialog(section!=='Contours');
  }
  console.log(`${device}: empty state, recording, reused file cards, selection across categories, navigation and top-bar back passed.`);
}
