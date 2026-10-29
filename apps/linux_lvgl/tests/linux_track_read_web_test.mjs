import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {webcrypto} from 'node:crypto';
import {mkdtemp,readFile,readdir,rm} from 'node:fs/promises';
import {createConnection} from 'node:net';
import {createInterface} from 'node:readline';
import {Duplex} from 'node:stream';
import {once} from 'node:events';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {HostlinkConnection} from '../../../site/device/src/hostlink.js';
import {ManagementSession,OPERATION} from '../../../site/device/src/management-protocol.js';
import {proveDeviceAuthor} from '../../../site/device/src/author-identity.js';
import {decode} from '../../../site/geocaching/src/protocol.js';
import {verifyTrackManifest} from '../../../site/tracks/src/manifest.js';
import {createDeviceTrackReadSession} from '../../../site/tracks/src/device-shared-read.js';

globalThis.crypto??=webcrypto;
const root=await mkdtemp(join(tmpdir(),'trailmate-track-read-web-'));
const peer=spawn(process.argv[2],[root,process.argv[3]],{stdio:['pipe','pipe','inherit']});
const exited=once(peer,'exit');
let connection,reading;
try{
  const lines=createInterface({input:peer.stdout});
  const [endpoint]=await Promise.race([once(lines,'line'),exited.then(()=>{throw Error('Read fixture exited');})]);
  const url=new URL(endpoint);
  const port={async open(){this.socket=createConnection({host:url.hostname,port:Number(url.port)});await once(this.socket,'connect');const streams=Duplex.toWeb(this.socket);this.readable=streams.readable;this.writable=streams.writable;},async setSignals(){},async close(){this.socket?.destroy();}};
  connection=new HostlinkConnection(port);await connection.open();
  const session=new ManagementSession(connection),author=await proveDeviceAuthor(session),operations=(await session.request(OPERATION.CAPABILITIES,[]))[4];
  assert.ok([91,92,93,94].every(op=>operations.includes(op)));
  const [publicKey,delivery,epoch,encodedManifest]=decode(new Uint8Array(await readFile(join(root,'read-fixture.cmp'))),8192);
  const signed=decode(encodedManifest,4166),manifest=await verifyTrackManifest(signed);
  reading=await createDeviceTrackReadSession({session,directory:{publicKey,delivery,epoch,capabilitiesVerified:true,fileLimit:8388608,chunkLimit:1024,queryLimit:32},author,operations,assertCurrent(){},retryMs:10});
  const caps=await reading.request(0,[]);assert.deepEqual(caps[2],publicKey);
  const response=await reading.request(2,[manifest.objectId,manifest.revisionHash]);assert.deepEqual(response,[signed,1]);
  const [hash,total,offset,content]=await reading.request(8,[manifest.record[13][0],0,manifest.record[13][1]]);
  assert.deepEqual(hash,manifest.record[13][0]);assert.equal(total,content.length);assert.equal(offset,0);
  assert.deepEqual(new Uint8Array(await crypto.subtle.digest('SHA-256',content)),hash);
  assert.deepEqual(await reading.request(1,[[0,0,100,100],null,null,0,16,100,null]),[100,null,[]]);
  // The production session automatically released every response before the
  // next request. No cache bytes or storage lease may remain after cleanup.
  assert.deepEqual(await readdir(join(root,'trailmate/track-read')),[]);
  console.log('LINUX_TRACK_READ_FORMAL_CLIENT_PASSED');
}finally{
  reading?.close();await connection?.close();peer.stdin.end('quit\n');
  const timer=setTimeout(()=>peer.kill('SIGKILL'),5000);const [code,signal]=await exited;clearTimeout(timer);assert.equal(signal,null);assert.equal(code,0);
  await rm(root,{recursive:true,force:true});
}
