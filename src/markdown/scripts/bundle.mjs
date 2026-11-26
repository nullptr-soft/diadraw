// Bundles diadraw.js, the script for any page with DiaDraw code blocks (src/embed.ts), into
// dist/embed/ with the core's diadraw.wasm beside it, where the bundled loader looks for it.

import { copyFileSync, mkdirSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { build } from 'esbuild';

const here = fileURLToPath(new URL('..', import.meta.url));
const out = join(here, 'dist/embed');
mkdirSync(out, { recursive: true });

await build({
  entryPoints: [join(here, 'src/embed.ts')],
  outfile: join(out, 'diadraw.js'),
  bundle: true,
  format: 'esm',
  platform: 'browser',
  target: 'es2022',
  minify: true,
  // The Emscripten loader imports these only when it runs in Node.
  external: ['node:*'],
  legalComments: 'none',
  logLevel: 'warning',
});

const wasm = join(
  dirname(createRequire(import.meta.url).resolve('@diadraw/wasm')),
  'core/diadraw.wasm',
);
copyFileSync(wasm, join(out, 'diadraw.wasm'));
console.log('@diadraw/markdown: dist/embed/diadraw.js and diadraw.wasm');
