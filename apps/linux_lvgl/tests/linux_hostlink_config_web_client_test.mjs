import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {webcrypto} from 'node:crypto';
import {encodeManagement,decodeManagement,OPERATION} from '../../../site/device/src/management-protocol.js';
import {readConfigSnapshot} from '../../../site/device/src/config-snapshot.js';
import {saveConfigDocument} from '../../../site/device/src/config-save.js';

const child=spawn(process.argv[2],['--interop'],{stdio:['pipe','pipe','pipe']});
const pending=[];let buffered=Buffer.alloc(0),diagnostic='';
child.stderr.on('data',data=>{diagnostic=(diagnostic+data).slice(-8000);});
child.stdout.on('data',data=>{
 buffered=Buffer.concat([buffered,data]);
 while(buffered.length>=4){const length=buffered.readUInt32LE();if(buffered.length<length+4)break;const response=buffered.subarray(4,length+4);buffered=buffered.subarray(length+4);const request=pending.shift();assert(request);request.resolve(response);}
});
const complete=new Promise((resolve,reject)=>child.once('exit',(code,signal)=>{
 const error=Error(`Native config fixture exited ${code??signal}: ${diagnostic}`);
 for(const request of pending.splice(0))request.reject(error);
 if(code===0)resolve();else reject(error);
}));
complete.catch(()=>{});
const sessionId=new Uint8Array(16).fill(9);let requestNumber=0,busy=0;
const session={async request(operation,body){
 const requestId=new Uint8Array(16);new DataView(requestId.buffer).setUint32(0,++requestNumber,true);
 const payload=encodeManagement(sessionId,requestId,operation,body),header=Buffer.alloc(4);header.writeUInt32LE(payload.length);
 const reply=new Promise((resolve,reject)=>pending.push({resolve,reject}));child.stdin.write(header);child.stdin.write(payload);
 const decoded=decodeManagement(new Uint8Array(await reply));assert.deepEqual(decoded.session,sessionId);assert.deepEqual(decoded.request,requestId);
 if(decoded.code){if(decoded.code===5)busy++;throw Object.assign(Error(`Management status ${decoded.code}`),{code:decoded.code});}
 return decoded.body;
}};
try{
 const caps=await session.request(OPERATION.CAPABILITIES,[]);
 for(const operation of [17,20,21,32,33,34,35,36])assert(caps[4].includes(operation));assert.equal(caps[5],7);
 const digest=async bytes=>new Uint8Array(await webcrypto.subtle.digest('SHA-256',bytes));
 const snapshot=await readConfigSnapshot(session,{digest,readyPollMs:0});
 snapshot.document.set('device.node_name','Browser Native');snapshot.document.set('ui.audio.speaker_volume',67);
 const id=new Uint8Array(16).fill(7);
 const receipt=await saveConfigDocument(session,snapshot.document,snapshot.sha256,{id,digest,readyPollMs:0});
 assert(receipt.restartPending);assert(busy>=3);
 const replay=await session.request(OPERATION.CONFIG_COMMIT,[id,snapshot.sha256]);
 assert.deepEqual(replay,[id,receipt.sha256,1]);
 child.stdin.end();await complete;
 console.log('Production web client -> native management: TMSET7 read, edit, durable upload, Busy wait, atomic commit and receipt replay passed');
}catch(error){child.kill();await complete.catch(()=>{});throw error;}
