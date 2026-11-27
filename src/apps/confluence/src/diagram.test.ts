import { describe, expect, it } from 'vitest';
import { loadDiaDraw } from '@diadraw/wasm';
import {
  BUILT_IN_THEMES,
  STARTER_SOURCE,
  draw,
  importFileName,
  importText,
  readConfig,
  withTheme,
} from './diagram.js';
import { exportAdf } from './index.js';
import { fileNameFor, standaloneSvg } from './download.js';

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

describe('readConfig', () => {
  it('keeps the text and a view, and ignores anything else', () => {
    expect(readConfig({ source: shop, view: 'people', extra: 1 })).toEqual({
      source: shop,
      view: 'people',
    });
    expect(readConfig({ source: shop, view: '' })).toEqual({ source: shop });
    expect(readConfig(undefined)).toEqual({ source: '' });
    expect(readConfig({ source: 42 })).toEqual({ source: '' });
  });
});

describe('draw', () => {
  it('draws the first view unless one is picked', () => {
    const first = draw(dd, { source: shop }, 'dd-');
    expect(first.result.ok && first.result.view).toBe('overview');
    expect(first.views).toEqual(['overview', 'people']);
    const people = draw(dd, { source: shop, view: 'people' }, 'dd-');
    expect(people.result.ok && people.result.view).toBe('people');
    expect(people.result.ok && people.result.svg).toMatch(/^<svg\b/);
  });

  it('falls back to the first view when the picked one is gone', () => {
    const drawing = draw(dd, { source: shop, view: 'renamed' }, 'dd-');
    expect(drawing.result.ok && drawing.result.view).toBe('overview');
  });

  it('prefixes ids so several diagrams in one frame keep their markers apart', () => {
    const drawing = draw(dd, { source: shop }, 'p1-');
    expect(drawing.result.ok).toBe(true);
    if (!drawing.result.ok) return;
    const ids = [...drawing.result.svg.matchAll(/\sid="([^"]+)"/g)].map((m) => m[1]);
    expect(ids.length).toBeGreaterThan(0);
    expect(ids.every((id) => id!.startsWith('p1-'))).toBe(true);
  });

  it('reports errors with their position and lists the built-in themes', () => {
    const drawing = draw(dd, { source: 'model {\n  a = \n}\n' }, 'dd-');
    expect(drawing.result.ok).toBe(false);
    expect(drawing.diagnostics.some((d) => d.severity === 'error')).toBe(true);
    expect(drawing.themes).toEqual([
      'default',
      'dark',
      'mono',
      'ocean',
      'forest',
      'sunset',
      'lavender',
      'sand',
      'graphite',
      'pastel',
      'vivid',
      'contrast',
      'blueprint',
      'midnight',
      'aurora',
      'ember',
      'terminal',
    ]);
  });

  it('draws the starter diagram', () => {
    expect(draw(dd, { source: STARTER_SOURCE }, 'dd-').result.ok).toBe(true);
  });
});

describe('withTheme', () => {
  it("sets the document's theme, and removes it for default", () => {
    const dark = withTheme(dd, shop, 'overview', 'dark');
    expect(dd.parse(dark).model.theme).toBe('dark');
    expect(draw(dd, { source: dark }, 'dd-').theme).toBe('dark');
    const back = withTheme(dd, dark, 'overview', 'default');
    expect(dd.parse(back).model.theme).toBeUndefined();
  });

  it('changes the view’s own theme when the view has one', () => {
    const own = shop.replace('view people {\n', 'view people {\n  theme mono\n');
    const changed = withTheme(dd, own, 'people', 'dark');
    const { model } = dd.parse(changed);
    expect(model.views.people?.theme).toBe('dark');
    expect(model.theme).toBeUndefined();
  });

  it('offers the themes the document declares', () => {
    const branded = `theme brand extends dark {\n  background "#101820"\n}\n${shop}`;
    expect(draw(dd, { source: branded }, 'dd-').themes).toEqual([...BUILT_IN_THEMES, 'brand']);
  });
});

describe('importText', () => {
  it('converts Mermaid and PlantUML to DiaDraw text', () => {
    const mermaid = importText(
      dd,
      'flowchart LR\n  A[Start] --> B[Done]\n',
      importFileName('mermaid'),
    );
    expect(mermaid.ok).toBe(true);
    if (mermaid.ok) {
      expect(mermaid.source).toContain('"Start"');
      expect(draw(dd, { source: mermaid.source }, 'dd-').result.ok).toBe(true);
    }
    const plantuml = importText(
      dd,
      '@startuml\nactor User\nUser -> (Log in)\n@enduml\n',
      importFileName('plantuml'),
    );
    expect(plantuml.ok).toBe(true);
    if (plantuml.ok) expect(draw(dd, { source: plantuml.source }, 'dd-').result.ok).toBe(true);
  });
});

describe('exportAdf', () => {
  it('shows the text as a code block, wherever the payload keeps the config', () => {
    for (const payload of [
      { config: { source: shop, view: 'people' } },
      { extensionPayload: { config: { source: shop, view: 'people' } } },
      { context: { extension: { config: { source: shop, view: 'people' } } } },
    ]) {
      const adf = exportAdf(payload);
      expect(adf.content?.[0]?.content?.[0]?.text).toBe('DiaDraw diagram (view people)');
      expect(adf.content?.[1]).toEqual({
        type: 'codeBlock',
        attrs: { language: 'diadraw' },
        content: [{ type: 'text', text: shop }],
      });
    }
    expect(exportAdf(undefined).content?.[1]?.content).toEqual([]);
  });
});

describe('downloads', () => {
  it('names files after the view and makes the SVG standalone', () => {
    expect(fileNameFor('checkout flow', 'png')).toBe('checkout-flow.png');
    expect(fileNameFor('', 'svg')).toBe('diagram.svg');
    const svg = standaloneSvg('<svg style="max-width:100%;height:auto" width="10"></svg>');
    expect(svg).toBe(
      '<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" width="10"></svg>',
    );
  });
});
