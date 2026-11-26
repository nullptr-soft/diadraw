// Regenerates assets/drawio/ from draw.io's stencils at a pinned version, converted by the
// core (dd::io::read_stencil_sets) through the WebAssembly build.
//
//   npm run build -w @diadraw/wasm
//   node tools/gen/drawio_stencils.mjs [--from <jgraph/drawio checkout>]
//
// Without --from, the pinned files are downloaded from GitHub. Either way the stencil bundle must
// match the pinned SHA-256, so the output is the same on every machine.
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { gzipSync } from 'node:zlib';

export const DRAWIO = {
  version: '32.6.9',
  commit: '3e707fd20b6930778d6f6bf6e4d0079b4bce366b',
  /** SHA-256 of src/main/webapp/js/stencils.min.js at that commit. */
  sha256: 'b6acdcf854ae27368cbc3108564d93de1eec91090126d2ebca9a69664bbecb3d',
};

const VENDOR =
  /^(alibaba|android|atlassian|aws|azure|cisco|citrix|gcp|ibm|kubernetes|mscae|office|openstack|salesforce|sap|veeam|vvd|weblogos|webicons|dropbox|google|microsoft|oracle|red_hat|redhat|fortinet|juniper|arista|hpe|dell|vmware|nutanix|lean_mapping_logos)/;

const repo = fileURLToPath(new URL('../..', import.meta.url));
const root = join(repo, 'assets');
const out = join(root, 'drawio');
const from = process.argv.includes('--from')
  ? process.argv[process.argv.indexOf('--from') + 1]
  : undefined;

async function read(path) {
  if (from) return readFileSync(join(from, path));
  const url = `https://raw.githubusercontent.com/jgraph/drawio/${DRAWIO.commit}/${path}`;
  const res = await fetch(url);
  if (!res.ok) throw new Error(`${url}: HTTP ${res.status}`);
  return Buffer.from(await res.arrayBuffer());
}

const stencils = await read('src/main/webapp/js/stencils.min.js');
const sha = createHash('sha256').update(stencils).digest('hex');
if (sha !== DRAWIO.sha256)
  throw new Error(`stencils.min.js is ${sha}, expected the pinned ${DRAWIO.sha256}`);
const restriction = (await read('src/main/webapp/stencils/LICENSE')).toString('utf8').trim();

const { loadDiaDraw } = await import(join(repo, 'src/wasm/dist/index.js'));
const dd = await loadDiaDraw();
const { sets, colors } = dd.readStencilSets(stencils.toString('utf8'), 'stencils.min.js');
const [STENCIL_FILL, STENCIL_STROKE, GLYPH_FILL] = [colors.fill, colors.stroke, colors.glyph];

const LICENSE_NOTES = `${restriction}\n\nThese stencils come from draw.io (https://github.com/jgraph/drawio, Apache-2.0, Copyright JGraph Ltd), version ${DRAWIO.version}.`;
const VENDOR_NOTE =
  "This set shows a vendor's product icons and logos, which remain subject to that vendor's trademark and icon usage terms.";

rmSync(out, { recursive: true, force: true });
mkdirSync(join(out, 'sets'), { recursive: true });
const infos = [];
const styles = {};
const names = new Set();
for (const set of [...sets].sort((a, b) => a.file.localeCompare(b.file))) {
  if (set.shapes.length === 0) continue;
  const short = set.name.replace(/^mxgraph\./, '');
  let name = `drawio-${short.replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '')}`;
  for (let n = 2; names.has(name); n++) name = `drawio-${short.replace(/[^a-z0-9]+/g, '-')}-${n}`;
  names.add(name);
  const vendor = VENDOR.test(short);
  const bundle = {
    format: 'diadraw-library',
    formatVersion: 1,
    name,
    version: DRAWIO.version,
    description: `draw.io's ${set.name} stencils (${set.file}), converted to SVG.`,
    license: 'Apache-2.0',
    licenseNotes: vendor ? `${LICENSE_NOTES}\n\n${VENDOR_NOTE}` : LICENSE_NOTES,
    author: 'JGraph Ltd (draw.io)',
    homepage: 'https://github.com/jgraph/drawio',
    source: {
      kind: 'drawio-stencils',
      url: 'https://github.com/jgraph/drawio',
      commit: DRAWIO.commit,
      sets: [set.file],
    },
    colors: { fill: [STENCIL_FILL, GLYPH_FILL], stroke: [STENCIL_STROKE] },
    shapes: Object.fromEntries(
      set.shapes.map((s) => [
        s.id,
        {
          label: s.label,
          category: short.replace(/[._]/g, ' '),
          svg: s.body,
          viewBox: `0 0 ${s.width} ${s.height}`,
          width: s.width,
          height: s.height,
        },
      ]),
    ),
    mappings: { drawio: Object.fromEntries(set.shapes.map((s) => [s.style, s.id])) },
  };
  const gz = gzipSync(Buffer.from(JSON.stringify(bundle)), { level: 9 });
  writeFileSync(join(out, 'sets', `${name}.json.gz`), gz);
  const i = infos.length;
  infos.push({
    name,
    version: DRAWIO.version,
    source: set.file,
    prefix: set.name,
    shapes: set.shapes.length,
    bytes: gz.length,
    vendor,
  });
  for (const s of set.shapes) if (!(s.style in styles)) styles[s.style] = [i, s.id];
}
const index = {
  drawio: DRAWIO,
  license: 'Apache-2.0',
  licenseNotes: LICENSE_NOTES,
  sets: infos,
  styles,
};
writeFileSync(
  join(out, 'index.json.gz'),
  gzipSync(Buffer.from(JSON.stringify(index)), { level: 9 }),
);
const total = infos.reduce((n, s) => n + s.bytes, 0);
console.log(
  `${infos.length} sets, ${infos.reduce((n, s) => n + s.shapes, 0)} shapes, ${(total / 1024 / 1024).toFixed(2)} MB of sets; index ${(readFileSync(join(out, 'index.json.gz')).length / 1024).toFixed(0)} KB`,
);
if (!existsSync(join(root, 'NOTICE'))) console.warn('assets/NOTICE is missing');
