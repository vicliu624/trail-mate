import {DirectoryClient} from './protocol-client.js';
import {makeGpx} from './gpx-download.js';

let client = null;
self.onmessage = async ({data}) => {
  const {id, command, args = {}} = data;
  try {
    let result;
    if (command === 'connect') {
      await client?.close();
      client = new DirectoryClient(event => self.postMessage({event}));
      await client.connect(args.url, null, args.discoverySeeds);
    } else if (command === 'disconnect') {
      await client?.close(); client = null;
    } else {
      if (!client) throw Error('Connect to Reticulum first');
      if (command === 'query') await client.query(args.bounds, args.stateMask, args.region, args.queryToken);
      else if (command === 'more') await client.more();
      else if (command === 'get') result = await client.get(args.cacheId);
      else if (command === 'download') {
        if (!Array.isArray(args.cacheIds) || !args.cacheIds.length || args.cacheIds.length > 20) throw Error('Select 1–20 caches');
        const records = [], failures = [];
        for (const cacheId of args.cacheIds) {
          try { records.push((await client.get(cacheId)).signed); }
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
