import type { DocumentFields } from './style-fields';
import { BUILT_IN_THEMES } from './style-fields';
import type { StyleBlocksControl, ThemeNodesControl } from './controls';
import { typingIn } from './controls';

const FIELDS: readonly {
  key: keyof DocumentFields;
  label: string;
  /** The control: a theme name to type or pick, unless given. */
  control?: 'dd-style-blocks' | 'dd-theme-keys' | 'dd-theme-nodes';
  hint?: string;
  /** In the section of the declared theme the view is drawn with. */
  declared?: boolean;
}[] = [
  { key: 'theme', label: 'Theme', hint: 'default, dark, mono or a new name' },
  { key: 'viewTheme', label: "This view's theme", hint: "the document's" },
  { key: 'styles', label: 'Style blocks', control: 'dd-style-blocks' },
  { key: 'extends', label: 'Extends', hint: 'default', declared: true },
  { key: 'themeKeys', label: 'Theme keys', control: 'dd-theme-keys', declared: true },
  { key: 'themeNodes', label: 'Node colours', control: 'dd-theme-nodes', declared: true },
];

/**
 * `<dd-style-panel>`: the document's theme and `style` blocks as fields, in the inspector's place
 * while nothing is selected and the Styles button is on. Committing a field (Enter, leaving it
 * after a change, or a change in one of its controls) fires `dd-document-update` with all the
 * fields as typed; Escape puts the field back. Set `fields` to show them, or undefined to hide the
 * panel.
 *
 * The themes are fields that suggest the themes there are; the style blocks are a list, each with
 * a selector and the visual style editor; a declared theme's keys and node colours are colour
 * wells (controls.ts).
 */
export class StylePanel extends HTMLElement {
  #fields: DocumentFields | undefined;
  /** Theme names to suggest besides the built-in ones. */
  themes: readonly string[] = [];

  /** What the style blocks' selector pickers offer (field-editors.ts selectorChoices). */
  set selectors(selectors: readonly string[]) {
    this.#selectors = selectors;
    const blocks = this.querySelector<StyleBlocksControl>('dd-style-blocks');
    if (blocks) blocks.selectors = selectors;
  }

  /** What the node colours' kind picker offers. */
  set kinds(kinds: readonly string[]) {
    this.#kinds = kinds;
    const nodes = this.querySelector<ThemeNodesControl>('dd-theme-nodes');
    if (nodes) nodes.kinds = kinds;
  }

  #selectors: readonly string[] = [];
  #kinds: readonly string[] = [];

  connectedCallback(): void {
    if (this.childElementCount) return;
    this.classList.add('inspector', 'style-panel');
    this.setAttribute('role', 'complementary');
    this.setAttribute('aria-label', 'Theme and styles');
    this.innerHTML = `
      <div class="inspector-head"><strong>Theme and styles</strong></div>
      <datalist id="dd-themes"></datalist>
      ${FIELDS.map(
        (f) =>
          `${f.key === 'extends' ? '<div class="inspector-head" data-declared><strong class="theme-title"></strong></div>' : ''}
          ${
            f.control
              ? `<div class="field" ${f.declared ? 'data-declared' : ''}>${f.label}<${f.control} data-field="${f.key}"></${f.control}></div>`
              : `<label ${f.declared ? 'data-declared' : ''}>${f.label}<input data-field="${f.key}" list="dd-themes" placeholder="${f.hint ?? ''}" spellcheck="false"></label>`
          }`,
      ).join('')}`;
    this.selectors = this.#selectors;
    this.kinds = this.#kinds;
    this.addEventListener('change', (e) => {
      if ((e.target as HTMLElement).dataset.field) this.#commit();
    });
    this.addEventListener('keydown', (e) => {
      const field = e.target as HTMLElement;
      if (!field.dataset.field) return;
      if (e.key === 'Escape') {
        this.#fill(true);
        field.blur();
      } else if (e.key === 'Enter' && field.tagName === 'INPUT') {
        e.preventDefault();
        this.#commit();
      }
    });
    this.hidden = true;
  }

  get fields(): DocumentFields | undefined {
    return this.#fields;
  }

  set fields(fields: DocumentFields | undefined) {
    const sameTheme = fields?.declared === this.#fields?.declared;
    this.#fields = fields;
    this.hidden = !fields;
    if (fields) this.#fill(false, sameTheme);
  }

  #field(key: keyof DocumentFields): HTMLElement & { value: string } {
    return this.querySelector(`[data-field="${key}"]`)!;
  }

  /** Shows the values; a field being typed in keeps its text unless `all`. */
  #fill(all: boolean, sameTheme = true): void {
    const fields = this.#fields;
    if (!fields) return;
    const names = [...new Set([...BUILT_IN_THEMES, ...this.themes])];
    this.querySelector('#dd-themes')!.innerHTML = names
      .map((n) => `<option value="${n}">`)
      .join('');
    for (const el of this.querySelectorAll<HTMLElement>('[data-declared]'))
      el.hidden = !fields.declared;
    this.querySelector('.theme-title')!.textContent = `Theme ${fields.declared}`;
    if (!sameTheme) this.querySelector<ThemeNodesControl>('dd-theme-nodes')!.forget();
    for (const f of FIELDS) {
      const input = this.#field(f.key);
      if (!all && typingIn(input) && (!f.declared || sameTheme)) continue;
      input.value = fields[f.key];
    }
  }

  #commit(): void {
    const fields = this.#fields;
    if (!fields) return;
    const typed = {
      ...fields,
      ...Object.fromEntries(FIELDS.map((f) => [f.key, this.#field(f.key).value])),
    } as DocumentFields;
    if (JSON.stringify(typed) === JSON.stringify(fields)) return;
    this.dispatchEvent(
      new CustomEvent('dd-document-update', { detail: { fields: typed }, bubbles: true }),
    );
  }
}

customElements.define('dd-style-panel', StylePanel);
