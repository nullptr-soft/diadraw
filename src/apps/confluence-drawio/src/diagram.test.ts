import { describe, expect, it, vi } from 'vitest';
import {
  fileBaseName,
  isEmptyDiagram,
  MAX_SVG_BYTES,
  packSvg,
  pageNames,
  readConfig,
  svgDataUri,
  svgFromDataUri,
  unpackSvg,
  withColorScheme,
} from './diagram.js';
import { Session, editorUrl, parseEvent, type Host } from './drawio.js';
import { exportAdf } from './index.js';
import { standaloneSvg } from './download.js';

/** An editable SVG as draw.io 32 exports it (`xmlsvg`), with one page named "Login flow". */
const drawn =
  '<svg xmlns="http://www.w3.org/2000/svg" style="background: transparent; background-color: transparent; color-scheme: light dark;" ' +
  'xmlns:xlink="http://www.w3.org/1999/xlink" version="1.1" width="122px" height="62px" viewBox="0 0 122 62" ' +
  'content="&lt;mxfile host=&quot;embed.diagrams.net&quot;&gt;&lt;diagram id=&quot;a1&quot; name=&quot;Login flow&quot;&gt;jZKxbo&lt;/diagram&gt;&lt;/mxfile&gt;">' +
  '<defs/><g><rect x="1" y="1" width="120" height="60" rx="9" fill="light-dark(#ffffff, var(--ge-dark-color, #121212))"/></g></svg>';

/** An empty page: draw.io draws it 1×1. */
const empty =
  '<svg xmlns="http://www.w3.org/2000/svg" style="color-scheme: light dark;" version="1.1" width="1px" height="1px" viewBox="0 0 1 1" ' +
  'content="&lt;mxfile&gt;&lt;diagram id=&quot;b&quot; name=&quot;Page-1&quot;&gt;dZE&lt;/diagram&gt;&lt;/mxfile&gt;"><defs/><g/></svg>';

describe('readConfig', () => {
  it('keeps a stored diagram and ignores anything else', () => {
    expect(readConfig({ diagram: 'abc', extra: 1 })).toEqual({ diagram: 'abc' });
    expect(readConfig({ diagram: '' })).toEqual({});
    expect(readConfig({ diagram: 42 })).toEqual({});
    expect(readConfig(undefined)).toEqual({});
    expect(readConfig(null)).toEqual({});
  });
});

describe('packSvg and unpackSvg', () => {
  it('round-trip an SVG through its stored form, smaller than the SVG', async () => {
    const big = drawn.replace(
      '<g>',
      `<g>${'<rect x="1" y="1" width="2" height="2"/>'.repeat(500)}`,
    );
    const packed = await packSvg(big);
    expect(packed).toMatch(/^[A-Za-z0-9+/]+=*$/);
    expect(packed.length).toBeLessThan(big.length / 10);
    expect(await unpackSvg(packed)).toBe(big);
  });

  it('keeps text outside ASCII', async () => {
    const svg = drawn.replace('Login flow', 'Вхід — 登录');
    expect(await unpackSvg(await packSvg(svg))).toBe(svg);
  });

  it('refuses a stored SVG that decompresses past the limit', async () => {
    const packed = await packSvg(drawn);
    expect(await unpackSvg(packed, drawn.length)).toBe(drawn);
    await expect(unpackSvg(packed, drawn.length - 1)).rejects.toThrow(/larger than/);
    const bomb = await packSvg(`<svg>${' '.repeat(MAX_SVG_BYTES)}</svg>`);
    expect(bomb.length).toBeLessThan(100_000);
    await expect(unpackSvg(bomb)).rejects.toThrow(/larger than 33554432 bytes/);
  });

  it('refuses what is not a stored SVG', async () => {
    await expect(unpackSvg('not base64!')).rejects.toThrow();
    await expect(unpackSvg(await packSvg('<html></html>'))).rejects.toThrow(/not an SVG/);
  });
});

