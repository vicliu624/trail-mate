import test from 'node:test';
import assert from 'node:assert/strict';
import {canEditInterfaceAccess} from '../../site/device/src/config-interface-capabilities.js';
import {CAPABILITY} from '../../site/device/src/hostlink.js';
test('AutoInterface access controls require a bearer capability, not a model name',()=>{
 assert.equal(canEditInterfaceAccess(1),false);
 assert.equal(canEditInterfaceAccess(1,CAPABILITY.AUTO_INTERFACE_IFAC),true);
 assert.equal(canEditInterfaceAccess(2),true);
 assert.equal(canEditInterfaceAccess(0,CAPABILITY.AUTO_INTERFACE_IFAC),false);
 assert.equal(canEditInterfaceAccess(0,0,true),true);
 assert.equal(canEditInterfaceAccess(1,0,true),true);
});
