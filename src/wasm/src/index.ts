/**
 * The DiaDraw C++ core (src/) as WebAssembly.
 *
 * `loadDiaDraw()` instantiates the module once; everything after is synchronous. Models,
 * operations and geometry have the shapes in `types.ts`, and text offsets are UTF-16, like
 * JavaScript strings and editors.
 */
import type {
  Box,
  EdgeRoute,
  Element,
  LayoutHint,
  Model,
  Operation,
  Point,
  Relationship,
} from './types.js';
import type { ModuleOptions, RawModule, RawSession } from './core/diadraw.mjs';
import { Emitter } from './events.js';

export type { ModuleOptions } from './core/diadraw.mjs';
export type * from './types.js';

export interface Span {
  readonly from: number;
  readonly to: number;
}

export interface Diagnostic extends Span {
  readonly severity: 'error' | 'warning';
  readonly message: string;
  /** 1-based line and column of `from`; the column counts UTF-16 units. */
  readonly line: number;
  readonly column: number;
}

export interface SourceMap {
  readonly model?: Span;
  readonly elements: Readonly<
    Record<string, { decl: Span; name: Span; kind: Span; label?: Span; body?: Span }>
  >;
  readonly relationships: Readonly<Record<string, { decl: Span; target: Span; label?: Span }>>;
  readonly groups: Readonly<Record<string, { decl: Span; body: Span }>>;
  readonly views: Readonly<
    Record<
      string,
      {
        decl: Span;
        body: Span;
        layout?: Span;
        hints: readonly Span[];
        /** One per decoration, in view order; omitted when there are none. */
        decorations?: readonly Span[];
        selectors: readonly {
          list: 'include' | 'exclude';
          selector: string;
          span: Span;
          statement: Span;
        }[];
      }
    >
  >;
  readonly libraries?: Readonly<Record<string, Span>>;
}

export interface ParseResult {
  readonly model: Model;
  readonly diagnostics: readonly Diagnostic[];
  readonly sourceMap: SourceMap;
  readonly operations: readonly Operation[];
}

export type HighlightToken = readonly [from: number, to: number, tag: string];

export interface TextEdit {
  readonly from: number;
  readonly to: number;
  readonly insert: string;
}

/** A change to the text: apply `edits` to the old text, or take `source` whole. */
export interface SourceUpdate {
  readonly source: string;
  readonly edits: readonly TextEdit[];
  readonly model: Model;
  /** False when the change could not be made in place and the file was reprinted. */
  readonly minimal: boolean;
  readonly diagnostics: readonly Diagnostic[];
}

/** Inspector fields an edit would refuse, each with why: `{ id: '"2x" is not a name: …' }`. */
export type FieldProblems = Readonly<Record<string, string>>;

export interface SessionState {
  /** The model, with an implicit view of everything when the text declares none. */
  readonly model: Model;
  readonly diagnostics: readonly Diagnostic[];
  readonly views: readonly { readonly id: string; readonly algorithm: string }[];
}

/** A laid-out, rendered view: SVG markup to place in an `<svg>`, and the geometry behind it. */
export interface RenderedView {
  readonly content: string;
  readonly viewBox: string;
  readonly width: number;
  readonly height: number;
  /** The view's layout algorithm: `layered`, `fixed` or `sequence`. */
  readonly algorithm: string;
  readonly nodes: Readonly<Record<string, Box>>;
  readonly edges: Readonly<Record<string, EdgeRoute>>;
  /** Turned nodes: degrees clockwise about the centre of their (unturned) box. */
  readonly rotations: Readonly<Record<string, number>>;
  readonly ignoredHints: readonly LayoutHint[];
  /** Ids of the elements the view shows, of those drawn as containers, and of its relationships. */
  readonly elements: readonly string[];
  readonly containers: readonly string[];
  readonly relationships: readonly string[];
  /**
   * The elements at each end of the relationships written in the text, the ones a canvas can
   * reconnect (relationships a view lifts from hidden elements are left out).
   */
  readonly ends: Readonly<Record<string, { readonly source: string; readonly target: string }>>;
}

