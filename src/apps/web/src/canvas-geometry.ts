import type { Box, Point } from '@diadraw/wasm';

/**
 * The geometry behind the canvas's handles, without the DOM, so it can be tested. It follows the
 * shared C++ canvas (src/canvas/src/canvas.cpp), so a gesture writes the same in every shell.
 */

export const MIN_SIZE = 10; // the smallest box a resize leaves
const SNAP_STEP = 15; // degrees a turn snaps to…
const SNAP_WITHIN = 5; // …when it is this close

export function center(b: Box): Point {
  return { x: b.x + b.width / 2, y: b.y + b.height / 2 };
}

/** `p` turned `degrees` clockwise (y down) about `about`. */
export function rotatePoint(p: Point, about: Point, degrees: number): Point {
  if (degrees === 0) return p;
  const r = (degrees * Math.PI) / 180;
  const c = Math.cos(r);
  const s = Math.sin(r);
  const dx = p.x - about.x;
  const dy = p.y - about.y;
  return { x: about.x + dx * c - dy * s, y: about.y + dx * s + dy * c };
}

/** The axis-aligned box around `b` turned `degrees` about its centre. */
export function rotatedBounds(b: Box, degrees: number): Box {
  if (degrees === 0) return b;
  const r = (degrees * Math.PI) / 180;
  const c = Math.abs(Math.cos(r));
  const s = Math.abs(Math.sin(r));
  const w = b.width * c + b.height * s;
  const h = b.width * s + b.height * c;
  const m = center(b);
  return { x: m.x - w / 2, y: m.y - h / 2, width: w, height: h };
}

export function inside(b: Box, p: Point): boolean {
  return p.x >= b.x && p.x <= b.x + b.width && p.y >= b.y && p.y <= b.y + b.height;
}

/** Whether `p` is in `b` turned `degrees` about its centre. */
export function insideTurned(b: Box, degrees: number, p: Point): boolean {
  return inside(b, rotatePoint(p, center(b), -degrees));
}

/** A box's corners, clockwise from the top left. */
export function corners(b: Box): Point[] {
  return [
    { x: b.x, y: b.y },
    { x: b.x + b.width, y: b.y },
    { x: b.x + b.width, y: b.y + b.height },
    { x: b.x, y: b.y + b.height },
  ];
}

/** `v` on the nearest line of a grid of `step` (unchanged without a grid, `step` 0). */
export function snap(v: number, step: number): number {
  return step > 0 ? Math.round(v / step) * step : v;
}

/** How a resize lines up with a grid: its step (0: none), and whether the view is fixed. */
export interface ResizeGrid {
  readonly step: number;
  readonly fixed: boolean;
}

/**
 * The box a resize leaves: `handle` (a corner, clockwise from the top left) dragged from `start`
 * to `at`; the opposite corner stays. On a node turned `degrees` this happens in the node's own
 * frame, and the box is moved so the opposite corner stays where it is on screen. With `keep`,
 * both sides scale by one factor: the larger of the two the pointer asks for, so the box reaches
 * it, off the grid (a grid step rarely keeps a ratio). Otherwise, with a grid, an upright node in
 * a fixed view gets its dragged corner on a grid point; elsewhere (only the size is written) and
 * on a turned node the sides become whole grid steps.
 */
export function resizedBox(
  b: Box,
  degrees: number,
  handle: number,
  start: Point,
  at: Point,
  keep: boolean,
  grid: ResizeGrid = { step: 0, fixed: false },
): Box {
  const cs = corners(b);
  const fixed = cs[(handle + 2) % 4]!;
  const held = cs[handle]!;
  const delta = rotatePoint({ x: at.x - start.x, y: at.y - start.y }, { x: 0, y: 0 }, -degrees);
  let x = held.x + delta.x;
  let y = held.y + delta.y;
  const left = handle === 0 || handle === 3;
  const top = handle === 0 || handle === 1;
  if (keep && b.width > 0 && b.height > 0) {
    const s = Math.max(
      (left ? fixed.x - x : x - fixed.x) / b.width,
      (top ? fixed.y - y : y - fixed.y) / b.height,
      MIN_SIZE / b.width,
      MIN_SIZE / b.height,
    );
    x = fixed.x + (left ? -b.width : b.width) * s;
    y = fixed.y + (top ? -b.height : b.height) * s;
  } else if (grid.step > 0) {
    const g = grid.step;
    if (grid.fixed && degrees === 0) {
      // The corner is written where it is: on a grid point, the nearest one at least the smallest
      // size away from the fixed corner (which may itself be off the grid).
      x = left
        ? Math.min(snap(x, g), Math.floor((fixed.x - MIN_SIZE) / g) * g)
        : Math.max(snap(x, g), Math.ceil((fixed.x + MIN_SIZE) / g) * g);
      y = top
        ? Math.min(snap(y, g), Math.floor((fixed.y - MIN_SIZE) / g) * g)
        : Math.max(snap(y, g), Math.ceil((fixed.y + MIN_SIZE) / g) * g);
    } else {
      const least = Math.ceil(MIN_SIZE / g) * g;
      const w = Math.max(least, snap(left ? fixed.x - x : x - fixed.x, g));
      const h = Math.max(least, snap(top ? fixed.y - y : y - fixed.y, g));
      x = fixed.x + (left ? -w : w);
      y = fixed.y + (top ? -h : h);
    }
  } else {
    x = left ? Math.min(x, fixed.x - MIN_SIZE) : Math.max(x, fixed.x + MIN_SIZE);
    y = top ? Math.min(y, fixed.y - MIN_SIZE) : Math.max(y, fixed.y + MIN_SIZE);
  }
  const out = {
    x: Math.min(x, fixed.x),
    y: Math.min(y, fixed.y),
    width: Math.abs(x - fixed.x),
    height: Math.abs(y - fixed.y),
  };
  if (degrees !== 0) {
    const was = rotatePoint(fixed, center(b), degrees);
    const now = rotatePoint(fixed, center(out), degrees);
    out.x += was.x - now.x;
    out.y += was.y - now.y;
  }
  return out;
}

