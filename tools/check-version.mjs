// Checks that every place that states DiaDraw's version agrees with the VERSION file, the one
// source: CMake reads it for the core (dd_version), the native apps and the WASM module, and the
// site reads it at build time. The files below cannot read it, so they carry a copy that this
// script keeps honest. `npm run lint` runs it; `npm run version:sync` updates them, and
// `npm run version:bump` (tools/bump-version.mjs) changes VERSION and then updates them.
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const root = new URL('../', import.meta.url);
const read = (path) => readFileSync(new URL(path, root), 'utf8');

const packages = [
  'src/wasm',
  'src/markdown',
  'bindings/node',
  'src/apps/web',
  'src/apps/desktop',
  'src/apps/vscode',
  'src/apps/confluence',
  'src/apps/confluence-drawio',
];

/** [file, pattern whose first group is the version] */
const copies = [
  ...packages.map((dir) => [`${dir}/package.json`, /^ {2}"version": "([^"]+)"/m]),
  ['site/package.json', /^ {2}"version": "([^"]+)"/m],
  ['site/package-lock.json', /^ {2}"version": "([^"]+)"/m],
  ['site/package-lock.json', /^ {4}"": \{\n {6}"name": "[^"]+",\n {6}"version": "([^"]+)"/m],
  ...packages.map((dir) => [
    'package-lock.json',
    new RegExp(`^ {4}"${dir}": \\{\\n {6}"name": "[^"]+",\\n {6}"version": "([^"]+)"`, 'm'),
  ]),
  ['src/apps/desktop/src-tauri/Cargo.toml', /^version = "([^"]+)"/m],
  ['bindings/rust/Cargo.toml', /^version = "([^"]+)"/m],
  ['src/apps/desktop/src-tauri/tauri.conf.json', /^ {2}"version": "([^"]+)"/m],
  ['src/apps/gtk/data/com.diadraw.DiaDraw.metainfo.xml', /<release version="([^"]+)"/],
  ['plugins/diadraw/.claude-plugin/plugin.json', /^ {2}"version": "([^"]+)"/m],
  ['.github/ISSUE_TEMPLATE/bug_report.yml', /placeholder: '(\d+\.\d+\.\d+)'/],
];

/** Every file, relative to the repository root, that holds a copy of the version. */
export const versionFiles = [...new Set(copies.map(([file]) => file))];

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const version = read('VERSION').trim();
  if (!/^\d+\.\d+\.\d+$/.test(version)) {
    console.error(`VERSION holds "${version}", not a MAJOR.MINOR.PATCH version`);
    process.exit(1);
  }
  const write = process.argv.includes('--write');
  let stale = 0;
  for (const [file, pattern] of copies) {
    const text = read(file);
    const match = pattern.exec(text);
    if (!match) {
      console.error(`${file}: no version found (${pattern})`);
      stale++;
      continue;
    }
    if (match[1] === version) continue;
    if (write) {
      const at = match.index + match[0].lastIndexOf(match[1]);
      writeFileSync(
        new URL(file, root),
        text.slice(0, at) + version + text.slice(at + match[1].length),
      );
      console.log(`${file}: ${match[1]} → ${version}`);
    } else {
      console.error(`${file}: ${match[1]}, but VERSION is ${version}`);
      stale++;
    }
  }
  if (stale) {
    console.error('Run `npm run version:sync` to update the copies.');
    process.exit(1);
  }
}
