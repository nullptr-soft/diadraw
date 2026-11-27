/**
 * Format Document's one setting: spaces per indentation level, the person's preference for every
 * document, kept in the browser (as the native apps keep it in their settings).
 */

/** The sizes the text pane offers; the core takes 1 to 8. */
export const INDENT_SIZES = [2, 3, 4, 8] as const;
export const DEFAULT_INDENT_SIZE = 2;

const INDENT = 'diadraw.format.indent';

/** Storage that may be missing or throw (private windows, blocked site data). */
type Store = Pick<Storage, 'getItem' | 'setItem'> | undefined;

function store(): Store {
  try {
    return globalThis.localStorage;
  } catch {
    return undefined;
  }
}

/** The remembered indent size, or the default when there is none or it is not 1 to 8. */
export function readIndentSize(from: Store = store()): number {
  try {
    const size = Number(from?.getItem(INDENT));
    return Number.isInteger(size) && size >= 1 && size <= 8 ? size : DEFAULT_INDENT_SIZE;
  } catch {
    return DEFAULT_INDENT_SIZE;
  }
}

export function writeIndentSize(size: number, to: Store = store()): void {
  try {
    to?.setItem(INDENT, String(size));
  } catch {
    // Not remembered; formatting still uses it on this page.
  }
}

/**
 * The one change that turns `before` into `after`: the span between their common start and end
 * (UTF-16 offsets into `before`), so the caret and scroll stay put where nothing changed.
 */
export function changeBetween(
  before: string,
  after: string,
): { from: number; to: number; insert: string } | undefined {
  if (before === after) return undefined;
  let start = 0;
  const max = Math.min(before.length, after.length);
  while (start < max && before.charCodeAt(start) === after.charCodeAt(start)) start++;
  let end = 0;
  while (
    end < max - start &&
    before.charCodeAt(before.length - 1 - end) === after.charCodeAt(after.length - 1 - end)
  )
    end++;
  return { from: start, to: before.length - end, insert: after.slice(start, after.length - end) };
}
