import {Destination, DestType, Identity, Resource, Reticulum, toHex} from '@reticulum/core';
import {WebSocketClientInterface} from '@reticulum/core/src/interfaces/websocket.js';
import {LXMessage, LXMRouter} from '@reticulum/lxmf';
import {APP_TYPE, bytes, decode, encode, equal, integer, validText, validateSummary, verifySigned, viewportBoxes} from './protocol.js';
import {compressionProvider, MAX_LXMF_BYTES} from './compression.js';

// These limits apply to the dedicated worker's stack instance, before links
// are accepted. Large LXMF messages still use normal RNS Resource transport.
Resource.DEFAULT_MAX_SIZE = MAX_LXMF_BYTES;
Resource.DEFAULT_MAX_TOTAL_SIZE = MAX_LXMF_BYTES;
Resource.DEFAULT_MAX_PARTS = 128;
Resource.DEFAULT_MAX_SEGMENTS = 1;

class BrowserLXMRouter extends LXMRouter {
  observedLinks = new WeakSet();

  async recoverDirectLink(destinationHash) {
    // A TCP upstream can fail while the browser's WSS interface stays online.
    // The native transport may already have a new path, but a cached LXMF link
    // still belongs to the old path. Retrying on that link cannot recover it.
    const key = toHex(destinationHash), link = this.directLinks.get(key);
    this.directLinks.delete(key);
    if (link) await link.teardown().catch(() => {});
    this.rns.transport.routingTable.expireRoute(destinationHash);
    await this.rns.transport.requestPathAuto(destinationHash);
  }

  _attachLinkMessageListeners(link) {
    // The upstream router calls this on every send, including reused links.
    // Keep one listener pair per link so browsing doesn't accumulate handlers.
    if (this.observedLinks.has(link)) return;
    this.observedLinks.add(link);
    // @reticulum/lxmf 0.9.0 injects bz2 on accepted links and on outgoing
    // large messages, but not on a short request's outbound reply link.
    link.bz2 = compressionProvider;
    link.maxResourceSize = MAX_LXMF_BYTES;
    super._attachLinkMessageListeners(link);
  }
}

export class DirectoryClient {
  constructor(notify = () => {}) {
    this.notify = notify;
    this.directories = new Map();
    this.rows = new Map();
    this.pages = [];
    this.tail = Promise.resolve();
    this.generation = 0;
    this.pending = null;
    this.closed = false;
  }

  async connect(url, privateKey = null, discoverySeeds = []) {
    if (!Array.isArray(discoverySeeds) || discoverySeeds.length > 3 ||
        discoverySeeds.some(seed => typeof seed !== 'string' || !/^[0-9a-f]{32}$/i.test(seed))) throw Error('Invalid directory discovery seeds');
    const endpoint = new URL(url);
    if (endpoint.username || endpoint.password || endpoint.hash ||
        !(endpoint.protocol === 'wss:' || (endpoint.protocol === 'ws:' &&
          ['localhost', '127.0.0.1', '[::1]'].includes(endpoint.hostname)))) throw Error('Use a WSS endpoint');
    this.rns = new Reticulum({compressionProvider, logLevel: typeof __GEOCACHING_LOG_LEVEL__ === 'undefined' ? 'ERROR' : __GEOCACHING_LOG_LEVEL__});
    this.identity = privateKey ? await Identity.fromPrivateKey(privateKey) : await Identity.generate();
    this.router = new BrowserLXMRouter(this.identity, this.rns);
    await this.router.init();
    this.router.addEventListener('message', event => this.receive(event.detail.message).catch(() => {}));
    this.rns.transport.addEventListener('announce', event => this.discover(event.detail).catch(() => {}));
    this.interface = new WebSocketClientInterface({url: endpoint.href, framing: 'raw', maxReconnectTries: 5});
    this.interface.addEventListener('disconnected', () => {
      this.generation++;
      this.pending?.reject(Error('Map connection interrupted'));
      this.notify({type: 'status', state: 'disconnected'});
    });
    this.interface.addEventListener('connected', () => {
      this.notify({type: 'status', state: 'discovering'});
      this.enqueue(async () => {
        await this.router.announce('Trail Mate web visitor');
        // Operator-owned discovery hints only: discover() still verifies the
        // signed announcement and binds the service to its delivery identity.
        for (const seed of discoverySeeds) {
          await this.rns.transport.requestPathAuto(Uint8Array.from(seed.match(/../g), pair => parseInt(pair, 16)));
        }
        // A service may announce only every six hours. Revalidate known peers
        // immediately after reconnect instead of leaving the UI disabled until
        // another discovery announce happens to arrive.
        for (const entry of [...this.directories.values()].filter(d => d.ready).slice(0, 3)) {
          try {
            await this.router.recoverDirectLink(entry.destination);
            await this.checkDirectory(entry);
          } catch (error) {
            this.notify({type: 'source-error', name: entry.name, message: error.message});
          }
        }
      }).catch(() => {});
    });
    // Bind before the first dial. A failed first dial can reconnect later;
    // attaching only after connect() resolves leaves that interface orphaned.
    this.rns.addInterface(this.interface, true);
    await this.interface.connect();
  }

