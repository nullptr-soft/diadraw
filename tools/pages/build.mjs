// Builds what GitHub Pages serves: the website (site/, Astro + Starlight) at the root and the web
// app (src/apps/web) under app/, in one folder.
//
//   node tools/pages/build.mjs [--url https://<owner>.github.io/<repo>/] [--out <dir>]
//
// --url is where the folder will be served; the site and the app are built for its path. Without
// it they are built for http://localhost:4321/, which `npm run pages:preview` serves. --out
// defaults to diadraw-build/pages next to the checkout (DIADRAW_BUILD_DIR moves diadraw-build).
//
// Needs everything the web app needs (Emscripten for the core, see src/wasm) and `npm ci` at the
// root; installs the site's own dependencies when they are missing. Steps:
//
//   1. the core as WebAssembly, the web app, for <path>app/ (turbo, DIADRAW_WEB_BASE), and the
//      Markdown plugins with diadraw.js (src/markdown);
//   2. the tutorial's documents, checked and rendered by that core (site/scripts/render-tutorial.mjs);
//   3. the website, for <path> (SITE_URL, SITE_BASE);
//   4. all copied into the output folder, diadraw.js under embed/, and every local link in it
//      checked.

import { execFileSync } from 'node:child_process';
import {
  cpSync,
  existsSync,
  mkdirSync,
  readFileSync,
  readdirSync,
  rmSync,
  statSync,
} from 'node:fs';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';

const root = fileURLToPath(new URL('../..', import.meta.url));
const { values } = parseArgs({
  options: { url: { type: 'string' }, out: { type: 'string' } },
});

const url = new URL(values.url ?? 'http://localhost:4321/');
if (!url.pathname.endsWith('/')) url.pathname += '/';
const base = url.pathname;
const out = values.out
  ? resolve(values.out)
  : resolve(process.env.DIADRAW_BUILD_DIR ?? join(root, '../diadraw-build'), 'pages');

const windows = process.platform === 'win32';
const run = (command, args, options = {}) => {
  console.log(`\n> ${command} ${args.join(' ')}`);
  execFileSync(windows ? `${command}.cmd` : command, args, {
    cwd: root,
    stdio: 'inherit',
    shell: windows,
    ...options,
    env: { ...process.env, ...options.env },
  });
};

// 1. The core, the web app and the Markdown plugins (the site draws ```diadraw render blocks).
run('npx', ['turbo', 'run', 'build', '--filter=@diadraw/web...', '--filter=@diadraw/markdown...'], {
  env: { DIADRAW_WEB_BASE: `${base}app/` },
});

// 2 and 3. The website, with the tutorial rendered by the core just built.
const site = join(root, 'site');
if (!existsSync(join(site, 'node_modules'))) run('npm', ['ci'], { cwd: site });
run('npm', ['run', 'render-tutorial'], { cwd: site });
run('npm', ['run', 'build'], { cwd: site, env: { SITE_URL: url.origin, SITE_BASE: base } });

// 4. One folder, the app under app/.
rmSync(out, { recursive: true, force: true });
mkdirSync(out, { recursive: true });
cpSync(join(site, 'dist'), out, { recursive: true });
if (existsSync(join(out, 'app')))
  throw new Error('site/dist has an app/ folder; it would hide the web app');
cpSync(join(root, 'src/apps/web/dist'), join(out, 'app'), { recursive: true });
// diadraw.js and its core, for pages anywhere that draw DiaDraw code blocks in the browser.
if (existsSync(join(out, 'embed')))
  throw new Error('site/dist has an embed/ folder; it would hide diadraw.js');
cpSync(join(root, 'src/markdown/dist/embed'), join(out, 'embed'), { recursive: true });

checkLinks(out, base);
console.log(`\nPages: ${out} (for ${url.href})`);

/**
 * Fails when an href or src in the output's HTML points inside the site but at no file: a page
 * the site does not have, a reference page whose link was not rewritten, an asset outside the
 * base.
 */
function checkLinks(dir, basePath) {
  const files = [];
  const walk = (d) => {
    for (const name of readdirSync(d)) {
      const path = join(d, name);
      if (statSync(path).isDirectory()) walk(path);
      else if (name.endsWith('.html')) files.push(path);
    }
  };
  walk(dir);
  const broken = [];
  for (const file of files) {
    const html = readFileSync(file, 'utf8');
    const page = new URL(
      basePath + file.slice(dir.length + 1).replaceAll('\\', '/'),
      'http://pages.invalid',
    );
    for (const [, link] of html.matchAll(/\s(?:href|src)="([^"]*)"/g)) {
      if (/^(?:[a-z]+:|\/\/|#)/i.test(link) || link === '') continue;
      const target = new URL(link.replaceAll('&amp;', '&'), page);
      if (!target.pathname.startsWith(basePath)) {
        broken.push(`${page.pathname}: ${link} (outside ${basePath})`);
        continue;
      }
      let path = join(dir, decodeURIComponent(target.pathname.slice(basePath.length)));
      if (target.pathname.endsWith('/')) path = join(path, 'index.html');
      if (!existsSync(path)) broken.push(`${page.pathname}: ${link}`);
    }
  }
  if (broken.length) {
    console.error(`\nBroken links (${broken.length}):\n  ${broken.join('\n  ')}`);
    process.exit(1);
  }
  console.log(`\nLinks: ${files.length} pages checked.`);
}
