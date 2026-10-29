import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {mkdtemp,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fixture,connect} from './linux_identity_test_transport.mjs';
import {proveDeviceAuthor} from '../../../site/device/src/author-identity.js';
import {ManagementSession,OPERATION,MANAGEMENT_STATUS as STATUS} from '../../../site/device/src/management-protocol.js';
import {saveDeviceDraft} from '../../../site/geocaching/src/device-draft-save.js';
import {readDeviceDraft} from '../../../site/geocaching/src/device-maintenance.js';
import {readDeviceDraftPage} from '../../../site/geocaching/src/device-draft-catalog.js';
import {encodeDraftEdit} from '../../../site/geocaching/src/device-draft.js';

globalThis.crypto??=webcrypto;
process.env.TRAIL_MATE_EXPERIMENTAL_GEOCACHING_DRAFTS='1';
const root=await mkdtemp(join(tmpdir(),'trailmate-web-draft-maintenance-'));
const id=()=>crypto.getRandomValues(new Uint8Array(16));
const hash=async bytes=>new Uint8Array(await crypto.subtle.digest('SHA-256',bytes));
const busyRequest=async(session,operation,body)=>{
  const deadline=performance.now()+5000;
  for(;;){
    try{return await session.request(operation,body);}
    catch(error){if(error.code!==STATUS.BUSY||performance.now()>=deadline)throw error;}
    await new Promise(resolve=>setTimeout(resolve,10));
  }
};
let app,client;
try{
  app=await fixture(root,'seed');let endpoint=await app.next('ENDPOINT');client=await connect(endpoint);
  const author=await proveDeviceAuthor(client.session);
  assert.ok([64,65,66].every(op=>client.operations.includes(op)));
  const draftId=id(),transferId=id(),fields={name:'Native web draft',description:'UTF-8 山 '.repeat(70),hint:'Under a rock',latitudeE7:10000000,longitudeE7:20000000};
  const first=await saveDeviceDraft(client.session,draftId,fields,0,author,{transferId,retryMs:10});
  assert.equal(first.generation,1);
  let snapshot=await readDeviceDraft(client.session,draftId,author,{retryMs:10});
  assert.ok(snapshot.editable);assert.equal(snapshot.draft.description,fields.description);
  const page=await readDeviceDraftPage(client.session,author,{retryMs:10});
  assert.equal(page.rows.length,1);assert.equal(page.rows[0].name,fields.name);assert.equal(page.rows[0].generation,1);
  await client.connection.close();client=await connect(endpoint);
  assert.equal((await saveDeviceDraft(client.session,draftId,fields,0,author,{transferId,retryMs:10})).generation,1);
  assert.equal((await readDeviceDraftPage(client.session,author)).catalogGeneration,page.catalogGeneration);
  const secondId=id();await saveDeviceDraft(client.session,secondId,{...fields,name:'Second draft'},0,author,{transferId:id(),retryMs:10});
  await assert.rejects(client.session.request(OPERATION.CACHE_LIST_OWNED,[1,page.catalogGeneration,author.generation]),error=>error.code===STATUS.CONFLICT);
  await assert.rejects(client.session.request(OPERATION.CACHE_DRAFT_GET,[draftId,0,320,0,author.generation+1]),error=>error.code===STATUS.STALE_IDENTITY);
  const resumedId=id(),resumedTransfer=id(),raw=encodeDraftEdit(resumedId,{...fields,name:'Resumed draft'},0),digest=await hash(raw);
  await client.session.request(OPERATION.TRANSFER_BEGIN,[resumedTransfer,2,raw.length,digest]);
  await client.session.request(OPERATION.TRANSFER_CHUNK,[resumedTransfer,0,raw.slice(0,320)]);
  await assert.rejects(client.session.request(OPERATION.TRANSFER_BEGIN,[id(),3,raw.length,digest]),error=>error.code===STATUS.BUSY);
  await client.connection.close();client=undefined;await app.stop();app=undefined;
  app=await fixture(root,'seed');endpoint=await app.next('ENDPOINT');client=await connect(endpoint);
  const refreshed=await proveDeviceAuthor(client.session);assert.deepEqual(refreshed.publicKey,author.publicKey);assert.equal(refreshed.generation,author.generation);
  assert.equal((await saveDeviceDraft(client.session,resumedId,{...fields,name:'Resumed draft'},0,author,{transferId:resumedTransfer,retryMs:10})).generation,1);
  snapshot=await readDeviceDraft(client.session,resumedId,author);assert.equal(snapshot.draft.name,'Resumed draft');
  // A transfer ID cannot silently select another kind's completed candidate.
  for(const kind of [1,3])await assert.rejects(client.session.request(OPERATION.TRANSFER_BEGIN,[resumedTransfer,kind,raw.length,digest]),error=>error.code===STATUS.CONFLICT);
  const other=new ManagementSession(client.connection);
  await assert.rejects(other.request(OPERATION.CACHE_DRAFT_PUT,[resumedTransfer,resumedId,0,author.generation]),error=>error.code===STATUS.UNAUTHORIZED);
  await client.session.request(OPERATION.SESSION_CLOSE,[]);
  assert.equal((await saveDeviceDraft(other,resumedId,{...fields,name:'Resumed draft'},0,author,{transferId:resumedTransfer,retryMs:10})).generation,1);
  await other.request(OPERATION.SESSION_CLOSE,[]);other.close();
  const configId=id();await busyRequest(client.session,OPERATION.TRANSFER_BEGIN,[configId,1,raw.length,digest]);
  await assert.rejects(client.session.request(OPERATION.TRANSFER_BEGIN,[id(),2,raw.length,digest]),error=>error.code===STATUS.BUSY);
  await busyRequest(client.session,OPERATION.TRANSFER_ABORT,[configId]);
  const final=await readDeviceDraftPage(client.session,author,{limit:16,retryMs:10});assert.equal(final.rows.length,3);
  console.log('LINUX_GEOCACHING_DRAFT_FORMAL_CLIENT_PASSED');
}finally{
  await client?.connection.close();await app?.stop();await rm(root,{recursive:true,force:true});
}
