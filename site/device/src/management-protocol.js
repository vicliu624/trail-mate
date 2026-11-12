import {encode, decode, bytes, integer, equal} from '../../geocaching/src/protocol.js';
import {FRAME, CAPABILITY, HostlinkError} from './hostlink.js';

export const MANAGEMENT_VERSION = 1;
export const OPERATION = Object.freeze({CAPABILITIES: 0, AUTHOR_INFO: 1, AUTHOR_PROVE: 2,
  SESSION_CLOSE: 3, IDENTITY_BACKUP_EXPORT: 4, IDENTITY_BACKUP_RESTORE: 5, IDENTITY_RESTORE_FORMATS: 6, RADIO_IDENTITY_INFO: 7, RADIO_IDENTITY_BACKUP_EXPORT: 8, RADIO_IDENTITY_BACKUP_RESTORE: 9, ENTER_BOOTLOADER: 10, CONFIG_SCHEMA: 16, CONFIG_SNAPSHOT: 17, CONFIG_BEGIN: 18,
  CONFIG_PATCH: 19, CONFIG_VALIDATE: 20, CONFIG_COMMIT: 21, CONFIG_ABORT: 22,
  CONFIG_COMMIT_STATUS: 23, TRANSFER_BEGIN: 32, TRANSFER_CHUNK: 33,
  TRANSFER_STATUS: 34, TRANSFER_FINISH: 35, TRANSFER_ABORT: 36,
  TRANSFER_CLEANUP: 37, TRANSFER_LIST_OWNED: 38,
  JOB_GET: 48, JOB_LIST: 49, JOB_CANCEL: 50, CACHE_LIST_OWNED: 64,
  CACHE_DRAFT_GET: 65, CACHE_DRAFT_PUT: 66, CACHE_PREPARE: 67,
  CACHE_COMMIT: 68, CACHE_HISTORY: 69, CACHE_DIRECTORY_LIST: 70,
  CACHE_SIGN_BEGIN: 71, CACHE_SIGN_STATUS: 72, CACHE_SIGN_READ: 73, CACHE_SIGN_RELEASE: 74,
  CACHE_CONFIRM_BEGIN: 75, CACHE_CONFIRM_STATUS: 76, CACHE_CONFIRM_RELEASE: 77, TRACK_LIST: 80, TRACK_SNAPSHOT: 81,
  TRACK_PREPARE: 82, TRACK_COMMIT: 83, TRACK_HISTORY: 84, ROUTE_INSTALL: 85,
  TRACK_SNAPSHOT_BUILD: 86, TRACK_SNAPSHOT_CANCEL: 87, TRACK_SNAPSHOT_RELEASE: 88, TRACK_DIRECTORY_LIST: 89, TRACK_DIRECTORY_DISCOVER: 90,
  TRACK_READ_BEGIN: 91, TRACK_READ_STATUS: 92, TRACK_READ_CHUNK: 93, TRACK_READ_RELEASE: 94, ROUTE_INSTALL_STATUS: 95, TRACK_SIGN_PREPARE: 96, TRACK_CONFIRM_PUBLICATION: 97, TRACK_IMPORT_HISTORY: 98, TRACK_PACKAGE_SIGN_PREPARE: 99, TRACK_PACKAGE_INSTALL: 100, TRACK_PACKAGE_INSTALL_STATUS: 101});
export const MANAGEMENT_STATUS = Object.freeze({OK: 0, UNSUPPORTED: 1,
  UNAUTHORIZED: 2, INVALID_FIELD: 3, CONFLICT: 4, BUSY: 5,
  STORAGE_UNAVAILABLE: 6, LIMIT_EXCEEDED: 7, HASH_MISMATCH: 8,
  STALE_IDENTITY: 9, EXPIRED: 10, INTERNAL: 11});
const statusMessages = Object.freeze({
  1:'设备未实现此管理操作，请检查固件能力',
  2:'设备未授权此操作，请重新验证设备身份',
  3:'配置不符合设备约束，请检查字段值与依赖关系',
  4:'设备上的配置版本或传输状态已变化，请核对后重试',
  5:'设备正在处理其他操作，请稍后重试',
  6:'设备存储不可用，请检查 SD 卡或身份存储状态；修复后重新启动设备再连接',
  7:'数据超过设备容量限制，请减少内容或文件大小',
  8:'设备校验发现内容哈希不一致，上传未被接受',
  9:'设备作者身份已变化，请重新验证身份',
  10:'设备操作已过期，请重新开始',
  11:'设备处理失败，操作结果尚未确认'
});

