import { isolateHistory, redo, undo } from '@codemirror/commands';
import { EditorState } from '@codemirror/state';
import { EditorView, keymap } from '@codemirror/view';
import type { Diagnostic, FoldRange, TextEdit } from '@diadraw/wasm';
import { basicSetup } from 'codemirror';
import { changeBetween, INDENT_SIZES, readIndentSize, writeIndentSize } from './format-settings';
import { diadraw } from './language';

/**
 * `<dd-text>`: CodeMirror 6 with DiaDraw highlighting, and the core's diagnostics and fold ranges
 * (fold markers in the gutter for `model`, `view` and other blocks). The editor owns
 * the text: canvas edits arrive as transactions (`applyEdits`), so one undo history covers both
 * panes. Fires `dd-change` with `{ text }` whenever the text changes, and `dd-rename-element` with
 * `{ offset }` (the cursor) on F2, to rename the element named there. Its bar formats the text
 * (Format, or ⇧⌥F) with the indent size chosen beside it, which the browser remembers.
 */
export class TextPane extends HTMLElement {
  #view: EditorView | undefined;
  /** Supplies diagnostics for the current text; set by the app before `load`. */
  diagnostics: (text: string) => readonly Diagnostic[] = () => [];
  /** Formats text with an indent size (the core's formatSource); set by the app. */
  formatter: ((text: string, indentSize: number) => string) | undefined;
  /** Supplies the foldable blocks of the current text; set by the app before `load`. */
  folds: (text: string) => readonly FoldRange[] = () => [];

  constructor() {
    super();
    const root = this.attachShadow({ mode: 'open' });
    root.innerHTML = `<style>
      :host { display: flex; flex-direction: column; min-height: 0; overflow: hidden; }
      .bar {
        display: flex; align-items: center; justify-content: flex-end; gap: 0.4rem;
        padding: 0.25rem 0.5rem; border-bottom: 1px solid var(--border, #e2e8f0);
        background: var(--panel, #f8fafc); color: var(--muted, #64748b); font-size: 0.8rem;
      }
      .bar button, .bar select {
        font: inherit; padding: 0.15rem 0.5rem; border: 1px solid var(--border, #e2e8f0);
        border-radius: 6px; background: var(--bg, #fff); color: var(--fg, #0f172a);
      }
      .bar button:hover { background: var(--panel, #f8fafc); }
      .host { flex: 1; min-height: 0; }
      .cm-editor { height: 100%; font-size: 13px; }
    </style>
    <div class="bar">
      <label>Indent <select data-role="indent" title="Spaces per indentation level when formatting"></select></label>
      <button type="button" data-action="format" title="Format the text: indentation, spacing and blank lines (⇧⌥F)">Format</button>
    </div>
    <div class="host"></div>`;
    const indent = root.querySelector<HTMLSelectElement>('[data-role="indent"]')!;
    const sizes: readonly number[] = INDENT_SIZES;
    const chosen = readIndentSize();
    indent.append(
      ...[...new Set([...sizes, chosen])]
        .sort((a, b) => a - b)
        .map((n) =>
          Object.assign(document.createElement('option'), {
            value: String(n),
            textContent: String(n),
          }),
        ),
    );
    indent.value = String(chosen);
    indent.addEventListener('change', () => writeIndentSize(Number(indent.value)));
    root.querySelector('[data-action="format"]')!.addEventListener('click', () => {
      this.format();
      this.#view?.focus();
    });
  }

  /** Spaces per indentation level that Format uses. */
  get indentSize(): number {
    return Number(this.shadowRoot!.querySelector<HTMLSelectElement>('[data-role="indent"]')!.value);
  }

  /**
   * Format Document: reformats the text with the chosen indent size as one undoable change over
   * the lines that differ. False when there is no formatter or nothing to change.
   */
  format(): boolean {
    const view = this.#view;
    if (!view || !this.formatter) return false;
    const before = view.state.doc.toString();
    const change = changeBetween(before, this.formatter(before, this.indentSize));
    if (!change) return false;
    // Its own undo step, never merged with typing just before or after.
    view.dispatch({
      changes: change,
      userEvent: 'input.format',
      annotations: isolateHistory.of('full'),
    });
    return true;
  }

  /** Starts a new document (a fresh undo history). */
  load(text: string): void {
    this.#view?.destroy();
    const root = this.shadowRoot!;
    this.#view = new EditorView({
      root,
      parent: root.querySelector('.host')!,
      state: EditorState.create({
        doc: text,
        extensions: [
          basicSetup,
          diadraw(
            (t) => this.diagnostics(t),
            (t) => this.folds(t),
          ),
          keymap.of([
            {
              key: 'Shift-Alt-f',
              run: () => {
                this.format();
                return true;
              },
            },
            {
              key: 'F2',
              run: (view) => {
                const offset = view.state.selection.main.head;
                this.dispatchEvent(new CustomEvent('dd-rename-element', { detail: { offset } }));
                return true;
              },
            },
          ]),
          EditorView.updateListener.of((u) => {
            if (u.docChanged)
              this.dispatchEvent(
                new CustomEvent('dd-change', { detail: { text: u.state.doc.toString() } }),
              );
          }),
        ],
      }),
    });
  }

  get text(): string {
    return this.#view?.state.doc.toString() ?? '';
  }

  /** Applies edits from the core (UTF-16 offsets into the current text) as one transaction. */
  applyEdits(edits: readonly TextEdit[], userEvent = 'input.canvas'): void {
    this.#view?.dispatch({
      changes: edits.map((e) => ({ from: e.from, to: e.to, insert: e.insert })),
      userEvent,
    });
  }

  /** Replaces the whole text as one undoable change. */
  replace(text: string, userEvent = 'input.replace'): void {
    const view = this.#view;
    if (!view) return;
    view.dispatch({ changes: { from: 0, to: view.state.doc.length, insert: text }, userEvent });
  }

  /** Selects a range and scrolls it into view. */
  select(from: number, to: number): void {
    this.#view?.dispatch({ selection: { anchor: from, head: to }, scrollIntoView: true });
  }

  undo(): void {
    if (this.#view) undo(this.#view);
  }

  redo(): void {
    if (this.#view) redo(this.#view);
  }

  disconnectedCallback(): void {
    this.#view?.destroy();
    this.#view = undefined;
  }
}

customElements.define('dd-text', TextPane);

declare global {
  interface HTMLElementTagNameMap {
    'dd-text': TextPane;
  }
}