/** What an importer could not carry over. */
export interface ImportReport {
  readonly dropped: readonly {
    readonly line?: number;
    readonly message: string;
    readonly construct?: string;
  }[];
  readonly notes: readonly string[];
  readonly unknownShapes?: Readonly<Record<string, number>>;
  readonly unknownStyles?: Readonly<Record<string, number>>;
  readonly stats?: Readonly<Record<string, number>>;
}

export interface ImportResult {
  readonly model: Model;
  readonly report: ImportReport;
}

/** Reads local includes (PlantUML, Structurizr): a file, or every file under a directory. */
export type IncludeResolver = (
  path: string,
  from: string | undefined,
) => readonly { readonly path: string; readonly text: string }[] | undefined;

export interface ImportOptions {
  /** Draw draw.io stencils with the artwork of the drawio-* libraries (needs `loadDrawioIndex`). */
  readonly stencils?: boolean;
  readonly resolve?: IncludeResolver;
}

/** draw.io stencil sets drawn as SVG (`readStencilSets`). */
export interface StencilSets {
  readonly sets: readonly {
    readonly name: string;
    readonly file: string;
    readonly shapes: readonly {
      readonly style: string;
      readonly id: string;
      readonly label: string;
      readonly width: number;
      readonly height: number;
      readonly body: string;
    }[];
    readonly skipped: readonly { readonly name: string; readonly reason: string }[];
  }[];
  readonly colors: { readonly fill: string; readonly stroke: string; readonly glyph: string };
}

/** One page of a draw.io or BPMN export: a view and its laid-out geometry. */
export interface ExportPage {
  readonly view: string;
  readonly nodes: Readonly<Record<string, Box>>;
  readonly edges: Readonly<
    Record<string, { readonly points: readonly { x: number; y: number }[] }>
  >;
}

/** A library shape as the renderer and the palette show it. */
export interface ShapeArt {
  readonly body?: string;
  readonly viewBox?: string;
  readonly base?: string;
  readonly width?: number;
  readonly height?: number;
  readonly kind?: string;
  readonly labelPosition?: 'below' | 'inside';
  readonly colors?: { readonly fill?: readonly string[]; readonly stroke?: readonly string[] };
  readonly label: string;
}

/** One entry of the palette: a shape, a composite, an element kind or a fragment of a library a model uses. */
export interface PaletteItem {
  readonly type: 'shape' | 'composite' | 'kind' | 'fragment';
  readonly alias: string;
  readonly library: string;
  readonly id: string;
  /** `alias.id`, what `shape` refers to (shapes and composites) or what elements are declared as (kinds). */
  readonly ref: string;
  readonly label: string;
  readonly category: string;
  /** The kind of the element the item adds: built-in (`component`) or a library's (`alias.kind`). */
  readonly kind?: string;
  readonly keywords: readonly string[];
  readonly art?: ShapeArt;
  readonly description?: string;
}

export interface Palette {
  readonly items: readonly PaletteItem[];
  /** `use` lines whose library is not available, with why. */
  readonly unresolved: readonly { readonly spec: string; readonly message: string }[];
}

export interface LibraryInfo {
  readonly name: string;
  readonly version: string;
  readonly description?: string;
  readonly builtin: boolean;
}

export interface LibraryIssue {
  readonly severity: 'error' | 'warning';
  readonly message: string;
  readonly path?: string;
}

export interface Selection {
  readonly elements: readonly string[];
  readonly relationships: readonly string[];
}

/** A change to a session's text: from the host (`setSource`), or an operation or gesture. */
export interface SessionChange {
  /** The text update an operation or gesture made; absent when the host set the text. */
  readonly update?: SourceUpdate;
  readonly state: SessionState;
}

/** What a session tells its listeners, by event type. */
export interface SessionEvents {
  /** The text changed; the session is already in its new state. */
  readonly change: SessionChange;
  /** Library shapes were resolved again (`refreshLibraries`): render the views again. */
  readonly libraries: undefined;
}