describe('svgFromDataUri', () => {
  it('reads Base64 and percent-encoded SVG', () => {
    const base64 = `data:image/svg+xml;base64,${Buffer.from(drawn).toString('base64')}`;
    expect(svgFromDataUri(base64)).toBe(drawn);
    expect(svgFromDataUri(`data:image/svg+xml;charset=utf-8,${encodeURIComponent(drawn)}`)).toBe(
      drawn,
    );
    expect(svgFromDataUri(svgDataUri(drawn))).toBe(drawn);
  });

  it('refuses other images', () => {
    expect(() => svgFromDataUri('data:image/png;base64,iVBOR')).toThrow(/not send an SVG/);
  });
});

describe('isEmptyDiagram', () => {
  it('tells an empty page from a drawing', () => {
    expect(isEmptyDiagram(empty)).toBe(true);
    expect(isEmptyDiagram(drawn)).toBe(false);
  });

  it('never calls a file with several pages empty', () => {
    const twoPages = empty.replace(
      '&lt;/mxfile&gt;',
      '&lt;diagram id=&quot;c&quot; name=&quot;Page-2&quot;&gt;x&lt;/diagram&gt;&lt;/mxfile&gt;',
    );
    expect(isEmptyDiagram(twoPages)).toBe(false);
  });
});

describe('pageNames and fileBaseName', () => {
  it('read the page names from the embedded file', () => {
    expect(pageNames(drawn)).toEqual(['Login flow']);
    expect(fileBaseName(drawn)).toBe('Login-flow');
    const escaped = drawn.replace('Login flow', 'A &amp;amp; B');
    expect(pageNames(escaped)).toEqual(['A & B']);
  });

  it('fall back when the SVG carries no file', () => {
    expect(pageNames('<svg width="1px"></svg>')).toEqual([]);
    expect(fileBaseName('<svg></svg>')).toBe('diagram');
  });
});

describe('withColorScheme', () => {
  it('pins the colour scheme of the root element only', () => {
    const dark = withColorScheme(drawn, 'dark');
    expect(dark).toContain('background-color: transparent; color-scheme: dark;');
    expect(dark.replace('color-scheme: dark;', 'color-scheme: light dark;')).toBe(drawn);
  });

  it('adds one when the SVG has none', () => {
    expect(withColorScheme('<svg width="1"><g/></svg>', 'light')).toBe(
      '<svg style="color-scheme: light" width="1"><g/></svg>',
    );
    expect(withColorScheme('<svg style="fill: red"></svg>', 'dark')).toBe(
      '<svg style="color-scheme: dark; fill: red"></svg>',
    );
  });
});

describe('editorUrl', () => {
  it('opens embed mode with the JSON protocol and one Save & Exit button', () => {
    const url = new URL(editorUrl('https://embed.diagrams.net/'));
    expect(url.origin).toBe('https://embed.diagrams.net');
    expect(Object.fromEntries(url.searchParams)).toEqual({
      embed: '1',
      proto: 'json',
      spin: '1',
      ui: 'atlas',
      saveAndExit: '1',
      noSaveBtn: '1',
      libraries: '1',
    });
  });

  it('passes the language and dark theme, and keeps parameters of a self-hosted copy', () => {
    const url = new URL(
      editorUrl('https://draw.example.com/drawio/?offline=1', { lang: 'de-DE', dark: true }),
    );
    expect(url.pathname).toBe('/drawio/');
    expect(url.searchParams.get('offline')).toBe('1');
    expect(url.searchParams.get('lang')).toBe('de');
    expect(url.searchParams.get('dark')).toBe('1');
    expect(
      new URL(editorUrl('https://x.test/', { lang: '"><script>' })).searchParams.has('lang'),
    ).toBe(false);
  });
});

