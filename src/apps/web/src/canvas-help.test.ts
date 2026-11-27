import { describe, expect, it } from 'vitest';
import { canvasHelp, isApple, parseCanvasHelp } from './canvas-help';

// The same samples as the C++ reader's tests (src/host/tests/canvas_help_test.cpp).
const SAMPLE =
  '// a comment\n' +
  '\n' +
  '# Select\n' +
  'Select everything\t⌘A\tCtrl+A\t-\n' +
  '  Clear the selection \t Esc \t=\t=\r\n' +
  '\n' +
  '#   Edit  \n' +
  'Undo\t⌘Z\tCtrl+Z\tCtrl+Z\n' +
  'Only on Windows\t-\t-\tCtrl+Y\n' +
  'Only on a Mac\t⌥\t-\t=\n';

describe('parseCanvasHelp', () => {
  it('reads the column for each kind of keys', () => {
    expect(parseCanvasHelp(SAMPLE, 'mac')).toEqual([
      {
        title: 'Select',
        entries: [
          { keys: '⌘A', action: 'Select everything' },
          { keys: 'Esc', action: 'Clear the selection' },
        ],
      },
      {
        title: 'Edit',
        entries: [
          { keys: '⌘Z', action: 'Undo' },
          { keys: '⌥', action: 'Only on a Mac' },
        ],
      },
    ]);
    expect(parseCanvasHelp(SAMPLE, 'pc')).toEqual([
      {
        title: 'Select',
        entries: [
          { keys: 'Ctrl+A', action: 'Select everything' },
          { keys: 'Esc', action: 'Clear the selection' },
        ],
      },
      { title: 'Edit', entries: [{ keys: 'Ctrl+Z', action: 'Undo' }] },
    ]);
    expect(parseCanvasHelp(SAMPLE, 'windows')).toEqual([
      { title: 'Select', entries: [{ keys: 'Esc', action: 'Clear the selection' }] },
      {
        title: 'Edit',
        entries: [
          { keys: 'Ctrl+Z', action: 'Undo' },
          { keys: 'Ctrl+Y', action: 'Only on Windows' },
        ],
      },
    ]);
  });

  it('leaves out what is not an entry', () => {
    const text =
      'Orphan\ta\tb\tc\n' +
      '# Empty\n' +
      '#\n' +
      '# Kept\n' +
      'Too few\ta\tb\n' +
      'Too many\ta\tb\tc\td\n' +
      '\ta\tb\tc\n' +
      'Repeats nothing\t=\t=\t=\n' +
      'Kept\ta\tb\tc\n';
    for (const keys of ['mac', 'pc', 'windows'] as const) {
      const sections = parseCanvasHelp(text, keys);
      expect(sections.map((s) => s.title)).toEqual(['Kept']);
      expect(sections[0]!.entries.map((e) => e.action)).toEqual(['Kept']);
    }
    expect(parseCanvasHelp('', 'mac')).toEqual([]);
  });
});

describe('canvasHelp', () => {
  it('names a Mac’s keys on a Mac and Ctrl elsewhere', () => {
    expect(isApple('MacIntel')).toBe(true);
    expect(isApple('macOS')).toBe(true);
    expect(isApple('Win32')).toBe(false);
    expect(isApple('Linux x86_64')).toBe(false);
    const keys = (platform: string) =>
      canvasHelp(platform)
        .flatMap((s) => s.entries.map((e) => e.keys))
        .join(' ');
    expect(keys('MacIntel')).toContain('⌘A');
    expect(keys('MacIntel')).not.toContain('Ctrl');
    expect(keys('Win32')).toContain('Ctrl+A');
    expect(keys('Win32')).not.toContain('⌘');
  });

  it('lists the gestures the old hint line did', () => {
    const actions = canvasHelp('MacIntel').flatMap((s) => s.entries.map((e) => e.action));
    for (const action of [
      'Place by the neighbours',
      'Connect two elements',
      'Reconnect a line',
      'Rename the id everywhere',
      'Edit properties',
      'Size to fit the content',
      'Reroute a line in a fixed view',
      'Undo and redo, in both panes',
    ])
      expect(actions).toContain(action);
  });
});
