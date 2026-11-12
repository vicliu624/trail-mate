import assert from 'node:assert/strict';
import {downloadCandidates, requestDownloadMode} from './enter_download_mode.mjs';
import {FRAME} from '../../site/device/src/hostlink.js';
import {OPERATION, decodeManagement, encodeManagement} from '../../site/device/src/management-protocol.js';

const hello = Uint8Array.from([1,0,0,2,0,4,0,0,9,...Buffer.from('TrailMate'),3,...Buffer.from('dev')]);
for (const scenario of ['ok','unsupported','busy','uncorrelated','invalid_ack']) {
  const operations = [];
  let randomCounter = 0;
  const exchange = async (type, sequence, payload, responseType) => {
    if (type === FRAME.HELLO) {
      assert.equal(sequence, 1); assert.equal(responseType, FRAME.HELLO_ACK);
      return {payload: hello};
    }
    assert.equal(type, FRAME.MANAGEMENT_REQUEST);
    assert.equal(responseType, FRAME.MANAGEMENT_RESPONSE);
    const request = decodeManagement(payload);
    operations.push(request.code);
    let status = 0, body;
    if (request.code === OPERATION.CAPABILITIES) {
      assert.deepEqual(request.body, []);
      body = [1,512,320,1,scenario === 'unsupported' ? [0,1,2,3] : [0,1,2,3,10],7];
    } else {
      assert.equal(request.code, OPERATION.ENTER_BOOTLOADER);
      assert.deepEqual(request.body, [true]);
      status = scenario === 'busy' ? 5 : 0;
      body = scenario === 'invalid_ack' ? [false] : [true];
    }
    const id = request.request.slice();
    if (scenario === 'uncorrelated') id[0] ^= 1;
    return {payload: encodeManagement(request.session, id, status, body)};
  };
  const work = requestDownloadMode(exchange, size => new Uint8Array(size).fill(++randomCounter));
  if (scenario === 'ok') assert.equal((await work).accepted, true);
  else await assert.rejects(work);
  assert.deepEqual(operations, scenario === 'unsupported' || scenario === 'uncorrelated' ? [0] : [0,10]);
}
const app = {port:'COM25',vid:0x303a,pid:0x82d4,location:'1-3:x.1',serial:'E072A1B3B57C'};
const rom = {port:'COM22',vid:0x303a,pid:0x1001,location:'1-3:x.0',serial:'e0:72:a1:b3:b5:7c'};
assert.deepEqual(downloadCandidates([app],[rom],app),[rom]);
assert.deepEqual(downloadCandidates([app],[{...rom,serial:'112233445566'}],app),[]);
assert.deepEqual(downloadCandidates([app],[{...rom,serial:null}],app),[{...rom,serial:null}]);
assert.deepEqual(downloadCandidates([app],[{...rom,serial:null,location:'1-4:x.0'}],app),[]);
console.log('HostLink download client: shared framing, capability gate, explicit confirmation, correlation, failures and physical-port identity PASS');
