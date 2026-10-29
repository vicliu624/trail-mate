import {OPERATION} from './management-protocol.js';
import {bytes,equal,integer} from '../../geocaching/src/protocol.js';
import {TmsDocument} from './tms-document.js';
import {configurationReadySession} from './config-ready-request.js';

export async function readConfigSnapshot(session,{signal,onProgress=()=>{},readyPollMs,readyTimeoutMs,digest=async data=>new Uint8Array(await crypto.subtle.digest('SHA-256',data))}={}){
 session=configurationReadySession(session,{signal,readyPollMs,readyTimeoutMs});
 let offset=0,total,hash,output;
 do{
   if(signal?.aborted)throw signal.reason??new DOMException('Read cancelled','AbortError');
   const result=await session.request(OPERATION.CONFIG_SNAPSHOT,[offset,320,hash??null]);
   if(!Array.isArray(result)||result.length!==4||result[0]!==offset||!integer(result[1],1,32768)||
      !bytes(result[2],32)||!(result[3] instanceof Uint8Array)||!result[3].length||result[3].length>320)throw Error('Invalid configuration snapshot response');
   if(!output){total=result[1];hash=result[2].slice();output=new Uint8Array(total);}
   if(total!==result[1]||!equal(hash,result[2])||result[3].length>total-offset)throw Error('Device configuration changed during read');
   output.set(result[3],offset);offset+=result[3].length;onProgress({offset,total});
 }while(offset<total);
 if(!equal(await digest(output),hash))throw Error('Configuration snapshot hash mismatch');
 return {document:new TmsDocument(output),sha256:hash};
}