/** Stops a listener. */
export type Unsubscribe = () => void;

/**
 * An editing session: the text, its parse and the last layout of each view. Gestures turn into
 * minimal text updates; the session moves to the new text and tells its listeners, so panes can
 * follow changes whoever made them. Call `dispose()` when done.
 */
export interface Session {
  on<K extends keyof SessionEvents>(
    type: K,
    listener: (event: SessionEvents[K]) => void,
  ): Unsubscribe;
  readonly source: string;
  setSource(source: string): void;
  state(): SessionState;
  render(viewId: string): RenderedView;
  apply(operation: Operation): SourceUpdate;
  /** Turns a dropped node into a hint (alignment, a pin, or `pin … at` when `freeform`). */
  drop(viewId: string, id: string, box: Box, freeform?: boolean): SourceUpdate | undefined;
  /** The hint a drop would write, without writing it: for a preview while dragging. */
  dropHint(viewId: string, id: string, box: Box, freeform?: boolean): LayoutHint | undefined;
  /**
   * A node resized on the canvas. In a `layout fixed` view: `pin id at X, Y size W, H`, rewritten
   * where the pin stands; in other views `size id W, H`, which the layout keeps. Undefined when
   * nothing changes.
   */
  resize(viewId: string, id: string, box: Box): SourceUpdate | undefined;
  /**
   * Sizes elements to fit their content: removes their `size` hints and the sizes of their pins
   * (in a fixed view a container's pin goes, so it wraps its children). Undefined when nothing
   * changes.
   */
  fit(viewId: string, ids: string[]): SourceUpdate | undefined;
  /** A node turned `degrees` clockwise on the canvas: `rotate id D` (0 removes it). */
  rotate(viewId: string, id: string, degrees: number): SourceUpdate | undefined;
  /**
   * Several elements dragged together by `offset`, as one change: boxes get `pin … at` where they
   * land, whatever the layout (in a fixed view an unpinned container whose drawn children all move
   * stays unpinned, and lines whose ends both move keep their bend points relative to them); ids
   * not drawn in the view are ignored. Undefined when nothing changes; throws on an offset that is
   * not finite.
   */
  move(viewId: string, ids: readonly string[], offset: Point): SourceUpdate | undefined;
  /**
   * A relationship's bend points dragged in a `layout fixed` view: `route "id" via …`, keeping its
   * `from` and `to`. No points and no `from`/`to` remove the route.
   */
  reroute(viewId: string, id: string, points: readonly Point[]): SourceUpdate | undefined;
  /** A standalone SVG document of a view. */
  exportSvg(viewId: string): string;
  connect(source: string, target: string, label?: string): SourceUpdate;
  /**
   * Moves one end of a relationship onto another element. `id` is the relationship's id
   * afterwards: a named one keeps its id, one derived from its endpoints (`a->b`) follows them.
   * Undefined when that end is already on `element`.
   */
  reconnect(
    id: string,
    end: 'source' | 'target',
    element: string,
  ): { id: string; update: SourceUpdate } | undefined;
  create(
    kind: string,
    options?: { parent?: string; name?: string; shape?: string },
  ): { id: string; update: SourceUpdate };
  remove(selection: Selection): SourceUpdate | undefined;
  /**
   * An inspector's edit of a relationship: `relationship`'s label, technology, arrow, tags, style
   * and properties (its id says which one), with `name` as its explicit id (`name = a -> b`), or
   * none for an id derived from its ends. A new name can renumber the derived ids of later lines
   * between the same elements; their routes follow. `id` is the relationship's id afterwards;
   * `update` is undefined when nothing changed.
   */
  updateRelationship(
    relationship: Relationship,
    name?: string,
  ): { id: string; update: SourceUpdate | undefined };
  /**
   * Renames an element: `to` replaces the last part of its id (`shop.web` with `store` becomes
   * `shop.store`), and its descendants, every reference to them and the ids derived from them
   * (unnamed relationships and their routes, groups inside it) follow; the text changes only where
   * the names are written. An element without a label shows its id, so its name follows too. `id`
   * is the element's id afterwards; `update` is undefined when the name stays. Throws when `to`
   * is not a name, is taken, or would change what another reference in the text names.
   */
  renameElement(id: string, to: string): { id: string; update: SourceUpdate | undefined };
  /**
   * What an inspector's edit of an element would refuse, field by field, without changing
   * anything, so the inspector can mark those fields while they are typed in: `element` with the
   * typed fields applied (its id still the one it has) and `localId` as typed in the Id field. A
   * field (`id`, `kind`, `shape` or `style`) maps to why; `{}` when the edit applies.
   */
  elementProblems(element: Element, localId: string): FieldProblems;
  /** The same for a relationship (`name` and `style`), with `name` as `updateRelationship` takes it. */
  relationshipProblems(relationship: Relationship, name?: string): FieldProblems;
  /**
   * The element whose name is written at `offset` (UTF-16, as the text editor counts), in its
   * declaration or in a reference to it (`shop` or `web` in `shop.web`); undefined elsewhere.
   */
  elementAt(offset: number): string | undefined;
  /** A free id for a new view: `view1`, `view2`, … */
  freshViewId(): string;
  /**
   * Adds `view id { include * }` (`of scope` when given) after the other views. A text without
   * views has its implied `view default` written out first, so it stays. Throws when `id` is not
   * a name or is taken.
   */
  addView(id: string, scope?: string): SourceUpdate;
  /** Removes a view; throws for the document's only view. */
  removeView(id: string): SourceUpdate;
  /** Gives a view a new id where it stands; undefined when the id stays. */
  renameView(id: string, to: string): SourceUpdate | undefined;
  /** Draws library shapes again, after `addLibrary` made more libraries available. */
  refreshLibraries(): void;
  dispose(): void;
}

