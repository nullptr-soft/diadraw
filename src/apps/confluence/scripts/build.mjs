// Builds the app's two Custom UI resources into dist/: dist/macro (the macro in the page) and
// dist/editor (its editor), each an index.html with its bundled script, the shared stylesheet
// and the core's diadraw.wasm, which the bundled loader fetches from beside the script; and
// dist/cli.js, the publisher for Node.
//
// `--dev` builds the local preview into dist-dev/ instead, with a stand-in for @forge/bridge
// (src/dev/bridge.ts) and a page that shows the editor above the macro.

import { copyFileSync, mkdirSync, rmSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { build } from 'esbuild';

const here = fileURLToPath(new URL('..', import.meta.url));
const dev = process.argv.includes('--dev');
const out = join(here, dev ? 'dist-dev' : 'dist');
const wasm = join(
  dirname(createRequire(import.meta.url).resolve('@diadraw/wasm')),
  'core/diadraw.wasm',
);

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
    // The Emscripten loader imports these only when it runs in Node.
    external: ['node:*'],
    alias: dev ? { '@forge/bridge': join(here, 'src/dev/bridge.ts') } : {},
    legalComments: 'none',
    logLevel: 'warning',
  });
  copyFileSync(join(here, `static/${name}.html`), join(dir, 'index.html'));
  copyFileSync(join(here, 'static/styles.css'), join(dir, 'styles.css'));
  copyFileSync(wasm, join(dir, 'diadraw.wasm'));
}
if (!dev) {
  // The publisher, `diadraw-confluence publish` (src/cli.ts), for Node. It loads the core from
  // @diadraw/wasm at run time rather than bundling it.
  await build({
    entryPoints: [join(here, 'src/cli.ts')],
    outfile: join(out, 'cli.js'),
    bundle: true,
    format: 'esm',
    platform: 'node',
    target: 'node20',
    external: ['@diadraw/wasm', '@diadraw/wasm/*'],
    banner: { js: '#!/usr/bin/env node' },
    legalComments: 'none',
    logLevel: 'warning',
  });
}
if (dev) copyFileSync(join(here, 'static/dev.html'), join(out, 'index.html'));
console.log(
  `@diadraw/confluence: ${dev ? 'dist-dev/macro and editor' : 'dist/macro, editor and cli.js'}`,
);
