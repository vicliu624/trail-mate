import {CAPABILITY} from './hostlink.js';

export function canEditInterfaceAccess(type, capabilities = 0, hasAccess = false) {
  if (hasAccess) return true; // Unsupported retained access must remain removable.
  return type === 2 || (type === 1 && (capabilities & CAPABILITY.AUTO_INTERFACE_IFAC) !== 0);
}
