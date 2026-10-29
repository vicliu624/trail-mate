import {OPERATION} from './management-protocol.js';
import {uploadObject} from './object-transfer.js';
import {bytes,equal,integer} from '../../geocaching/src/protocol.js';
import {prepareConfigurationSave} from './config-save-preparation.js';
import {configurationReadySession} from './config-ready-request.js';

export async function saveConfigDocument(session,document,baseHash,{id=crypto.getRandomValues(new Uint8Array(16)),signal,onProgress=()=>{},beforeCommit=()=>{},readyPollMs,readyTimeoutMs,digest=async raw=>new Uint8Array(await crypto.subtle.digest('SHA-256',raw))}={}){
 const ready=configurationReadySession(session,{signal,readyPollMs,readyTimeoutMs});
 if(!bytes(baseHash,32)||!bytes(id,16))throw Error('Invalid configuration transaction');
 const {source}=prepareConfigurationSave(document),hash=await digest(source);
 await uploadObject(session,source,{id,kind:1,sha256:hash,signal,onProgress});
 if(signal?.aborted)throw signal.reason??new DOMException('Save cancelled','AbortError');
 await beforeCommit({id:id.slice(),sha256:hash.slice(),source:source.slice()});
 if(signal?.aborted)throw signal.reason??new DOMException('Save cancelled','AbortError');
 const reply=await ready.request(OPERATION.CONFIG_COMMIT,[id,baseHash]);
 if(!Array.isArray(reply)||reply.length!==3||!bytes(reply[0],16)||!equal(reply[0],id)||!bytes(reply[1],32)||!equal(reply[1],hash)||!integer(reply[2],0,1))throw Error('Invalid configuration commit receipt');
 return {id:id.slice(),sha256:hash,source:source.slice(),restartPending:reply[2]===1};
}
