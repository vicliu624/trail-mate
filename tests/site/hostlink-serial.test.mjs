import test from 'node:test';
import assert from 'node:assert/strict';
import {crc16,encodeFrame,FrameDecoder,decodeHello,HostlinkConnection,FRAME} from '../../site/device/src/hostlink.js';

test('TinyUSB connection asserts DTR and RTS before HELLO and releases both on close',async()=>{
 const port=fakePort((frame,send)=>{
  assert.deepEqual(port.signals[0],{dataTerminalReady:true,requestToSend:true});
  if(frame.type===FRAME.HELLO)send(encodeFrame(FRAME.HELLO_ACK,frame.sequence,helloBytes()));
 });
 const connection=new HostlinkConnection(port);
 await connection.open();await connection.close();
 assert.deepEqual(port.signals.at(-1),{dataTerminalReady:false,requestToSend:false});
});

function helloBytes() {
  const model=new TextEncoder().encode('T-Deck'), fw=new TextEncoder().encode('1.0');
  const b=new Uint8Array(10+model.length+fw.length),d=new DataView(b.buffer);
  d.setUint16(0,1,true);d.setUint16(2,512,true);d.setUint32(4,2,true);
  b[8]=model.length;b.set(model,9);b[9+model.length]=fw.length;b.set(fw,10+model.length);return b;
}
function fakePort(handler) {
  let input;
  return {signals:[],opened:false,
    async open(){this.opened=true;this.readable=new ReadableStream({start(c){input=c;}});
      this.writable=new WritableStream({write:b=>handler(new FrameDecoder().push(b)[0],bytes=>input.enqueue(bytes))});},
    async setSignals(s){this.signals.push(s)},async close(){this.opened=false},
    end(){input.close()}};
}