describe('parseEvent', () => {
  it('reads draw.io events and ignores other messages', () => {
    expect(parseEvent('{"event":"init"}')).toEqual({ event: 'init' });
    expect(parseEvent('{"action":"load"}')).toBeUndefined();
    expect(parseEvent('{not json')).toBeUndefined();
    expect(parseEvent({ event: 'init' })).toBeUndefined();
    expect(parseEvent('drawio-dev-saved')).toBeUndefined();
  });
});

function fakeHost(): Host & { posted: Record<string, unknown>[]; saved: (string | undefined)[] } {
  const posted: Record<string, unknown>[] = [];
  const saved: (string | undefined)[] = [];
  return {
    posted,
    saved,
    post: (action) => posted.push(action),
    save: (svg) => {
      saved.push(svg);
      return Promise.resolve();
    },
    close: vi.fn(),
    fail: vi.fn(),
  };
}

describe('Session', () => {
  const dataUri = (svg: string) => svgDataUri(svg);

  it('loads the stored diagram when the editor is ready', async () => {
    const host = fakeHost();
    await new Session(host, drawn, isEmptyDiagram, svgFromDataUri).handle({ event: 'init' });
    expect(host.posted).toEqual([{ action: 'load', xml: drawn, autosave: 0 }]);
  });

  it('saves an editable SVG on Save & Exit, and nothing for an empty diagram', async () => {
    const host = fakeHost();
    const session = new Session(host, '', isEmptyDiagram, svgFromDataUri);
    await session.handle({ event: 'save', exit: true });
    expect(host.posted).toEqual([{ action: 'export', format: 'xmlsvg', spinKey: 'saving' }]);
    await session.handle({ event: 'exit' }); // Sent after save by some versions: ignored.
    await session.handle({ event: 'export', format: 'svg', data: dataUri(drawn) });
    expect(host.saved).toEqual([drawn]);
    expect(host.close).not.toHaveBeenCalled();

    const again = fakeHost();
    const second = new Session(again, drawn, isEmptyDiagram, svgFromDataUri);
    await second.handle({ event: 'save', exit: true });
    await second.handle({ event: 'export', format: 'svg', data: dataUri(empty) });
    expect(again.saved).toEqual([undefined]);
  });

  it('ignores exports it did not ask for', async () => {
    const host = fakeHost();
    await new Session(host, '', isEmptyDiagram, svgFromDataUri).handle({
      event: 'export',
      data: dataUri(drawn),
    });
    expect(host.saved).toEqual([]);
  });

  it('reports a failed save and lets the user save again', async () => {
    const host = fakeHost();
    host.save = () => Promise.reject(new Error('too large'));
    const session = new Session(host, '', isEmptyDiagram, svgFromDataUri);
    await session.handle({ event: 'save', exit: true });
    await session.handle({ event: 'export', data: dataUri(drawn) });
    expect(host.fail).toHaveBeenCalledWith('Could not save the diagram: too large');
    expect(host.posted.at(-1)).toEqual({ action: 'spinner', show: false });
    await session.handle({ event: 'save', exit: true });
    expect(host.posted.at(-1)).toMatchObject({ action: 'export' });
  });

  it('closes on Exit', async () => {
    const host = fakeHost();
    await new Session(host, drawn, isEmptyDiagram, svgFromDataUri).handle({
      event: 'exit',
      modified: true,
    });
    expect(host.close).toHaveBeenCalledOnce();
    expect(host.saved).toEqual([]);
  });
});

describe('exportAdf', () => {
  it('gives an export a paragraph in place of the frame', () => {
    const doc = exportAdf();
    expect(doc).toMatchObject({ type: 'doc', version: 1 });
    expect(doc.content?.[0]?.type).toBe('paragraph');
  });
});

describe('standaloneSvg', () => {
  it('adds the XML declaration once', () => {
    expect(standaloneSvg(drawn)).toMatch(/^<\?xml version="1.0" encoding="UTF-8"\?>\n<svg /);
    expect(standaloneSvg(standaloneSvg(drawn))).toBe(standaloneSvg(drawn));
  });
});
