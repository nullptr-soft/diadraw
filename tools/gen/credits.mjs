// Generates DiaDraw's credits from one source: the hand-kept list of third-party code and assets
// (tools/credits/components.json) plus the npm packages each shipped package depends on, read
// from package-lock.json and site/package-lock.json. It writes
//
//   docs/credits.md            everything, by where it ships: the website's Credits page
//   src/host/data/credits.tsv  what the apps with an About dialog ship (GTK, macOS, WinUI, web and
//                              desktop): the native apps compile it in, the web app imports it
//
//   node tools/gen/credits.mjs           write both
//   node tools/gen/credits.mjs --check   fail when either is out of date (npm run lint runs it)
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import * as prettier from 'prettier';

const root = fileURLToPath(new URL('../..', import.meta.url));
const read = (path) => readFileSync(root + path, 'utf8');
const check = process.argv.includes('--check');

/** Where things ship, in the order the docs list them. */
const APPS = {
  gtk: 'Linux app',
  macos: 'macOS app',
  winui: 'Windows app',
  web: 'Web app',
  desktop: 'Desktop app (Tauri)',
  cli: 'CLI',
  vscode: 'VS Code extension',
  confluence: 'Confluence app',
  'confluence-drawio': 'Confluence draw.io app',
  markdown: '@diadraw/markdown',
  node: '@diadraw/node',
  python: 'Python package',
  rust: 'Rust crate',
  site: 'Website',
  repo: 'Repository only',
};
const NATIVE = ['cli', 'gtk', 'macos', 'winui', 'node', 'python', 'rust'];
const WASM = ['web', 'desktop', 'vscode', 'confluence', 'markdown'];
const ALIASES = { core: [...NATIVE, ...WASM], native: NATIVE, wasm: WASM };
/** The apps with an About dialog, which show credits.tsv. */
const ABOUT = ['gtk', 'macos', 'winui', 'web', 'desktop'];

/** The workspace packages that ship, and where their npm dependencies end up. */
const WORKSPACES = {
  'src/apps/web': ['web', 'desktop'],
  'src/apps/vscode': ['vscode'],
  'src/apps/confluence': ['confluence'],
  'src/apps/confluence-drawio': ['confluence-drawio'],
  'src/markdown': ['markdown'],
  'src/node': ['node'],
};

const expand = (apps) => [...new Set(apps.flatMap((a) => ALIASES[a] ?? [a]))];
const order = (apps) => Object.keys(APPS).filter((a) => apps.includes(a));

const { components, npmLicenses = {} } = JSON.parse(read('tools/credits/components.json'));
/** The licence of an npm package: its own, or the one components.json gives for it. */
const npmLicense = (name, license) => {
  const rule = Object.keys(npmLicenses).find(
    (key) => !key.startsWith('$') && (key.endsWith('/') ? name.startsWith(key) : name === key),
  );
  return rule ? npmLicenses[rule] : license || 'see package';
};
for (const c of components) {
  for (const key of ['name', 'version', 'license', 'url', 'use', 'apps'])
    if (!c[key]?.length) throw new Error(`tools/credits/components.json: ${c.name} has no ${key}`);
  c.apps = order(expand(c.apps));
  const unknown = expand(c.apps).filter((a) => !(a in APPS));
  if (unknown.length) throw new Error(`${c.name}: unknown apps ${unknown.join(', ')}`);
}

/**
 * The production dependencies of `from` in a lockfile, all the way down, as lockfile entries
 * keyed by name@version. Workspace packages (DiaDraw's own) are followed but not listed.
 */
function closure(packages, from) {
  const found = new Map();
  const seen = new Set();
  /** Node's lookup: node_modules/<name> in `dir`, then in each folder above it. */
  const resolve = (name, dir) => {
    for (let at = dir; ;) {
      const key = `${at ? `${at}/` : ''}node_modules/${name}`;
      if (packages[key]) return key;
      if (!at) return undefined;
      const up = at.lastIndexOf('/node_modules/');
      at = up >= 0 ? at.slice(0, up) : '';
    }
  };
  const visit = (key) => {
    let entry = packages[key];
    if (entry?.link) {
      key = entry.resolved;
      entry = packages[key];
    }
    if (!entry || seen.has(key)) return;
    seen.add(key);
    const own = key === from || !key.includes('node_modules/');
    if (!own) {
      const name = entry.name ?? key.slice(key.lastIndexOf('node_modules/') + 13);
      found.set(`${name}@${entry.version}`, {
        name,
        version: entry.version,
        license: entry.license,
      });
    }
    for (const dep of Object.keys({ ...entry.dependencies, ...entry.optionalDependencies })) {
      const next = resolve(dep, key);
      if (next) visit(next);
    }
  };
  visit(from);
  return [...found.values()];
}

