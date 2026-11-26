import { describe, expect, it } from 'vitest';
import { COLOR_SWATCHES, parseSwatches } from './swatches';
import { normalizedColor } from './field-editors';

describe('parseSwatches', () => {
  it('reads rows of colours and names', () => {
    expect(
      parseSwatches(
        '// a comment\n\n#FFFFFF White\n  #abc   Light  blue  \r\n\n\n#000000\tBlack\n',
      ),
    ).toEqual([
      [
        { color: '#ffffff', name: 'White' },
        { color: '#aabbcc', name: 'Light  blue' },
      ],
      [{ color: '#000000', name: 'Black' }],
    ]);
  });

  it('leaves out lines that are not swatches', () => {
    expect(parseSwatches('red Red\n#12345 Short\n#123456\nplain words\n#654321 Kept')).toEqual([
      [{ color: '#654321', name: 'Kept' }],
    ]);
    expect(parseSwatches('')).toEqual([]);
    expect(parseSwatches('\n// only comments\n\n')).toEqual([]);
  });
});

describe('COLOR_SWATCHES', () => {
  it('is a grid of distinct named colours', () => {
    expect(COLOR_SWATCHES.length).toBeGreaterThan(0);
    const all = COLOR_SWATCHES.flat();
    for (const row of COLOR_SWATCHES) expect(row.length).toBe(COLOR_SWATCHES[0]!.length);
    for (const s of all) {
      expect(normalizedColor(s.color)).toBe(s.color);
      expect(s.name).not.toBe('');
    }
    expect(new Set(all.map((s) => s.color)).size).toBe(all.length);
    expect(new Set(all.map((s) => s.name)).size).toBe(all.length);
    expect(all.map((s) => s.color)).toEqual(expect.arrayContaining(['#ffffff', '#000000']));
  });
});
