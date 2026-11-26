import type { Relationship } from '@diadraw/wasm';
import { describe, expect, it } from 'vitest';
import {
  applyRelationshipFields,
  isName,
  relationshipFieldsOf,
  relationshipTitle,
} from './relationship-fields';

const writes: Relationship = {
  id: 'shop.web->shop.db',
  source: { element: 'shop.web' },
  target: { element: 'shop.db', port: 'sql' },
  label: 'writes',
  technology: 'JDBC',
  line: 'solid',
  bidirectional: false,
  tags: ['a', 'b'],
  properties: { pool: '8' },
  style: { stroke: '#c00' },
};

describe('relationship inspector fields', () => {
  it('round-trips a relationship unchanged, without a name for a derived id', () => {
    const fields = relationshipFieldsOf(writes);
    expect(fields.name).toBe('');
    expect(fields.arrow).toBe('->');
    expect(applyRelationshipFields(writes, fields)).toEqual({
      relationship: writes,
      name: undefined,
    });
  });

  it('shows an explicit id as the name and in the title', () => {
    const named = { ...writes, id: 'persist' };
    expect(relationshipFieldsOf(named).name).toBe('persist');
    expect(relationshipTitle(named)).toBe('persist = shop.web -> shop.db.sql');
    expect(relationshipTitle(writes)).toBe('shop.web -> shop.db.sql');
  });

  it('reads what was typed: arrows, trimmed text, cleared optional fields', () => {
    const { relationship, name } = applyRelationshipFields(writes, {
      ...relationshipFieldsOf(writes),
      label: '  ',
      technology: ' R2DBC ',
      arrow: '<->',
      name: ' persist ',
      tags: 'a, , c ',
      style: '',
      props: 'pool = 16\n\nretries=3',
    });
    expect(name).toBe('persist');
    expect(relationship.id).toBe(writes.id);
    expect(relationship.label).toBeUndefined();
    expect(relationship.technology).toBe('R2DBC');
    expect(relationship.bidirectional).toBe(true);
    expect(relationship.line).toBe('solid');
    expect(relationship.tags).toEqual(['a', 'c']);
    expect(relationship.style).toBeUndefined();
    expect(relationship.properties).toEqual({ pool: '16', retries: '3' });

    const dashed = applyRelationshipFields(writes, {
      ...relationshipFieldsOf(writes),
      arrow: '-->',
    });
    expect(dashed.relationship.line).toBe('dashed');
    expect(dashed.relationship.bidirectional).toBe(false);
  });

  it('knows names from derived ids', () => {
    expect(isName('calls')).toBe(true);
    expect(isName('_x2')).toBe(true);
    expect(isName('a->b')).toBe(false);
    expect(isName('2fast')).toBe(false);
    expect(isName('')).toBe(false);
  });
});
