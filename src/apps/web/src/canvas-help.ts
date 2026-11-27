import text from '../../../host/data/canvas-help.txt?raw';

// The Help dialog's list (help.ts): what the canvas does with the mouse and the keyboard. The file
// is the one the native apps compile in (host/canvas_help.hpp), read by the same rules.

/** Whose keys the list names: the file has one key column for each. */
export type HelpKeys = 'mac' | 'pc' | 'windows';

/** One gesture or shortcut and what it does. */
export interface HelpEntry {
  /** `⌘A`, `Drag a corner` */
  readonly keys: string;
  /** `Select everything` */
  readonly action: string;
}

/** A heading and its entries. */
export interface HelpSection {
  readonly title: string;
  readonly entries: readonly HelpEntry[];
}

const COLUMNS: readonly HelpKeys[] = ['mac', 'pc', 'windows'];

/** `s` without ASCII blanks at either end, as the C++ reader trims. */
const trimAscii = (s: string): string => s.replace(/^[ \t\r\n]+|[ \t\r\n]+$/g, '');

/**
 * Reads the file's format for `keys`' column: `# Heading` starts a section; other lines are
 * `action<TAB>mac<TAB>pc<TAB>windows`, where `=` repeats the column before and `-` leaves the line
 * out. Blank lines, `//` comments, malformed lines and sections left empty are skipped.
 */
export function parseCanvasHelp(source: string, keys: HelpKeys): HelpSection[] {
  const column = COLUMNS.indexOf(keys) + 1;
  const sections: { title: string; entries: HelpEntry[] }[] = [];
  for (const raw of source.split('\n')) {
    const line = trimAscii(raw);
    if (!line || line.startsWith('//')) continue;
    if (line.startsWith('#')) {
      const title = trimAscii(line.slice(1));
      if (title) sections.push({ title, entries: [] });
      continue;
    }
    const fields = line.split('\t').map(trimAscii);
    const section = sections[sections.length - 1];
    if (!section || fields.length !== 4 || !fields[0]) continue;
    let from = column;
    while (from > 1 && fields[from] === '=') from--;
    const key = fields[from]!;
    if (!key || key === '-' || key === '=') continue;
    section.entries.push({ keys: key, action: fields[0] });
  }
  return sections.filter((s) => s.entries.length > 0);
}

/** Whether `platform` (navigator.platform or userAgentData's) is a Mac's, iPad's or iPhone's. */
export const isApple = (platform: string): boolean => /mac|iphone|ipad|ipod/i.test(platform);

/** The built-in list with the keys for `platform`: a Mac's, or Ctrl and Alt everywhere else. */
export const canvasHelp = (platform: string): HelpSection[] =>
  parseCanvasHelp(text, isApple(platform) ? 'mac' : 'pc');
