import { describe, expect, it } from 'vitest';
import {
  gridLines,
  insideTurned,
  resizedBox,
  rotateHandle,
  rotatePoint,
  rotatedBounds,
  snap,
  turned,
} from './canvas-geometry';

// The same cases as the shared C++ canvas's tests (src/canvas/tests/canvas_test.cpp), so the web
// canvas writes what the native shells write.

const a = { x: 0, y: 0, width: 100, height: 50 };
const rounded = (b: { x: number; y: number; width: number; height: number }) =>
  `${Math.round(b.x)},${Math.round(b.y)} ${Math.round(b.width)}x${Math.round(b.height)}`;

describe('turned nodes', () => {
  it('hit where they are drawn', () => {
    // Turned a quarter, `a` covers x 25..75 and y -25..75.
    expect(insideTurned(a, 90, { x: 50, y: 70 })).toBe(true);
    expect(insideTurned(a, 90, { x: 90, y: 25 })).toBe(false);
    expect(rounded(rotatedBounds(a, 90))).toBe('25,-25 50x100');
  });

  it('resize in their own frame and keep the opposite corner on screen', () => {
    // The box's own bottom-right corner is drawn at the bottom left once turned a quarter.
    const br = rotatePoint({ x: 100, y: 50 }, { x: 50, y: 25 }, 90);
    expect(br.x).toBeCloseTo(25);
    expect(br.y).toBeCloseTo(75);
    // Dragging it 20 down makes the box 20 longer (its own width).
    const start = br;
    const at = { x: 25, y: 95 };
    expect(rounded(resizedBox(a, 90, 2, start, at, false))).toBe('-10,10 120x50');
    // With Shift the height follows in proportion.
    expect(rounded(resizedBox(a, 90, 2, start, at, true))).toBe('-15,5 120x60');
  });
});

describe('resizing', () => {
  it('moves the held corner and keeps the smallest size', () => {
    const s = { x: 100, y: 50 };
    expect(resizedBox(a, 0, 2, s, { x: 130, y: 60 }, false)).toEqual({
      x: 0,
      y: 0,
      width: 130,
      height: 60,
    });
    expect(resizedBox(a, 0, 2, s, { x: -500, y: -500 }, false)).toEqual({
      x: 0,
      y: 0,
      width: 10,
      height: 10,
    });
    // The top-left corner held: the bottom right stays.
    expect(resizedBox(a, 0, 0, { x: 0, y: 0 }, { x: -20, y: 10 }, false)).toEqual({
      x: -20,
      y: 10,
      width: 120,
      height: 40,
    });
  });

  it('scales both sides by the larger factor with Shift', () => {
    const b = resizedBox(a, 0, 2, { x: 100, y: 50 }, { x: 150, y: 50 }, true);
    expect(b.width).toBeCloseTo(150);
    expect(b.height).toBeCloseTo(75);
  });
});

describe('turning', () => {
  const handle = rotateHandle(a, 0, 17);
  const carried = (degrees: number) => rotatePoint(handle, { x: 50, y: 25 }, degrees);

  it('follows the pointer about the centre and snaps to 15° steps within 5°', () => {
    expect(turned(a, 0, handle, carried(90))).toBe(90);
    // 3° off 45 snaps to it; 7° off does not.
    expect(turned(a, 0, handle, carried(48))).toBe(45);
    expect(turned(a, 0, handle, carried(-38))).toBe(-38);
  });

  it('adds to the turn the node has, within (-180, 180]', () => {
    expect(turned(a, 168, handle, carried(20))).toBe(-172);
    expect(turned(a, 90, handle, carried(90))).toBe(180);
    expect(turned(a, -90, handle, carried(90))).toBe(0);
    expect(Object.is(turned(a, -90, handle, carried(90)), -0)).toBe(false);
  });

  it('has its handle off the top-right corner, turned with the node', () => {
    const d = 17 / Math.SQRT2;
    expect(handle).toEqual({ x: 100 + d, y: -d });
    const h = rotateHandle(a, 90, 17);
    // Turned a quarter, the top-right corner is drawn at the bottom right.
    expect(h.x).toBeCloseTo(75 + d);
    expect(h.y).toBeCloseTo(75 + d);
  });

  it('gives nothing with the pointer on the centre', () => {
    expect(turned(a, 0, handle, { x: 50, y: 25 })).toBeUndefined();
  });
});

