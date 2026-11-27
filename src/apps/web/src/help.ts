import { canvasHelp } from './canvas-help';

// Help: what the canvas does with the mouse and the keyboard, in a dialog the toolbar's Help
// button opens (the list is canvas-help.ts, shared with the native apps).

const TEMPLATE = `
  <dialog aria-labelledby="dd-help-title">
    <form method="dialog" class="help-head">
      <h2 id="dd-help-title">Mouse and keyboard</h2>
      <button type="submit" aria-label="Close">✕</button>
    </form>
    <div class="help-sections"></div>
  </dialog>
`;

/** The platform the keys are named for: userAgentData's where there is one. */
function platform(): string {
  const nav = navigator as Navigator & { userAgentData?: { platform?: string } };
  return nav.userAgentData?.platform || navigator.platform || navigator.userAgent;
}

/** `<dd-help>`: the Help dialog. `show()` opens it. */
export class HelpDialog extends HTMLElement {
  #dialog!: HTMLDialogElement;

  connectedCallback(): void {
    if (this.#dialog) return;
    this.innerHTML = TEMPLATE;
    this.#dialog = this.querySelector('dialog')!;
    const sections = this.querySelector('.help-sections')!;
    for (const section of canvasHelp(platform())) {
      const heading = Object.assign(document.createElement('h3'), { textContent: section.title });
      const list = document.createElement('dl');
      for (const entry of section.entries) {
        const keys = document.createElement('dt');
        keys.append(Object.assign(document.createElement('kbd'), { textContent: entry.keys }));
        list.append(
          keys,
          Object.assign(document.createElement('dd'), { textContent: entry.action }),
        );
      }
      sections.append(heading, list);
    }
    // A click on the backdrop closes the dialog.
    this.#dialog.addEventListener('click', (e) => {
      if (e.target === this.#dialog) this.#dialog.close();
    });
  }

  /** Opens the dialog. */
  show(): void {
    if (!this.#dialog.open) this.#dialog.showModal();
  }
}

customElements.define('dd-help', HelpDialog);
