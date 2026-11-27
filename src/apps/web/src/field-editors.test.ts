import type { Element, Model, PaletteItem, Relationship } from '@diadraw/wasm';
import { describe, expect, it } from 'vitest';
import {
  LINE_STYLE_KEY,
  blockRows,
  blockText,
  fieldChoices,
  fontStyleBits,
  isColorKey,
  joinTags,
  normalizedColor,
  otherStyle,
  propertyRows,
  propertyText,
  selectorChoices,
  selectorTarget,
  splitTags,
  styleControlValue,
  styleKeys,
  styleSetting,
  styleValue,
  withOtherStyle,
  withStyleSetting,
  withStyleValue,
} from './field-editors';

// The same cases as the native apps' src/host/tests/field_editors_test.cpp.

const element = (id: string, kind: string, extra: Partial<Element> = {}): Element =>
  ({ id, kind, name: id, tags: [], ports: [], properties: {}, ...extra }) as Element;

describe('field choices', () => {
  it('offers built-ins, the libraries and what the model uses', () => {
    const model = {
      elements: {
        web: element('web', 'container', { tags: ['external', 'legacy'], technology: 'React' }),
        q: element('q', 'quue'),
      },
      relationships: {
        'web->q': {
          id: 'web->q',
          tags: ['async'],
          technology: 'AMQP',
        } as unknown as Relationship,
      },
      themes: [{ name: 'brand', extends: 'dark' }, { name: 'dark' }],
    } as unknown as Model;
    const palette = [
      { type: 'kind', ref: 'aws.lambda', label: 'Lambda', id: 'lambda', alias: 'aws' },
      { type: 'shape', ref: 'aws.s3', label: '', id: 's3', alias: 'aws' },
      { type: 'fragment', ref: 'aws.x', label: 'X', id: 'x', alias: 'aws' },
    ] as unknown as PaletteItem[];
    const c = fieldChoices(model, palette);
    expect(c.kinds[0]).toEqual({ value: 'person', label: 'person', group: '' });
    expect(c.kinds).toContainEqual({ value: 'aws.lambda', label: 'Lambda', group: 'aws' });
    expect(c.kinds.at(-1)).toEqual({ value: 'quue', label: 'quue', group: 'Used' });
    expect(c.shapes).toContainEqual({ value: 'aws.s3', label: 's3', group: 'aws' });
    expect(c.shapes.some((s) => s.value === 'aws.x')).toBe(false);
    expect(c.tags).toEqual(['async', 'external', 'legacy']);
    expect(c.technologies).toEqual(['AMQP', 'React']);
    expect(c.themes.map((t) => t.value)).toEqual([
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
      'brand',
    ]);
    expect(c.themes.at(-1)!.group).toBe('Declared');
  });

  it('splits and joins tags', () => {
    expect(splitTags(' external,legacy , ,external')).toEqual(['external', 'legacy']);
    expect(splitTags('')).toEqual([]);
    expect(joinTags(['external', ' legacy ', ''])).toBe('external, legacy');
  });
});

