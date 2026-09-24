import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {countryBounds,countryContains,countryMatches} from '../../site/geocaching/src/country-boundaries.js';
import {nearbyBounds,requestNearbyPosition} from '../../site/geocaching/src/nearby-location.js';
import {DirectoryClient} from '../../site/geocaching/src/protocol-client.js';

const feature = coordinates => ({type:'Feature',properties:{name:'Test',zh:'测试'},geometry:{type:'Polygon',coordinates}});
const square=feature([[[0,0],[10,0],[10,10],[0,10],[0,0]],[[3,3],[7,3],[7,7],[3,7],[3,3]]]);

test('polygon boundaries include edges but exclude holes and neighboring points',()=>{
  assert.equal(countryContains(square,1,1),true);
  assert.equal(countryContains(square,1,0),true);
  assert.equal(countryContains(square,5,5),false);
  assert.equal(countryContains(square,11,1),false);
});

test('date-line regions keep both hemispheres and a compact query extent',()=>{
  const region=feature([[[170,-10],[-170,-10],[-170,10],[170,10],[170,-10]]]);
  assert.equal(countryContains(region,0,179),true);
  assert.equal(countryContains(region,0,-179),true);
  assert.equal(countryContains(region,0,0),false);
  assert.deepEqual(countryBounds(region),[-10,170,10,190]);
});

test('vendored countries cover representative mainland and island points',async()=>{
  const data=JSON.parse(await readFile(new URL('../../site/geocaching/data/countries.json',import.meta.url)));
  assert.equal(data.features.length,242);
  for(const [id,lat,lon] of [['CHN',39.9,116.4],['JPN',35.68,139.69],['FJI',-17.8,178],['USA',21.31,-157.86]]) {
    const region=data.features.find(f=>f.id===id);
    assert.ok(countryContains(region,lat,lon),id);
    assert.ok(!countryContains(region,0,0),id);
  }
  assert.ok(countryMatches(data.features.find(f=>f.id==='CHN'),'中国'));
});

test('nearby location requests low accuracy and rounds the query center',async()=>{
  let options;
  const result=await requestNearbyPosition({getCurrentPosition(success,failure,opts){
    options=opts; success({coords:{latitude:31.234567,longitude:121.456789,accuracy:1000}});
  }});
  assert.equal(options.enableHighAccuracy,false);
  assert.equal(options.timeout,10000);
  assert.ok(Math.abs((result[0]+result[2])/2-31.23)<1e-10);
  assert.ok(Math.abs((result[1]+result[3])/2-121.46)<1e-10);
  assert.ok(nearbyBounds(89.99,179.99).every(Number.isFinite));
  await assert.rejects(requestNearbyPosition(null));
  await assert.rejects(requestNearbyPosition({getCurrentPosition(success,failure){failure(Error('denied'));}}));
});

test('IHO oceans retain polar and date-line coverage without including inland points',async()=>{
  const data=JSON.parse(await readFile(new URL('../../site/geocaching/data/seas.json',import.meta.url)));
  assert.equal(data.features.length,101);
  const sea=name=>data.features.find(f=>f.properties.name===name);
  for(const lon of [-179,-90,0,90,179]) {
    assert.ok(countryContains(sea('Arctic Ocean'),89,lon),`Arctic ${lon}`);
    assert.ok(countryContains(sea('Southern Ocean'),-65,lon),`Southern ${lon}`);
  }
  assert.ok(countryContains(sea('South Pacific Ocean'),-30,170));
  assert.ok(countryContains(sea('South Pacific Ocean'),-30,-170));
  assert.ok(countryContains(sea('North Atlantic Ocean'),0,0));
  assert.equal(data.features.some(f=>countryContains(f,40,116)),false);
  assert.equal(countryBounds(sea('Arctic Ocean'))[1],-180);
  assert.equal(countryBounds(sea('Arctic Ocean'))[3],180);
});

test('empty filtered pages retain cursors and later matches can be loaded',async()=>{
  const events=[], client=new DirectoryClient(event=>events.push(event));
  const directory={key:'test',ready:true,limit:20}; client.directories.set('test',directory);
  const summary=(key,lat,lon)=>[new Uint8Array(32).fill(key),1,new Uint8Array(32).fill(key),0,lat*1e7,lon*1e7,'Example',2,2,0,100];
  let calls=0;
  client.request=async()=>[new Uint8Array(16),[summary(++calls,calls<=6?5:1,calls<=6?5:1)],calls<=6?new Uint8Array([calls]):null,60];
  await client.query([0,0,10,10],3,square,7);
  assert.equal(client.rows.size,0); assert.equal(client.pages.length,1);
  assert.equal(calls,5,'Automatically advances five empty filtered pages, then yields');
  await client.more();
  assert.equal(client.rows.size,1); assert.equal(client.pages.length,0);
  assert.equal(calls,7);
  assert.equal(events.at(-1).queryToken,7);
});