/** A JSON-RPC message of the Language Server Protocol. */
export interface JsonRpcMessage {
  readonly jsonrpc: '2.0';
  readonly id?: number | string | null;
  readonly method?: string;
  readonly params?: unknown;
  readonly result?: unknown;
  readonly error?: { readonly code: number; readonly message: string };
}

/** What a language server tells its listeners. */
export interface LanguageServerEvents {
  /** A reply or a notification (diagnostics) for the client. */
  readonly message: JsonRpcMessage;
}

/**
 * The DiaDraw language server, without a transport: hand it client messages with `send`, and
 * deliver its `message` events to the client. Call `dispose()` when done.
 */
export interface LanguageServer {
  send(message: JsonRpcMessage | string): void;
  on<K extends keyof LanguageServerEvents>(
    type: K,
    listener: (event: LanguageServerEvents[K]) => void,
  ): Unsubscribe;
  /** Set once the client sent `exit`: 0 after `shutdown`, 1 otherwise. */
  readonly exitCode: number | undefined;
  dispose(): void;
}

/**
 * The host's file system, for libraries on disk: the `diadraw-lock.json` next to a document,
 * the library cache and draw.io's stencil sets. Paths are the host's own.
 */
export interface HostFileSystem {
  /** A file's bytes, or undefined when it cannot be read. */
  read(path: string): Uint8Array | undefined;
  /** Names in a directory; empty or undefined when it is not one. */
  list(dir: string): readonly string[] | undefined;
  join(dir: string, name: string): string;
  /** The directory holding `path`; `path` itself at the root. */
  parent(path: string): string;
  env(name: string): string | undefined;
}

