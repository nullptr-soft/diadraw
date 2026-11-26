import { COPYRIGHT, CREDITS_URL, LICENSE, LICENSE_URL, creditsFor } from './credits';

// Help › About in the web and desktop apps: the version, DiaDraw's licence and the open-source
// software the app ships (credits.ts), like the native apps' About dialogs.

const TEMPLATE = `
  <dialog aria-labelledby="dd-about-title">
    <form method="dialog" class="about-head">
      <h2 id="dd-about-title">About DiaDraw</h2>
      <button type="submit" aria-label="Close">✕</button>
    </form>
    <p class="about-version"></p>
    <p>Diagrams as text, with a canvas that writes the text back.</p>
    <p><span class="about-copyright"></span>. Free software under the
      <a class="about-license" target="_blank" rel="noopener"></a>.</p>
    <h3>Open-source software</h3>
    <ul class="about-credits"></ul>
    <p><a class="about-all" target="_blank" rel="noopener">Everything DiaDraw uses, and what for</a></p>
  </dialog>
`;

/** `<dd-about>`: the About dialog. `show(version, app)` fills it in and opens it. */
export class AboutDialog extends HTMLElement {
  #dialog!: HTMLDialogElement;

  connectedCallback(): void {
    if (this.#dialog) return;
    this.innerHTML = TEMPLATE;
    this.#dialog = this.querySelector('dialog')!;
    const license = this.querySelector<HTMLAnchorElement>('.about-license')!;
    license.textContent = LICENSE;
    license.href = LICENSE_URL;
    this.querySelector('.about-copyright')!.textContent = COPYRIGHT;
    this.querySelector<HTMLAnchorElement>('.about-all')!.href = CREDITS_URL;
    // A click on the backdrop closes the dialog.
    this.#dialog.addEventListener('click', (e) => {
      if (e.target === this.#dialog) this.#dialog.close();
    });
  }

  /** Opens the dialog for DiaDraw `version`, crediting what `app` (`web` or `desktop`) ships. */
  show(version: string, app: string): void {
    this.querySelector('.about-version')!.textContent = `Version ${version}`;
    const list = this.querySelector('.about-credits')!;
    list.replaceChildren(
      ...creditsFor(app).map((c) => {
        const item = document.createElement('li');
        const link = Object.assign(document.createElement('a'), {
          href: c.url,
          target: '_blank',
          rel: 'noopener',
          textContent: c.name,
        });
        item.append(link, ` ${c.version} (${c.license})`);
        return item;
      }),
    );
    if (!this.#dialog.open) this.#dialog.showModal();
  }
}

customElements.define('dd-about', AboutDialog);
