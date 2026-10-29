import test from 'node:test';
import assert from 'node:assert/strict';
import {requestConfigReady} from '../../site/device/src/config-ready-request.js';

test('explicit Busy retries retain the exact operation body',async()=>{
 const body=[new Uint8Array(16),new Uint8Array(32)];let calls=0;
 const session={async request(op,value){assert.equal(op,21);assert.equal(value,body);if(++calls<3)throw Object.assign(Error('busy'),{code:calls===1?5:'BUSY'});return 'ready';}};
 assert.equal(await requestConfigReady(session,21,body,{pollMs:0}),'ready');assert.equal(calls,3);
});
test('transport and configuration failures are not retried',async()=>{
 for(const code of ['DISCONNECTED','TIMEOUT',4,6]){
  let calls=0;const error=Object.assign(Error('unconfirmed'),{code});
  await assert.rejects(requestConfigReady({async request(){calls++;throw error;}},21,[]),e=>e===error);assert.equal(calls,1);
 }
});
test('Busy waits are bounded and abortable',async()=>{
 const busy=Object.assign(Error('busy'),{code:5});
 await assert.rejects(requestConfigReady({async request(){throw busy;}},17,[],{timeoutMs:5,pollMs:0}),e=>e===busy);
 const controller=new AbortController();let calls=0;
 const promise=requestConfigReady({async request(){calls++;controller.abort(Error('cancelled'));throw busy;}},17,[],{signal:controller.signal});
 await assert.rejects(promise,/cancelled/);assert.equal(calls,1);
 await assert.rejects(requestConfigReady({},17,[],{timeoutMs:0}),/budget/);
});