export interface DiaDraw {
  readonly version: string;
  parse(source: string): ParseResult;
  print(model: Model): string;
  /**
   * Syntax colouring: `[from, to, tag]` runs in UTF-16 offsets, with CodeMirror's tag names
   * (`keyword`, `typeName`, `string`, …); text between runs is plain.
   */
  highlight(text: string): readonly HighlightToken[];
  /** Applies an operation to source text as a minimal patch. */
  updateSource(source: string, operation: Operation): SourceUpdate;
  /** A standalone SVG document for a view, laid out and rendered by the core. */
  renderSvg(model: Model, viewId: string): string;
  /**
   * An editing session. With `path` (where the file is saved) and a file system from
   * `setFileSystem`, library shapes resolve like the CLI resolves them for that file.
   */
  createSession(source?: string, options?: { readonly path?: string }): Session;
  /** A language server; unsaved documents resolve libraries as if they were in `cwd`. */
  createLanguageServer(options?: { readonly cwd?: string }): LanguageServer;
  /**
   * Gives the core the host's file system, so sessions and language servers read libraries on
   * disk (`drawio`: where draw.io's converted stencil sets are). `undefined` takes it back.
   */
  setFileSystem(fs: HostFileSystem | undefined, options?: { readonly drawio?: string }): void;

  /**
   * Imports Mermaid, PlantUML, Structurizr, draw.io (text or the bytes of a .drawio.png/.svg) or
   * BPMN; the format comes from the file name and the content.
   */
  importDiagram(data: string | Uint8Array, fileName: string, options?: ImportOptions): ImportResult;
  /** The index of draw.io's stencil sets (`index.json`, gunzipped), for importing with artwork. */
  loadDrawioIndex(index: unknown): void;
  /**
   * Draws draw.io's stencil sources (a set's `.xml`, or `stencils.min.js`) as SVG, for converting
   * them into `drawio-*` libraries. `colors` are the placeholders draw.io fills from the cell style.
   */
  readStencilSets(text: string, fileName: string): StencilSets;
  /** Every page as a draw.io file. */
  exportDrawio(model: Model, pages: readonly ExportPage[]): string;
  /** BPMN 2.0 XML with diagram interchange, one diagram per page. */
  exportBpmn(model: Model, pages: readonly ExportPage[]): string;
  /** Pages from views with fixed geometry, without a layout (what imported BPMN views hold). */
  bpmnFixedPages(model: Model): ExportPage[];

  /** Makes a built library (a draw.io stencil set, a user's library) available to documents. */
  addLibrary(bundle: object | string): void;
  /** The built-in libraries and the ones added with `addLibrary`. */
  libraries(): readonly LibraryInfo[];
  /** The shapes, composites and fragments of the libraries a model uses. */
  palette(model: Model): Palette;
  /** A fragment's text with its placeholders filled. */
  expandFragment(
    model: Model,
    alias: string,
    id: string,
    values?: Readonly<Record<string, string>>,
  ): string;
  /**
   * Builds a library from its manifest, reading its files with `read`. `license` says whether
   * the library could ship with DiaDraw.
   */
  buildLibrary(
    manifest: object | string,
    read: (path: string) => string | undefined,
  ): {
    readonly text?: string;
    readonly issues: readonly LibraryIssue[];
    readonly license?: {
      readonly status: 'allowed' | 'restricted' | 'disallowed' | 'unknown';
      readonly reason: string;
    };
  };
}

/** A refusal from the core: an invalid operation, malformed input, an unknown view. */
export class DiaDrawError extends Error {
  override readonly name = 'DiaDrawError';
}

function decode<T>(json: string): T {
  const value = JSON.parse(json) as T | { error: string } | null;
  if (
    value !== null &&
    typeof value === 'object' &&
    'error' in value &&
    typeof value.error === 'string'
  ) {
    throw new DiaDrawError(value.error);
  }
  return value as T;
}

