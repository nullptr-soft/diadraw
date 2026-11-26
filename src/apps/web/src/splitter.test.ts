import { describe, expect, it } from 'vitest';
import { CANVAS_MIN, KEY_STEP, KEY_STEP_LARGE, dragged, keyed, paneRange } from './splitter';

describe('splitters', () => {
  it('let a pane grow by what the canvas can give up', () => {
    expect(paneRange(440, 280, 600)).toEqual({ min: 280, max: 440 + 600 - CANVAS_MIN });
    // A canvas already at or below its minimum gives nothing.
    expect(paneRange(440, 280, 100)).toEqual({ min: 280, max: 440 });
    // A pane squeezed below its minimum can still be dragged back to it.
    expect(paneRange(200, 280, 0)).toEqual({ min: 280, max: 280 });
  });

  it('move a pane on the left with the pointer and one on the right against it', () => {
    const range = { min: 240, max: 600 };
    expect(dragged(300, 50, 'before', range)).toBe(350);
    expect(dragged(300, 50, 'after', range)).toBe(250);
    expect(dragged(300, -500, 'before', range)).toBe(240);
    expect(dragged(300, -500, 'after', range)).toBe(600);
    expect(dragged(300, 10.6, 'before', range)).toBe(311);
  });

  it('move with the arrow keys, Home and End', () => {
    const range = { min: 240, max: 600 };
    expect(keyed(300, 'ArrowRight', false, 'before', range)).toBe(300 + KEY_STEP);
    expect(keyed(300, 'ArrowRight', false, 'after', range)).toBe(300 - KEY_STEP);
    expect(keyed(300, 'ArrowLeft', true, 'before', range)).toBe(300 - KEY_STEP_LARGE);
    expect(keyed(245, 'ArrowLeft', true, 'before', range)).toBe(240);
    expect(keyed(300, 'Home', false, 'before', range)).toBe(240);
    expect(keyed(300, 'Home', false, 'after', range)).toBe(600);
    expect(keyed(300, 'End', false, 'before', range)).toBe(600);
    expect(keyed(300, 'End', false, 'after', range)).toBe(240);
    expect(keyed(300, 'a', false, 'before', range)).toBeUndefined();
  });
});
