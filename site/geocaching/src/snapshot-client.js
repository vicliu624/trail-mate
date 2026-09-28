import {decode, validateSummary, verifySigned, viewportBoxes} from './protocol.js';
import {countryContains} from './country-boundaries.js';

const hex = bytes => Array.from(bytes, value => value.toString(16).padStart(2, '0')).join('');

// Public summaries load independently of WSS. Full objects remain immutable,
// are fetched on demand, and use the same author verification as live replies.
export class SnapshotClient {
  constructor(notify, fetcher = (...args) => fetch(...args)) { this.notify = notify; this.fetcher = fetcher; this.rows = new Map(); }

  async load(url) {
    this.base = new URL('.', url);
    const response = await this.fetcher(url, {cache:'no-cache', signal:AbortSignal.timeout(15000)});
    if (!response.ok) throw Error('Published directory unavailable');
    const text = await response.text();
    if (text.length > 16 * 1024 * 1024) throw Error('Published directory too large');
    const data = JSON.parse(text);
    if (data.version !== 1 || !Number.isFinite(Date.parse(data.updatedAt)) || !Array.isArray(data.summaries) || data.summaries.length > 20000) throw Error('Invalid published directory');
    const rows = new Map();
    for (const encoded of data.summaries) {
      if (typeof encoded !== 'string' || encoded.length > 1024) throw Error('Invalid published summary');
      const summary = validateSummary(decode(Uint8Array.from(atob(encoded), c => c.charCodeAt(0))));
      const id = hex(summary[0]);
      if (rows.has(id)) throw Error('Duplicate published cache');
      rows.set(id, {id, summary, conflict:false, sourceName:'Published directory'});
    }
    this.rows = rows; this.updatedAt = data.updatedAt;
    return this.updatedAt;
  }

  query(bounds, stateMask, region, queryToken) {
    const boxes = viewportBoxes(bounds), found = [];
    for (const row of this.rows.values()) {
      const s = row.summary;
      if (!(stateMask & (1 << s[3])) || !boxes.some(([south,west,north,east]) => s[4] >= south && s[4] <= north && s[5] >= west && s[5] <= east)) continue;
      if (region && !countryContains(region, s[4]/1e7, s[5]/1e7)) continue;
      found.push(row);
      if (found.length > 500) break;
    }
    this.notify({type:'results', queryToken, rows:found.slice(0,500), more:false, limited:found.length > 500, snapshotAt:this.updatedAt});
  }

  async get(id) {
    const row = this.rows.get(id);
    if (!row) throw Error('Cache not present in published directory');
    const response = await this.fetcher(new URL(`objects/${hex(row.summary[2])}.bin`, this.base), {signal:AbortSignal.timeout(15000)});
    if (!response.ok) throw Error('Published details unavailable');
    const raw = new Uint8Array(await response.arrayBuffer());
    const verified = await verifySigned(decode(raw, 8192), row.summary);
    return {...verified, isCurrent:false, snapshotAt:this.updatedAt, checkedAt:Date.now(), sourceName:'Published directory'};
  }
}