  enqueue(action) {
    const task = this.tail.then(() => { if (this.closed) throw Error('Connection closed'); return action(); });
    this.tail = task.catch(() => {});
    return task;
  }

  async discover({destinationHash, identity, appData, packet}) {
    if (this.closed || !(appData instanceof Uint8Array) || appData.length > 96) return;
    const service = await Destination.OUT('trailmate.geocache.directory', DestType.SINGLE, identity, this.rns);
    if (!equal(service.destinationHash, destinationHash)) return;
    const data = decode(appData, 96);
    if (!Array.isArray(data) || data.length !== 5 || data[0] !== 1 || !bytes(data[1], 16) ||
        !bytes(data[2], 16) || !validText(data[4], 40, false, true) ||
        !(integer(data[3], 0, Number.MAX_SAFE_INTEGER) || (typeof data[3] === 'bigint' && data[3] >= 0n && data[3] <= 0xffffffffffffffffn))) return;
    const delivery = await Destination.OUT('lxmf.delivery', DestType.SINGLE, identity, this.rns);
    if (!equal(data[1], delivery.destinationHash)) return;
    const key = toHex(data[1]);
    if (this.directories.has(key) || this.directories.size >= 32) return;
    const entry = {key, destination: data[1], identity, name: data[4], ready: false};
    this.directories.set(key, entry);
    await this.rns.transport.rememberIdentity(await packet.getHash(), data[1], await identity.getPublicKey(), null);
    this.enqueue(() => this.checkDirectory(entry))
      .catch(error => this.notify({type: 'source-error', name: entry.name, message: error.message}));
  }

  async checkDirectory(entry) {
    const caps = await this.request(entry, 0, []);
    if (!Array.isArray(caps) || caps.length !== 12 || !Array.isArray(caps[0]) || !caps[0].includes(1) ||
        !Array.isArray(caps[1]) || !caps[1].includes(1) || !Array.isArray(caps[2]) ||
        ![0,1,2,3,4].every(op => caps[2].includes(op)) || caps[3] !== 8192 || caps[4] !== 4096 ||
        !integer(caps[5], 1, 64) || caps[8] !== 2 || caps[10] !== 1) throw Error('Directory is not compatible');
    entry.ready = true;
    entry.limit = Math.min(20, caps[5]);
    this.notify({type: 'directory', name: entry.name, key: entry.key, count: [...this.directories.values()].filter(d => d.ready).length});
  }

  async receive(message) {
    const pending = this.pending;
    if (!pending || !equal(message.sourceHash, pending.directory.destination) ||
        !equal(message.destinationHash, this.router.deliveryDest.destinationHash) ||
        !await message.verifySignature(pending.directory.identity)) return;
    const field = key => message.fields instanceof Map ? message.fields.get(key) : message.fields[key];
    if (field(0xfb) !== APP_TYPE) return;
    const response = decode(field(0xfc));
    if (!Array.isArray(response) || response.length !== 6 || response[0] !== 1 || response[1] !== 1 ||
        response[2] !== pending.operation || !equal(response[3], pending.id)) return;
    if (this.pending !== pending) return;
    if (response[4] === 200) pending.resolve(response[5]);
    else if (integer(response[4], 400, 599) && Array.isArray(response[5])) {
      pending.reject(Error(`Directory ${response[4]}: ${String(response[5][0]).slice(0, 60)}`));
    }
  }

