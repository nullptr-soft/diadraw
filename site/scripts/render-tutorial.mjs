// Checks the tutorial's documents with the DiaDraw core and renders each of their views to
// public/tutorial/<file>.<view>.svg, where the tutorial page shows them. Any diagnostic, error or
// warning, fails the build, so the tutorial never teaches text the engine does not accept.
//
// Needs the core built for WebAssembly (src/wasm/dist, `npx turbo run build --filter=@diadraw/wasm`);
// tools/pages/build.mjs runs it after that. Without it the tutorial still builds, without images.

import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const site = fileURLToPath(new URL('..', import.meta.url));
const core = join(site, '../src/wasm/dist/index.js');
if (!existsSync(core)) {
  console.error('render-tutorial: build @diadraw/wasm first (src/wasm/dist is missing).');
  process.exit(1);
}
const { loadDiaDraw } = await import(pathToFileURL(core).href);
const dd = await loadDiaDraw();

const sources = join(site, 'src/tutorial');
const out = join(site, 'public/tutorial');
rmSync(out, { recursive: true, force: true });
mkdirSync(out, { recursive: true });

let failed = false;
let rendered = 0;
for (const file of readdirSync(sources)
  .filter((f) => f.endsWith('.draw'))
  .sort()) {
  const { model, diagnostics } = dd.parse(readFileSync(join(sources, file), 'utf8'));
  for (const d of diagnostics) {
    console.error(`site/src/tutorial/${file}:${d.line}:${d.column}: ${d.severity}: ${d.message}`);
    failed = true;
  }
  for (const view of Object.keys(model.views)) {
    writeFileSync(
      join(out, `${file.slice(0, -'.draw'.length)}.${view}.svg`),
      dd.renderSvg(model, view),
    );
    rendered += 1;
  }
}
if (failed) process.exit(1);
console.log(`render-tutorial: ${rendered} views into public/tutorial/`);
