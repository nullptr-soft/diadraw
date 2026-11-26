import { mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { beforeAll, describe, expect, it } from 'vitest';
import type { DiaDraw, JsonRpcMessage, SessionChange, SourceUpdate } from './index.js';
import { DiaDrawError, loadDiaDraw } from './index.js';
import { nodeFileSystem } from './node.js';

const corpus = (name: string) =>
  readFileSync(new URL(`../../../corpus/${name}`, import.meta.url), 'utf8');

let dd: DiaDraw;
beforeAll(async () => {
  dd = await loadDiaDraw();
});

describe('@diadraw/wasm', () => {
  it('parses, and prints text that parses back to the same model', () => {
    const source = corpus('c4-02-banking.draw');
    const parsed = dd.parse(source);
    expect(parsed.diagnostics).toEqual([]);
    expect(Object.keys(parsed.model.elements).length).toBeGreaterThan(5);
    const printed = dd.print(parsed.model);
    expect(dd.parse(printed).model).toEqual(parsed.model);
    expect(dd.print(dd.parse(printed).model)).toBe(printed);
  });

  it('reports syntax errors with UTF-16 lines and columns', () => {
    const { diagnostics } = dd.parse('model {\n  ö = process "😀" ]\n}\n');
    expect(diagnostics.length).toBeGreaterThan(0);
    expect(diagnostics[0]).toMatchObject({ severity: 'error', line: 2 });
  });

  it('patches text minimally, in UTF-16 offsets', () => {
    const source = 'model {\n  a = process "Ünïcödé 😀"\n  b = process\n}\n';
    const ours = dd.updateSource(source, { type: 'renameElement', id: 'b', name: 'Bé' });
    expect(ours.source).toBe('model {\n  a = process "Ünïcödé 😀"\n  b = process "Bé"\n}\n');
    expect(ours.minimal).toBe(true);
    let patched = source;
    for (const e of [...ours.edits].sort((x, y) => y.from - x.from))
      patched = patched.slice(0, e.from) + e.insert + patched.slice(e.to);
    expect(patched).toBe(ours.source);
  });

  it('refuses invalid operations with a DiaDrawError', () => {
    expect(() =>
      dd.updateSource('model { a = process }', { type: 'removeElement', id: 'zz' }),
    ).toThrow(DiaDrawError);
  });

  it('renders every view of the sample documents as standalone SVG', () => {
    const files = readdirSync(new URL('../../../corpus/', import.meta.url)).filter((f) =>
      f.endsWith('.draw'),
    );
    expect(files.length).toBeGreaterThan(4);
    for (const file of files.sort()) {
      const session = dd.createSession(corpus(file));
      const { model, views } = session.state();
      for (const { id } of views) {
        const svg = dd.renderSvg(model, id);
        expect(svg).toMatch(/^<svg [^>]*xmlns="http:\/\/www.w3.org\/2000\/svg"/);
        expect(svg).toMatchSnapshot(`${file} ${id}`);
      }
      session.dispose();
    }
  });
});

describe('Session', () => {
  const SOURCE =
    'model {\n  b = process "B"   // start\n  c = process "C"\n  b -> c\n}\n\nview v {\n  include *\n  pin c below b\n}\n';

  it('renders a view and reports its geometry', () => {
    const s = dd.createSession(SOURCE);
    const r = s.render('v');
    expect(r.content).toContain('data-id="b"');
    expect(Object.keys(r.nodes).sort()).toEqual(['b', 'c']);
    expect(r.relationships).toEqual(['b->c']);
    s.dispose();
  });

  it('turns gestures into text updates and keeps comments', () => {
    const s = dd.createSession(SOURCE);
    const b = s.render('v').nodes.b!;
    const moved = s.drop('v', 'c', { ...b, x: b.x + 400 });
    expect(moved?.source).toContain('align b c horizontally');
    const created = s.create('decision');
    expect(created.id).toBe('decision1');
    expect(s.connect('decision1', 'b', 'back').source).toContain('decision1 -> b "back"');
    const removed = s.remove({ elements: ['c'], relationships: [] });
    expect(removed?.source).not.toContain('c = process');
    expect(s.source).toContain('// start');
    expect(s.state().views.map((v) => v.id)).toEqual(['v']);
    s.dispose();
    expect(() => s.state()).toThrow(DiaDrawError);
  });

  it('resizes nodes and moves bend points in fixed views', () => {
    const s = dd.createSession(
      'model {\n  a = process "A"\n  b = process "B"\n  pay = a -> b\n}\n\nview v {\n  include *\n  layout fixed\n  pin a at 0, 0  // left\n}\n',
    );
    expect(s.render('v').algorithm).toBe('fixed');
    const resized = s.resize('v', 'a', { x: 0, y: 0, width: 140.4, height: 60 });
    expect(resized?.minimal).toBe(true);
    expect(s.source).toContain('  pin a at 0, 0 size 140, 60  // left\n');
    expect(s.resize('v', 'a', { x: 0, y: 0, width: 140, height: 60 })).toBeUndefined();
    s.fit('v', ['a']);
    expect(s.source).toContain('  pin a at 0, 0  // left\n');
    expect(s.fit('v', ['a'])).toBeUndefined();
    s.resize('v', 'a', { x: 0, y: 0, width: 140, height: 60 });
    s.reroute('v', 'pay', [{ x: 100, y: 120 }]);
    expect(s.source).toContain('route "pay" via 100, 120\n}');
    expect(s.render('v').edges.pay!.points[1]).toEqual({ x: 100, y: 120 });
    s.reroute('v', 'pay', []);
    expect(s.source).not.toContain('route');
    // Views laid out automatically keep only the size.
    const laidOut = dd.createSession(SOURCE);
    laidOut.resize('v', 'b', { x: 0, y: 0, width: 90, height: 40 });
    expect(laidOut.source).toContain('size b 90, 40');
    laidOut.dispose();
    s.dispose();
  });

  it('moves several elements as one change', () => {
    const s = dd.createSession(
      'model {\n  a = process "A"\n  b = process "B"\n  c = process "C"\n}\n\nview v {\n  include *\n  layout fixed\n  pin a at 0, 0\n  pin b at 200, 0\n  pin c at 400, 0\n}\n',
    );
    const update = s.move('v', ['a', 'b'], { x: 10, y: 20 });
    expect(update).toBeDefined();
    expect(s.source).toContain('  pin a at 10, 20\n  pin b at 210, 20\n  pin c at 400, 0\n');
    expect(s.move('v', ['a'], { x: 0, y: 0 })).toBeUndefined();
    expect(() => s.move('v', 'a' as unknown as string[], { x: 1, y: 1 })).toThrow(DiaDrawError);
    expect(() => s.move('v', ['a'], { x: NaN, y: 0 })).toThrow(DiaDrawError);
    expect(s.source).not.toContain('null');
    s.dispose();
  });

  it('edits a relationship from an inspector, renaming it with its routes', () => {
    const s = dd.createSession(SOURCE + 'view w {\n  include *\n  route "b->c" via 1, 2\n}\n');
    const r = s.state().model.relationships['b->c']!;
    const named = s.updateRelationship({ ...r, label: 'next', line: 'dashed' }, 'step');
    expect(named.id).toBe('step');
    expect(named.update?.source).toContain('  step = b --> c "next"\n');
    expect(named.update?.source).toContain('route "step" via 1, 2');
    expect(named.update?.source).toContain('// start');
    const step = s.state().model.relationships.step!;
    expect(s.updateRelationship(step, 'step')).toEqual({ id: 'step', update: undefined });
    expect(() => s.updateRelationship(step, 'not a name')).toThrow(DiaDrawError);
    const unnamed = s.updateRelationship(step);
    expect(unnamed.id).toBe('b->c');
    expect(unnamed.update?.source).toContain('  b --> c "next"\n');
    s.dispose();
  });

  it('renames an element and every reference to it', () => {
    const text =
      'model {\n  shop = system {\n    web = container\n    db = container "Orders"\n    web -> db\n  }\n  me = person "Ä"\n  me -> shop.web // buys\n}\n\nview v {\n  include *\n  layout fixed\n  route "shop.web->shop.db" via 1, 2\n}\n';
    const s = dd.createSession(text);
    // Offsets count UTF-16 units, as the text editor does; `Ä` before it does not shift them.
    expect(s.elementAt(text.indexOf('shop.web //') + 6)).toBe('shop.web');
    expect(s.elementAt(text.indexOf('shop.web //') + 1)).toBe('shop');
    expect(s.elementAt(text.indexOf('container'))).toBeUndefined();
    const renamed = s.renameElement('shop.web', ' store ');
    expect(renamed.id).toBe('shop.store');
    expect(renamed.update?.minimal).toBe(true);
    expect(renamed.update?.source).toContain('    store = container\n');
    expect(renamed.update?.source).toContain('    store -> db\n');
    expect(renamed.update?.source).toContain('  me -> shop.store // buys\n');
    expect(renamed.update?.source).toContain('route "shop.store->shop.db" via 1, 2');
    // Without a label, the name follows the id.
    expect(s.state().model.elements['shop.store']?.name).toBe('store');
    expect(s.renameElement('shop.store', 'store')).toEqual({ id: 'shop.store', update: undefined });
    expect(() => s.renameElement('shop.store', 'db')).toThrow(DiaDrawError);
    expect(() => s.renameElement('shop.store', '9x')).toThrow(DiaDrawError);
    s.dispose();
  });

  it('says which inspector fields an edit would refuse, changing nothing', () => {
    const text =
      'model {\n  shop = system {\n    web = container\n    db = container\n  }\n  me = person\n  buys = me -> shop.web\n  me -> shop.db\n}\n';
    const s = dd.createSession(text);
    const { elements, relationships } = s.state().model;
    const web = elements['shop.web']!;
    expect(s.elementProblems(web, 'web')).toEqual({});
    expect(s.elementProblems(web, ' store ')).toEqual({});
    expect(Object.keys(s.elementProblems(web, '9x'))).toEqual(['id']);
    expect(s.elementProblems(web, 'db').id).toMatch(/already/);
    expect(
      Object.keys(s.elementProblems({ ...web, kind: 'web app', shape: 'aws.lambda' }, 'web')),
    ).toEqual(['kind']);
    expect(Object.keys(s.elementProblems({ ...web, shape: 'round box' }, 'web'))).toEqual([
      'shape',
    ]);
    const derived = relationships['me->shop.db']!;
    expect(s.relationshipProblems(derived)).toEqual({});
    expect(s.relationshipProblems(derived, 'pays')).toEqual({});
    expect(s.relationshipProblems(derived, 'buys').name).toMatch(/already named/);
    expect(Object.keys(s.relationshipProblems(derived, '2nd'))).toEqual(['name']);
    expect(s.relationshipProblems(relationships.buys!, 'buys')).toEqual({});
    expect(s.state().model.elements['shop.web']).toEqual(web);
    s.dispose();
  });

  it('adds, renames and removes views through the text', () => {
    const s = dd.createSession('model {\n  a = process\n}\n');
    expect(s.freshViewId()).toBe('view1');
    // The implied default view is written out before the new one.
    const added = s.addView(' second ');
    expect(added.source).toContain(
      'view default {\n  include *\n}\n\nview second {\n  include *\n}\n',
    );
    expect(s.state().views.map((v) => v.id)).toEqual(['default', 'second']);
    expect(() => s.addView('second')).toThrow(DiaDrawError);
    expect(() => s.addView('2nd')).toThrow(DiaDrawError);
    expect(s.renameView('default', 'default')).toBeUndefined();
    expect(s.renameView('default', 'main')?.source).toContain('view main {');
    expect(s.state().views.map((v) => v.id)).toEqual(['main', 'second']);
    expect(s.removeView('second').source).not.toContain('view second');
    expect(() => s.removeView('main')).toThrow(DiaDrawError);
    s.dispose();
  });

  it('reports view edits against the text before them', () => {
    const before =
      'model {\n  a = process "Café ☕"\n}\n\nview one {\n  include *\n}\n\nview two {\n  include *\n}\n';
    const s = dd.createSession(before);
    let text = before;
    const check = (update: SourceUpdate | undefined) => {
      const edits = [...(update?.edits ?? [])].sort((x, y) => y.from - x.from);
      for (const e of edits) text = text.slice(0, e.from) + e.insert + text.slice(e.to);
      expect(text).toBe(s.source);
    };
    check(s.renameView('one', 'first'));
    check(s.addView('three'));
    check(s.removeView('three'));
    check(s.removeView('two'));
    s.dispose();
  });

  it('reconnects a line and reports its new id', () => {
    const s = dd.createSession(
      'model {\n  a = process\n  b = process\n  c = process\n  a -> b "go"\n}\n\nview v {\n  include *\n  route "a->b" via 10, 10\n}\n',
    );
    expect(s.render('v').ends['a->b']).toEqual({ source: 'a', target: 'b' });
    expect(s.reconnect('a->b', 'target', 'b')).toBeUndefined();
    const moved = s.reconnect('a->b', 'target', 'c');
    expect(moved?.id).toBe('a->c');
    expect(moved?.update.minimal).toBe(true);
    expect(s.source).toContain('  a -> c "go"\n');
    expect(s.source).toContain('route "a->c" via 10, 10');
    expect(() => s.reconnect('nope', 'target', 'c')).toThrow(DiaDrawError);
    s.dispose();
  });
});

describe('import, export and libraries', () => {
  it('imports every format and reports what it dropped', () => {
    const mermaid = dd.importDiagram(
      'flowchart LR\n  a[Start] --> b{Ok?}\n  click a callback\n',
      'x.mmd',
    );
    expect(Object.keys(mermaid.model.elements)).toEqual(['a', 'b']);
    expect(mermaid.report.dropped.length).toBeGreaterThan(0);
    const puml = dd.importDiagram(
      '@startuml\n!include parts.iuml\nA -> B : hi\n@enduml\n',
      'x.puml',
      {
        resolve: (path) =>
          path === 'parts.iuml' ? [{ path, text: 'participant C\n' }] : undefined,
      },
    );
    expect(Object.keys(puml.model.elements)).toEqual(['C', 'A', 'B']);
    expect(() => dd.importDiagram('hello', 'x.txt')).toThrow(DiaDrawError);
  });

  it('exports draw.io and BPMN, and round-trips BPMN geometry', () => {
    const bpmn = dd.importDiagram(
      corpus('bpmn/bpmn-miwg_bpmn-miwg-test-suite/Reference/A.1.0.bpmn'),
      'a.bpmn',
    );
    const pages = dd.bpmnFixedPages(bpmn.model);
    expect(pages.length).toBeGreaterThan(0);
    const xml = dd.exportBpmn(bpmn.model, pages);
    expect(xml).toContain('<bpmndi:BPMNDiagram');
    expect(dd.exportDrawio(bpmn.model, pages)).toContain('<mxfile');
  });

  it('offers the palette of the libraries a model uses and draws their shapes', () => {
    const { model } = dd.parse(
      'use "bpmn@^1" as bpmn\nmodel {\n  t = task shape bpmn.user_task\n}\n',
    );
    const palette = dd.palette(model);
    expect(palette.unresolved).toEqual([]);
    const task = palette.items.find((i) => i.ref === 'bpmn.user_task');
    expect(task?.art?.body).toBeTruthy();
    expect(dd.renderSvg(model, 'default')).toBeTruthy();
    const missing = dd.palette(dd.parse('use "nope@^1" as n\nmodel {\n}\n').model);
    expect(missing.unresolved[0]?.message).toMatch(/not installed/);
  });

  it('builds libraries and makes added ones available', () => {
    const built = dd.buildLibrary(
      {
        name: 'demo-shapes',
        version: '1.0.0',
        license: 'MIT',
        shapes: { box: { svg: 'box.svg' } },
      },
      (path) =>
        path === 'box.svg'
          ? '<svg viewBox="0 0 10 10"><rect width="10" height="10"/></svg>'
          : undefined,
    );
    expect(built.issues).toEqual([]);
    dd.addLibrary(built.text!);
    const { model } = dd.parse('use "demo-shapes@^1" as demo\nmodel {\n}\n');
    expect(dd.palette(model).items.map((i) => i.ref)).toEqual(['demo.box']);
  });
});

describe('events', () => {
  it('tells session listeners about every change, from the text or from gestures', () => {
    const s = dd.createSession('model {\n  a = process\n  b = process\n}\n');
    const heard: SessionChange[] = [];
    const stop = s.on('change', (e) => heard.push(e));
    let refreshed = 0;
    s.on('libraries', () => refreshed++);
    s.setSource('model {\n  a = process\n  b = process\n  c = process\n}\n');
    const update = s.connect('a', 'c');
    s.refreshLibraries();
    stop();
    s.setSource('model {\n}\n');
    expect(heard).toHaveLength(2);
    expect(heard[0]!.update).toBeUndefined();
    expect(Object.keys(heard[0]!.state.model.elements)).toEqual(['a', 'b', 'c']);
    expect(heard[1]!.update).toEqual(update);
    expect(heard[1]!.state.model.relationships['a->c']).toBeDefined();
    expect(refreshed).toBe(1);
    s.dispose();
  });

  it('runs the language server: messages in, message events out', () => {
    const server = dd.createLanguageServer();
    const out: JsonRpcMessage[] = [];
    server.on('message', (m) => out.push(m));
    server.send({ jsonrpc: '2.0', id: 1, method: 'initialize', params: {} });
    expect(out[0]).toMatchObject({ id: 1, result: { capabilities: { hoverProvider: true } } });
    server.send({
      jsonrpc: '2.0',
      method: 'textDocument/didOpen',
      params: {
        textDocument: {
          uri: 'untitled:x',
          version: 1,
          text: 'model {\n  a = process "😀"\n  a -> b\n}\n',
        },
      },
    });
    expect(out[1]).toMatchObject({ method: 'textDocument/publishDiagnostics' });
    server.send({
      jsonrpc: '2.0',
      id: 2,
      method: 'textDocument/hover',
      params: { textDocument: { uri: 'untitled:x' }, position: { line: 2, character: 3 } },
    });
    expect(JSON.stringify(out[2])).toContain('😀');
    expect(() => server.send('{nope')).toThrow(DiaDrawError);
    server.send({ jsonrpc: '2.0', id: 3, method: 'shutdown' });
    server.send({ jsonrpc: '2.0', method: 'exit' });
    expect(server.exitCode).toBe(0);
    server.dispose();
  });

  it('reads libraries on disk through the host file system', () => {
    const dir = mkdtempSync(join(tmpdir(), 'diadraw-wasm-'));
    try {
      const bundle = (version: string) =>
        JSON.stringify({
          format: 'diadraw-library',
          formatVersion: 1,
          name: 'acme',
          version,
          license: 'MIT',
          shapes: { box: { svg: '<rect width="10" height="10"/>', viewBox: '0 0 10 10' } },
        });
      mkdirSync(join(dir, 'cache/acme'), { recursive: true });
      writeFileSync(join(dir, 'cache/acme/1.0.0.json'), bundle('1.0.0'));
      writeFileSync(join(dir, 'cache/acme/1.1.0.json'), bundle('1.1.0'));
      mkdirSync(join(dir, 'project'));
      writeFileSync(
        join(dir, 'project/diadraw-lock.json'),
        JSON.stringify({
          lockfileVersion: 1,
          libraries: { 'acme@^1': { version: '1.0.0', resolved: 'x' } },
        }),
      );
      dd.setFileSystem(nodeFileSystem({ DIADRAW_CACHE: join(dir, 'cache') }));
      const text = 'use "acme@^1" as acme\nmodel {\n  a = component shape acme.box\n}\n';
      const session = dd.createSession(text, { path: join(dir, 'project/a.draw') });
      expect(session.render('default').content).toContain('<rect width="10" height="10"/>');
      session.dispose();

      const server = dd.createLanguageServer();
      const out: JsonRpcMessage[] = [];
      server.on('message', (m) => out.push(m));
      const uri = pathToFileURL(join(dir, 'project/a.draw')).href;
      server.send({
        jsonrpc: '2.0',
        method: 'textDocument/didOpen',
        params: { textDocument: { uri, version: 1, text: text.replace('acme.box', 'acme.nope') } },
      });
      expect(JSON.stringify(out[0])).toContain('No shape \\"acme.nope\\"');
      server.dispose();
    } finally {
      dd.setFileSystem(undefined);
      rmSync(dir, { recursive: true, force: true });
    }
  });
});
