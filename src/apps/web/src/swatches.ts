import text from '../../../host/data/color-swatches.txt?raw';
import { normalizedColor } from './field-editors';

// The preset colours every colour picker offers. The file is the one the native apps compile in
// (host/swatches.hpp), read by the same rules.

/** One preset colour. */
export interface Swatch {
  /** `#rrggbb`, lower case. */
  readonly color: string;
  /** `Light blue`: the swatch's tooltip and accessible label. */
  readonly name: string;
}

/**
 * Swatches in the file's format, row by row: `#rrggbb Name` per line, a blank line between rows,
 * `//` comment lines. Lines that are not a colour and a name are left out, as are empty rows.
 */
export function parseSwatches(source: string): Swatch[][] {
  const rows: Swatch[][] = [[]];
  for (const raw of source.split('\n')) {
    const line = raw.replace(/^[ \t\r]+|[ \t\r]+$/g, '');
    if (!line) {
      if (rows[rows.length - 1]!.length) rows.push([]);
      continue;
    }
    if (line.startsWith('//')) continue;
    const space = line.search(/[ \t]/);
    if (space < 0) continue;
    const color = normalizedColor(line.slice(0, space));
    const name = line.slice(space).replace(/^[ \t\r]+/, '');
    if (!color || !name) continue;
    rows[rows.length - 1]!.push({ color, name });
  }
  if (!rows[rows.length - 1]!.length) rows.pop();
  return rows;
}

/** The built-in swatches. */
export const COLOR_SWATCHES: readonly (readonly Swatch[])[] = parseSwatches(text);
