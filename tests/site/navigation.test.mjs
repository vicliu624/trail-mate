import {test} from 'node:test';
import assert from 'node:assert/strict';
import {navigationHref,navigationText} from '../../site/navigation.js';
import {locales} from '../../site/i18n/registry.js';

test('every supported locale has complete navigation and task descriptions',()=>{
 const keys=Object.keys(navigationText('en'));
 for(const {id} of locales){
  const text=navigationText(id);assert.deepEqual(Object.keys(text),keys);
  for(const key of keys)assert.ok(typeof text[key]==='string'&&text[key].trim(),`${id}:${key}`);
 }
});
test('tool, home and install links retain the Pages prefix and explicit locale',()=>{
 for(const {id} of locales)for(const root of ['https://example.org/','https://example.org/trail-mate/']){
  for(const page of ['home','device','tracks','geocaching','flash']){
   const url=new URL(navigationHref(page,id,root));
   assert.equal(url.origin,'https://example.org');assert.equal(url.searchParams.get('lang'),id);
   assert.equal(url.pathname,new URL(root).pathname+(['home','flash'].includes(page)?'':page+'/'));
   assert.equal(url.hash,page==='flash'?'#webflasher':'');
  }
 }
});
