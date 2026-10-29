import assert from 'node:assert/strict';
import {CAPABILITY} from '../src/hostlink.js';
import {canEditInterfaceAccess} from '../src/config-interface-capabilities.js';

assert.equal(canEditInterfaceAccess(2, 0), true);
assert.equal(canEditInterfaceAccess(1, 0), false);
assert.equal(canEditInterfaceAccess(1, CAPABILITY.AUTO_INTERFACE_IFAC), true);
assert.equal(canEditInterfaceAccess(0, CAPABILITY.AUTO_INTERFACE_IFAC), false);
assert.equal(canEditInterfaceAccess(1, 0, true), true);
assert.equal(canEditInterfaceAccess(0, 0, true), true);
console.log('Interface access uses advertised capability and preserves removal of retained unsupported access');
