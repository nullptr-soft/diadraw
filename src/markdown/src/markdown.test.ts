import MarkdownIt from 'markdown-it';
import rehypeStringify from 'rehype-stringify';
import remarkParse from 'remark-parse';
import remarkRehype from 'remark-rehype';
import { markdownToHtml } from 'satteri';
import { unified } from 'unified';
import { describe, expect, it } from 'vitest';
import { loadDiaDraw } from '@diadraw/wasm';
import { parseMeta, renderBlock } from './index.js';
import markdownItDiadraw from './markdown-it.js';
import remarkDiadraw, { type RemarkDiadrawOptions } from './remark.js';
import satteriDiadraw, { type SatteriDiadrawOptions } from './satteri.js';

const dd = await loadDiaDraw();

const shop = `model {
  customer = person "Customer"
  shop = system "Shop"
  customer -> shop "places order"
}
view overview {
  include *
}
view people {
  include customer
}
`;

const page = (info: string, text = shop) => `# Shop\n\n\`\`\`${info}\n${text}\`\`\`\n\nAfter.\n`;

const remark = (markdown: string, options: RemarkDiadrawOptions = {}) =>
  unified()
    .use(remarkParse)
    .use(remarkDiadraw, { diadraw: dd, ...options })
    .use(remarkRehype)
    .use(rehypeStringify)
    .process(markdown);

describe('parseMeta', () => {
  it('reads the view and the flags', () => {
    expect(parseMeta('view=people render')).toEqual({
      view: 'people',
      render: true,
      source: false,
    });
    expect(parseMeta('view="shop.web" norender')).toMatchObject({ view: 'shop.web', source: true });
    expect(parseMeta(undefined)).toEqual({ view: undefined, render: false, source: false });
  });
});

describe('renderBlock', () => {
  it('draws the first view by default', () => {
    const result = renderBlock(dd, shop);
    expect(result.ok && result.view).toBe('overview');
    expect(result.ok && result.svg).toMatch(/^<svg style="max-width:100%;height:auto"/);
  });

  it('draws a document without views through its default view', () => {
    const result = renderBlock(dd, 'model {\n  a = person "A"\n}\n');
    expect(result.ok && result.view).toBe('default');
  });

  it('prefixes ids and the references to them', () => {
    const result = renderBlock(dd, shop, { idPrefix: 'p-' });
    if (!result.ok) throw new Error(result.message);
    const ids = [...result.svg.matchAll(/\sid="([^"]+)"/g)].map((m) => m[1]);
    expect(ids.length).toBeGreaterThan(0);
    expect(ids.every((id) => id?.startsWith('p-'))).toBe(true);
    for (const [, ref] of result.svg.matchAll(/url\(#([^)]+)\)/g)) expect(ids).toContain(ref);
  });

  it('reports errors and unknown views', () => {
    const broken = renderBlock(dd, 'model {\n  a -> \n}\n');
    expect(broken.ok).toBe(false);
    expect(!broken.ok && broken.message).toMatch(/^\d+:\d+: /);
    const missing = renderBlock(dd, shop, { view: 'nope' });
    expect(!missing.ok && missing.message).toBe(
      'no view "nope" (the document has "overview", "people")',
    );
  });
});

describe('remarkDiadraw', () => {
  it('replaces diadraw blocks with a figure holding the SVG', async () => {
    const html = String(await remark(page('diadraw')));
    expect(html).toContain('<figure class="diadraw" data-view="overview"><svg');
    expect(html).toContain('viewBox=');
    expect(html).not.toContain('<code');
    expect(html).toContain('<p>After.</p>');
  });

  it('picks the view, and leaves blocks marked source', async () => {
    expect(String(await remark(page('diadraw view=people')))).toContain('data-view="people"');
    expect(String(await remark(page('diadraw source')))).toContain(
      '<code class="language-diadraw">',
    );
  });

  it('draws only blocks marked render in marked mode', async () => {
    expect(String(await remark(page('diadraw'), { blocks: 'marked' }))).toContain('<code');
    expect(String(await remark(page('diadraw render'), { blocks: 'marked' }))).toContain('<figure');
  });

  it('fails the build on errors, with the line in the Markdown file', async () => {
    await expect(remark(page('diadraw', 'model {\n  a -> \n}\n'))).rejects.toThrow(/line 6:1: /);
  });

  it('warns and keeps the code when asked to', async () => {
    const file = await remark(page('diadraw', 'model {\n  a -> \n}\n'), { onError: 'warn' });
    expect(file.messages).toHaveLength(1);
    expect(String(file)).toContain('<code class="language-diadraw">');
  });
});

describe('markdownItDiadraw', () => {
  const md = new MarkdownIt().use(markdownItDiadraw, { diadraw: dd });

  it('replaces diadraw fences with a figure holding the SVG', () => {
    const html = md.render(page('diadraw view=people'));
    expect(html).toContain('<figure class="diadraw" data-view="people"><svg');
    expect(md.render(page('js'))).toContain('<code class="language-js">');
  });

  it('throws on errors by default, or shows them under the code', () => {
    expect(() => md.render(page('diadraw', 'model {\n  a -> \n}\n'))).toThrow(/line 6:1: /);
    const lenient = new MarkdownIt().use(markdownItDiadraw, { diadraw: dd, onError: 'source' });
    expect(lenient.render(page('diadraw', 'model {\n  a -> \n}\n'))).toMatch(
      /<code class="language-diadraw">[\s\S]*<p class="diadraw-error" role="alert">line 6:1: /,
    );
  });
});

describe('satteriDiadraw', () => {
  const html = async (markdown: string, options: SatteriDiadrawOptions = {}) =>
    (
      await markdownToHtml(markdown, {
        mdastPlugins: [satteriDiadraw({ diadraw: dd, ...options }) as never],
      })
    ).html;

  it('replaces diadraw blocks with a figure holding the SVG', async () => {
    const out = await html(page('diadraw view=people'));
    expect(out).toContain('<figure class="diadraw" data-view="people"><svg');
    expect(out).toContain('</svg></figure>');
    expect(out).toContain('<p>After.</p>');
    expect(await html(page('diadraw'), { blocks: 'marked' })).toContain(
      '<code class="language-diadraw">',
    );
  });

  it('fails on errors, with the line in the Markdown file', async () => {
    await expect(html(page('diadraw', 'model {\n  a -> \n}\n'))).rejects.toThrow(/line 6:1: /);
  });
});
