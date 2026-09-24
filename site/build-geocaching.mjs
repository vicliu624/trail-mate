import {build} from 'esbuild';
import {readFile, writeFile} from 'node:fs/promises';
await build({entryPoints:['geocaching/src/page.js','geocaching/src/reticulum-worker.js'],bundle:true,
  outdir:'geocaching/dist',format:'esm',target:'es2022',minify:true,legalComments:'linked',
  loader:{'.png':'file','.svg':'file','.woff2':'file','.woff':'file','.ttf':'file'},sourcemap:true,
  define:{__GEOCACHING_LOG_LEVEL__:JSON.stringify(process.env.GEOCACHING_LOG_LEVEL || 'ERROR')}});
const notices = await Promise.all(['animal-island-ui','react','react-dom','leaflet','seek-bzip','buffer','base64-js','ieee754'].map(async name =>
  `${name}\n${await readFile(`node_modules/${name}/LICENSE`,'utf8')}`));
notices.push(`@reticulum/core and @reticulum/lxmf\n${await readFile('geocaching/licenses/EUPL-1.2.txt','utf8')}`);
await writeFile('geocaching/dist/THIRD-PARTY.txt', notices.join('\n\n--------------------\n\n'));
console.log('Geocaching page and browser RNS/LXMF worker built.');
