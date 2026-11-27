// A Content Security Policy on every page of the built site, as a <meta> tag (GitHub Pages cannot
// send headers). Diagrams on the site are SVG the core renders at build time, so this is a second
// line of defence behind its escaping: only the site's own script files and the inline scripts
// the build wrote run, each allowed by its hash.
//
// Astro's own `security.csp` does not hash the inline scripts Starlight adds (theme, search,
// tabs), so this integration hashes what each finished page holds instead. Styles may be inline:
// Starlight and its code blocks use <style> elements and style attributes. 'wasm-unsafe-eval' is
// for the search, Pagefind, which is WebAssembly.

import { createHash } from 'node:crypto';
import { readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const SCRIPT = /<script\b([^>]*)>([\s\S]*?)<\/script>/gi;
/** Types a browser runs; JSON-LD and other data blocks are not scripts. */
const RUNS = /^\s*$|^(module|(text|application)\/(java|ecma)script)$/i;

const typeOf = (attrs) => /\btype\s*=\s*["']?([^"'\s>]+)/i.exec(attrs)?.[1] ?? '';

/** The `'sha256-…'` sources of the inline scripts in `html`. */
export function inlineScriptHashes(html) {
  const hashes = new Set();
  for (const [, attrs, body] of html.matchAll(SCRIPT)) {
    if (/\bsrc\s*=/i.test(attrs) || !RUNS.test(typeOf(attrs))) continue;
    hashes.add(`'sha256-${createHash('sha256').update(body, 'utf8').digest('base64')}'`);
  }
  return [...hashes];
}

export function contentSecurityPolicy(scriptHashes) {
  return [
    "default-src 'self'",
    ["script-src 'self' 'wasm-unsafe-eval'", ...scriptHashes].join(' '),
    "style-src 'self' 'unsafe-inline'",
    "img-src 'self' data:",
    "font-src 'self' data:",
    "connect-src 'self'",
    "worker-src 'self' blob:",
    "object-src 'none'",
    "base-uri 'self'",
    "form-action 'none'",
  ].join('; ');
}

/** `html` with the policy as the first element of its <head>, before any script. */
export function withPolicy(html) {
  const head = /<head\b[^>]*>/i.exec(html);
  if (!head) return html;
  const policy = contentSecurityPolicy(inlineScriptHashes(html)).replace(/"/g, '&quot;');
  const at = head.index + head[0].length;
  return `${html.slice(0, at)}<meta http-equiv="Content-Security-Policy" content="${policy}">${html.slice(at)}`;
}

function* htmlFiles(dir) {
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const path = join(dir, entry.name);
    if (entry.isDirectory()) yield* htmlFiles(path);
    else if (entry.name.endsWith('.html')) yield path;
  }
}

/** The Astro integration: adds the policy to every page `astro build` writes. */
export default function csp() {
  return {
    name: 'diadraw-csp',
    hooks: {
      'astro:build:done': ({ dir, logger }) => {
        let pages = 0;
        for (const file of htmlFiles(fileURLToPath(dir))) {
          writeFileSync(file, withPolicy(readFileSync(file, 'utf8')));
          ++pages;
        }
        logger.info(`Content Security Policy added to ${pages} pages`);
      },
    },
  };
}
