/**
 * The grid is the person's preference, for every document: whether it shows and its step in
 * diagram units, kept in the browser (as the macOS app keeps it in its user defaults). Not in the
 * text, which stays the drawing alone.
 */

export const GRID_SIZES = [5, 10, 20, 40] as const;
export const DEFAULT_GRID_SIZE = 10;

const SHOW = 'diadraw.grid.show';
const SIZE = 'diadraw.grid.size';

export interface GridSettings {
  readonly show: boolean;
  readonly size: number;
}

/** Storage that may be missing or throw (private windows, blocked site data). */
type Store = Pick<Storage, 'getItem' | 'setItem'> | undefined;

function store(): Store {
  try {
    return globalThis.localStorage;
  } catch {
    return undefined;
  }
}

export function readGrid(from: Store = store()): GridSettings {
  try {
    const size = Number(from?.getItem(SIZE));
    return {
      show: from?.getItem(SHOW) === 'true',
      size: Number.isFinite(size) && size > 0 ? size : DEFAULT_GRID_SIZE,
    };
  } catch {
    return { show: false, size: DEFAULT_GRID_SIZE };
  }
}

export function writeGrid(settings: GridSettings, to: Store = store()): void {
  try {
    to?.setItem(SHOW, String(settings.show));
    to?.setItem(SIZE, String(settings.size));
  } catch {
    // Not remembered; the grid still shows for this page.
  }
}

/** The step the canvas shows: the chosen size, or 0 while the grid is hidden. */
export function shownStep(settings: GridSettings): number {
  return settings.show ? settings.size : 0;
}