describe('style editor', () => {
  it('sets and removes keys in place', () => {
    const style = 'fill=#dae8fc;stroke=#6c8ebf;rounded=1';
    expect(styleValue(style, 'stroke')).toBe('#6c8ebf');
    expect(styleValue(style, 'dashed')).toBeUndefined();
    expect(withStyleValue(style, 'stroke', '#000000')).toBe(
      'fill=#dae8fc;stroke=#000000;rounded=1',
    );
    expect(withStyleValue(style, 'dashed', '1')).toBe(`${style};dashed=1`);
    expect(withStyleValue(style, 'fill', undefined)).toBe('stroke=#6c8ebf;rounded=1');
    expect(withStyleValue('', 'fill', '#ffffff')).toBe('fill=#ffffff');
    expect(withStyleValue('fill=#fff;;', 'fill', '#fff')).toBe('fill=#fff;;');
    expect(withStyleValue('fill=#fff;;', 'dashed', undefined)).toBe('fill=#fff;;');
  });

  it('removes a key when its control is back at the default', () => {
    const key = (name: string) => styleKeys('elements').find((k) => k.key === name)!;
    expect(styleSetting(key('rounded'), '0')).toBeUndefined();
    expect(styleSetting(key('rounded'), '1')).toBe('1');
    expect(styleSetting(key('align'), '')).toBeUndefined();
    expect(styleSetting(key('align'), 'left')).toBe('left');
    expect(styleSetting(key('fontStyle'), '0')).toBeUndefined();
    expect(styleSetting(key('fill'), '')).toBeUndefined();
    expect(styleSetting(key('fontSize'), '14')).toBe('14');
    const endFill = styleKeys('relationships').find((k) => k.key === 'endFill')!;
    expect(styleSetting(endFill, '1')).toBeUndefined();
    expect(styleSetting(endFill, '0')).toBe('0');
  });

  it('keeps the keys without a control in Other', () => {
    const style = 'fill=#dae8fc;shadow=1;stroke=#6c8ebf;whiteSpace=wrap';
    expect(otherStyle(style, 'elements')).toBe('shadow=1;whiteSpace=wrap');
    expect(withOtherStyle(style, 'elements', 'shadow=1;whiteSpace=wrap')).toBe(style);
    expect(withOtherStyle(style, 'elements', 'whiteSpace=wrap ; shadow=1')).toBe(style);
    expect(withOtherStyle(style, 'elements', 'shadow=0')).toBe(
      'fill=#dae8fc;stroke=#6c8ebf;shadow=0',
    );
    expect(withOtherStyle(style, 'elements', '')).toBe('fill=#dae8fc;stroke=#6c8ebf');
    expect(withOtherStyle('fill=#fff', 'elements', 'fill=#000;glass=1')).toBe('fill=#000;glass=1');
    expect(
      otherStyle('endArrow=open;edgeStyle=orthogonalEdgeStyle;jettySize=auto', 'relationships'),
    ).toBe('jettySize=auto');
    expect(otherStyle('endArrow=open', 'elements')).toBe('endArrow=open');
  });

  it('reads colours and font styles', () => {
    expect(normalizedColor('#ABC')).toBe('#aabbcc');
    expect(normalizedColor(' #DAE8FC ')).toBe('#dae8fc');
    expect(normalizedColor('none')).toBeUndefined();
    expect(normalizedColor('#12345')).toBeUndefined();
    expect(normalizedColor('#ggg')).toBeUndefined();
    expect(fontStyleBits('5')).toBe(5);
    expect(fontStyleBits('bold')).toBe(0);
    expect(fontStyleBits('')).toBe(0);
  });

  it('picks the line style from one pop-up and lists the ends as the native apps do', () => {
    const edge = styleKeys('relationships');
    for (const target of ['elements', 'relationships'] as const) {
      const keys = styleKeys(target);
      expect(keys.some((k) => k.key === 'dashed' || k.key === 'dashPattern')).toBe(false);
      expect(keys.find((k) => k.key === LINE_STYLE_KEY)!.choices!.map((c) => c.label)).toEqual([
        'Solid',
        'Dashed',
        'Dotted',
        'Short dashes',
        'Long dashes',
        'Dash and dot',
        'Dash and two dots',
      ]);
    }
    const line = edge.find((k) => k.key === LINE_STYLE_KEY)!;
    expect(withStyleSetting('stroke=#c00', line, '1 3')).toBe(
      'stroke=#c00;dashed=1;dashPattern=1 3',
    );
    expect(withStyleSetting('dashed=1;dashPattern=1 3', line, '8 3 1 3')).toBe(
      'dashed=1;dashPattern=8 3 1 3',
    );
    expect(withStyleSetting('dashed=1;dashPattern=1 3', line, 'dashed')).toBe('dashed=1');
    expect(withStyleSetting('stroke=#c00', line, 'dashed')).toBe('stroke=#c00;dashed=1');
    expect(withStyleSetting('stroke=#c00;dashed=1;dashPattern=1 3', line, '')).toBe('stroke=#c00');
    expect(withStyleSetting('dashed=0', line, '')).toBe('');
    expect(styleControlValue('stroke=#c00', line)).toBeUndefined();
    expect(styleControlValue('dashed=0;dashPattern=1 3', line)).toBeUndefined();
    expect(styleControlValue('dashed=1', line)).toBe('dashed');
    expect(styleControlValue('dashPattern=1 3;dashed=1', line)).toBe('1 3');
    expect(
      styleControlValue(
        'stroke=#c00',
        edge.find((k) => k.key === 'stroke')!,
      ),
    ).toBe('#c00');
    expect(otherStyle('dashed=1;dashPattern=1 3;shadow=1', 'elements')).toBe('shadow=1');
    const end = edge.find((k) => k.key === 'endArrow')!;
    expect(end.choices!.filter((c) => c.group === "Crow's foot").map((c) => c.value)).toEqual([
      'ERone',
      'ERmandOne',
      'ERzeroToOne',
      'ERmany',
      'ERoneToMany',
      'ERzeroToMany',
    ]);
  });

  it('has well-formed keys', () => {
    for (const target of ['elements', 'relationships'] as const) {
      for (const k of styleKeys(target)) {
        expect(k.label, k.key).not.toBe('');
        expect(k.section, k.key).not.toBe('');
        if (k.control === 'choice') {
          expect(k.choices!.length, k.key).toBeGreaterThanOrEqual(2);
          expect(k.choices![0]!.value, k.key).toBe('');
        }
        if (k.control === 'number') expect(k.min!, k.key).toBeLessThan(k.max!);
        if (k.control === 'flag') expect(['0', '1'], k.key).toContain(k.fallback);
      }
    }
  });
});

