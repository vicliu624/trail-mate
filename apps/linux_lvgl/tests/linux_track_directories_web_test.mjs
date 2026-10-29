import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {mkdtemp, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fixture, connect} from './linux_identity_test_transport.mjs';
import {readTrackDirectories} from '../../../site/tracks/src/device-directories.js';
import {ManagementSession, OPERATION, MANAGEMENT_STATUS as STATUS} from '../../../site/device/src/management-protocol.js';
globalThis.crypto ??= webcrypto;
const root=await mkdtemp(join(tmpdir(),'trailmate-directory-session-'));
let app,client;
try {
  app=await fixture(root,'seed'); client=await connect(await app.next('ENDPOINT'));
  let supported=false;
  for(let attempt=0;attempt<100;++attempt) {
    const caps=await client.session.request(OPERATION.CAPABILITIES,[]);
    if([48,50,83,89,90].every(operation=>caps[4].includes(operation))){supported=true;break;}
    await new Promise(resolve=>setTimeout(resolve,20));
  }
  assert.ok(supported,'Active native Reticulum owner did not register directory operations');
  assert.deepEqual(await readTrackDirectories(client.session),[]);
  const author=await client.session.request(OPERATION.AUTHOR_INFO,[]),unknown=new Uint8Array(16).fill(22);
  const eventualError=async(operation,body,expected)=>{
    for(let attempt=0;attempt<100;++attempt){
      try{await client.session.request(operation,body);throw Error('Unexpected successful publication response');}
      catch(error){if(error.code===STATUS.BUSY){await new Promise(resolve=>setTimeout(resolve,20));continue;}assert.equal(error.code,expected);return;}
    }
    throw Error('Publication request remained busy');
  };
  await eventualError(OPERATION.JOB_GET,[unknown,author[3],1],STATUS.EXPIRED);
  await eventualError(OPERATION.JOB_CANCEL,[unknown,author[3],1],STATUS.EXPIRED);
  await eventualError(OPERATION.JOB_GET,[unknown,author[3]+1,1],STATUS.STALE_IDENTITY);
  await eventualError(OPERATION.JOB_GET,[unknown,author[3],0],STATUS.INVALID_FIELD);
  await eventualError(OPERATION.TRACK_COMMIT,[],STATUS.INVALID_FIELD);
  await eventualError(OPERATION.TRACK_COMMIT,[unknown,new Uint8Array(32).fill(1),new Uint8Array(32).fill(2),author[3],1,new Uint8Array(16).fill(3),new Uint8Array(16).fill(4)],STATUS.CONFLICT);
  const other=new ManagementSession(client.connection);
  await assert.rejects(readTrackDirectories(other),error=>error.code===STATUS.CONFLICT);
  await client.session.request(OPERATION.SESSION_CLOSE,[]);
  assert.deepEqual(await readTrackDirectories(other),[]);
  await assert.rejects(other.request(OPERATION.TRACK_DIRECTORY_DISCOVER,[new Uint8Array(16)]),error=>error.code===STATUS.INVALID_FIELD);
  await assert.rejects(other.request(OPERATION.TRACK_DIRECTORY_DISCOVER,[new Uint8Array(16).fill(3)]),error=>error.code===STATUS.BUSY);
  await other.request(OPERATION.SESSION_CLOSE,[]); other.close();
  assert.deepEqual(await readTrackDirectories(client.session),[]);
  console.log('LINUX_TRACK_DIRECTORY_FORMAL_CLIENT_PASSED');
} finally {
  await client?.connection.close(); await app?.stop(); await rm(root,{recursive:true,force:true});
}