/**
 * The turn a rotate drag gives a node now turned `degrees`: as far as the pointer turned about the
 * node's centre from `start` to `at`, in (-180, 180], snapped to 15° steps within 5°, otherwise
 * whole degrees. Undefined when the pointer is on the centre.
 */
export function turned(b: Box, degrees: number, start: Point, at: Point): number | undefined {
  const c = center(b);
  if (at.x === c.x && at.y === c.y) return undefined;
  const angle = (p: Point) => (Math.atan2(p.y - c.y, p.x - c.x) * 180) / Math.PI;
  let d = (degrees + angle(at) - angle(start)) % 360;
  if (d > 180) d -= 360;
  if (d <= -180) d += 360;
  const snapped = Math.round(d / SNAP_STEP) * SNAP_STEP;
  d = Math.abs(d - snapped) <= SNAP_WITHIN ? snapped : Math.round(d);
  if (d <= -180) d += 360;
  return d === 0 ? 0 : d; // no -0
}

/**
 * Where a node's rotate handle is: off its top-right corner, diagonally, `reach` from it, turned
 * with the node.
 */
export function rotateHandle(b: Box, degrees: number, reach: number): Point {
  const d = reach / Math.SQRT2;
  return rotatePoint({ x: b.x + b.width + d, y: b.y - d }, center(b), degrees);
}

export function distanceToSegment(p: Point, a: Point, b: Point): number {
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const len = dx * dx + dy * dy;
  const t = len === 0 ? 0 : Math.max(0, Math.min(1, ((p.x - a.x) * dx + (p.y - a.y) * dy) / len));
  return Math.hypot(a.x + t * dx - p.x, a.y + t * dy - p.y);
}

const MAJOR = 5; // every fifth line is darker
const MIN_GAP = 8; // pixels between drawn lines at least
const MAX_LINES = 100000; // more than any screen holds at MIN_GAP pixels apart

/**
 * The grid lines to draw over the area from `from` to `to` (diagram units): every step where they
 * are at least 8 pixels apart, every fifth (`major`) darker; zoomed far out only the major ones,
 * or none. Each line is its coordinate, `vertical` or not.
 */
export function gridLines(
  from: Point,
  to: Point,
  step: number,
  unitsPerPixel: number,
): { minor: [number, boolean][]; major: [number, boolean][] } {
  const out = { minor: [] as [number, boolean][], major: [] as [number, boolean][] };
  if (!(step > 0) || !Number.isFinite(step)) return out;
  const every =
    step / unitsPerPixel >= MIN_GAP
      ? step
      : (step * MAJOR) / unitsPerPixel >= MIN_GAP
        ? step * MAJOR
        : 0;
  if (every === 0) return out;
  // Counted by an index, not by adding the step, so lines do not drift on fractional steps.
  const lines = (lo: number, hi: number, vertical: boolean) => {
    const first = Math.ceil(lo / every);
    const last = Math.floor(hi / every);
    if (!Number.isFinite(first) || !Number.isFinite(last) || last - first > MAX_LINES) return;
    for (let i = first; i <= last; i++) {
      const v = i * every;
      const strong = Math.abs(Math.round(v / step)) % MAJOR === 0;
      (strong ? out.major : out.minor).push([v, vertical]);
    }
  };
  lines(Math.min(from.x, to.x), Math.max(from.x, to.x), true);
  lines(Math.min(from.y, to.y), Math.max(from.y, to.y), false);
  return out;
}
