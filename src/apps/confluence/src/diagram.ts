/**
 * What the DiaDraw macro stores and how it is drawn. The macro view and the editor share this
 * module, so a diagram looks the same in the editor's preview and in the page.
 */
import { renderBlock, type BlockResult } from '@diadraw/markdown';
import type { DiaDraw, Diagnostic, Model } from '@diadraw/wasm';

/**
 * The macro's configuration, saved in the page with the macro. The DSL text is the source of
 * truth: the theme picker writes `theme …` into it rather than keeping a separate setting.
 */
export interface MacroConfig {
  /** The diagram's DiaDraw text. */
  readonly source: string;
  /** The view to draw; the document's first view when absent or empty. */
  readonly view?: string;
}

/** Reads a macro configuration from whatever Confluence handed over (absent, partial, old). */
export function readConfig(raw: unknown): MacroConfig {
  if (!raw || typeof raw !== 'object') return { source: '' };
  const { source, view } = raw as Record<string, unknown>;
  return {
    source: typeof source === 'string' ? source : '',
    ...(typeof view === 'string' && view && { view }),
  };
}

/** The themes every document can use without declaring them. */
export const BUILT_IN_THEMES: readonly string[] = ['default', 'dark', 'mono'];

/** Formats the importer understands, by the file extension it reads them from. */
export const IMPORT_FORMATS = [
  { id: 'mermaid', label: 'Mermaid', fileName: 'diagram.mmd' },
  { id: 'plantuml', label: 'PlantUML', fileName: 'diagram.puml' },
  { id: 'structurizr', label: 'Structurizr DSL', fileName: 'workspace.dsl' },
  { id: 'drawio', label: 'draw.io', fileName: 'diagram.drawio' },
  { id: 'bpmn', label: 'BPMN 2.0', fileName: 'diagram.bpmn' },
] as const;

export type ImportFormat = (typeof IMPORT_FORMATS)[number]['id'];

/** The diagram a new macro starts with. */
export const STARTER_SOURCE = `model {
  user = person "User"
  app = system "Our app" {
    web = container "Web app" tech "React"
    db = container "Database" tech "Postgres" shape cylinder
  }
  user -> app.web "uses"
  app.web -> app.db "reads and writes"
}

view overview of app {
  include *
  layout layered direction right
}
`;

/** The ids of the views a model draws: the declared ones, or `default` when there are none. */
export function viewIds(model: Model): string[] {
  const declared = Object.keys(model.views);
  return declared.length ? declared : ['default'];
}

/** The themes a document can pick: the built-in ones, then the ones it declares. */
export function themeNames(model: Model): string[] {
  const names = [...BUILT_IN_THEMES];
  for (const theme of model.themes ?? []) if (!names.includes(theme.name)) names.push(theme.name);
  return names;
}

/** The theme a view is drawn with: its own, else the document's, else `default`. */
export function currentTheme(model: Model, view: string | undefined): string {
  const own = view !== undefined ? model.views[view]?.theme : undefined;
  return own ?? model.theme ?? 'default';
}

/**
 * Returns `source` with `theme` applied to what draws `view`: the view's own `theme` line when it
 * has one, the document's otherwise. Picking `default` removes the line.
 */
export function withTheme(
  dd: DiaDraw,
  source: string,
  view: string | undefined,
  theme: string,
): string {
  const { model } = dd.parse(source);
  const target = view !== undefined && model.views[view]?.theme !== undefined ? view : undefined;
  const set = theme === 'default' && target === undefined ? undefined : theme;
  return dd.updateSource(source, {
    type: 'setTheme',
    ...(target !== undefined && { view: target }),
    ...(set !== undefined && { theme: set }),
  }).source;
}

export type ImportOutcome =
  | { readonly ok: true; readonly source: string; readonly notes: readonly string[] }
  | { readonly ok: false; readonly message: string };

/**
 * Converts a diagram in another format to DiaDraw text with the core's importers. `notes` lists
 * what the importer could not carry over, one line each.
 */
export function importText(dd: DiaDraw, text: string, fileName: string): ImportOutcome {
  try {
    const { model, report } = dd.importDiagram(text, fileName);
    const notes = [
      ...report.dropped.map((d) => (d.line ? `line ${d.line}: ${d.message}` : d.message)),
      ...report.notes,
    ];
    return { ok: true, source: dd.print(model), notes };
  } catch (error) {
    return { ok: false, message: error instanceof Error ? error.message : String(error) };
  }
}

/** The file name the importer should see for a file the user picked, or for pasted text. */
export function importFileName(format: ImportFormat): string {
  return IMPORT_FORMATS.find((f) => f.id === format)!.fileName;
}

export interface Drawing {
  readonly result: BlockResult;
  /** Every view the text has, for the view picker; empty when the text does not parse. */
  readonly views: readonly string[];
  readonly themes: readonly string[];
  readonly theme: string;
  readonly diagnostics: readonly Diagnostic[];
}

/**
 * Draws a macro configuration. A view that no longer exists (renamed in the text) falls back to
 * the first one, so an edit to the text never leaves the page blank.
 */
export function draw(dd: DiaDraw, config: MacroConfig, idPrefix: string): Drawing {
  let model: Model | undefined;
  let diagnostics: readonly Diagnostic[] = [];
  try {
    ({ model, diagnostics } = dd.parse(config.source));
  } catch {
    // renderBlock reports the same failure below.
  }
  const views = model ? viewIds(model) : [];
  const view = config.view && views.includes(config.view) ? config.view : views[0];
  const result = renderBlock(dd, config.source, { view, idPrefix });
  return {
    result,
    views,
    themes: model ? themeNames(model) : [...BUILT_IN_THEMES],
    theme: model ? currentTheme(model, view) : 'default',
    diagnostics,
  };
}
