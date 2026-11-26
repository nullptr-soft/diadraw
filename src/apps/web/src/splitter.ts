/**
 * Movable splitters between the app's panes, as the macOS app's split view has: the text pane |
 * the canvas | the properties panel. Dragging a splitter sets the width of the pane on its side
 * (the text pane to its left, the properties panel to its right); the canvas, the one pane without
 * a width of its own, takes the difference, and so also the window's changes in width. A pane
 * never gets narrower than its CSS `min-width`, and the canvas keeps at least `CANVAS_MIN`.
 * Arrow keys move a focused splitter, Home and End take it to its ends (the WAI-ARIA window
 * splitter pattern). Like the macOS app, the widths are not kept across reloads.
 */

/** The narrowest the canvas gets from dragging a splitter, in CSS pixels. */
export const CANVAS_MIN = 160;
/** How far an arrow key moves a splitter, and with Shift. */
export const KEY_STEP = 10;
export const KEY_STEP_LARGE = 50;

/** The widths a pane can take: `[min, max]`, with `max` never below `min`. */
export interface Range {
  readonly min: number;
  readonly max: number;
}

/**
 * The widths the pane can take while the canvas keeps `canvasMin`: from its own minimum to its
 * width plus what the canvas can give up.
 */
export function paneRange(
  width: number,
  min: number,
  canvasWidth: number,
  canvasMin = CANVAS_MIN,
): Range {
  return { min, max: Math.max(min, width + Math.max(0, canvasWidth - canvasMin)) };
}

export function clamp(width: number, range: Range): number {
  return Math.round(Math.min(range.max, Math.max(range.min, width)));
}

/**
 * The pane's width after the pointer moved `dx` from where the drag started at `start`: a pane on
 * the splitter's left follows the pointer, one on its right grows as the pointer moves left.
 */
export function dragged(start: number, dx: number, side: 'before' | 'after', range: Range): number {
  return clamp(side === 'before' ? start + dx : start - dx, range);
}

/**
 * The pane's width after a key on the splitter, or `undefined` for a key that does not move it.
 * Left and Right move the splitter itself, so they narrow or widen the pane by its side.
 */
export function keyed(
  width: number,
  key: string,
  shift: boolean,
  side: 'before' | 'after',
  range: Range,
): number | undefined {
  const step = shift ? KEY_STEP_LARGE : KEY_STEP;
  const toward = side === 'before' ? 1 : -1; // Right widens a pane on the left
  switch (key) {
    case 'ArrowRight':
      return clamp(width + toward * step, range);
    case 'ArrowLeft':
      return clamp(width - toward * step, range);
    case 'Home':
      return side === 'before' ? range.min : range.max;
    case 'End':
      return side === 'before' ? range.max : range.min;
    default:
      return undefined;
  }
}

/**
 * Makes `handle` (a `role="separator"` element) the splitter that sizes `pane`, which sits on its
 * `side`; `canvas` is the pane that takes the difference.
 */
export function attachSplitter(
  handle: HTMLElement,
  pane: HTMLElement,
  side: 'before' | 'after',
  canvas: HTMLElement,
): void {
  const range = (): Range => {
    const min = parseFloat(getComputedStyle(pane).minWidth) || 0;
    return paneRange(pane.getBoundingClientRect().width, min, canvas.getBoundingClientRect().width);
  };
  const show = (width: number, r: Range): void => {
    pane.style.setProperty('--pane-width', `${width}px`);
    handle.setAttribute('aria-valuenow', String(width));
    handle.setAttribute('aria-valuemin', String(Math.round(r.min)));
    handle.setAttribute('aria-valuemax', String(Math.round(r.max)));
  };

  handle.addEventListener('pointerdown', (e) => {
    if (e.button !== 0) return;
    e.preventDefault(); // no text selection while dragging
    const startX = e.clientX;
    const start = pane.getBoundingClientRect().width;
    const r = range();
    handle.setPointerCapture(e.pointerId);
    handle.classList.add('dragging');
    document.documentElement.classList.add('dd-resizing');
    const move = (m: PointerEvent): void => show(dragged(start, m.clientX - startX, side, r), r);
    const end = (): void => {
      handle.removeEventListener('pointermove', move);
      handle.removeEventListener('pointerup', end);
      handle.removeEventListener('lostpointercapture', end);
      handle.classList.remove('dragging');
      document.documentElement.classList.remove('dd-resizing');
    };
    handle.addEventListener('pointermove', move);
    handle.addEventListener('pointerup', end);
    handle.addEventListener('lostpointercapture', end);
  });

  handle.addEventListener('keydown', (e) => {
    const r = range();
    const width = keyed(pane.getBoundingClientRect().width, e.key, e.shiftKey, side, r);
    if (width === undefined) return;
    e.preventDefault();
    show(width, r);
  });
}
