import { existsSync, readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { loadDiaDraw } from '@diadraw/wasm';
import { renderOnce } from './diagram';
import { MessageReader } from './framing';
import { previewHtml } from './preview';

const root = new URL('..', import.meta.url);
const json = (path: string) => JSON.parse(readFileSync(new URL(path, root), 'utf8'));

describe('extension manifest', () => {
  const manifest = json('package.json');

  it('points at files that exist', () => {
    for (const g of manifest.contributes.grammars)
      expect(existsSync(new URL(g.path, root))).toBe(true);
    for (const l of manifest.contributes.languages)
      expect(existsSync(new URL(l.configuration, root))).toBe(true);
  });

  it('has a grammar whose patterns compile and highlight keywords', () => {
    const grammar = json('syntaxes/diadraw.tmLanguage.json');
    const patterns: string[] = [];
    const walk = (node: unknown) => {
      if (node && typeof node === 'object') {
        for (const [k, v] of Object.entries(node)) {
          if ((k === 'match' || k === 'begin' || k === 'end') && typeof v === 'string')
            patterns.push(v);
          else walk(v);
        }
      }
    };
    walk(grammar);
    expect(patterns.length).toBeGreaterThan(5);
    for (const p of patterns) expect(() => new RegExp(p)).not.toThrow();
    const keyword = new RegExp(grammar.repository.keywords.patterns[1].match);
    expect(keyword.test('pin a below b')).toBe(true);
    const decl = new RegExp(grammar.repository.declaration.match).exec('  db = container "Orders"');
    expect(decl?.slice(1, 4)).toEqual(['db', '=', 'container']);
  });
});

describe('previewHtml', () => {
  it('embeds the diagram with a nonce-locked script and a view picker', () => {
    const html = previewHtml(
      { views: ['a', 'b<'], view: 'b<', svg: '<svg></svg>' },
      'N0NCE',
      'vscode-resource:',
    );
    expect(html).toContain("script-src 'nonce-N0NCE'");
    expect(html).toContain('<script nonce="N0NCE">');
    expect(html).toContain('<option value="b&lt;" selected>view b&lt;</option>');
    expect(html).toContain('<div class="diagram"><svg></svg></div>');
  });

  it('shows errors escaped', () => {
    const html = previewHtml({ views: ['a'], view: 'a', error: 'line 1: <bad>' }, 'n', 'x');
    expect(html).toContain('<p class="error">line 1: &lt;bad&gt;</p>');
    expect(html).not.toContain('<select');
  });
});

describe('MessageReader', () => {
  it('splits a stream into messages, across chunk boundaries and multibyte text', () => {
    const got: string[] = [];
    const reader = new MessageReader((m) => got.push(m));
    const frame = (body: string) =>
      `Content-Length: ${Buffer.byteLength(body)}\r\nContent-Type: x\r\n\r\n${body}`;
    const stream = Buffer.from(frame('{"a":"😀"}') + frame('{"b":1}'), 'utf8');
    for (let i = 0; i < stream.length; i += 3) reader.push(stream.subarray(i, i + 3));
    expect(got).toEqual(['{"a":"😀"}', '{"b":1}']);
  });
});

describe('renderState', () => {
  it('renders the chosen view, and reports syntax errors instead', async () => {
    const dd = await loadDiaDraw();
    const ok = renderOnce(
      dd,
      'model {\n  a = process\n}\nview one {\n  include *\n}\nview two {\n  include *\n}\n',
      undefined,
      'two',
    );
    expect(ok.views).toEqual(['one', 'two']);
    expect(ok.view).toBe('two');
    expect(ok.svg).toMatch(/^<svg /);
    const bad = renderOnce(dd, 'model {\n  a = \n}\n', undefined);
    expect(bad.svg).toBeUndefined();
    expect(bad.error).toMatch(/^line \d+:\d+: /);
  });
});
