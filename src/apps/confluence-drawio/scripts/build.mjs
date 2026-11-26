// Builds the app's two Custom UI resources into dist/: dist/macro (the macro in the page) and
// dist/editor (the draw.io editor around it), each an index.html with its bundled script and the
// shared stylesheet. dist/editor/drawio is draw.io itself, from scripts/fetch-drawio.mjs, which
// the editor loads in a frame from the app's own files.
//
// DRAWIO_URL builds an editor that loads draw.io from that URL instead (https://embed.diagrams.net/
// or a self-hosted copy) and bundles none; its origin must then be in manifest.yml's
// permissions.external.frames (see README.md).
//
// `--dev` builds the local preview into dist-dev/ instead, with a stand-in for @forge/bridge
// (src/dev/bridge.ts) and a page that shows the editor above the macro.

import { copyFileSync, cpSync, mkdirSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { build } from 'esbuild';
import { VERSION, fetchDrawio } from './fetch-drawio.mjs';

const here = fileURLToPath(new URL('..', import.meta.url));
const dev = process.argv.includes('--dev');
const out = join(here, dev ? 'dist-dev' : 'dist');
const drawioUrl = process.env.DRAWIO_URL ?? '';
if (drawioUrl) new URL(drawioUrl); // Fails the build on a malformed URL.

rmSync(out, { recursive: true, force: true });
for (const name of ['macro', 'editor']) {
  const dir = join(out, name);
  mkdirSync(dir, { recursive: true });
  await build({
    entryPoints: [join(here, `src/${name}.ts`)],
    outfile: join(dir, `${name}.js`),
    bundle: true,
    format: 'esm',
    platform: 'browser',
    target: 'es2022',
    minify: !dev,
    sourcemap: dev,
    define: { DRAWIO_URL: JSON.stringify(drawioUrl) },
    alias: dev ? { '@forge/bridge': join(here, 'src/dev/bridge.ts') } : {},
    legalComments: 'none',
    logLevel: 'warning',
  });
  copyFileSync(join(here, `static/${name}.html`), join(dir, 'index.html'));
  copyFileSync(join(here, 'static/styles.css'), join(dir, 'styles.css'));
}
if (!drawioUrl) {
  cpSync(await fetchDrawio(), join(out, 'editor/drawio'), {
    recursive: true,
    filter: (source) => !source.endsWith('.complete'),
  });
}
if (dev) copyFileSync(join(here, 'static/dev.html'), join(out, 'index.html'));
console.log(
  `@diadraw/confluence-drawio: ${dev ? 'dist-dev' : 'dist'}/macro and editor` +
    (drawioUrl ? `, draw.io from ${drawioUrl}` : `, draw.io ${VERSION} bundled`),
);