function wrapSession(
  create: (listener: (type: string, json: string) => void) => RawSession,
): Session {
  const events = new Emitter<SessionEvents>();
  const raw = create((type, json) => {
    if (type === 'change') {
      const { update, state } = JSON.parse(json) as {
        update: SourceUpdate | null;
        state: SessionState;
      };
      events.emit('change', update ? { update, state } : { state });
    } else if (type === 'libraries') {
      events.emit('libraries', undefined);
    }
  });
  let disposed = false;
  const live = () => {
    if (disposed) throw new DiaDrawError('The session was disposed');
    return raw;
  };
  return {
    on: (type, listener) => events.on(type, listener),
    get source() {
      return live().source();
    },
    setSource: (source) => live().setSource(source),
    state: () => decode<SessionState>(live().state()),
    render: (viewId) => decode<RenderedView>(live().render(viewId)),
    apply: (operation) => decode<SourceUpdate>(live().apply(JSON.stringify(operation))),
    drop: (viewId, id, box, freeform = false) =>
      decode<SourceUpdate | null>(
        live().drop(viewId, id, box.x, box.y, box.width, box.height, freeform),
      ) ?? undefined,
    dropHint: (viewId, id, box, freeform = false) =>
      decode<LayoutHint | null>(
        live().dropHint(viewId, id, box.x, box.y, box.width, box.height, freeform),
      ) ?? undefined,
    resize: (viewId, id, box) =>
      decode<SourceUpdate | null>(live().resize(viewId, id, box.x, box.y, box.width, box.height)) ??
      undefined,
    fit: (viewId, ids) =>
      decode<SourceUpdate | null>(live().fit(viewId, JSON.stringify(ids))) ?? undefined,
    rotate: (viewId, id, degrees) =>
      decode<SourceUpdate | null>(live().rotate(viewId, id, degrees)) ?? undefined,
    move: (viewId, ids, offset) =>
      decode<SourceUpdate | null>(live().move(viewId, JSON.stringify(ids), offset.x, offset.y)) ??
      undefined,
    reroute: (viewId, id, points) =>
      decode<SourceUpdate | null>(
        live().reroute(viewId, id, JSON.stringify(points.map(({ x, y }) => ({ x, y })))),
      ) ?? undefined,
    exportSvg: (viewId) => decode<{ svg: string }>(live().exportSvg(viewId)).svg,
    connect: (source, target, label) =>
      decode<SourceUpdate>(live().connect(source, target, label ?? '')),
    reconnect: (id, end, element) =>
      decode<{ id: string; update: SourceUpdate } | null>(live().reconnect(id, end, element)) ??
      undefined,
    create: (kind, options = {}) =>
      decode<{ id: string; update: SourceUpdate }>(
        live().create(kind, options.parent ?? '', options.name ?? '', options.shape ?? ''),
      ),
    remove: (selection) =>
      decode<SourceUpdate | null>(live().remove(JSON.stringify(selection))) ?? undefined,
    updateRelationship: (relationship, name) => {
      const r = decode<{ id: string; update: SourceUpdate | null }>(
        live().updateRelationship(JSON.stringify(relationship), name ?? ''),
      );
      return { id: r.id, update: r.update ?? undefined };
    },
    renameElement: (id, to) => {
      const r = decode<{ id: string; update: SourceUpdate | null }>(live().renameElement(id, to));
      return { id: r.id, update: r.update ?? undefined };
    },
    elementProblems: (element, localId) =>
      decode<FieldProblems>(live().elementProblems(JSON.stringify(element), localId)),
    relationshipProblems: (relationship, name) =>
      decode<FieldProblems>(live().relationshipProblems(JSON.stringify(relationship), name ?? '')),
    elementAt: (offset) => live().elementAt(offset) || undefined,
    freshViewId: () => live().freshViewId(),
    addView: (id, scope) => decode<SourceUpdate>(live().addView(id, scope ?? '')),
    removeView: (id) => decode<SourceUpdate>(live().removeView(id)),
    renameView: (id, to) => decode<SourceUpdate | null>(live().renameView(id, to)) ?? undefined,
    refreshLibraries: () => live().refreshLibraries(),
    dispose: () => {
      if (!disposed) raw.delete();
      disposed = true;
      events.clear();
    },
  };
}

