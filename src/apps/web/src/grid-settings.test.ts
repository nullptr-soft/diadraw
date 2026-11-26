import { describe, expect, it } from 'vitest';
import { DEFAULT_GRID_SIZE, readGrid, shownStep, writeGrid } from './grid-settings';

function memory(): Pick<Storage, 'getItem' | 'setItem'> {
  const items = new Map<string, string>();
  return { getItem: (k) => items.get(k) ?? null, setItem: (k, v) => void items.set(k, v) };
}

describe('grid settings', () => {
  it('start hidden at the default size', () => {
    expect(readGrid(memory())).toEqual({ show: false, size: DEFAULT_GRID_SIZE });
    expect(readGrid(undefined)).toEqual({ show: false, size: DEFAULT_GRID_SIZE });
  });

  it('are remembered', () => {
    const s = memory();
    writeGrid({ show: true, size: 20 }, s);
    expect(readGrid(s)).toEqual({ show: true, size: 20 });
    expect(shownStep(readGrid(s))).toBe(20);
    writeGrid({ show: false, size: 20 }, s);
    expect(shownStep(readGrid(s))).toBe(0);
  });

  it('ignore a bad size and storage that throws', () => {
    const s = memory();
    s.setItem('diadraw.grid.size', 'nope');
    expect(readGrid(s).size).toBe(DEFAULT_GRID_SIZE);
    const broken = {
      getItem: () => {
        throw new Error('blocked');
      },
      setItem: () => {
        throw new Error('blocked');
      },
    };
    expect(readGrid(broken)).toEqual({ show: false, size: DEFAULT_GRID_SIZE });
    expect(() => writeGrid({ show: true, size: 5 }, broken)).not.toThrow();
  });
});
