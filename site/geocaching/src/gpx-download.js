import {verifySigned} from './protocol.js';

const xml = value => String(value).replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;')
  .replaceAll('"', '&quot;').replaceAll("'", '&apos;');
const base64 = raw => btoa(String.fromCharCode(...raw));
const states = ['active', 'disabled', 'archived'];
const containers = ['not-specified', 'micro', 'small', 'regular', 'large', 'other'];
const groundspeakContainers = ['Not chosen', 'Micro', 'Small', 'Regular', 'Large', 'Other'];

export async function makeGpx(signedRecords) {
  if (!Array.isArray(signedRecords) || !signedRecords.length || signedRecords.length > 20) throw Error('Select 1–20 caches');
  const points = [];
  for (const signed of signedRecords) {
    const item = await verifySigned(signed), r = item.record;
    const description = `${r[9]}\n\n--- Reticulum Geocaching ---\nCache-ID: ${item.cacheId}\nRevision: ${r[3]}` +
      `\nState: ${states[r[5]]}\nDifficulty: ${(r[11] / 2).toFixed(1)}\nTerrain: ${(r[12] / 2).toFixed(1)}` +
      `\nContainer: ${containers[r[13]]}\nHint: ${r[10]}`;
    const timestamp = r[15] || r[14];
    const time = timestamp ? `<time>${new Date(timestamp * 1000).toISOString().replace('.000Z', 'Z')}</time>` : '';
    points.push(`<wpt lat="${(r[6] / 1e7).toFixed(7)}" lon="${(r[7] / 1e7).toFixed(7)}">${time}` +
      `<name>${xml(r[8])}</name><cmt>${xml(r[10])}</cmt><desc>${xml(description)}</desc>` +
      '<src>Reticulum Geocaching</src><sym>Geocache</sym><type>Geocache|Traditional Cache</type><extensions>' +
      `<groundspeak:cache available="${r[5] === 0 ? 'True' : 'False'}" archived="${r[5] === 2 ? 'True' : 'False'}">` +
      `<groundspeak:name>${xml(r[8])}</groundspeak:name><groundspeak:placed_by>Reticulum author ${item.authorHash}</groundspeak:placed_by>` +
      `<groundspeak:type>Traditional Cache</groundspeak:type><groundspeak:container>${groundspeakContainers[r[13]]}</groundspeak:container>` +
      `<groundspeak:difficulty>${(r[11] / 2).toFixed(1)}</groundspeak:difficulty><groundspeak:terrain>${(r[12] / 2).toFixed(1)}</groundspeak:terrain>` +
      '<groundspeak:short_description html="False">Public cache shared over Reticulum.</groundspeak:short_description>' +
      `<groundspeak:long_description html="False">${xml(r[9])}</groundspeak:long_description>` +
      `<groundspeak:encoded_hints>${xml(r[10])}</groundspeak:encoded_hints></groundspeak:cache>` +
      `<tm:record version="1"><tm:payload encoding="base64">${base64(signed[0])}</tm:payload>` +
      `<tm:signature encoding="base64">${base64(signed[1])}</tm:signature></tm:record></extensions></wpt>`);
  }
  const result = '<?xml version="1.0" encoding="UTF-8"?>\n' +
    '<gpx version="1.1" creator="Trail Mate" xmlns="http://www.topografix.com/GPX/1/1" ' +
    'xmlns:groundspeak="http://www.groundspeak.com/cache/1/0/1" xmlns:tm="urn:trailmate:geocaching:gpx:1">\n' +
    points.join('\n') + '\n</gpx>\n';
  if (new TextEncoder().encode(result).length > signedRecords.length * 65536) throw Error('GPX size limit');
  return result;
}