const npm = new Map();
const addNpm = (pkg, apps) => {
  const id = `${pkg.name}@${pkg.version}`;
  const known = npm.get(id);
  if (known) known.apps = order([...new Set([...known.apps, ...apps])]);
  else
    npm.set(id, {
      ...pkg,
      license: npmLicense(pkg.name, pkg.license),
      url: `https://www.npmjs.com/package/${pkg.name}`,
      apps,
    });
};
const lock = JSON.parse(read('package-lock.json')).packages;
for (const [workspace, apps] of Object.entries(WORKSPACES))
  for (const pkg of closure(lock, workspace)) addNpm(pkg, apps);
// The website ships a static build, not its build tools: its direct dependencies are credited.
const siteLock = JSON.parse(read('site/package-lock.json')).packages;
for (const name of Object.keys(siteLock[''].dependencies ?? {})) {
  const entry = siteLock[`node_modules/${name}`];
  addNpm({ name, version: entry.version, license: entry.license }, ['site']);
}

const byName = (a, b) => a.name.localeCompare(b.name, 'en', { sensitivity: 'base' });
const npmPackages = [...npm.values()].sort(byName);
const all = [...components, ...npmPackages].sort(byName);

// ── src/host/data/credits.tsv ───────────────────────────────────────────────────────────────────

const field = (value) => {
  if (/[\t\n\r]/.test(value)) throw new Error(`credits: a tab or line break in "${value}"`);
  return value;
};
const tsv = [
  '// Generated by tools/gen/credits.mjs from tools/credits/components.json and the npm lockfiles.',
  '// Do not edit. The third-party software and assets in the apps with an About dialog, one per',
  '// line: name, version, licence, address and the apps that ship it, separated by tabs.',
  ...all
    .map((c) => ({ ...c, apps: c.apps.filter((a) => ABOUT.includes(a)) }))
    .filter((c) => c.apps.length)
    .map((c) => [c.name, c.version, c.license, c.url, c.apps.join(' ')].map(field).join('\t')),
].join('\n');

// ── docs/credits.md ─────────────────────────────────────────────────────────────────────────────

const cell = (text) => String(text).replace(/\|/g, '\\|');
/** The apps as the docs name them; every build of the core is "Everywhere the core is". */
const where = (apps) => {
  const core = ALIASES.core.every((a) => apps.includes(a));
  const rest = apps.filter((a) => !core || !ALIASES.core.includes(a)).map((a) => APPS[a]);
  return [...(core ? ['Every app and package with the core'] : []), ...rest].join(', ');
};
const md = [
  '<!-- Generated by tools/gen/credits.mjs from tools/credits/components.json and the npm lockfiles. Do not edit. -->',
  '',
  '# Credits',
  '',
  'DiaDraw is free software under the [MIT License](../LICENSE). It is built on the open-source',
  'software and assets below, and thanks their authors. Each keeps its own licence; the',
  '[Licensing](../README.md#licensing) section of the README says which parts of the repository are not under',
  'the MIT License.',
  '',
  'The apps list what they ship in their About dialogs.',
  '',
  '## Libraries and assets',
  '',
  '| Component | Licence | Used for | Where |',
  '| --- | --- | --- | --- |',
  ...[...components]
    .sort(byName)
    .map(
      (c) =>
        `| [${cell(c.name)}](${c.url}) ${cell(c.version)} | ${cell(c.license)} | ${cell(c.use)}${
          c.copyright ? ` (© ${cell(c.copyright)})` : ''
        } | ${where(c.apps)} |`,
    ),
  '',
  ...components
    .filter((c) => c.notes)
    .sort(byName)
    .flatMap((c) => [`**${c.name}.** ${c.notes}`, '']),
  '## npm packages',
  '',
  'The packages below are bundled into the JavaScript of the apps, with everything they depend on.',
  'The website is a static build; it credits the packages it is built with.',
  '',
  '| Package | Version | Licence | Where |',
  '| --- | --- | --- | --- |',
  ...npmPackages.map(
    (p) =>
      `| [${cell(p.name)}](${p.url}) | ${cell(p.version)} | ${cell(p.license)} | ${where(p.apps)} |`,
  ),
  '',
  '## Test data',
  '',
  'The folders of `corpus/` other than its own `*.draw` examples hold diagrams from other projects,',
  'used only to test the importers. They are not part of the apps, and each folder keeps the',
  'licence of its source, listed in the README of [bpmn](../corpus/bpmn/README.md),',
  '[drawio](../corpus/drawio/README.md), [plantuml](../corpus/plantuml/README.md) and',
  '[structurizr](../corpus/structurizr/README.md).',
  '',
].join('\n');

const options = (await prettier.resolveConfig(root + 'docs/credits.md')) ?? {};
const outputs = {
  'docs/credits.md': await prettier.format(md, { ...options, parser: 'markdown' }),
  'src/host/data/credits.tsv': `${tsv}\n`,
};

let stale = 0;
for (const [path, text] of Object.entries(outputs)) {
  let current = '';
  try {
    current = read(path);
  } catch {
    // not written yet
  }
  if (current === text) continue;
  if (check) {
    console.error(`${path} is out of date: run node tools/gen/credits.mjs`);
    stale++;
  } else {
    writeFileSync(root + path, text);
    console.log(`credits: wrote ${path}`);
  }
}
if (stale) process.exit(1);