  async request(directory, operation, body) {
    if (this.pending) throw Error('A directory request is already active');
    const id = crypto.getRandomValues(new Uint8Array(16));
    const raw = encode([1, 0, operation, id, 8192, body]);
    let timer;
    const response = new Promise((resolve, reject) => {
      this.pending = {directory, operation, id, resolve, reject};
      timer = setTimeout(() => reject(Error('Directory response timed out')), 45000);
    });
    // Attach rejection handling before asynchronous link setup can finish.
    response.catch(() => {});
    const send = () => this.router.send(new LXMessage({sourceHash: this.router.deliveryDest.destinationHash,
      destinationHash: directory.destination, title: 'Trail Mate Geocache v1', content: 'Geocache request',
      fields: new Map([[0xfb, APP_TYPE], [0xfc, raw]])}), this.identity, null);
    let retry, attempts = 0, sending = false;
    const attempt = async () => {
      if (this.pending?.id !== id || sending || attempts >= 3) return;
      sending = true;
      attempts++;
      let delay = 20000;
      try {
        if (attempts > 1) await this.router.recoverDirectLink(directory.destination);
        if (this.pending?.id === id) await send();
      } catch (error) {
        // A transit node can answer from its old path cache while learning a
        // surviving uplink. Allow a fresh link attempt after that failed
        // handshake, within the same deadline and using the same request ID.
        delay = 1000;
        if (this.pending?.id === id && attempts >= 3) this.pending.reject(error);
      } finally {
        sending = false;
        if (this.pending?.id === id && attempts < 3) retry = setTimeout(attempt, delay);
      }
    };
    try {
      void attempt();
      return await response;
    } finally {
      clearTimeout(timer); clearTimeout(retry);
      this.pending = null;
    }
  }

  query(bounds, stateMask = 3) {
    if (!integer(stateMask, 1, 7)) return Promise.reject(Error('Invalid state filter'));
    const generation = ++this.generation;
    return this.enqueue(async () => {
      this.rows.clear(); this.pages = [];
      const sources = [...this.directories.values()].filter(d => d.ready).slice(0, 3);
      if (!sources.length) throw Error('No verified public directory is available');
      let succeeded = 0;
      for (const directory of sources) for (const bbox of viewportBoxes(bounds)) {
        if (generation !== this.generation) return;
        const page = {directory, bbox, stateMask, cursor: null, snapshot: null, last: null};
        try { await this.readPage(page, generation); succeeded++; }
        catch (error) { this.notify({type: 'source-error', name: directory.name, message: error.message}); }
      }
      if (!succeeded) throw Error('All directory queries failed. Results are not live.');
      this.emitRows(generation);
    });
  }

  async readPage(page, generation) {
    const result = await this.request(page.directory, 2, [page.bbox, page.stateMask, null, page.directory.limit, page.cursor]);
    if (generation !== this.generation) return;
    if (!Array.isArray(result) || result.length !== 4 || !bytes(result[0], 16) || !Array.isArray(result[1]) ||
        result[1].length > page.directory.limit || !(result[2] === null || (result[2] instanceof Uint8Array && result[2].length >= 1 && result[2].length <= 64)) ||
        !integer(result[3], 0, 604800) || (result[2] !== null && !result[1].length) ||
        (page.snapshot && !equal(page.snapshot, result[0]))) throw Error('Invalid query page');
    for (const summary of result[1]) {
      validateSummary(summary);
      const id = toHex(summary[0]);
      if (page.last && id <= page.last) throw Error('Directory page order changed');
      page.last = id;
      const previous = this.rows.get(id);
      if (!previous && this.rows.size >= 500) break;
      if (previous && previous.summary[1] === summary[1] && !equal(previous.summary[2], summary[2])) {
        previous.conflict = true; continue;
      }
      if (!previous || previous.summary[1] < summary[1]) this.rows.set(id, {id, summary, source: page.directory.key,
        sourceName: page.directory.name, checkedAt: Date.now(), conflict: false});
    }
    page.snapshot = result[0]; page.cursor = result[2];
    if (page.cursor && this.rows.size < 500) this.pages.push(page);
    this.emitRows(generation);
  }

  more() {
    const generation = this.generation;
    return this.enqueue(async () => {
      const pages = this.pages.splice(0);
      for (const page of pages) {
        if (generation !== this.generation) return;
        await this.readPage(page, generation);
      }
      this.emitRows(generation);
    });
  }

  emitRows(generation) {
    if (generation === this.generation) this.notify({type: 'results', generation,
      rows: [...this.rows.values()], more: this.pages.length > 0, limited: this.rows.size >= 500});
  }

  get(id) {
    const row = this.rows.get(id);
    return this.enqueue(async () => {
      if (!row || row.conflict) throw Error('Cache is missing or sources disagree');
      const body = await this.request(this.directories.get(row.source), 3, [row.summary[0], row.summary[2], null]);
      if (!Array.isArray(body) || body.length !== 3 || !integer(body[1], 0, 1) || !integer(body[2], 0, 1)) throw Error('Invalid get response');
      if (body[2]) throw Error('Directory reports an author version conflict');
      const verified = await verifySigned(body[0], row.summary);
      return {...verified, isCurrent: body[1] === 1, checkedAt: Date.now(), sourceName: row.sourceName};
    });
  }

  async close() {
    this.closed = true; this.generation++;
    this.pending?.reject(Error('Disconnected'));
    await this.interface?.disconnect();
    await this.rns?.stop();
  }
}
