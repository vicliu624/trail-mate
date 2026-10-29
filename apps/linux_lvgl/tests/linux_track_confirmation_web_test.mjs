import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {mkdtemp, readFile, readdir, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join, dirname} from 'node:path';
import {Identity} from '../../../site/node_modules/@reticulum/core/src/index.js';
import {fixture, connect} from './linux_identity_test_transport.mjs';
import {proveDeviceAuthor} from '../../../site/device/src/author-identity.js';
import {OPERATION} from '../../../site/device/src/management-protocol.js';
import {encode} from '../../../site/geocaching/src/protocol.js';
import {encodeTrackManifest, verifyTrackManifest} from '../../../site/tracks/src/manifest.js';
import {signDeviceTrackManifest} from '../../../site/tracks/src/device-local-signature.js';
import {confirmDeviceTrackPublication} from '../../../site/tracks/src/device-ip-confirmation.js';

globalThis.crypto ??= webcrypto;
const id=()=>crypto.getRandomValues(new Uint8Array(16));
const hash=async bytes=>new Uint8Array(await crypto.subtle.digest('SHA-256',bytes));
for (const importHistory of [false,true]) {
  const root=await mkdtemp(join(tmpdir(),'trailmate-ip-confirmation-web-'));
  let app,client;
  try {
    app=await fixture(root,'seed'); const endpoint=await app.next('ENDPOINT'); client=await connect(endpoint);
    assert.ok([97,98].every(op=>client.operations.includes(op)));
    const author=await proveDeviceAuthor(client.session),kml=new TextEncoder().encode('<kml xmlns="http://www.opengis.net/kml/2.2"><Document><Placemark><Point><coordinates>1,2</coordinates></Point></Placemark></Document></kml>');
    const digest=await hash(kml),operationId=id(),kmlUploadId=id(),manifestUploadId=id();
    const raw=encodeTrackManifest([1,author.publicKey,id(),0,1,null,0,100,100,'IP confirmation','',[10000000,20000000,10000000,20000000],[0,null,null,1],[digest,kml.length,'application/vnd.google-earth.kml+xml',1]]);
    const signed=await signDeviceTrackManifest(client.session,{kml,manifest:raw,author,operationId,kmlUploadId,manifestUploadId},{operations:client.operations,prepareOptions:{retryMs:10,timeoutMs:20000}});
    const manifest=await verifyTrackManifest(signed.signed,{expectedAuthor:author.publicKey});
    const directory=await Identity.generate(),directoryKey=await directory.getPublicKey(),epoch=id(),requestId=id();
    const nameHash=(await Identity.fullHash(new TextEncoder().encode('lxmf.delivery'))).slice(0,10);
    const delivery=await Identity.truncatedHash(new Uint8Array([...nameHash,...directory.identityHash]));
    const rawReceipt=encode([1,requestId,manifest.objectId,manifest.revisionHash,1,0,digest,kml.length,directoryKey,delivery,epoch,1,100]);
    const signature=await directory.sign(new Uint8Array([...new TextEncoder().encode('TrailMate.Track.Receipt.v1\0'),...rawReceipt]));
    const receipt=encode([rawReceipt,signature]);
    const record={stage:'confirmed',draftId:'native-confirmation',draftVersion:1,authorKey:author.publicKey,requestId,directoryKey,delivery,epoch,signature:signed.signed[1],receipt,
      plan:{operationId,kmlUploadId,manifestUploadId,generation:author.generation,manifest:raw,fileHash:digest,fileBytes:kml.length}};
    const objectRoot=join(root,'trailmate/tracks',Buffer.from(manifest.objectId).toString('hex'));
    await client.connection.close(); client=await connect(endpoint);
    if(importHistory) {
      assert.equal(dirname(objectRoot),join(root,'trailmate/tracks'));
      await rm(objectRoot,{recursive:true,force:true}); // This fixture's temporary object only.
    }
    const options={id:id(),manifestId:id(),importHistory,operations:client.operations,retryMs:10,timeoutMs:20000};
    let result=await confirmDeviceTrackPublication(client.session,record,author,options);
    assert.ok(result.confirmed&&result.durable);
    const files=await readdir(objectRoot);
    assert.ok(files.some(name=>name.endsWith('.rcp'))&&files.some(name=>name.endsWith('.cmp')));
    assert.ok((await Promise.all(files.map(name=>readFile(join(objectRoot,name))))).some(bytes=>bytes.equals(Buffer.from(receipt))));
    await client.connection.close(); client=await connect(endpoint);
    result=await confirmDeviceTrackPublication(client.session,record,author,{...options,operations:client.operations});
    assert.ok(result.confirmed&&result.durable);
    const cleanup=[[options.id,7,receipt.length,await hash(receipt)],...(importHistory?[[options.manifestId,4,raw.length,await hash(raw)]]:[])];
    for(const binding of cleanup)assert.deepEqual(await client.session.request(OPERATION.TRANSFER_CLEANUP,binding),[]);
    assert.deepEqual((await proveDeviceAuthor(client.session)).publicKey,author.publicKey);
    console.log('LINUX_TRACK_CONFIRMATION_FORMAL_CLIENT_PASSED',importHistory?'history-import':'local-confirmation');
  } finally {
    await client?.connection.close(); await app?.stop(); await rm(root,{recursive:true,force:true});
  }
}
