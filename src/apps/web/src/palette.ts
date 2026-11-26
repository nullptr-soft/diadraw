import type { DiaDraw, LibraryInfo, Model, PaletteItem } from '@diadraw/wasm';
import type { DrawioIndex } from './drawio';
import { drawioAlias, drawioIndex, loadDrawioSet } from './drawio';

/** The library an item comes from, as a `use` line names it. */
export interface PaletteLibrary {
  readonly name: string;
  readonly version: string;
}

export interface InsertDetail {
  readonly item: PaletteItem;
  readonly library: PaletteLibrary;
}

/** The default alias of a library, as the core derives it. */
export function defaultAlias(name: string): string {
  const base = name.split('/').pop() ?? name;
  return base.replace(/[^A-Za-z0-9_]+/g, '_').replace(/^(\d)/, '_$1') || 'lib';
}

/** A model that uses the given libraries, for asking the core for their palette. */
const using = (libraries: readonly { name: string; version: string; alias: string }[]): Model =>
  ({
    elements: {},
    relationships: {},
    groups: {},
    views: {},
    libraries: libraries.map((l) => ({
      name: l.name,
      range: `^${l.version.split('.')[0]}`,
      alias: l.alias,
    })),
  }) as unknown as Model;

const escape = (text: string) =>
  text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;');

/**
 * `<dd-palette>`: shapes, composites and fragments of the built-in libraries, grouped by library
 * and category, then draw.io's stencil sets (restricted), loaded when opened. Clicking an item
 * fires `dd-insert` with the item and its library.
 */
export class ShapePalette extends HTMLElement {
  #dd: DiaDraw | undefined;
  #query = '';
  #builtin: PaletteItem[] = [];
  #libraries = new Map<string, PaletteLibrary>();
  #index: DrawioIndex | undefined;
  /** Items of opened draw.io sets, by set name. */
  #sets = new Map<string, PaletteItem[]>();
  #open = new Set<string>();

  set dd(dd: DiaDraw) {
    this.#dd = dd;
    const builtins: LibraryInfo[] = dd.libraries().filter((l) => l.builtin);
    for (const l of builtins) this.#libraries.set(defaultAlias(l.name), l);
    const palette = dd.palette(using(builtins.map((l) => ({ ...l, alias: defaultAlias(l.name) }))));
    this.#builtin = [...palette.items];
    void drawioIndex(dd).then((index) => {
      this.#index = index;
      this.#renderSets();
    });
    this.#render();
  }

