// Parity check for the C++ syntax highlighter (src/dsl/highlight.cpp) against the CodeMirror
// stream tokenizer in src/apps/web/src/language.ts, run line by line through CodeMirror's own
// StringStream. Every corpus file is tokenized as text (whatever its format), plus the DSL that
// each importable file turns into, plus a few hand-written edge cases.
//
// Usage (from the repo root, after `npm run build -w @diadraw/wasm` and `npm install`):
//   node tools/parity-highlight.mjs [--limit N]

import {
  existsSync,
  mkdtempSync,
  readFileSync,
  readdirSync,
  statSync,
  writeFileSync,
  rmSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = fileURLToPath(new URL('..', import.meta.url));
const args = process.argv.slice(2);
const limit = args.includes('--limit') ? Number(args[args.indexOf('--limit') + 1]) : Infinity;
const ts = (await import(join(root, 'node_modules/typescript/lib/typescript.js'))).default;
const { loadDiaDraw } = await import(join(root, 'src/wasm/dist/index.js'));
const dd = await loadDiaDraw();

// language.ts, transpiled, with its CodeMirror imports pointed at the web app's copies.
const esm = (name) => {
  // The web app's own copy when npm nested one, else the hoisted one.
  const modules = [join(root, 'src/apps/web/node_modules'), join(root, 'node_modules')].find((m) =>
    existsSync(join(m, name, 'package.json')),
  );
  const pkg = JSON.parse(readFileSync(join(modules, name, 'package.json'), 'utf8'));
  return pathToFileURL(join(modules, name, pkg.module ?? pkg.main)).href;
};
let source = ts.transpileModule(readFileSync(join(root, 'src/apps/web/src/language.ts'), 'utf8'), {
  compilerOptions: { module: ts.ModuleKind.ESNext, target: ts.ScriptTarget.ES2022 },
}).outputText;
source = source.replace(/from '(@codemirror\/[a-z]+)'/g, (_, name) => `from '${esm(name)}'`);
const dir = mkdtempSync(join(tmpdir(), 'dd-highlight-'));
writeFileSync(join(dir, 'language.mjs'), source);
const { diadraw } = await import(pathToFileURL(join(dir, 'language.mjs')).href);
const { StringStream } = await import(esm('@codemirror/language'));
rmSync(dir, { recursive: true });
const parser = diadraw(() => []).language.streamParser;

/** The reference tokens: [from, to, tag], adjacent runs of one tag merged. */
function reference(text) {
  const out = [];
  const state = parser.startState(2);
  const re = /\r\n?|\n/g;
  const lines = [];
  let last = 0;
  for (let m; (m = re.exec(text)); last = m.index + m[0].length)
    lines.push([last, text.slice(last, m.index)]);
  lines.push([last, text.slice(last)]);
  for (const [start, line] of lines) {
    const stream = new StringStream(line, 4, 2);
    while (!stream.eol()) {
      const tag = parser.token(stream, state);
      if (stream.pos === stream.start) throw new Error('tokenizer did not advance');
      if (tag) {
        const from = start + stream.start;
        const to = start + stream.pos;
        const prev = out[out.length - 1];
        if (prev && prev[2] === tag && prev[1] === from) prev[1] = to;
        else out.push([from, to, tag]);
      }
      stream.start = stream.pos;
    }
  }
  return out;
}

function* walk(path) {
  for (const name of readdirSync(path).sort()) {
    const p = join(path, name);
    if (statSync(p).isDirectory()) yield* walk(p);
    else yield p;
  }
}

const texts = [
  ['edge: empty', ''],
  ['edge: crlf', 'model {\r\n  a = process "x"\r\n}\r\nview v {\r  include *\r}'],
  ['edge: comments', 'a /* one\n two */ b = c /* x */ d // e\n/*\n\n*/ "s'],
  ['edge: strings', '"a\\"b" "c\\\\" "unterminated\n"\\' + '\n"é\\é" "😀"'],
  ['edge: numbers and arrows', '-1 -1.5 1. 2.x -> --> ->> <-> - > -- a-1 b->c ==d = = x == y'],
  ['edge: kinds across lines', 'a =\n\n  process b\nc = d\nx　= y'],
  ['edge: unicode', 'ñame = präfix "ü" 𝒳 = z'],
];
for (const path of walk(join(root, 'corpus'))) {
  if (texts.length >= limit) break;
  const raw = readFileSync(path);
  texts.push([path, raw.toString('utf8')]);
  if (path.endsWith('.draw') || /\.md$/i.test(path)) continue;
  try {
    const input = /\.(png|svg|vsdx)$/i.test(path) ? new Uint8Array(raw) : raw.toString('utf8');
    texts.push([`${path} (as DSL)`, dd.print(dd.importDiagram(input, path).model)]);
  } catch {
    // not importable: the raw text is still checked
  }
}

let tokens = 0;
const failures = [];
for (const [name, text] of texts) {
  const want = reference(text);
  const got = dd.highlight(text);
  tokens += want.length;
  const n = Math.max(want.length, got.length);
  for (let i = 0; i < n; i++) {
    if (JSON.stringify(want[i]) !== JSON.stringify(got[i])) {
      const [from] = want[i] ?? got[i];
      failures.push(
        `${name}: token ${i}: want ${JSON.stringify(want[i])} got ${JSON.stringify(got[i])} near ${JSON.stringify(text.slice(Math.max(0, from - 20), from + 20))}`,
      );
      break;
    }
  }
}
console.log(`${texts.length} texts, ${tokens} tokens, ${failures.length} failures`);
for (const f of failures.slice(0, 20)) console.log('  ' + f);
process.exit(failures.length ? 1 : 0);
