// Serves the folder tools/pages/build.mjs wrote, as GitHub Pages would, to check it before it
// is deployed:
//
//   node tools/pages/serve.mjs [--url http://localhost:4321/] [--dir <dir>]
//
// --url must be the address the folder was built for (build.mjs's --url, or its default);
// its path is where the folder is served, so a build for https://<owner>.github.io/<repo>/ is
// previewed with --url http://localhost:4321/<repo>/.

import { createReadStream, existsSync, statSync } from 'node:fs';
import { createServer } from 'node:http';
import { extname, join, normalize, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';

const root = fileURLToPath(new URL('../..', import.meta.url));
const { values } = parseArgs({ options: { url: { type: 'string' }, dir: { type: 'string' } } });
const url = new URL(values.url ?? 'http://localhost:4321/');
const base = url.pathname.endsWith('/') ? url.pathname : `${url.pathname}/`;
const dir = values.dir
  ? resolve(values.dir)
  : resolve(process.env.DIADRAW_BUILD_DIR ?? join(root, '../diadraw-build'), 'pages');
if (!existsSync(dir)) {
  console.error(`${dir} does not exist; run npm run pages:build first.`);
  process.exit(1);
}

const types = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript',
  '.mjs': 'text/javascript',
  '.css': 'text/css',
  '.json': 'application/json',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.wasm': 'application/wasm',
  '.gz': 'application/gzip',
  '.xml': 'application/xml',
  '.txt': 'text/plain; charset=utf-8',
};

createServer((req, res) => {
  const path = decodeURIComponent(new URL(req.url ?? '/', url).pathname);
  if (!path.startsWith(base)) {
    res.writeHead(302, { location: base }).end();
    return;
  }
  let file = normalize(join(dir, path.slice(base.length)));
  if (!file.startsWith(dir)) {
    res.writeHead(403).end();
    return;
  }
  if (existsSync(file) && statSync(file).isDirectory()) {
    if (!path.endsWith('/')) {
      res.writeHead(301, { location: `${path}/` }).end();
      return;
    }
    file = join(file, 'index.html');
  }
  const found = existsSync(file);
  if (!found) file = join(dir, '404.html');
  res.writeHead(found ? 200 : 404, {
    'content-type': types[extname(file)] ?? 'application/octet-stream',
  });
  createReadStream(file).pipe(res);
}).listen(Number(url.port || 80), url.hostname, () => {
  console.log(`Serving ${dir} at ${url.href}`);
});
