import { redo, undo } from '@codemirror/commands';
import { EditorState } from '@codemirror/state';
import { EditorView, keymap } from '@codemirror/view';
import type { Diagnostic, TextEdit } from '@diadraw/wasm';
import { basicSetup } from 'codemirror';
import { diadraw } from './language';

/**
 * `<dd-text>`: CodeMirror 6 with DiaDraw highlighting and the core's diagnostics. The editor owns
 * the text: canvas edits arrive as transactions (`applyEdits`), so one undo history covers both
 * panes. Fires `dd-change` with `{ text }` whenever the text changes, and `dd-rename-element` with
 * `{ offset }` (the cursor) on F2, to rename the element named there.
 */
export class TextPane extends HTMLElement {
  #view: EditorView | undefined;
  /** Supplies diagnostics for the current text; set by the app before `load`. */
  diagnostics: (text: string) => readonly Diagnostic[] = () => [];

  constructor() {
    super();
    const root = this.attachShadow({ mode: 'open' });
    root.innerHTML = `<style>
      :host { display: block; min-height: 0; overflow: hidden; }
      .cm-editor { height: 100%; font-size: 13px; }
    </style><div class="host" style="height:100%"></div>`;
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
          diadraw((t) => this.diagnostics(t)),
          keymap.of([
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
