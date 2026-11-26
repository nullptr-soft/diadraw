import { copyFileSync, mkdirSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { build } from 'esbuild';

// VS Code loads CommonJS; everything except the `vscode` module is bundled. The C++ core stays
// a separate ES module with its .wasm file (dist/core/), which @diadraw/wasm imports on first use.
const core = {
  name: 'diadraw-core',
  setup(b) {
    b.onResolve({ filter: /\/core\/diadraw\.mjs$/ }, () => ({
      path: './core/diadraw.mjs',
      external: true,
    }));
  },
};
const common = {
  bundle: true,
  platform: 'node',
  target: 'node20',
  format: 'cjs',
  sourcemap: true,
  external: ['vscode'],
  plugins: [core],
  logLevel: 'warning',
};

await build({ ...common, entryPoints: ['src/extension.ts'], outfile: 'dist/extension.cjs' });
await build({ ...common, entryPoints: ['src/server.ts'], outfile: 'dist/server.cjs' });

const wasm = join(dirname(createRequire(import.meta.url).resolve('@diadraw/wasm')), 'core');
mkdirSync('dist/core', { recursive: true });
for (const file of ['diadraw.mjs', 'diadraw.wasm'])
  copyFileSync(join(wasm, file), join('dist/core', file));
