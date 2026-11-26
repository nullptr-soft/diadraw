import { describe, expect, it } from 'vitest';
import { readFileSync } from 'node:fs';
import { COPYRIGHT, LICENSE, creditsFor, parseCredits } from './credits';

describe('parseCredits', () => {
  it('reads tab-separated lines', () => {
    expect(
      parseCredits(
        '// a comment\n\nminiz\t3.0.2\tMIT\thttps://github.com/richgel999/miniz\tgtk macos  web\r\nGTK\t4\tLGPL-2.1-or-later\thttps://www.gtk.org\tgtk',
      ),
    ).toEqual([
      {
        name: 'miniz',
        version: '3.0.2',
        license: 'MIT',
        url: 'https://github.com/richgel999/miniz',
        apps: ['gtk', 'macos', 'web'],
      },
      {
        name: 'GTK',
        version: '4',
        license: 'LGPL-2.1-or-later',
        url: 'https://www.gtk.org',
        apps: ['gtk'],
      },
    ]);
  });

  it('gives a line ending in a tab no apps', () => {
    expect(parseCredits('x\t1\tMIT\thttps://x\t')).toEqual([
      { name: 'x', version: '1', license: 'MIT', url: 'https://x', apps: [] },
    ]);
  });

  it('leaves out lines that are not credits', () => {
    expect(parseCredits('')).toEqual([]);
    expect(parseCredits('\n// only comments\n   \n')).toEqual([]);
    expect(parseCredits('name only\nname\t1\tMIT\thttps://x\n\t1\tMIT\thttps://x\tgtk\n')).toEqual(
      [],
    );
  });
});

describe('the licence', () => {
  it('matches the LICENSE file', () => {
    const text = readFileSync(new URL('../../../../LICENSE', import.meta.url), 'utf8');
    expect(text.startsWith(LICENSE)).toBe(true);
    expect(text).toContain(COPYRIGHT.replace('Copyright ©', 'Copyright (c)'));
  });
});

describe('creditsFor', () => {
  const names = (app: string) => creditsFor(app).map((c) => c.name);

  it('credits what the web app ships, the core included', () => {
    expect(names('web')).toEqual(
      expect.arrayContaining(['miniz', 'codemirror', 'Emscripten runtime']),
    );
    expect(names('web')).not.toContain('gtkmm');
    expect(names('web')).not.toContain('Tauri');
  });

  it('credits Tauri in the desktop app', () => {
    expect(names('desktop')).toEqual(expect.arrayContaining(['Tauri', 'codemirror', 'miniz']));
  });

  it('gives every credit a version, a licence and an address', () => {
    for (const c of [...creditsFor('web'), ...creditsFor('desktop')]) {
      expect(c.version, c.name).not.toBe('');
      expect(c.license, c.name).not.toBe('');
      expect(c.url, c.name).toMatch(/^https:\/\//);
    }
  });
});