  connectedCallback(): void {
    this.classList.add('palette');
    this.setAttribute('role', 'complementary');
    this.setAttribute('aria-label', 'Shape palette');
    this.innerHTML = `
      <div class="palette-head">
        <input type="search" placeholder="Search shapes…" aria-label="Search shapes">
        <button type="button" data-close aria-label="Close palette">×</button>
      </div>
      <div class="palette-builtin"></div>
      <div class="palette-sets"></div>`;
    this.querySelector('input')!.addEventListener('input', (e) => {
      this.#query = (e.target as HTMLInputElement).value.trim().toLowerCase();
      this.#render();
    });
    this.querySelector('[data-close]')!.addEventListener('click', () =>
      this.dispatchEvent(new CustomEvent('dd-close', { bubbles: true })),
    );
    this.addEventListener('click', (e) => {
      const button = (e.target as Element).closest<HTMLElement>('[data-ref]');
      if (!button) return;
      const set = button.dataset.set;
      const items = set ? (this.#sets.get(set) ?? []) : this.#builtin;
      const item = items.find((i) => `${i.type}:${i.ref}` === button.dataset.ref);
      if (!item) return;
      const library = set ? this.#setLibrary(set) : this.#libraries.get(item.alias);
      if (library)
        this.dispatchEvent(
          new CustomEvent<InsertDetail>('dd-insert', { bubbles: true, detail: { item, library } }),
        );
    });
    this.addEventListener('toggle', (e) => this.#toggled(e.target as HTMLDetailsElement), true);
  }

  #matches = (i: PaletteItem) =>
    !this.#query ||
    i.label.toLowerCase().includes(this.#query) ||
    i.id.toLowerCase().includes(this.#query) ||
    i.keywords.some((k) => k.toLowerCase().includes(this.#query));

  #setLibrary(name: string): PaletteLibrary | undefined {
    const info = this.#index?.sets.find((s) => s.name === name);
    return info && { name: info.name, version: info.version };
  }

  #item(item: PaletteItem, set?: string): string {
    const art = item.art;
    const icon = art?.body
      ? `<svg class="palette-icon" viewBox="${escape(art.viewBox ?? `0 0 ${art.width ?? 48} ${art.height ?? 48}`)}" preserveAspectRatio="xMidYMid meet">${art.body}</svg>`
      : `<span class="palette-icon palette-icon-text">${item.type === 'fragment' ? '{ }' : '□'}</span>`;
    const what =
      item.type === 'fragment'
        ? 'Fragment'
        : item.type === 'kind'
          ? `kind ${item.ref}`
          : `shape ${item.ref}`;
    const title = `${item.label}${item.description ? ` — ${item.description}` : ''}\n${what}`;
    // The markup was sanitised when the library was built.
    return `<button type="button" class="palette-item" title="${escape(title)}" data-ref="${escape(`${item.type}:${item.ref}`)}"${set ? ` data-set="${escape(set)}"` : ''}>${icon}<span>${escape(item.label)}</span></button>`;
  }

  #render(): void {
    const groups = new Map<string, PaletteItem[]>();
    for (const i of this.#builtin.filter(this.#matches)) {
      const key = `${i.library} · ${i.category}`;
      groups.set(key, [...(groups.get(key) ?? []), i]);
    }
    const builtin = this.querySelector('.palette-builtin');
    if (!builtin) return;
    builtin.innerHTML = [...groups]
      .map(
        ([group, items]) =>
          `<section><h2>${escape(group)}</h2><div class="palette-grid">${items.map((i) => this.#item(i)).join('')}</div></section>`,
      )
      .join('');
    this.#renderSets();
  }

  #renderSets(): void {
    const container = this.querySelector('.palette-sets');
    const index = this.#index;
    if (!container) return;
    const q = this.#query;
    const sets = index?.sets.filter((s) => !q || s.name.includes(q) || s.source.includes(q)) ?? [];
    if (!index || sets.length === 0) {
      container.innerHTML =
        this.#builtin.some(this.#matches) || !q
          ? ''
          : '<p class="palette-empty">No shapes match.</p>';
      return;
    }
    container.innerHTML = `<section>
      <h2 title="${escape(index.licenseNotes)}">draw.io stencils ${escape(index.drawio.version)} <span class="palette-restricted">restricted</span></h2>
      <p class="palette-licence">Apache-2.0 with a restriction: not for use in or with Atlassian products without permission. Vendor icons (AWS, Azure, GCP, Cisco…) are subject to the vendors' trademark terms.</p>
      ${sets
        .map(
          (
            s,
          ) => `<details class="palette-drawio-set" data-set="${escape(s.name)}"${this.#open.has(s.name) ? ' open' : ''}>
            <summary title="${escape(`${s.source}: ${s.shapes} shapes`)}">${escape(s.name.replace(/^drawio-/, ''))} <span class="palette-restricted">${s.vendor ? 'restricted · vendor marks' : 'restricted'}</span></summary>
            <div class="palette-grid">${this.#setItems(s.name)}</div>
          </details>`,
        )
        .join('')}
    </section>`;
  }

  #setItems(name: string): string {
    if (!this.#open.has(name)) return '';
    const items = this.#sets.get(name);
    if (!items) return '<p class="palette-empty">Loading…</p>';
    const shown = items.filter(
      (i) =>
        !this.#query || i.label.toLowerCase().includes(this.#query) || name.includes(this.#query),
    );
    const more =
      shown.length > 300
        ? `<p class="palette-empty">${shown.length - 300} more; search to narrow.</p>`
        : '';
    return (
      shown
        .slice(0, 300)
        .map((i) => this.#item(i, name))
        .join('') + more
    );
  }

  async #toggled(details: HTMLDetailsElement): Promise<void> {
    const name = details.dataset.set;
    const dd = this.#dd;
    if (!name || !dd) return;
    if (!details.open) {
      this.#open.delete(name);
      return;
    }
    this.#open.add(name);
    if (!this.#sets.has(name)) {
      details.querySelector('.palette-grid')!.innerHTML = '<p class="palette-empty">Loading…</p>';
      const info = this.#index?.sets.find((s) => s.name === name);
      if (info && (await loadDrawioSet(dd, name))) {
        const palette = dd.palette(
          using([{ name, version: info.version, alias: drawioAlias(name) }]),
        );
        this.#sets.set(name, [...palette.items]);
      } else {
        this.#sets.set(name, []);
      }
    }
    const grid = this.querySelector(`details[data-set="${CSS.escape(name)}"] .palette-grid`);
    if (grid) grid.innerHTML = this.#setItems(name);
  }
}

customElements.define('dd-palette', ShapePalette);

declare global {
  interface HTMLElementTagNameMap {
    'dd-palette': ShapePalette;
  }
}