function wrapLanguageServer(raw: RawModule, cwd: string): LanguageServer {
  const events = new Emitter<LanguageServerEvents>();
  const server = new raw.LanguageServer(
    (json) => events.emit('message', JSON.parse(json) as JsonRpcMessage),
    cwd,
  );
  let disposed = false;
  return {
    send: (message) => {
      if (disposed) throw new DiaDrawError('The language server was disposed');
      decode(server.send(typeof message === 'string' ? message : JSON.stringify(message)));
    },
    on: (type, listener) => events.on(type, listener),
    get exitCode() {
      const code = disposed ? -1 : server.exitCode();
      return code < 0 ? undefined : code;
    },
    dispose: () => {
      if (!disposed) server.delete();
      disposed = true;
      events.clear();
    },
  };
}

function wrap(raw: RawModule): DiaDraw {
  return {
    version: raw.version(),
    parse: (source) => decode<ParseResult>(raw.parse(source)),
    print: (model) => {
      const text = raw.print(JSON.stringify(model));
      // print returns plain text, or `{"error": …}` JSON for a malformed model.
      if (text.startsWith('{"error":')) decode(text);
      return text;
    },
    highlight: (text) => decode<HighlightToken[]>(raw.highlight(text)),
    updateSource: (source, operation) =>
      decode<SourceUpdate>(raw.updateSource(source, JSON.stringify(operation))),
    renderSvg: (model, viewId) =>
      decode<{ svg: string }>(raw.renderView(JSON.stringify(model), viewId)).svg,
    createSession: (source = '', options = {}) =>
      wrapSession((listener) => new raw.Session(source, options.path ?? '', listener)),
    createLanguageServer: (options = {}) => wrapLanguageServer(raw, options.cwd ?? ''),
    setFileSystem: (fs, options = {}) => raw.setFileSystem(fs, options.drawio ?? ''),

    importDiagram: (data, fileName, options = {}) =>
      decode<ImportResult>(
        raw.importDiagram(
          data,
          fileName,
          options.stencils ?? false,
          options.resolve ? (path, from) => options.resolve!(path, from)?.slice() : undefined,
        ),
      ),
    loadDrawioIndex: (index) => {
      decode(raw.loadDrawioIndex(typeof index === 'string' ? index : JSON.stringify(index)));
    },
    readStencilSets: (text, fileName) => decode<StencilSets>(raw.readStencilSets(text, fileName)),
    exportDrawio: (model, pages) =>
      decode<{ xml: string }>(raw.exportDrawio(JSON.stringify(model), JSON.stringify(pages))).xml,
    exportBpmn: (model, pages) =>
      decode<{ xml: string }>(raw.exportBpmn(JSON.stringify(model), JSON.stringify(pages))).xml,
    bpmnFixedPages: (model) =>
      decode<{ pages: ExportPage[] }>(raw.bpmnFixedPages(JSON.stringify(model))).pages,

    addLibrary: (bundle) => {
      decode(raw.addLibrary(typeof bundle === 'string' ? bundle : JSON.stringify(bundle)));
    },
    libraries: () => decode<LibraryInfo[]>(raw.libraryList()),
    palette: (model) => decode<Palette>(raw.libraryPalette(JSON.stringify(model))),
    expandFragment: (model, alias, id, values = {}) =>
      decode<{ text: string }>(
        raw.expandFragment(JSON.stringify(model), alias, id, JSON.stringify(values)),
      ).text,
    buildLibrary: (manifest, read) =>
      decode<ReturnType<DiaDraw['buildLibrary']>>(
        raw.buildLibrary(typeof manifest === 'string' ? manifest : JSON.stringify(manifest), read),
      ),
  };
}

let loaded: Promise<DiaDraw> | undefined;

/** Loads the WebAssembly module (once; later calls share it). */
export function loadDiaDraw(options?: ModuleOptions): Promise<DiaDraw> {
  // Imported when first needed: bundlers that output CommonJS (the VS Code extension) keep the
  // Emscripten module as a separate ES module file next to its .wasm.
  loaded ??= import('./core/diadraw.mjs').then(({ default: create }) => create(options)).then(wrap);
  return loaded;
}
