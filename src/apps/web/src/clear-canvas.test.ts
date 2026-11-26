import { describe, expect, it } from 'vitest';
import { clearAction, EMPTY_DIAGRAM } from './clear-canvas';

describe('clear canvas', () => {
  const text = 'model {\n  a = system "A"\n}\n';

  it('leaves an empty diagram alone', () => {
    expect(clearAction(EMPTY_DIAGRAM, undefined)).toBe('nothing');
    expect(clearAction(EMPTY_DIAGRAM, text)).toBe('nothing');
  });

  it('clears a saved diagram without asking', () => {
    expect(clearAction(text, text)).toBe('clear');
  });

  it('asks before clearing unsaved changes', () => {
    expect(clearAction(text, 'model {\n}\n// older\n')).toBe('ask');
    expect(clearAction(text, '')).toBe('ask');
  });

  it('asks for a diagram that was never saved', () => {
    expect(clearAction(text, undefined)).toBe('ask');
  });
});
