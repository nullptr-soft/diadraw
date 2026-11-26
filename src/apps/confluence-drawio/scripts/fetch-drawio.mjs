// Fetches the draw.io web app that the editor bundles: the `draw.war` of a pinned jgraph/drawio
// release (a zip of the prebuilt web app), checked against its SHA-256, unpacked into
// vendor/drawio-<version>/ without what an embedded editor never loads, with draw.io's licence.
//
// The download is kept in vendor/ (ignored by git), so later builds work offline. Run it on its
// own with `npm run fetch-drawio`; `npm run build` runs it when the folder is missing.
//
// To move to a newer draw.io: change VERSION, run this script once with the new version and no
// hashes to print them, put them in WAR_SHA256 and LICENSE_SHA256, then build and try the editor.

import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, renameSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { unzipSync } from 'fflate';

export const VERSION = '32.7.7';
const WAR_URL = `https://github.com/jgraph/drawio/releases/download/v${VERSION}/draw.war`;
const WAR_SHA256 = 'e84ac13ee1e6c2bded36b6e920a2e8abfa37df277a6d9697ae8dc28f7bbeb9f1';
const LICENSE_URL = `https://raw.githubusercontent.com/jgraph/drawio/v${VERSION}/LICENSE`;
const LICENSE_SHA256 = '43070e2d4e532684de521b885f385d0841030efa2b1a20bafb76133a5e1379c1';

const here = fileURLToPath(new URL('..', import.meta.url));
const vendor = join(here, 'vendor');
/** Where the unpacked web app is: the build copies this folder to dist/editor/drawio. */
export const drawioDir = join(vendor, `drawio-${VERSION}`);

/**
 * What the embedded editor never loads: the Java servlet side, source maps, the unminified
 * sources (draw.io loads them only with `dev=1`), the offline service worker, the pages and
 * bundles of draw.io's other integrations, and the viewer (draw.io links its "embed as HTML"
 * export to viewer.diagrams.net, not to a local copy). That takes a third off the bundle.
 */
const SKIP = [
  /^(WEB-INF|META-INF)\//,
  /\.map$/,
  /^js\/(diagramly|grapheditor)\//,
  /^js\/(integrate|viewer|viewer-static)\.min\.js$/,
  /^(service-worker|workbox-[\w-]+)\.js$/,
  /^connect\//,
  /^(teams|onedrive3|vsdxImporter|clear|open)\.html$/,
  /^monday-app-association\.json$/,
];

function sha256(bytes) {
  return createHash('sha256').update(bytes).digest('hex');
}

async function download(url, expected, cache) {
  if (existsSync(cache)) {
    const bytes = readFileSync(cache);
    if (!expected || sha256(bytes) === expected) return bytes;
    rmSync(cache);
  }
  console.log(`fetch-drawio: downloading ${url}`);
  const response = await fetch(url);
  if (!response.ok) throw new Error(`${url}: HTTP ${response.status}`);
  const bytes = new Uint8Array(await response.arrayBuffer());
  const actual = sha256(bytes);
  if (!expected) {
    console.log(`fetch-drawio: ${url} has SHA-256 ${actual}; pin it in scripts/fetch-drawio.mjs`);
  } else if (actual !== expected) {
    throw new Error(`${url}: SHA-256 is ${actual}, expected ${expected}`);
  }
  mkdirSync(dirname(cache), { recursive: true });
  writeFileSync(cache, bytes);
  return bytes;
}

/** Makes sure vendor/drawio-<VERSION>/ holds the web app; returns its path. */
export async function fetchDrawio() {
  const marker = join(drawioDir, '.complete');
  if (existsSync(marker)) return drawioDir;

  const war = await download(WAR_URL, WAR_SHA256, join(vendor, `draw-${VERSION}.war`));
  const license = await download(LICENSE_URL, LICENSE_SHA256, join(vendor, `LICENSE-${VERSION}`));

  // Unpacks into a temporary folder and renames it, so an interrupted run leaves nothing half done.
  const partial = `${drawioDir}.partial`;
  rmSync(partial, { recursive: true, force: true });
  let files = 0;
  let bytes = 0;
  const entries = unzipSync(war, {
    filter: (file) => !file.name.endsWith('/') && !SKIP.some((pattern) => pattern.test(file.name)),
  });
  for (const [name, content] of Object.entries(entries)) {
    if (name.split('/').some((part) => part === '..' || part === '') || name.startsWith('/'))
      throw new Error(`draw.war holds an unsafe path: ${name}`);
    const path = join(partial, name);
    mkdirSync(dirname(path), { recursive: true });
    writeFileSync(path, content);
    files += 1;
    bytes += content.length;
  }
  writeFileSync(join(partial, 'LICENSE'), license);
  writeFileSync(
    join(partial, 'README.txt'),
    `draw.io ${VERSION} (https://github.com/jgraph/drawio), © JGraph Ltd, under the Apache\n` +
      `License 2.0 in LICENSE. Unchanged files from its draw.war, without what an embedded editor\n` +
      `never loads. Bundled by the DiaDraw draw.io diagram app, which is not made, endorsed or\n` +
      `supported by JGraph or draw.io.\n`,
  );
  writeFileSync(join(partial, '.complete'), '');
  rmSync(drawioDir, { recursive: true, force: true });
  renameSync(partial, drawioDir);
  console.log(
    `fetch-drawio: draw.io ${VERSION}, ${files} files, ${(bytes / 1e6).toFixed(1)} MB in ${drawioDir}`,
  );
  return drawioDir;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  await fetchDrawio();
}
