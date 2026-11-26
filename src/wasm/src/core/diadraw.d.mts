// The raw Emscripten module built from src/wasm/src/bindings.cpp. Structured values cross as
// JSON strings; src/index.ts wraps them in typed calls. Not part of the public API.

export interface RawSession {
  source(): string;
  setSource(source: string): void;
  state(): string;
  render(viewId: string): string;
  apply(operationJson: string): string;
  drop(
    viewId: string,
    id: string,
    x: number,
    y: number,
    width: number,
    height: number,
    freeform: boolean,
  ): string;
  dropHint(
    viewId: string,
    id: string,
    x: number,
    y: number,
    width: number,
    height: number,
    freeform: boolean,
  ): string;
  resize(viewId: string, id: string, x: number, y: number, width: number, height: number): string;
  /** `idsJson`: `["id", …]`. */
  fit(viewId: string, idsJson: string): string;
  rotate(viewId: string, id: string, degrees: number): string;
  /** `idsJson`: `["id", …]`. */
  move(viewId: string, idsJson: string, dx: number, dy: number): string;
  /** `pointsJson`: `[{ "x": …, "y": … }, …]`. */
  reroute(viewId: string, id: string, pointsJson: string): string;
  exportSvg(viewId: string): string;
  connect(source: string, target: string, label: string): string;
  reconnect(id: string, end: string, element: string): string;
  create(kind: string, parent: string, name: string, shape: string): string;
  remove(selectionJson: string): string;
  updateRelationship(relationshipJson: string, name: string): string;
  renameElement(id: string, to: string): string;
  elementProblems(elementJson: string, localId: string): string;
  relationshipProblems(relationshipJson: string, name: string): string;
  elementAt(offset: number): string;
  freshViewId(): string;
  addView(id: string, scope: string): string;
  removeView(id: string): string;
  renameView(id: string, to: string): string;
  refreshLibraries(): void;
  delete(): void;
}

export interface RawLanguageServer {
  send(messageJson: string): string;
  exitCode(): number;
  delete(): void;
}

/** What `setFileSystem` reads libraries on disk through. */
export interface RawFileSystem {
  read(path: string): Uint8Array | string | undefined;
  list(dir: string): readonly string[] | undefined;
  join(dir: string, name: string): string;
  parent(path: string): string;
  env(name: string): string | undefined;
}

export interface RawModule {
  version(): string;
  parse(source: string): string;
  print(modelJson: string): string;
  updateSource(source: string, operationJson: string): string;
  renderView(modelJson: string, viewId: string): string;
  importDiagram(
    data: string | Uint8Array,
    fileName: string,
    stencils: boolean,
    resolve:
      | ((path: string, from: string | undefined) => { path: string; text: string }[] | undefined)
      | undefined,
  ): string;
  loadDrawioIndex(indexJson: string): string;
  readStencilSets(text: string, file: string): string;
  highlight(text: string): string;
  exportDrawio(modelJson: string, pagesJson: string): string;
  exportBpmn(modelJson: string, pagesJson: string): string;
  bpmnFixedPages(modelJson: string): string;
  addLibrary(bundleJson: string): string;
  libraryList(): string;
  libraryPalette(modelJson: string): string;
  expandFragment(modelJson: string, alias: string, id: string, valuesJson: string): string;
  buildLibrary(manifestJson: string, read: (path: string) => string | undefined): string;
  setFileSystem(fs: RawFileSystem | undefined, drawioDir: string): void;
  Session: new (
    source: string,
    path: string,
    listener: ((type: string, json: string) => void) | undefined,
  ) => RawSession;
  LanguageServer: new (
    listener: (messageJson: string) => void,
    fallbackDir: string,
  ) => RawLanguageServer;
}

export interface ModuleOptions {
  /** Where the `.wasm` file is, when the bundler cannot tell (`locateFile` in Emscripten terms). */
  locateFile?: (path: string, prefix: string) => string;
  /** The compiled module itself, when the host loads it. */
  wasmBinary?: ArrayBuffer | Uint8Array;
}

declare function createDiaDraw(options?: ModuleOptions): Promise<RawModule>;
export default createDiaDraw;
