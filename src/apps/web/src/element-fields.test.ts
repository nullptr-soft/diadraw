import type { Element } from '@diadraw/wasm';
import { describe, expect, it } from 'vitest';
import { applyFields, fieldsOf, parseStyleText, printStyleText } from './element-fields';

const db: Element = {
  id: 'shop.db',
  kind: 'container',
  name: 'Orders',
  parent: 'shop',
  technology: 'Postgres',
  tags: ['a', 'b'],
  ports: [],
  properties: { owner: 'Ann' },
  style: { fill: '#eee' },
};

describe('inspector fields', () => {
  it('shows the last part of the id, which the fields do not apply', () => {
    expect(fieldsOf(db).id).toBe('db');
    expect(applyFields(db, { ...fieldsOf(db), id: 'orders' })).toEqual(db);
  });

  it('round-trips an element unchanged', () => {
    expect(applyFields(db, fieldsOf(db))).toEqual(db);
  });

  it('reads what was typed, trimming and dropping empty optional fields', () => {
    const updated = applyFields(db, {
      ...fieldsOf(db),
      kind: ' database ',
      technology: '',
      shape: 'cylinder',
      tags: 'a, , c ',
      props: 'owner = Bob\n\ntier=1',
    });
    expect(updated).toEqual({
      ...db,
      kind: 'database',
      technology: undefined,
      shape: 'cylinder',
      tags: ['a', 'c'],
      properties: { owner: 'Bob', tier: '1' },
    });
    expect('technology' in updated).toBe(false);
    expect(updated.parent).toBe('shop');
  });

  it('falls back to the identifier for an empty name', () => {
    expect(applyFields(db, { ...fieldsOf(db), name: ' ' }).name).toBe('db');
  });

  it('keeps an empty style, which still draws the element as its own shape', () => {
    const styled = { ...db, style: {} };
    expect(applyFields(styled, fieldsOf(styled)).style).toEqual({});
    expect('style' in applyFields(db, { ...fieldsOf(db), style: '' })).toBe(false);
  });

  it('reads and writes styles as inline style text', () => {
    expect(parseStyleText('fill=#fff;dashed;;rounded=1')).toEqual({
      fill: '#fff',
      dashed: '',
      rounded: '1',
    });
    expect(printStyleText({ fill: '#fff', dashed: '' })).toBe('fill=#fff;dashed');
  });
});
