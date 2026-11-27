/**
 * The DiaDraw C++ core (src/) as a native Node.js addon.
 *
 * Everything is synchronous and runs on the calling thread. Models, operations, diagnostics and
 * import results have the same shapes as in `@diadraw/wasm` (and text offsets are UTF-16, like
 * JavaScript strings), so code can move between the two. On top of what the WebAssembly module
 * offers, the addon renders PNG and PDF, and resolves a document's libraries the way the `diadraw`
 * command line does: the lockfile next to the file, the library cache, the built-in libraries and
 * draw.io's stencil sets, all offline. Failures throw.
 */
import { createRequire } from 'node:module';
import { join } from 'node:path';
import type {
  Diagnostic,
  HighlightToken,
  ImportResult,
  Model,
  Operation,
  ParseResult,
  SourceUpdate,
} from '@diadraw/wasm';

export type {
  Diagnostic,
  HighlightToken,
  ImportReport,
  ImportResult,
  Model,
  Operation,
  ParseResult,
  SourceMap,
  SourceUpdate,
  Span,
  TextEdit,
} from '@diadraw/wasm';

interface Addon {
  version(): string;
  parse(source: string): string;
  print(modelJson: string): string;
  format(source: string): string;
  highlight(text: string): string;
  updateSource(source: string, operationJson: string): string;
  check(source: string, file: string): string;
  exportDocument(
    input: string,
    isModel: boolean,
    file: string,
    format: 'svg' | 'png' | 'pdf' | 'drawio' | 'bpmn',
    view: string,
    scale: number,
    background: string,
  ): [output: string | Buffer, warnings: string[]];
  importDiagram(
    data: string | Uint8Array,
    fileName: string,
    format: string,
    keepGeometry: boolean,
  ): string;
}

const addon = createRequire(import.meta.url)('./diadraw.node') as Addon;

/** A document: DSL source text, or a model (as `parse` returns it). */
export type DocumentInput = string | Model;

export interface DocumentOptions {
  /**
   * Where the document is (or would be) saved. Its folder is where the `diadraw-lock.json` that
   * pins library versions is looked for; the file need not exist. Defaults to a file in the
   * current directory.
   */
  readonly file?: string;
}

export interface RenderOptions extends DocumentOptions {
  /** The view to draw; the first one by default (a document without views has `default`). */
  readonly view?: string;
}

export interface PngOptions extends RenderOptions {
  /** Pixels per diagram unit, above 0 and at most 8; 2 by default, sharp on high-DPI screens. */
  readonly scale?: number;
  /** A colour behind the drawing, `#rgb` or `#rrggbb`; transparent by default. */
  readonly background?: string;
}

export interface ExportOptions extends DocumentOptions {
  /** The one view to export; every view, one page each, by default. */
  readonly view?: string;
}

export interface CheckResult {
  /** Parse errors and warnings, with UTF-16 offsets and 1-based lines and columns. */
  readonly diagnostics: readonly Diagnostic[];
  /** What the document's libraries could not supply: unresolved `use`s, unknown kinds, shapes and themes. */
  readonly libraryWarnings: readonly string[];
  /** True when there are no errors (warnings allowed), as `diadraw check` exits 0. */
  readonly ok: boolean;
}

export type ImportFormat = 'mermaid' | 'plantuml' | 'structurizr' | 'drawio' | 'bpmn';

export interface ImportOptions {
  /** The format; detected from the file name and the content by default. */
  readonly format?: ImportFormat;
  /** Keep the positions of formats that have them (draw.io, BPMN, Structurizr JSON); true by default. */
  readonly keepGeometry?: boolean;
}

/** The core's version, e.g. "0.0.6". */
export const version: string = addon.version();

/** Parses DSL source: the model, diagnostics, source map and the operations that build the model. */
export function parse(source: string): ParseResult {
  return JSON.parse(addon.parse(source)) as ParseResult;
}

/** A model as canonical DSL source. */
export function print(model: Model): string {
  return addon.print(JSON.stringify(model));
}

/** DSL source reprinted canonically, as `diadraw format` does; throws when it has errors. */
export function format(source: string): string {
  return addon.format(source);
}

/**
 * Syntax colouring: `[from, to, tag]` runs in UTF-16 offsets, with CodeMirror's tag names
 * (`keyword`, `typeName`, `string`, …); text between runs is plain.
 */
export function highlight(text: string): HighlightToken[] {
  return JSON.parse(addon.highlight(text)) as HighlightToken[];
}

/** Applies an operation to source text as a minimal patch. */
export function updateSource(source: string, operation: Operation): SourceUpdate {
  return JSON.parse(addon.updateSource(source, JSON.stringify(operation))) as SourceUpdate;
}

function fileOf(options: DocumentOptions | undefined): string {
  return options?.file ?? join(process.cwd(), 'untitled.draw');
}

/** Diagnostics and library warnings for DSL source, as `diadraw check` reports them. */
export function check(source: string, options?: DocumentOptions): CheckResult {
  const r = JSON.parse(addon.check(source, fileOf(options))) as Omit<CheckResult, 'ok'>;
  return { ...r, ok: !r.diagnostics.some((d) => d.severity === 'error') };
}

function exportDocument(
  input: DocumentInput,
  format: 'svg' | 'png' | 'pdf' | 'drawio' | 'bpmn',
  options: PngOptions | undefined,
): string | Buffer {
  const isModel = typeof input !== 'string';
  const [output, warnings] = addon.exportDocument(
    isModel ? JSON.stringify(input) : input,
    isModel,
    fileOf(options),
    format,
    options?.view ?? '',
    options?.scale ?? 2,
    options?.background ?? '',
  );
  // What `diadraw render` prints as warnings: unresolved libraries, kinds, shapes and themes they
  // do not have, and layout hints it could not honour.
  for (const message of warnings) process.emitWarning(message, 'DiaDrawWarning');
  return output;
}

/**
 * A standalone SVG document of a view, laid out and drawn with the document's libraries. Throws
 * when DSL source has errors or the view does not exist.
 */
export function renderSvg(input: DocumentInput, options?: RenderOptions): string {
  return exportDocument(input, 'svg', options) as string;
}

/** A view as a PNG image (the file's bytes). */
export function renderPng(input: DocumentInput, options?: PngOptions): Buffer {
  return exportDocument(input, 'png', options) as Buffer;
}

/** A view as a one-page vector PDF the size of the diagram (the file's bytes). */
export function renderPdf(input: DocumentInput, options?: RenderOptions): Buffer {
  return exportDocument(input, 'pdf', options) as Buffer;
}

/** The views laid out and written as a draw.io file, one page per view. */
export function exportDrawio(input: DocumentInput, options?: ExportOptions): string {
  return exportDocument(input, 'drawio', options) as string;
}

/** The views laid out and written as BPMN 2.0 XML with diagram interchange, one diagram per view. */
export function exportBpmn(input: DocumentInput, options?: ExportOptions): string {
  return exportDocument(input, 'bpmn', options) as string;
}

/**
 * Imports Mermaid, PlantUML, Structurizr, draw.io (text, or the bytes of a .drawio.png/.svg) or
 * BPMN into a model; `print` turns it into DSL source. Local includes (PlantUML, Structurizr)
 * are read from disk relative to `fileName`, never from the network.
 */
export function importDiagram(
  data: string | Uint8Array,
  fileName: string,
  options?: ImportOptions,
): ImportResult {
  return JSON.parse(
    addon.importDiagram(data, fileName, options?.format ?? '', options?.keepGeometry ?? true),
  ) as ImportResult;
}
