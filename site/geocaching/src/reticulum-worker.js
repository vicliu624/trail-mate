import {DirectoryClient} from './protocol-client.js';
import {makeGpx} from './gpx-download.js';
import {SnapshotClient} from './snapshot-client.js';

let client = null, snapshot = null, liveReady = false, selected = null;
const notify = event => self.postMessage({event});
async function readSelected(provider, cacheId) {
  if (!provider) throw Error('Search for caches first');
  if (provider === snapshot) return provider.get(cacheId);
  const row = provider.rows.get(cacheId), copy = snapshot?.rows.get(cacheId);
  const matches = row && !row.conflict && copy && row.summary[1] === copy.summary[1] &&
    row.summary[2].length === copy.summary[2].length &&
    row.summary[2].every((byte, index) => byte === copy.summary[2][index]);
  try {
    if (!liveReady || provider !== client) throw Error('Map connection interrupted');
    return await provider.get(cacheId);
  } catch (error) {
    if (!matches) throw error;
    return snapshot.get(cacheId);
  }
}
self.onmessage = async ({data}) => {
  const {id, command, args = {}} = data;
  try {
    let result;
    if (command === 'snapshot') {
      const candidate = new SnapshotClient(notify);
      result = await candidate.load(args.url);
      snapshot = candidate;
      notify({type:'snapshot', updatedAt:result});
    } else if (command === 'connect') {
      await client?.close();
      liveReady = false;
      client = new DirectoryClient(event => {
        if (event.type === 'directory') liveReady = true;
        if (event.type === 'status' && event.state === 'disconnected') liveReady = false;
        notify(event);
      });
      await client.connect(args.url, null, args.discoverySeeds);
    } else if (command === 'disconnect') {
      await client?.close(); client = null; liveReady = false;
    } else {
      if (!client && !snapshot) throw Error('Directory unavailable');
      if (command === 'query') {
        selected = liveReady ? client : snapshot;
        if (!selected) throw Error('Still discovering public directories');
        try { await selected.query(args.bounds, args.stateMask, args.region, args.queryToken); }
        catch (error) {
          if (!snapshot || selected === snapshot) throw error;
          selected = snapshot;
          await selected.query(args.bounds, args.stateMask, args.region, args.queryToken);
        }
      }
      else if (command === 'more') { if (selected === client) await client.more(); }
      else if (command === 'get') result = await readSelected(selected, args.cacheId);
      else if (command === 'download') {
        if (!Array.isArray(args.cacheIds) || !args.cacheIds.length || args.cacheIds.length > 20) throw Error('Select 1–20 caches');
        const records = [], failures = [], provider = selected;
        for (const cacheId of args.cacheIds) {
          try { records.push((await readSelected(provider, cacheId)).signed); }
          catch (error) { failures.push({cacheId, message: error.message}); }
        }
        result = {gpx: records.length ? await makeGpx(records) : null, succeeded: records.length, failures};
      } else throw Error('Unknown command');
    }
    self.postMessage({id, result});
  } catch (error) {
    self.postMessage({id, error: error.message});
  }
};