function validate(value) {
  if (!Array.isArray(value) || value.length !== 5 || value[0] !== MANAGEMENT_VERSION ||
      !bytes(value[1],16) || !bytes(value[2],16) || !integer(value[3],0,65535)) {
    throw new HostlinkError('Invalid management envelope', 'PROTOCOL');
  }
  return value;
}
function clearByteTree(value) {
  if (value instanceof Uint8Array) value.fill(0);
  else if (Array.isArray(value)) value.forEach(clearByteTree);
}
export function encodeManagement(session, request, operation, body) {
  const raw = encode(validate([MANAGEMENT_VERSION, session, request, operation, body]));
  if (raw.length > 512) { raw.fill(0); throw new HostlinkError('Use object transfer for large management data', 'LIMIT_EXCEEDED'); }
  // Decode also enforces the deterministic subset supported by firmware.
  let checked;
  try {
    checked=decode(raw,512,{allowBoolean:true});
    validate(checked);
    return raw;
  } catch(error) {
    raw.fill(0);
    throw error;
  } finally { clearByteTree(checked); }
}
export function decodeManagement(raw) {
  const [version,session,request,code,body]=validate(decode(raw,512,{allowBoolean:true}));
  return {version,session,request,code,body};
}

// Handles management correlation independently of the HostLink frame sequence.
// A rejected/expired session never authorises writes through a USB VID/PID.
export class ManagementSession {
  #connection; #session; #random; #pending = new Map(); #unsubscribe; #closed = false;
  constructor(connection, {sessionId, randomBytes = size => crypto.getRandomValues(new Uint8Array(size)), timeoutMs = 10000} = {}) {
    if (!Number.isFinite(timeoutMs) || timeoutMs <= 0) throw new RangeError('Invalid management timeout');
    if (!connection.hello || !(connection.hello.capabilities & CAPABILITY.MANAGEMENT)) {
      throw new HostlinkError('Firmware does not support device management', 'UNSUPPORTED');
    }
    this.#connection=connection; this.#random=randomBytes;
    this.#session=(sessionId ?? randomBytes(16)).slice();
    if (!bytes(this.#session,16)) throw new TypeError('Invalid session ID');
    this.timeoutMs=timeoutMs;
    this.#unsubscribe=connection.subscribe(event=>{
      if (event.type==='state' && event.state==='disconnected') this.close();
      if (event.type==='frame' && event.frame.type===FRAME.MANAGEMENT_RESPONSE) this.#receive(event.frame);
    });
  }
  get id(){return this.#session.slice();}
  #receive(frame) {
    let response;
    try { response=decodeManagement(frame.payload); } catch { return; }
    if (!equal(response.session,this.#session)) return;
    const key=Array.from(response.request,x=>x.toString(16).padStart(2,'0')).join('');
    const pending=this.#pending.get(key);
    if (!pending) return;
    clearTimeout(pending.timer);this.#pending.delete(key);
    if (response.code!==MANAGEMENT_STATUS.OK) pending.reject(new HostlinkError(statusMessages[response.code]??`设备返回未知管理状态 ${response.code}`, response.code));
    else pending.resolve(response.body);
  }
  request(operation,body,{requestId}={}) {
    if (this.#closed || this.#connection.state!=='ready') return Promise.reject(new HostlinkError('Management session closed','DISCONNECTED'));
    if (this.#pending.size>=2) return Promise.reject(new HostlinkError('Wait for the current device operation','BUSY'));
    const request=(requestId ?? this.#random(16)).slice();
    const sensitive=operation===OPERATION.IDENTITY_BACKUP_RESTORE||operation===OPERATION.RADIO_IDENTITY_BACKUP_RESTORE;
    let payload;
    try { payload=encodeManagement(this.#session,request,operation,body); } catch(e) { return Promise.reject(e); }
    const key=Array.from(request,x=>x.toString(16).padStart(2,'0')).join('');
    if(this.#pending.has(key)){if(sensitive)payload.fill(0);return Promise.reject(new HostlinkError('Request is already pending','BUSY'));}
    const pendingRequest=new Promise((resolve,reject)=>{
      const timer=setTimeout(()=>{this.#pending.delete(key);reject(new HostlinkError('Management response timed out','TIMEOUT'));},this.timeoutMs);
      this.#pending.set(key,{resolve,reject,timer});
      // HostLink rejects malformed/unsupported commands using ACK. The actual
      // response remains correlated by the management request ID.
      this.#connection.request(FRAME.MANAGEMENT_REQUEST,payload,{responseType:FRAME.MANAGEMENT_RESPONSE,timeoutMs:this.timeoutMs,sensitive})
        .catch(error=>{const p=this.#pending.get(key);if(p){clearTimeout(p.timer);this.#pending.delete(key);p.reject(error);}});
    });
    return pendingRequest.finally(()=>{if(sensitive)payload.fill(0);});
  }
  close() {
    if(this.#closed)return;
    this.#closed=true;this.#unsubscribe();
    for(const p of this.#pending.values()){clearTimeout(p.timer);p.reject(new HostlinkError('Management session closed','DISCONNECTED'));}
    this.#pending.clear();
  }
}