// With a grid: the C++ canvas's grid tests (CanvasTest.*Grid*, FixedCanvasTest.*Grid*).
describe('the grid', () => {
  const fixed = { step: 10, fixed: true };
  const laidOut = { step: 10, fixed: false };

  it('puts the dragged corner in a fixed view on a grid point', () => {
    const b = resizedBox(a, 0, 2, { x: 100, y: 50 }, { x: 133, y: 67 }, false, fixed);
    expect(b).toEqual({ x: 0, y: 0, width: 130, height: 70 });
  });

  it('keeps an off-grid box at least the smallest size, on a grid point', () => {
    const off = { x: 3, y: 3, width: 100, height: 50 };
    const b = resizedBox(off, 0, 2, { x: 103, y: 53 }, { x: -500, y: -500 }, false, fixed);
    // The corner lands on 20, the first grid line at least 10 from 3.
    expect(b).toEqual({ x: 3, y: 3, width: 17, height: 17 });
  });

  it('gives whole steps of size in laid-out views, at least the smallest size', () => {
    const b = resizedBox(a, 0, 2, { x: 100, y: 50 }, { x: 133, y: 67 }, false, laidOut);
    expect(b).toEqual({ x: 0, y: 0, width: 130, height: 70 });
    // A step of 3 does not divide the smallest size (10): the smallest box is four steps.
    const tiny = resizedBox(a, 0, 2, { x: 100, y: 50 }, { x: -900, y: -900 }, false, {
      step: 3,
      fixed: false,
    });
    expect([tiny.width, tiny.height]).toEqual([12, 12]);
  });

  it('gives a turned node whole steps of size, even in a fixed view', () => {
    const b = resizedBox(a, 90, 2, { x: 25, y: 75 }, { x: 25, y: 97 }, false, fixed);
    expect(Math.round(b.width)).toBe(120);
    expect(Math.round(b.height)).toBe(50);
  });

  it('leaves a Shift resize off the grid, keeping the ratio', () => {
    const b = resizedBox(a, 0, 2, { x: 100, y: 50 }, { x: 150, y: 50 }, true, {
      step: 7,
      fixed: false,
    });
    expect(b.width).toBeCloseTo(150);
    expect(b.height).toBeCloseTo(75);
  });

  it('snaps to the nearest line, and not at all without a grid', () => {
    expect(snap(14, 10)).toBe(10);
    expect(snap(15, 10)).toBe(20);
    expect(snap(-14, 10)).toBe(-10);
    expect(snap(14, 0)).toBe(14);
  });

  it('draws every step 8 pixels apart, every fifth darker; zoomed out fewer, then none', () => {
    const lines = gridLines({ x: 0, y: 0 }, { x: 100, y: 20 }, 10, 1);
    expect(lines.major).toEqual([
      [0, true],
      [50, true],
      [100, true],
      [0, false],
    ]);
    expect(lines.minor.length).toBe(8 + 2);
    // 2 units a pixel: steps are 5 pixels apart, so only every fifth.
    const out = gridLines({ x: 0, y: 0 }, { x: 100, y: 0 }, 10, 2);
    expect(out.minor).toEqual([]);
    expect(out.major.filter(([, v]) => v).map(([x]) => x)).toEqual([0, 50, 100]);
    expect(gridLines({ x: 0, y: 0 }, { x: 100, y: 100 }, 10, 10)).toEqual({ minor: [], major: [] });
  });
});
