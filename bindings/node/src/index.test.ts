import { mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Worker } from 'node:worker_threads';
import { describe, expect, it } from 'vitest';
import {
  check,
  exportBpmn,
  exportDrawio,
  format,
  highlight,
  importDiagram,
  parse,
  print,
  renderPdf,
  renderPng,
  renderSvg,
  updateSource,
  version,
} from './index.js';

const corpusDir = new URL('../../../corpus/', import.meta.url);
const corpus = (name: string) => readFileSync(new URL(name, corpusDir), 'utf8');

const tiny = 'model {\n  a = system "A"\n  b = system "B"\n  a -> b "uses"\n}\n';

describe('@diadraw/node', () => {
  it('reports the version in the VERSION file', () => {
    const expected = readFileSync(new URL('../../../VERSION', import.meta.url), 'utf8').trim();
    expect(version).toBe(expected);
  });

  it('parses, and prints text that parses back to the same model', () => {
    const source = corpus('c4-02-banking.draw');
    const parsed = parse(source);
    expect(parsed.diagnostics).toEqual([]);
    expect(Object.keys(parsed.model.elements).length).toBeGreaterThan(5);
    const printed = print(parsed.model);
    expect(parse(printed).model).toEqual(parsed.model);
    expect(format(source)).toBe(printed);
  });

  it('reports syntax errors with UTF-16 lines and columns', () => {
    const source = 'model {\n  ö = process "😀" ]\n}\n';
    const { diagnostics } = parse(source);
    expect(diagnostics.length).toBeGreaterThan(0);
    expect(diagnostics[0]).toMatchObject({ severity: 'error', line: 2 });
    const checked = check(source);
    expect(checked.ok).toBe(false);
    expect(checked.diagnostics).toEqual(diagnostics);
    expect(() => format(source)).toThrow(/2:\d+: error:/);
    expect(check(tiny)).toEqual({ diagnostics: [], libraryWarnings: [], ok: true });
  });

  it('emits what rendering warns about as process warnings, for source and models', async () => {
    const seen: string[] = [];
    const listener = (w: Error) => {
      if (w.name === 'DiaDrawWarning') seen.push(w.message);
    };
    process.on('warning', listener);
    try {
      // c4 is built in, so it resolves offline; the shape it does not have is warned about.
      const source = 'use "c4@^1" as c4\nmodel {\n  a = system shape c4.nosuch\n}\n';
      renderSvg(source);
      renderSvg(parse(source).model);
      // Process warnings are emitted on the next tick.
      await new Promise<void>((r) => setImmediate(r));
      expect(seen.filter((m) => m.includes('no shape "c4.nosuch"')).length).toBe(2);
    } finally {
      process.off('warning', listener);
    }
  });

  it('warns about libraries a document uses that are not installed', () => {
    const r = check('use "nope@^1" as n\nmodel {\n}\n');
    expect(r.ok).toBe(true);
    expect(r.libraryWarnings[0]).toMatch(/nope/);
  });

  it('patches source text minimally, in UTF-16 offsets', () => {
    const source = 'model {\n  a = process "Ünïcödé 😀"\n  b = process\n}\n';
    const update = updateSource(source, { type: 'renameElement', id: 'b', name: 'Bé' });
    expect(update.source).toBe('model {\n  a = process "Ünïcödé 😀"\n  b = process "Bé"\n}\n');
    expect(update.minimal).toBe(true);
    let patched = source;
    for (const e of [...update.edits].sort((x, y) => y.from - x.from))
      patched = patched.slice(0, e.from) + e.insert + patched.slice(e.to);
    expect(patched).toBe(update.source);
    expect(() =>
      updateSource('model { a = process }', { type: 'removeElement', id: 'zz' }),
    ).toThrow();
  });

  it('highlights with UTF-16 offsets', () => {
    const text = 'model {\n  😀 = process\n}\n';
    const tokens = highlight(text);
    expect(tokens[0]).toEqual([0, 5, 'keyword']);
    const kind = tokens.find(([from, to]) => text.slice(from, to) === 'process');
    expect(kind).toBeTruthy();
  });

  it('renders every view of the sample documents as SVG, from source and from a model', () => {
    const files = readdirSync(corpusDir).filter((f) => f.endsWith('.draw'));
    expect(files.length).toBeGreaterThan(4);
    for (const file of files) {
      const source = corpus(file);
      const { model } = parse(source);
      const views = Object.keys(model.views);
      for (const view of views.length > 0 ? views : [undefined]) {
        const options = {
          file: fileURLToPath(new URL(file, corpusDir)),
          ...(view ? { view } : {}),
        };
        const svg = renderSvg(source, options);
        expect(svg, `${file} ${view}`).toMatch(/^<svg[^>]*xmlns=/);
        expect(renderSvg(model, options)).toBe(svg);
      }
    }
  });

  it('renders PNG and PDF', () => {
    const png = renderPng(tiny, { scale: 1 });
    expect(png.subarray(0, 8)).toEqual(
      Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    );
    // The IHDR width doubles with the scale.
    const width = (b: Buffer) => b.readUInt32BE(16);
    expect(width(renderPng(tiny, { scale: 2 }))).toBe(2 * width(png));
    expect(() => renderPng(tiny, { scale: 0 })).toThrow(RangeError);
    expect(() => renderPng(tiny, { scale: Number.NaN })).toThrow(RangeError);
    expect(() => renderPng(tiny, { scale: 9 })).toThrow(RangeError);
    // Checked before the document is read: the bad scale is reported, not the bad source.
    expect(() => renderPng('model { a = }', { scale: 0 })).toThrow(RangeError);
    // The backend reads hex colours; anything else is refused rather than ignored.
    for (const background of ['#f00', '#00FF00'])
      expect(renderPng(tiny, { scale: 1, background }).subarray(1, 4).toString('latin1')).toBe(
        'PNG',
      );
    for (const background of ['white', 'rgb(0,0,0)', '#gggggg', '#12zzzz', '#1234'])
      expect(() => renderPng(tiny, { background }), background).toThrow(RangeError);
    const pdf = renderPdf(tiny);
    expect(pdf.subarray(0, 5).toString('latin1')).toBe('%PDF-');
  });

  it('refuses source with errors, unknown views and wrong arguments', () => {
    expect(() => renderSvg('model { a = }')).toThrow(/error/);
    expect(() => renderSvg(tiny, { view: 'nope' })).toThrow(/no view "nope" \(views: default\)/);
    expect(() => print('not a model' as never)).toThrow();
    expect(() => renderSvg({ elements: 'nope' } as never)).toThrow();
    expect(() => exportDrawio({ elements: 'nope' } as never)).toThrow();
    // @ts-expect-error a number is not source text
    expect(() => parse(42)).toThrow(TypeError);
  });

  it('imports other formats, from text and from bytes', () => {
    const mermaid = importDiagram(
      'flowchart LR\n  a[Start] --> b{Ok?}\n  click a callback\n',
      'x.mmd',
    );
    expect(Object.keys(mermaid.model.elements)).toEqual(['a', 'b']);
    expect(mermaid.report.dropped.length).toBeGreaterThan(0);
    const bytes = importDiagram(new TextEncoder().encode('flowchart LR\n  a --> b\n'), 'y.mmd');
    expect(Object.keys(bytes.model.elements)).toEqual(['a', 'b']);
    expect(() => importDiagram('hello', 'x.txt')).toThrow(/cannot tell the format/);
    expect(() => importDiagram('hello', 'x.txt', { format: 'nope' as never })).toThrow(
      /unknown format/,
    );
    expect(print(mermaid.model)).toContain('a = ');
    // Other typed arrays and plain buffers are not bytes the importers take.
    expect(() => importDiagram(new Uint16Array([65]) as never, 'x.mmd')).toThrow(TypeError);
    expect(() => importDiagram(new ArrayBuffer(4) as never, 'x.mmd')).toThrow(TypeError);
    // Broken draw.io PNGs and stray bytes are errors or empty models, never crashes.
    const png = renderPng(tiny, { scale: 1 });
    for (const bytes of [png.subarray(0, 40), Buffer.from([0xff, 0xfe, 0x00, 0xc3])]) {
      try {
        const r = importDiagram(bytes, 'broken.drawio.png', { format: 'drawio' });
        expect(r.model.elements).toBeDefined();
        expect(r.report.dropped).toBeDefined();
      } catch (e) {
        expect(e).toBeInstanceOf(Error);
      }
    }
  });

  it('reads PlantUML includes from disk next to the file', () => {
    const dir = mkdtempSync(join(tmpdir(), 'diadraw-node-'));
    try {
      mkdirSync(join(dir, 'parts'));
      writeFileSync(join(dir, 'parts', 'c.iuml'), 'participant C\n');
      const file = join(dir, 'x.puml');
      const r = importDiagram('@startuml\n!include parts/c.iuml\nA -> B : hi\n@enduml\n', file);
      expect(Object.keys(r.model.elements)).toEqual(['C', 'A', 'B']);
    } finally {
      rmSync(dir, { recursive: true, force: true });
    }
  });

  it('exports draw.io and BPMN', () => {
    const source = corpus('bpmn-01-order-process.draw');
    const bpmn = exportBpmn(source);
    expect(bpmn).toContain('<bpmndi:BPMNDiagram');
    const drawio = exportDrawio(source);
    expect(drawio).toContain('<mxfile');
    const views = Object.keys(parse(source).model.views);
    expect(drawio.match(/<diagram /g)?.length).toBe(views.length);
    expect(exportDrawio(source, { view: views[0] ?? '' }).match(/<diagram /g)?.length).toBe(1);
    expect(() => exportDrawio(source, { view: 'nope' })).toThrow(/no view "nope"/);
    const back = importDiagram(bpmn, 'round.bpmn');
    expect(Object.keys(back.model.elements).length).toBeGreaterThan(0);
  });

  it('runs in worker threads, rendering PNG at the same time', async () => {
    const url = new URL('./index.ts', import.meta.url).href;
    const code = `
      const { parentPort } = require('node:worker_threads');
      const { createRequire } = require('node:module');
      const addon = createRequire(${JSON.stringify(url)})('./diadraw.node');
      const [svg] = addon.exportDocument(${JSON.stringify(tiny)}, false, 'x.draw', 'svg', '', 2, '');
      for (let i = 0; i < 5; i++) addon.exportDocument(${JSON.stringify(tiny)}, false, 'x.draw', 'png', '', 2, '#fff');
      parentPort.postMessage(svg.slice(0, 4));
    `;
    const results = await Promise.all(
      [0, 1, 2, 3].map(
        () =>
          new Promise<string>((resolve, reject) => {
            const w = new Worker(code, { eval: true });
            w.once('message', resolve);
            w.once('error', reject);
          }),
      ),
    );
    expect(results).toEqual(['<svg', '<svg', '<svg', '<svg']);
  });
});
