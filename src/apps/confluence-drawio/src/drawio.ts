/**
 * The draw.io editor in embed mode, in a frame: its URL and its JSON message protocol
 * (https://www.drawio.com/doc/faq/embed-mode). The editor posts events (`init`, `save`, `exit`,
 * `export`) to this window and takes actions (`load`, `export`, `spinner`, `dialog`) back.
 */

/**
 * Where the editor comes from, relative to the editor page: the copy of draw.io bundled with the
 * app (scripts/fetch-drawio.mjs). A build can point it elsewhere (scripts/build.mjs, DRAWIO_URL).
 */
export const BUNDLED_DRAWIO_PATH = 'drawio/index.html';

export interface EditorOptions {
  /** The UI language, as a two-letter code (`en`, `de`); draw.io's own choice when absent. */
  readonly lang?: string | undefined;
  /** Draw the editor dark, for Confluence's dark theme. */
  readonly dark?: boolean;
}

/**
 * The editor's URL: embed mode with the JSON protocol, one Save & Exit button and an Exit
 * button, and Atlassian's look. Parameters already in `base` (for a self-hosted copy) stay.
 */
export function editorUrl(base: string, options: EditorOptions = {}): string {
  const url = new URL(base);
  const params: Record<string, string> = {
    embed: '1',
    proto: 'json',
    spin: '1',
    ui: 'atlas',
    saveAndExit: '1',
    noSaveBtn: '1',
    libraries: '1',
  };
  if (options.dark) params.dark = '1';
  if (options.lang && /^[a-z]{2}(?:[-_][a-z]{2,4})?$/i.test(options.lang))
    params.lang = options.lang.slice(0, 2).toLowerCase();
  for (const [name, value] of Object.entries(params)) url.searchParams.set(name, value);
  return url.href;
}

/** An event from the editor; only the fields this app reads. */
export type DrawioEvent =
  | { readonly event: 'init' }
  | { readonly event: 'load' }
  | { readonly event: 'save'; readonly exit?: boolean }
  | { readonly event: 'exit'; readonly modified?: boolean }
  | { readonly event: 'export'; readonly data: string; readonly format?: string }
  | { readonly event: 'autosave' | 'configure' | 'openLink' | string };

/** Parses a message from the editor; undefined for anything that is not one of its events. */
export function parseEvent(data: unknown): DrawioEvent | undefined {
  if (typeof data !== 'string' || !data.startsWith('{')) return undefined;
  try {
    const message = JSON.parse(data) as unknown;
    if (
      message &&
      typeof message === 'object' &&
      typeof (message as DrawioEvent).event === 'string'
    )
      return message as DrawioEvent;
  } catch {
    // Not JSON: another script's message.
  }
  return undefined;
}

/** What the host does with the editor; injected so the protocol can be tested without a browser. */
export interface Host {
  /** Posts an action to the editor. */
  post(action: Record<string, unknown>): void;
  /** Stores the diagram (an editable SVG, or undefined for an empty diagram) and closes. */
  save(svg: string | undefined): Promise<void>;
  /** Closes without storing. */
  close(): void;
  /** Tells the user something went wrong. */
  fail(message: string): void;
}

/** Whether `svg` is an empty diagram; see diagram.ts. Passed in to keep this module standalone. */
export type EmptyTest = (svg: string) => boolean;

/**
 * Runs one editing session: loads `svg` (an editable SVG, or '' for a new diagram) when the
 * editor is ready, asks for an editable SVG when the user saves, and stores it.
 */
export class Session {
  private saving = false;

  constructor(
    private readonly host: Host,
    private readonly svg: string,
    private readonly isEmpty: EmptyTest,
    private readonly decode: (dataUri: string) => string,
  ) {}

  /** Handles one event from the editor. */
  async handle(message: DrawioEvent): Promise<void> {
    switch (message.event) {
      case 'init':
        this.host.post({ action: 'load', xml: this.svg, autosave: 0 });
        break;
      case 'save':
        if (this.saving) break;
        this.saving = true;
        // The drawing and the whole file in one SVG, for the page and the next edit.
        this.host.post({ action: 'export', format: 'xmlsvg', spinKey: 'saving' });
        break;
      case 'export':
        if (!this.saving || !('data' in message) || typeof message.data !== 'string') break;
        try {
          const svg = this.decode(message.data);
          await this.host.save(this.isEmpty(svg) ? undefined : svg);
        } catch (error) {
          this.saving = false;
          this.host.post({ action: 'spinner', show: false });
          this.host.fail(`Could not save the diagram: ${errorText(error)}`);
        }
        break;
      case 'exit':
        if (!this.saving) this.host.close();
        break;
    }
  }
}

export function errorText(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
