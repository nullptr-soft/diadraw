import { describe, expect, it } from 'vitest';
import {
  changeBetween,
  DEFAULT_INDENT_SIZE,
  readIndentSize,
  writeIndentSize,
} from './format-settings';

function memory(): Pick<Storage, 'getItem' | 'setItem'> {
  const items = new Map<string, string>();
  return { getItem: (k) => items.get(k) ?? null, setItem: (k, v) => void items.set(k, v) };
}

describe('format settings', () => {
  it('start at the default indent size and are remembered', () => {
    const s = memory();
    expect(readIndentSize(s)).toBe(DEFAULT_INDENT_SIZE);
    expect(readIndentSize(undefined)).toBe(DEFAULT_INDENT_SIZE);
    writeIndentSize(4, s);
    expect(readIndentSize(s)).toBe(4);
  });

  it('ignore a bad size and storage that throws', () => {
    const s = memory();
    for (const bad of ['nope', '0', '9', '2.5']) {
      s.setItem('diadraw.format.indent', bad);
      expect(readIndentSize(s)).toBe(DEFAULT_INDENT_SIZE);
    }
    const broken = {
      getItem: () => {
        throw new Error('blocked');
      },
      setItem: () => {
        throw new Error('blocked');
      },
    };
    expect(readIndentSize(broken)).toBe(DEFAULT_INDENT_SIZE);
    expect(() => writeIndentSize(4, broken)).not.toThrow();
  });
});

describe('changeBetween', () => {
  it('covers only what differs', () => {
    expect(changeBetween('a\n b\nc\n', 'a\n  b\nc\n')).toEqual({ from: 3, to: 3, insert: ' ' });
    expect(changeBetween('same', 'same')).toBeUndefined();
    const before = 'model {\nx = "é"\n}\n';
    const after = 'model {\n  x = "é"\n}\n';
    const c = changeBetween(before, after)!;
    expect(before.slice(0, c.from) + c.insert + before.slice(c.to)).toBe(after);
  });
});
