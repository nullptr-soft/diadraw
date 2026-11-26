import { randomBytes } from 'node:crypto';
import * as path from 'node:path';
import type { DiaDraw, Session } from '@diadraw/wasm';
import { loadDiaDraw } from '@diadraw/wasm';
import { nodeFileSystem } from '@diadraw/wasm/node';
import * as vscode from 'vscode';
import type { LanguageClientOptions, ServerOptions } from 'vscode-languageclient/node';
import { LanguageClient, TransportKind } from 'vscode-languageclient/node';
import { renderOnce, renderState } from './diagram';
import { previewHtml } from './preview';

let client: LanguageClient | undefined;
let core: Promise<DiaDraw> | undefined;

/** The core, loaded once, reading libraries from disk like the CLI. */
function diadraw(): Promise<DiaDraw> {
  core ??= loadDiaDraw().then((dd) => {
    dd.setFileSystem(nodeFileSystem(), { drawio: process.env.DIADRAW_DRAWIO ?? '' });
    return dd;
  });
  return core;
}

const fileOf = (doc: vscode.TextDocument): string | undefined =>
  doc.uri.scheme === 'file' ? doc.uri.fsPath : undefined;

export async function activate(context: vscode.ExtensionContext): Promise<void> {
  const module = context.asAbsolutePath(path.join('dist', 'server.cjs'));
  const serverOptions: ServerOptions = {
    run: { module, transport: TransportKind.stdio },
    debug: {
      module,
      transport: TransportKind.stdio,
      options: { execArgv: ['--nolazy', '--inspect=6009'] },
    },
  };
  const clientOptions: LanguageClientOptions = { documentSelector: [{ language: 'diadraw' }] };
  client = new LanguageClient('diadraw', 'DiaDraw', serverOptions, clientOptions);
  await client.start();

  context.subscriptions.push(
    vscode.commands.registerCommand('diadraw.openPreview', async () => {
      const editor = vscode.window.activeTextEditor;
      if (editor?.document.languageId === 'diadraw')
        Preview.show(editor.document, context, await diadraw());
    }),
    vscode.commands.registerCommand('diadraw.exportSvg', exportSvg),
  );
}

export async function deactivate(): Promise<void> {
  await client?.stop();
}

async function exportSvg(): Promise<void> {
  const doc = vscode.window.activeTextEditor?.document;
  if (doc?.languageId !== 'diadraw') return;
  const dd = await diadraw();
  const { views } = renderOnce(dd, doc.getText(), fileOf(doc));
  const view =
    views.length > 1
      ? await vscode.window.showQuickPick([...views], { placeHolder: 'View to export' })
      : views[0];
  if (view === undefined) return;
  const outcome = renderOnce(dd, doc.getText(), fileOf(doc), view);
  if (outcome.svg === undefined) {
    void vscode.window.showErrorMessage(`DiaDraw: ${outcome.error ?? 'nothing to export'}`);
    return;
  }
  const target = await vscode.window.showSaveDialog({
    defaultUri: doc.uri.with({ path: doc.uri.path.replace(/\.draw$/, `.${view}.svg`) }),
    filters: { SVG: ['svg'] },
  });
  if (target) await vscode.workspace.fs.writeFile(target, Buffer.from(outcome.svg, 'utf8'));
}

/**
 * A live preview beside the editor. It keeps one core session for the document (so layouts stay
 * put across edits), feeds it the text as it changes, and draws again when the session reports
 * a change.
 */
class Preview {
  static readonly #open = new Map<string, Preview>();
  #view: string | undefined;
  #timer: NodeJS.Timeout | undefined;

  private constructor(
    private readonly doc: vscode.TextDocument,
    private readonly panel: vscode.WebviewPanel,
    private readonly session: Session,
  ) {}

  static show(doc: vscode.TextDocument, context: vscode.ExtensionContext, dd: DiaDraw): void {
    const key = doc.uri.toString();
    const existing = Preview.#open.get(key);
    if (existing) {
      existing.panel.reveal(vscode.ViewColumn.Beside);
      return;
    }
    const panel = vscode.window.createWebviewPanel(
      'diadraw.preview',
      `Preview ${path.basename(doc.fileName)}`,
      vscode.ViewColumn.Beside,
      { enableScripts: true, localResourceRoots: [] },
    );
    const file = fileOf(doc);
    const session = dd.createSession(doc.getText(), file === undefined ? {} : { path: file });
    const preview = new Preview(doc, panel, session);
    Preview.#open.set(key, preview);
    session.on('change', () => preview.render());
    session.on('libraries', () => preview.render());
    const changes = vscode.workspace.onDidChangeTextDocument((e) => {
      if (e.document.uri.toString() === key) preview.schedule();
    });
    panel.webview.onDidReceiveMessage((m: { type: string; view: string }) => {
      if (m.type === 'view') {
        preview.#view = m.view;
        preview.render();
      }
    });
    panel.onDidDispose(() => {
      clearTimeout(preview.#timer);
      changes.dispose();
      session.dispose();
      Preview.#open.delete(key);
    });
    context.subscriptions.push(changes);
    preview.render();
  }

  /** Hands the session the new text after a pause in typing; its `change` event redraws. */
  schedule(): void {
    clearTimeout(this.#timer);
    this.#timer = setTimeout(() => this.session.setSource(this.doc.getText()), 250);
  }

  render(): void {
    const state = renderState(this.session, this.#view);
    this.panel.webview.html = previewHtml(
      state,
      randomBytes(16).toString('base64'),
      this.panel.webview.cspSource,
    );
  }
}
