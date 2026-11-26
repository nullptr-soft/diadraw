import type { Element, Model } from '@diadraw/wasm';
import { describe, expect, it } from 'vitest';
import {
  documentFields,
  documentOperation,
  parseStyleLines,
  sharedStyles,
  sharedStylesOperation,
  styleOperation,
} from './style-fields';

const web: Element = {
  id: 'shop.web',
  kind: 'container',
  name: 'Web app',
  parent: 'shop',
  tags: ['external'],
  ports: [],
  properties: {},
};
const db: Element = { ...web, id: 'shop.db', kind: 'database', tags: [] };

const model: Model = {
  elements: { 'shop.web': web, 'shop.db': db },
  relationships: {},
  groups: {},
  views: {
    checkout: {
      id: 'checkout',
      scope: 'shop',
      include: ['*'],
      exclude: [],
      layout: { algorithm: 'layered', direction: 'down' },
      hints: [],
    },
  },
  theme: 'brand',
  themes: [
    {
      name: 'brand',
      extends: 'dark',
      background: '#101418',
      nodes: { person: { fill: '#08427b' } },
    },
  ],
  styles: [
    { target: 'elements', kind: 'container', style: { fill: '#438dd5' } },
    { target: 'elements', tag: 'external', style: { fill: '#999999', dashed: '1' } },
    { target: 'relationships', style: { dashed: '1' } },
  ],
};

describe('style lines', () => {
  it('show the blocks that apply to an element', () => {
    expect(sharedStyles(model, web)).toBe(
      'container: fill=#438dd5\ntag "external": fill=#999999;dashed=1',
    );
    expect(sharedStyles(model, db)).toBe('');
  });

  it('read selectors as the DSL writes them', () => {
    const rules = parseStyleLines(
      '*: fill=#fff\n\ncontainer tag "a:b": stroke=red\nrelationship tag async: dashed=1',
    );
    expect(rules).toEqual([
      { target: 'elements', style: { fill: '#fff' } },
      { target: 'elements', kind: 'container', tag: 'a:b', style: { stroke: 'red' } },
      { target: 'relationships', tag: 'async', style: { dashed: '1' } },
    ]);
    expect(() => parseStyleLines('container fill=red')).toThrow(/Line 1/);
    expect(() => parseStyleLines('relationship container: fill=red')).toThrow(/not a selector/);
  });

  it('refuse what adding the block would: a key or a kind that is not a name', () => {
    expect(() => parseStyleLines('container: fill=red\ncontainer: fill colour=red')).toThrow(
      /Line 2: Key "fill colour"/,
    );
    expect(() => parseStyleLines('web-app: fill=red')).toThrow(/Line 1: Kind "web-app"/);
    expect(() => parseStyleLines('container: =red')).toThrow(/Key ""/);
  });
});

describe('style operations', () => {
  it('update a changed block in place, keeping its key order', () => {
    const op = sharedStylesOperation(
      model,
      web,
      'container: fill=#438dd5\ntag "external": opacity=50;dashed=1;fill=#999999',
    );
    expect(op).toEqual({
      type: 'updateStyleRule',
      index: 1,
      rule: {
        target: 'elements',
        tag: 'external',
        style: { fill: '#999999', dashed: '1', opacity: '50' },
      },
    });
  });

  it('add new blocks after the shown ones and remove dropped ones', () => {
    expect(sharedStylesOperation(model, db, 'database: fill=#ffcc00')).toEqual({
      type: 'addStyleRule',
      rule: { target: 'elements', kind: 'database', style: { fill: '#ffcc00' } },
      index: 3,
    });
    expect(
      sharedStylesOperation(
        model,
        web,
        'container: fill=#438dd5\ncontainer tag "external": stroke=#000\ntag "external": fill=#999999;dashed=1',
      ),
    ).toEqual({
      type: 'addStyleRule',
      rule: { target: 'elements', kind: 'container', tag: 'external', style: { stroke: '#000' } },
      index: 1,
    });
    expect(sharedStylesOperation(model, web, 'tag "external": fill=#999999;dashed=1')).toEqual({
      type: 'removeStyleRule',
      index: 0,
    });
  });

  it('keep a block whose selector changed', () => {
    expect(
      styleOperation(
        model.styles!,
        [0, 1],
        parseStyleLines('container: fill=#438dd5\ncontainer tag "external": fill=#999999;dashed=1'),
      ),
    ).toEqual({
      type: 'updateStyleRule',
      index: 1,
      rule: {
        target: 'elements',
        kind: 'container',
        tag: 'external',
        style: { fill: '#999999', dashed: '1' },
      },
    });
  });

  it('change nothing when nothing changed, and refuse blocks that do not apply', () => {
    expect(sharedStylesOperation(model, web, sharedStyles(model, web))).toBeUndefined();
    expect(() => sharedStylesOperation(model, db, 'container: fill=red')).toThrow(
      /does not apply to shop.db/,
    );
  });
});

describe('document fields', () => {
  it('show the themes, every block and the declared theme', () => {
    const f = documentFields(model, 'checkout');
    expect(f).toEqual({
      theme: 'brand',
      viewTheme: '',
      styles:
        'container: fill=#438dd5\ntag "external": fill=#999999;dashed=1\nrelationship: dashed=1',
      declared: 'brand',
      extends: 'dark',
      themeKeys: 'background=#101418',
      themeNodes: 'person: fill=#08427b',
    });
    expect(documentOperation(model, 'checkout', f)).toBeUndefined();
  });

  it('pick a theme for the view, or start a new one from the old', () => {
    const f = documentFields(model, 'checkout');
    expect(documentOperation(model, 'checkout', { ...f, viewTheme: 'mono' })).toEqual({
      type: 'setTheme',
      view: 'checkout',
      theme: 'mono',
    });
    expect(documentOperation(model, 'checkout', { ...f, theme: 'night' })).toEqual({
      type: 'batch',
      operations: [
        { type: 'addTheme', theme: { name: 'night', extends: 'brand' } },
        { type: 'setTheme', theme: 'night' },
      ],
    });
    expect(documentOperation(model, 'checkout', { ...f, theme: '' })).toEqual({ type: 'setTheme' });
  });

  it("edit the declared theme's keys and nodes", () => {
    const f = documentFields(model, 'checkout');
    expect(
      documentOperation(model, 'checkout', {
        ...f,
        extends: 'mono',
        themeKeys: 'edge=#9aa5b1\nbackground=#000000',
        themeNodes: 'container: fill=#1168bd\nperson: text=#ffffff;fill=#08427b',
      }),
    ).toEqual({
      type: 'updateTheme',
      theme: {
        name: 'brand',
        extends: 'mono',
        background: '#000000',
        edge: '#9aa5b1',
        nodes: { person: { fill: '#08427b', text: '#ffffff' }, container: { fill: '#1168bd' } },
      },
    });
  });
});
