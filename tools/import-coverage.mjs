// How much of a corpus the importers carry over: per format (and per PlantUML diagram type), how
// many files import cleanly, which constructs are dropped most, and which shapes and styles are
// unknown. Imports through the WebAssembly core, with local includes read from disk.
//
// Usage (from the repo root, after `npm run build -w @diadraw/wasm`):
//   node tools/import-coverage.mjs corpus/plantuml [corpus/local-only/plantuml ...]

import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('..', import.meta.url));
const { loadDiaDraw } = await import(join(root, 'src/wasm/dist/index.js'));
const dd = await loadDiaDraw();

const dirs = process.argv.slice(2);
if (dirs.length === 0) {
  console.error('usage: node tools/import-coverage.mjs <folder>...');
  process.exit(2);
}

const FORMATS = {
  mermaid: /\.(mmd|mermaid)$/i,
  plantuml: /\.(puml|plantuml|pu|wsd)$/i,
  structurizr: /\.(dsl|json)$/i,
  drawio: /\.(drawio|dio|xml)$|\.drawio\.(png|svg)$/i,
  bpmn: /\.bpmn2?$/i,
};
const formatOf = (path) => Object.entries(FORMATS).find(([, re]) => re.test(path))?.[0];

function* walk(dir) {
  for (const name of readdirSync(dir).sort()) {
    if (name === '.git' || name === 'node_modules') continue;
    const path = join(dir, name);
    if (statSync(path).isDirectory()) yield* walk(path);
    else yield path;
  }
}

/** Local includes: a file, or every file under a directory, relative to the including file. */
const resolver = (file) => (path, from) => {
  const target = resolve(dirname(from ?? file), path);
  if (!existsSync(target)) return undefined;
  const paths = statSync(target).isDirectory() ? [...walk(target)] : [target];
  return paths.map((p) => ({ path: p, text: readFileSync(p, 'utf8') }));
};

const rows = {};
const constructs = {};
const shapes = {};
const styles = {};
const count = (into, key, n = 1) => (into[key] = (into[key] ?? 0) + n);
let failed = 0;

for (const dir of dirs.filter((d) => existsSync(d))) {
  for (const path of walk(dir)) {
    const format = formatOf(path);
    if (!format) continue;
    const bytes = readFileSync(path);
    const binary = /\.(png|svg)$/i.test(path);
    const text = bytes.toString('utf8');
    if (format === 'plantuml' && !/@start\w+/.test(text)) continue;
    if (format === 'structurizr' && path.endsWith('.json') && !text.includes('"model"')) continue;
    let report;
    try {
      report = dd.importDiagram(binary ? new Uint8Array(bytes) : text, path, {
        resolve: resolver(path),
      }).report;
    } catch (error) {
      failed++;
      console.error(`${relative(root, path)}: ${error.message}`);
      continue;
    }
    // PlantUML reports each diagram type it saw as a `type:<name>` statistic.
    const types = (format === 'plantuml' ? Object.keys(report.stats ?? {}) : [])
      .filter((k) => k.startsWith('type:'))
      .map((k) => `${format} ${k.slice(5)}`);
    for (const type of types.length ? types : [format]) {
      const prefix = type.split(' ')[1];
      const row = (rows[type] ??= { files: 0, clean: 0 });
      row.files++;
      const mine = report.dropped.filter(
        (d) => !prefix || !d.construct?.includes(':') || d.construct.startsWith(`${prefix}:`),
      );
      if (mine.length === 0) row.clean++;
    }
    for (const d of report.dropped) count(constructs, `${format} ${d.construct ?? d.message}`);
    for (const [k, v] of Object.entries(report.unknownShapes ?? {}))
      count(shapes, `${format} ${k}`, v);
    for (const [k, v] of Object.entries(report.unknownStyles ?? {}))
      count(styles, `${format} ${k}`, v);
  }
}

const top = (counts, n = 20) =>
  Object.entries(counts)
    .sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0]))
    .slice(0, n)
    .map(([k, v]) => `  ${k}: ${v}`)
    .join('\n') || '  (none)';
const table = Object.entries(rows)
  .sort((a, b) => b[1].files - a[1].files || a[0].localeCompare(b[0]))
  .map(
    ([t, r]) =>
      `  ${t.padEnd(28)} ${String(r.files).padStart(6)} ${String(r.clean).padStart(6)} ${`${Math.round((100 * r.clean) / r.files)}%`.padStart(6)}`,
  )
  .join('\n');
const files = Object.values(rows).reduce((n, r) => n + r.files, 0);
console.log(`Import coverage: ${files} imports, ${failed} failed

  format / type                 files  clean
${table}

most often dropped:
${top(constructs)}

unknown shapes:
${top(shapes, 10)}

unknown styles (kept as passthrough):
${top(styles, 10)}`);