test('CRC matches CCITT-FALSE reference vector and wire frame layout',()=>{
  assert.equal(crc16(new TextEncoder().encode('123456789')),0x29b1);
  assert.deepEqual([...encodeFrame(0x11,0x1234).slice(0,8)],[72,76,1,17,52,18,0,0]);
  assert.throws(()=>encodeFrame(1,0,new Uint8Array(513)),RangeError);
});
test('every split boundary preserves frames and sequence',()=>{
  const raw=encodeFrame(0x82,65535,new Uint8Array([0,72,76,255]));
  for(let i=0;i<=raw.length;i++){
    const d=new FrameDecoder(),result=[...d.push(raw.slice(0,i)),...d.push(raw.slice(i))];
    assert.equal(result.length,1);assert.equal(result[0].sequence,65535);
    assert.deepEqual([...result[0].payload],[0,72,76,255]);assert.equal(d.bufferedBytes,0);
  }
});
test('coalesced frames, noise, bad CRC, oversize and version resynchronise',()=>{
  const valid=encodeFrame(2,7,helloBytes()),bad=valid.slice();bad[bad.length-1]^=1;
  const d=new FrameDecoder(),noise=new Uint8Array(100000).fill(72);
  assert.deepEqual(d.push(noise),[]);assert.ok(d.bufferedBytes<=522);
  const bytes=new Uint8Array([...bad,72,76,2,1,0,0,0,0,72,76,1,1,0,0,255,255,...valid,...valid]);
  assert.equal(d.push(bytes).length,2);
});
test('hello is exact and bounded, not author identity',()=>{
  assert.deepEqual(decodeHello(helloBytes()),{version:1,maxPayload:512,capabilities:2,model:'T-Deck',firmware:'1.0'});
  assert.throws(()=>decodeHello(helloBytes().slice(0,-1)));
  assert.throws(()=>decodeHello(new Uint8Array([...helloBytes(),0])));
});
test('handshake, asynchronous events and correlated command reply',async()=>{
  const events=[];
  const port=fakePort((f,send)=>{
    if(f.type===1)send(encodeFrame(2,f.sequence,helloBytes()));
    else {send(encodeFrame(0x82,0,new Uint8Array([1])));send(encodeFrame(3,f.sequence,new Uint8Array([0])));}
  });
  const c=new HostlinkConnection(port);c.subscribe(e=>events.push(e));
  assert.equal((await c.open()).model,'T-Deck');assert.equal(c.state,'ready');
  await c.request(FRAME.SET_CONFIG,new Uint8Array([1,1,2]));
  assert.ok(events.some(e=>e.frame?.type===0x82));
  await c.close();assert.equal(port.opened,false);assert.equal(c.state,'disconnected');
  assert.equal(port.readable.locked,false);assert.equal(port.writable.locked,false);
});
test('device rejection and request timeout have distinct outcomes',async()=>{
  const port=fakePort((f,send)=>{if(f.type===1)send(encodeFrame(2,f.sequence,helloBytes()));else if(f.type===0x12)send(encodeFrame(3,f.sequence,new Uint8Array([4])));});
  const c=new HostlinkConnection(port,{timeoutMs:30});await c.open();
  await assert.rejects(c.request(0x12),e=>e.code===4);
  await assert.rejects(c.request(0x11),e=>e.code==='TIMEOUT');await c.close();
});
test('disconnect rejects pending work and releases locks on close',async()=>{
  const port=fakePort((f,send)=>{if(f.type===1)send(encodeFrame(2,f.sequence,helloBytes()));});
  const c=new HostlinkConnection(port);await c.open();const result=c.request(0x11);
  port.end();await assert.rejects(result,e=>e.code==='DISCONNECTED');await c.close();
  assert.equal(port.readable.locked,false);assert.equal(port.writable.locked,false);
});
test('unrelated sequence cannot complete a request; events survive listener errors',async()=>{
  const caught=[];
  const port=fakePort((f,send)=>{if(f.type===1)send(encodeFrame(2,f.sequence,helloBytes()));else send(encodeFrame(3,f.sequence+1,new Uint8Array([0])));});
  const c=new HostlinkConnection(port,{timeoutMs:30,onListenerError:e=>caught.push(e)});
  c.subscribe(()=>{throw Error('consumer error')});await c.open();
  await assert.rejects(c.request(0x12),e=>e.code==='TIMEOUT');assert.ok(caught.length);await c.close();
});
test('physical disconnect automatically releases locks before advertising disconnected',async()=>{
 const port=fakePort((f,send)=>{if(f.type===1)send(encodeFrame(2,f.sequence,helloBytes()));});
 const c=new HostlinkConnection(port);let resolve;
 const disconnected=new Promise(r=>{resolve=r;});
 c.subscribe(e=>{if(e.type==='state'&&e.state==='disconnected')resolve();});
 await c.open();port.end();await disconnected;
 assert.equal(port.readable.locked,false);assert.equal(port.writable.locked,false);assert.equal(port.opened,false);
});

test('connection failure identifies port, USB signalling and handshake stages',async()=>{
 for(const [operation,stage] of [['open','Opening serial port'],['setSignals','Enabling USB serial']]){
  const port=fakePort(()=>{});
  port[operation]=async()=>{throw new DOMException('Port access denied','NetworkError');};
  const connection=new HostlinkConnection(port,{timeoutMs:30});
  await assert.rejects(connection.open(),error=>error.stage===stage&&error.message.includes('Port access denied'));
  assert.equal(connection.state,'disconnected');
 }
 const port=fakePort(()=>{}),connection=new HostlinkConnection(port,{timeoutMs:30});
 await assert.rejects(connection.open(),error=>error.code==='TIMEOUT'&&error.stage==='HostLink handshake');
 assert.equal(port.readable.locked,false);assert.equal(port.writable.locked,false);
});

test('serial receive errors retain the actual diagnostic during handshake',async()=>{
 const port=fakePort(()=>{});
 port.open=async function(){
  this.readable=new ReadableStream({start(controller){controller.error(new DOMException('USB receive failed','NetworkError'));}});
  this.writable=new WritableStream();
 };
 const connection=new HostlinkConnection(port);
 await assert.rejects(connection.open(),error=>error.stage==='HostLink handshake'&&error.message.includes('USB receive failed'));
 assert.equal(port.readable.locked,false);assert.equal(port.writable.locked,false);
});