describe('properties', () => {
  it('reads rows as the field is read', () => {
    expect(propertyRows(' owner = Ann \n\ntier=1\nflag\n=orphan\nowner=Bob')).toEqual([
      ['owner', 'Bob'],
      ['tier', '1'],
      ['flag', ''],
    ]);
    expect(propertyRows('')).toEqual([]);
    expect(propertyRows('url=https://x?a=b')).toEqual([['url', 'https://x?a=b']]);
  });

  it('writes one line per row', () => {
    expect(
      propertyText([
        ['owner', 'Ann'],
        ['', 'dropped'],
        ['a=b', 'two\nlines'],
        [' tier ', ' 1 '],
      ]),
    ).toBe('owner=Ann\na_b=two lines\ntier=1');
    expect(propertyText([])).toBe('');
    expect(propertyText(propertyRows('owner=Ann\ntier=1'))).toBe('owner=Ann\ntier=1');
  });
});

describe('style blocks', () => {
  it('split at the first colon outside quotes', () => {
    const rows = blockRows('container: fill=#438dd5\n\n  tag "a:b" :dashed=1\nrelationship\n');
    expect(rows).toEqual([
      { selector: 'container', style: 'fill=#438dd5' },
      { selector: 'tag "a:b"', style: 'dashed=1' },
      { selector: 'relationship', style: '' },
    ]);
    expect(blockText(rows)).toBe('container: fill=#438dd5\ntag "a:b": dashed=1\nrelationship: ');
    expect(
      blockText([
        { selector: '', style: 'fill=#fff' },
        { selector: 'person', style: 'fill=#08427b;text=#ffffff' },
      ]),
    ).toBe('person: fill=#08427b;text=#ffffff');
  });

  it('know what they style', () => {
    expect(selectorTarget('relationship')).toBe('relationships');
    expect(selectorTarget(' relationship tag "async"')).toBe('relationships');
    expect(selectorTarget('relationships')).toBe('elements');
    expect(selectorTarget('tag "external"')).toBe('elements');
    expect(selectorTarget('*')).toBe('elements');
    const c = fieldChoices(undefined);
    expect(
      selectorChoices({
        ...c,
        kinds: [{ value: 'container', label: '', group: '' }],
        tags: ['a"b'],
      }),
    ).toEqual(['*', 'relationship', 'container', 'tag "a\\"b"', 'relationship tag "a\\"b"']);
    expect(isColorKey('background')).toBe(true);
    expect(isColorKey('fontFamily')).toBe(false);
  });
});
