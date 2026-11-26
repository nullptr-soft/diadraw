import type { Arrow } from './relationship-fields';
import type { BlockRow, StyleKey, StyleTarget } from './field-editors';
import { COLOR_SWATCHES } from './swatches';
import {
  blockRows,
  blockText,
  isColorKey,
  selectorTarget,
  fontStyleBits,
  joinTags,
  normalizedColor,
  otherStyle,
  propertyRows,
  propertyText,
  splitTags,
  styleKeys,
  styleControlValue,
  styleValue,
  withOtherStyle,
  withStyleSetting,
  withStyleValue,
} from './field-editors';

// The inspector's controls for fields that are more than a line of text. Each is an element with
// a text `value` (the field as the inspector reads and writes it) that fires a bubbling `change`
// when the person changes it, so the panels treat it like an <input>. Setting `value` never fires.

const escape = (text: string): string =>
  text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');

/** Whether the person is typing in `element` (or in a control inside it). */
export const typingIn = (element: Element): boolean =>
  document.activeElement !== null && element.contains(document.activeElement);

abstract class FieldControl extends HTMLElement {
  protected text = '';

  get value(): string {
    return this.text;
  }

  set value(value: string) {
    this.text = value;
    if (this.isConnected) this.render();
  }

  connectedCallback(): void {
    if (!this.childElementCount) this.build();
    this.render();
  }

  protected abstract build(): void;
  protected abstract render(): void;

  /** Sets the text as the person changed it and tells the panel. */
  protected commit(text: string): void {
    if (text === this.text) return;
    this.text = text;
    this.dispatchEvent(new Event('change', { bubbles: true }));
  }

  /** Events of the inner controls stay inside: the panel hears this element's own. */
  protected quiet(e: Event): void {
    if (e.target !== this) e.stopPropagation();
  }
}

// ── Colour ───────────────────────────────────────────────────────────────────────────────────

/**
 * `<dd-color>`: a colour button with a button that clears it. The colour button opens the preset
 * swatches (swatches.ts), with Custom… for the browser's colour picker and Default. Its value is
 * `#rrggbb`, empty for the default (the button shows a diagonal line), or another value the
 * picker cannot show, kept as is.
 */
export class ColorControl extends FieldControl {
  protected build(): void {
    this.classList.add('dd-color');
    const rows = COLOR_SWATCHES.map(
      (row) =>
        `<div class="dd-swatch-row" role="presentation">${row
          .map(
            (s) =>
              `<button type="button" class="dd-swatch" data-color="${s.color}" style="--swatch: ${s.color}" title="${escape(s.name)}  ${s.color}" aria-label="${escape(s.name)}" aria-pressed="false"></button>`,
          )
          .join('')}</div>`,
    ).join('');
    this.innerHTML =
      `<button type="button" class="dd-well" aria-haspopup="dialog"></button>` +
      `<button type="button" class="dd-clear" title="Use the default colour" aria-label="Use the default colour">×</button>` +
      `<input type="color" tabindex="-1" aria-hidden="true">` +
      `<div class="dd-swatches" popover role="dialog" aria-label="Colours">${rows}` +
      `<div class="dd-swatch-actions"><button type="button" class="dd-custom">Custom…</button>` +
      `<button type="button" class="dd-default">Default</button></div></div>`;
    const well = this.querySelector<HTMLButtonElement>('.dd-well')!;
    const picker = this.querySelector<HTMLInputElement>('input')!;
    const popup = this.querySelector<HTMLElement>('.dd-swatches')!;
    const pick = (color: string) => {
      popup.hidePopover();
      this.commit(color);
      this.render();
      well.focus();
    };
    well.addEventListener('click', () => popup.togglePopover());
    popup.addEventListener('toggle', (e) => {
      if ((e as ToggleEvent).newState !== 'open') return;
      this.place(popup, well);
      const current = popup.querySelector<HTMLElement>('[aria-pressed="true"]');
      (current ?? popup.querySelector<HTMLElement>('.dd-swatch'))?.focus();
    });
    popup.addEventListener('click', (e) => {
      const swatch = (e.target as HTMLElement).closest<HTMLElement>('.dd-swatch');
      if (swatch) pick(swatch.dataset.color!);
    });
    popup.addEventListener('keydown', (e) => this.arrow(e, popup));
    popup.querySelector('.dd-default')!.addEventListener('click', () => pick(''));
    popup.querySelector('.dd-custom')!.addEventListener('click', () => {
      popup.hidePopover();
      picker.value = normalizedColor(this.text) ?? '#ffffff';
      try {
        picker.showPicker();
      } catch {
        picker.click();
      }
    });
    picker.addEventListener('input', (e) => e.stopPropagation());
    picker.addEventListener('change', (e) => {
      e.stopPropagation();
      this.commit(picker.value);
      this.render();
    });
    this.querySelector('.dd-clear')!.addEventListener('click', () => {
      this.commit('');
      this.render();
    });
  }

  /** Opens the swatches under the colour button, or above it when there is no room below. */
  private place(popup: HTMLElement, well: HTMLElement): void {
    const at = well.getBoundingClientRect();
    const width = popup.offsetWidth;
    const height = popup.offsetHeight;
    const left = Math.max(4, Math.min(at.left, window.innerWidth - width - 4));
    const below = at.bottom + 4;
    const top = below + height <= window.innerHeight - 4 ? below : Math.max(4, at.top - height - 4);
    popup.style.left = `${left}px`;
    popup.style.top = `${top}px`;
  }

  /** Arrow keys move between swatches, row by row. */
  private arrow(e: KeyboardEvent, popup: HTMLElement): void {
    const step = { ArrowLeft: [0, -1], ArrowRight: [0, 1], ArrowUp: [-1, 0], ArrowDown: [1, 0] }[
      e.key
    ];
    const from = (e.target as HTMLElement).closest<HTMLElement>('.dd-swatch');
    if (!step || !from) return;
    e.preventDefault();
    const rows = [...popup.querySelectorAll('.dd-swatch-row')].map((r) => [
      ...r.querySelectorAll<HTMLElement>('.dd-swatch'),
    ]);
    const row = rows.findIndex((r) => r.includes(from));
    const col = rows[row]!.indexOf(from);
    const r = Math.max(0, Math.min(rows.length - 1, row + step[0]!));
    const c = Math.max(0, Math.min(rows[r]!.length - 1, col + step[1]!));
    rows[r]![c]!.focus();
  }

  protected render(): void {
    const well = this.querySelector<HTMLElement>('.dd-well')!;
    const color = normalizedColor(this.text);
    well.style.setProperty('--swatch', color ?? 'transparent');
    this.toggleAttribute('data-unset', !this.text);
    const title = this.text || 'default';
    well.title = title;
    well.setAttribute('aria-label', `${this.getAttribute('aria-label') ?? 'Colour'}: ${title}`);
    for (const s of this.querySelectorAll<HTMLElement>('.dd-swatch'))
      s.setAttribute('aria-pressed', String(s.dataset.color === color));
    this.querySelector<HTMLButtonElement>('.dd-default')!.disabled = !this.text;
    this.querySelector<HTMLElement>('.dd-clear')!.hidden = !this.text;
  }
}

// ── Style editor ─────────────────────────────────────────────────────────────────────────────

const ICONS: Record<string, string> = { B: '<b>B</b>', I: '<i>I</i>', U: '<u>U</u>' };

/** A pop-up's options, the entries of a group (`Crow's foot`) under its heading. */
function groupedOptions(
  choices: readonly { value: string; label: string; group: string }[],
): string {
  let out = '';
  let group = '';
  for (const c of choices) {
    if (c.group !== group) {
      if (group) out += '</optgroup>';
      group = c.group;
      if (group) out += `<optgroup label="${escape(group)}">`;
    }
    out += `<option value="${escape(c.value)}">${escape(c.label)}</option>`;
  }
  return group ? `${out}</optgroup>` : out;
}

function keyHtml(k: StyleKey): string {
  const id = `data-key="${k.key}"`;
  switch (k.control) {
    case 'color':
      return `<dd-color ${id} aria-label="${k.label}"></dd-color>`;
    case 'number':
      return `<input type="number" ${id} min="${k.min}" max="${k.max}" step="${k.step}" placeholder="${k.fallback ?? ''}">`;
    case 'flag':
      return `<input type="checkbox" ${id}>`;
    case 'choice':
      return `<select ${id}>${groupedOptions(k.choices ?? [])}</select>`;
    case 'fontStyle':
      return `<span class="dd-toggles" ${id}>${[
        ['B', 1, 'Bold'],
        ['I', 2, 'Italic'],
        ['U', 4, 'Underline'],
      ]
        .map(
          ([t, bit, name]) =>
            `<button type="button" data-bit="${bit}" aria-pressed="false" title="${name}" aria-label="${name}">${ICONS[t as string]}</button>`,
        )
        .join('')}</span>`;
  }
}

/**
 * `<dd-style-editor target="elements|relationships">`: an inline `style "k=v;…"` as a control
 * per key the renderer knows (colour wells, number fields, check boxes, pop-ups, bold, italic
 * and underline) and an Other field for the keys it has no control for. Every control writes one
 * key and leaves the rest of the text as it was.
 */
export class StyleEditor extends FieldControl {
  get target(): StyleTarget {
    return this.getAttribute('target') === 'relationships' ? 'relationships' : 'elements';
  }

  protected build(): void {
    this.classList.add('dd-style-editor');
    const sections: string[] = [];
    let section = '';
    for (const k of styleKeys(this.target)) {
      if (k.section !== section) {
        section = k.section;
        sections.push(`<div class="dd-section">${escape(section)}</div>`);
      }
      sections.push(`<span class="dd-key">${escape(k.label)}</span>${keyHtml(k)}`);
    }
    this.innerHTML = `<div class="dd-style-grid">${sections.join('')}
      <span class="dd-key">Other</span><input data-key="" class="dd-other" spellcheck="false" placeholder="shadow=1;whiteSpace=wrap"></div>
      <button type="button" class="dd-clear-style">Clear style</button>`;
    this.addEventListener('change', (e) => {
      if (e.target === this) return;
      e.stopPropagation();
      this.#changed(e.target as HTMLElement);
    });
    this.addEventListener('input', (e) => this.quiet(e));
    this.addEventListener('click', (e) => {
      const button = (e.target as HTMLElement).closest('button');
      if (!button) return;
      if (button.classList.contains('dd-clear-style')) {
        this.commit('');
        this.render();
      } else if (button.dataset.bit) {
        const bits =
          fontStyleBits(styleValue(this.text, 'fontStyle') ?? '') ^ Number(button.dataset.bit);
        this.#set('fontStyle', String(bits));
      }
    });
    this.addEventListener('keydown', (e) => {
      if (e.key !== 'Escape' || e.target === this) return;
      e.stopPropagation();
      this.render();
      (e.target as HTMLElement).blur();
    });
  }

  /** Sets one key as its control would; an empty value puts the control back to its default. */
  setKey(key: string, value: string): boolean {
    if (key === 'other') {
      this.commit(withOtherStyle(this.text, this.target, value));
      this.render();
      return true;
    }
    if (!styleKeys(this.target).some((k) => k.key === key)) return false;
    this.#set(key, value);
    return true;
  }

  #set(key: string, value: string): void {
    const spec = styleKeys(this.target).find((k) => k.key === key)!;
    this.commit(withStyleSetting(this.text, spec, value));
    this.render();
  }

  #changed(control: HTMLElement): void {
    const key = control.dataset.key;
    if (key === undefined) return;
    if (key === '') {
      this.commit(withOtherStyle(this.text, this.target, (control as HTMLInputElement).value));
      this.render();
      return;
    }
    if (control instanceof HTMLInputElement && control.type === 'checkbox') {
      this.#set(key, control.checked ? '1' : '0');
    } else {
      const value = (control as HTMLInputElement).value.trim();
      this.#set(key, value);
    }
  }

  protected render(): void {
    for (const k of styleKeys(this.target)) {
      const control = this.querySelector<HTMLElement>(`[data-key="${k.key}"]`)!;
      const value = styleControlValue(this.text, k) ?? '';
      switch (k.control) {
        case 'color':
          (control as ColorControl).value = value;
          break;
        case 'number':
          if (!typingIn(control)) (control as HTMLInputElement).value = value;
          break;
        case 'flag':
          (control as HTMLInputElement).checked = (value || k.fallback) === '1';
          break;
        case 'choice': {
          const select = control as HTMLSelectElement;
          // A value the pop-up does not list is shown as an extra entry, so it is not lost.
          select.querySelector('[data-extra]')?.remove();
          if (value && !k.choices?.some((c) => c.value === value))
            select.insertAdjacentHTML(
              'beforeend',
              `<option data-extra value="${escape(value)}">${escape(value)}</option>`,
            );
          select.value = value;
          break;
        }
        case 'fontStyle': {
          const bits = fontStyleBits(value);
          for (const b of control.querySelectorAll<HTMLElement>('[data-bit]'))
            b.setAttribute('aria-pressed', String((bits & Number(b.dataset.bit)) !== 0));
          break;
        }
      }
    }
    const other = this.querySelector<HTMLInputElement>('.dd-other')!;
    if (!typingIn(other)) other.value = otherStyle(this.text, this.target);
    this.querySelector<HTMLButtonElement>('.dd-clear-style')!.disabled = !this.text;
  }
}

// ── Tags ─────────────────────────────────────────────────────────────────────────────────────

/**
 * `<dd-tags>`: tags as chips with a remove button, and a field that adds one on Enter or a comma,
 * suggesting the tags the document uses. Its value is `external, legacy`.
 */
export class TagsControl extends FieldControl {
  #suggestions: readonly string[] = [];
  static #lists = 0;

  set suggestions(tags: readonly string[]) {
    this.#suggestions = tags;
    if (this.isConnected) this.#fillList();
  }

  protected build(): void {
    this.classList.add('dd-tags');
    const list = `dd-tag-list-${++TagsControl.#lists}`;
    this.innerHTML = `<span class="dd-chips"></span><input list="${list}" spellcheck="false" placeholder="${escape(
      this.getAttribute('placeholder') ?? 'add a tag',
    )}"><datalist id="${list}"></datalist>`;
    const input = this.querySelector('input')!;
    const add = () => {
      const typed = splitTags(input.value);
      input.value = '';
      if (typed.length)
        this.commit(
          joinTags([...splitTags(this.text), ...typed].filter((t, i, all) => all.indexOf(t) === i)),
        );
      this.render();
    };
    input.addEventListener('keydown', (e) => {
      if (e.key === 'Enter' || e.key === ',') {
        e.preventDefault();
        e.stopPropagation();
        add();
      } else if (e.key === 'Backspace' && !input.value) {
        this.commit(joinTags(splitTags(this.text).slice(0, -1)));
        this.render();
      } else if (e.key === 'Escape') {
        e.stopPropagation();
        input.value = '';
        input.blur();
      }
    });
    // Picking a suggestion adds it at once.
    input.addEventListener('input', (e) => {
      e.stopPropagation();
      if (
        (e as InputEvent).inputType === undefined ||
        (e as InputEvent).inputType === 'insertReplacementText'
      )
        if (this.#suggestions.includes(input.value)) add();
    });
    input.addEventListener('change', (e) => {
      e.stopPropagation();
      add();
    });
    this.addEventListener('click', (e) => {
      const remove = (e.target as HTMLElement).closest<HTMLElement>('[data-remove]');
      if (remove) {
        this.commit(joinTags(splitTags(this.text).filter((t) => t !== remove.dataset.remove)));
        this.render();
      } else if (e.target === this) {
        input.focus();
      }
    });
    this.#fillList();
  }

  #fillList(): void {
    const list = this.querySelector('datalist');
    if (list)
      list.innerHTML = this.#suggestions.map((t) => `<option value="${escape(t)}">`).join('');
  }

  protected render(): void {
    this.querySelector('.dd-chips')!.innerHTML = splitTags(this.text)
      .map(
        (t) =>
          `<span class="dd-chip">${escape(t)}<button type="button" data-remove="${escape(t)}" title="Remove ${escape(t)}" aria-label="Remove ${escape(t)}">×</button></span>`,
      )
      .join('');
  }
}

// ── Properties ───────────────────────────────────────────────────────────────────────────────

/**
 * `<dd-props>`: the Properties field (`key=value` per line) as a key/value table with + and −.
 * A row without a key is not written, so a new row counts once its key is typed.
 */
export class PropertiesControl extends FieldControl {
  protected build(): void {
    this.classList.add('dd-props');
    this.innerHTML = `<div class="dd-rows" role="table"></div>
      <button type="button" class="dd-add" title="Add a property" aria-label="Add a property">+</button>`;
    this.addEventListener('input', (e) => this.quiet(e));
    this.addEventListener('change', (e) => {
      if (e.target === this) return;
      e.stopPropagation();
      this.commit(propertyText(this.#rows()));
    });
    this.addEventListener('keydown', (e) => {
      if (e.target === this) return;
      if (e.key === 'Enter') {
        e.preventDefault();
        e.stopPropagation();
        this.commit(propertyText(this.#rows()));
        (e.target as HTMLElement).blur();
      } else if (e.key === 'Escape') {
        e.stopPropagation();
        (e.target as HTMLElement).blur();
        this.render();
      }
    });
    this.addEventListener('click', (e) => {
      const button = (e.target as HTMLElement).closest('button');
      if (!button) return;
      if (button.classList.contains('dd-add')) {
        this.querySelector('.dd-rows')!.insertAdjacentHTML('beforeend', this.#rowHtml('', ''));
        this.querySelector<HTMLInputElement>('.dd-row:last-child input')!.focus();
      } else if (button.classList.contains('dd-remove')) {
        button.closest('.dd-row')!.remove();
        this.commit(propertyText(this.#rows()));
      }
    });
  }

  #rowHtml(key: string, value: string): string {
    return `<div class="dd-row" role="row"><input class="dd-prop-key" value="${escape(key)}" placeholder="key" spellcheck="false" aria-label="Key"><input class="dd-prop-value" value="${escape(value)}" placeholder="value" aria-label="Value"><button type="button" class="dd-remove" title="Remove this property" aria-label="Remove this property">−</button></div>`;
  }

  #rows(): [string, string][] {
    return [...this.querySelectorAll('.dd-row')].map((row) => {
      const [k, v] = row.querySelectorAll('input');
      return [k!.value, v!.value];
    });
  }

  protected render(): void {
    if (typingIn(this)) return;
    this.querySelector('.dd-rows')!.innerHTML = propertyRows(this.text)
      .map(([k, v]) => this.#rowHtml(k, v))
      .join('');
  }
}

// ── Arrow ────────────────────────────────────────────────────────────────────────────────────

const ARROW_ICONS: Readonly<Record<Arrow, { title: string; svg: string }>> = {
  '->': {
    title: 'Solid',
    svg: '<line x1="3" y1="8" x2="27" y2="8"/><path d="M22 4 L28 8 L22 12"/>',
  },
  '-->': {
    title: 'Dashed',
    svg: '<line x1="3" y1="8" x2="27" y2="8" stroke-dasharray="4 3"/><path d="M22 4 L28 8 L22 12"/>',
  },
  '<->': {
    title: 'Both ends',
    svg: '<line x1="3" y1="8" x2="27" y2="8"/><path d="M8 4 L2 8 L8 12"/><path d="M22 4 L28 8 L22 12"/>',
  },
};

/** `<dd-arrow>`: the arrow of a relationship as three buttons that draw it. */
export class ArrowControl extends FieldControl {
  protected build(): void {
    this.classList.add('dd-arrow');
    this.setAttribute('role', 'radiogroup');
    this.innerHTML = (Object.keys(ARROW_ICONS) as Arrow[])
      .map(
        (a) =>
          `<button type="button" role="radio" data-arrow="${escape(a)}" title="${a}  ${ARROW_ICONS[a].title}" aria-label="${ARROW_ICONS[a].title}"><svg viewBox="0 0 30 16" width="30" height="16" fill="none" stroke="currentColor" stroke-width="1.5">${ARROW_ICONS[a].svg}</svg></button>`,
      )
      .join('');
    this.addEventListener('click', (e) => {
      const button = (e.target as HTMLElement).closest<HTMLElement>('[data-arrow]');
      if (!button) return;
      this.commit(button.dataset.arrow!);
      this.render();
    });
  }

  protected render(): void {
    for (const b of this.querySelectorAll<HTMLElement>('[data-arrow]'))
      b.setAttribute('aria-checked', String(b.dataset.arrow === this.text));
  }
}

customElements.define('dd-color', ColorControl);
customElements.define('dd-style-editor', StyleEditor);
customElements.define('dd-tags', TagsControl);
customElements.define('dd-props', PropertiesControl);
customElements.define('dd-arrow', ArrowControl);

// ── Theme and styles panel ───────────────────────────────────────────────────────────────────

let lists = 0;

/**
 * `<dd-style-blocks>`: the Style blocks field (`selector: k=v;…` per line) as a list of blocks,
 * each a selector picker, a remove button and, when opened, the visual style editor.
 */
export class StyleBlocksControl extends FieldControl {
  #rows: BlockRow[] = [];
  #open = new Set<number>();
  #selectors: readonly string[] = [];
  #list = `dd-selectors-${++lists}`;

  set selectors(selectors: readonly string[]) {
    this.#selectors = selectors;
    const list = this.querySelector('datalist');
    if (list) list.innerHTML = selectors.map((s) => `<option value="${escape(s)}">`).join('');
  }

  protected build(): void {
    this.classList.add('dd-blocks');
    this.innerHTML = `<datalist id="${this.#list}"></datalist><div class="dd-block-list"></div>
      <button type="button" class="dd-add-block">Add style block</button>`;
    this.selectors = this.#selectors;
    this.addEventListener('input', (e) => this.quiet(e));
    this.addEventListener('change', (e) => {
      if (e.target === this) return;
      e.stopPropagation();
      const target = e.target as HTMLElement;
      const block = target.closest<HTMLElement>('.dd-block');
      if (!block) return;
      const i = Number(block.dataset.index);
      const row = this.#rows[i]!;
      if (target.classList.contains('dd-selector')) {
        const selector = (target as HTMLInputElement).value.trim();
        if (selector === row.selector) return;
        const retarget = selectorTarget(selector) !== selectorTarget(row.selector);
        this.#rows[i] = { ...row, selector };
        if (retarget && this.#open.has(i)) this.#build();
      } else if (target instanceof StyleEditor) {
        this.#rows[i] = { ...row, style: target.value };
        block.querySelector('.dd-block-summary')!.textContent = target.value || 'no keys yet';
      }
      this.commit(blockText(this.#rows));
    });
    this.addEventListener('keydown', (e) => {
      const input = e.target as HTMLInputElement;
      if (!input.classList?.contains('dd-selector')) return;
      if (e.key === 'Enter') {
        e.preventDefault();
        e.stopPropagation();
        input.blur();
      } else if (e.key === 'Escape') {
        e.stopPropagation();
        const block = input.closest<HTMLElement>('.dd-block')!;
        input.value = this.#rows[Number(block.dataset.index)]!.selector;
        input.blur();
      }
    });
    this.addEventListener('click', (e) => {
      const button = (e.target as HTMLElement).closest('button');
      if (!button) return;
      const block = button.closest<HTMLElement>('.dd-block');
      const i = Number(block?.dataset.index);
      if (button.classList.contains('dd-add-block')) {
        this.#rows.push({ selector: '', style: '' });
        this.#open.add(this.#rows.length - 1);
        this.#build();
        this.querySelector<HTMLInputElement>('.dd-block:last-child .dd-selector')!.focus();
      } else if (button.classList.contains('dd-toggle')) {
        if (this.#open.has(i)) this.#open.delete(i);
        else this.#open.add(i);
        this.#build();
      } else if (button.classList.contains('dd-remove')) {
        this.#rows.splice(i, 1);
        this.#open = new Set(
          [...this.#open].filter((o) => o !== i).map((o) => (o > i ? o - 1 : o)),
        );
        this.#build();
        this.commit(blockText(this.#rows));
      }
    });
  }

  protected render(): void {
    if (typingIn(this)) return;
    const rows = blockRows(this.text);
    const sameShape =
      rows.length === this.#rows.length &&
      rows.every((r, i) => r.selector === this.#rows[i]!.selector) &&
      this.querySelectorAll('.dd-block').length === rows.length;
    // Rows written but without a selector (a block just added) stay until they get one.
    if (!sameShape && this.#rows.some((r) => !r.selector.trim())) return;
    this.#rows = rows;
    if (!sameShape) {
      this.#build();
      return;
    }
    this.querySelectorAll<HTMLElement>('.dd-block').forEach((block, i) => {
      const style = this.#rows[i]!.style;
      block.querySelector('.dd-block-summary')!.textContent = style || 'no keys yet';
      const editor = block.querySelector<StyleEditor>('dd-style-editor');
      if (editor) editor.value = style;
    });
  }

  #build(): void {
    for (const o of [...this.#open]) if (o >= this.#rows.length) this.#open.delete(o);
    this.querySelector('.dd-block-list')!.innerHTML = this.#rows
      .map((r, i) => {
        const open = this.#open.has(i);
        return `<div class="dd-block" data-index="${i}">
          <div class="dd-block-head"><button type="button" class="dd-toggle" aria-expanded="${open}" title="Edit this block's keys" aria-label="Edit this block's keys">${open ? '▾' : '▸'}</button><input class="dd-selector" list="${this.#list}" value="${escape(r.selector)}" spellcheck="false" placeholder='container, tag "external", relationship' aria-label="Selector"><button type="button" class="dd-remove" title="Remove this style block" aria-label="Remove this style block">−</button></div>
          ${open ? `<dd-style-editor target="${selectorTarget(r.selector)}"></dd-style-editor>` : `<code class="dd-block-summary">${escape(r.style || 'no keys yet')}</code>`}
        </div>`;
      })
      .join('');
    this.querySelectorAll<HTMLElement>('.dd-block').forEach((block, i) => {
      const editor = block.querySelector<StyleEditor>('dd-style-editor');
      if (editor) editor.value = this.#rows[i]!.style;
      else return;
      // The summary sits beside an open editor too, hidden, so updates have one place to go.
      editor.insertAdjacentHTML('beforebegin', '<code class="dd-block-summary" hidden></code>');
    });
  }
}

/** The keys a declared theme takes besides its base and node colours (dd::theme_keys). */
export const THEME_KEYS: readonly string[] = [
  'background',
  'fontFamily',
  'fontSize',
  'smallFontSize',
  'edge',
  'edgeText',
  'boundary',
  'boundaryText',
  'surface',
  'dividerFill',
  'labelText',
];

/**
 * `<dd-theme-keys>`: a declared theme's keys (`key=value` per line), a colour well per colour key
 * and a text field for the font keys. Keys without a control are kept.
 */
export class ThemeKeysControl extends FieldControl {
  protected build(): void {
    this.classList.add('dd-theme-keys');
    this.innerHTML = `<div class="dd-style-grid">${THEME_KEYS.map(
      (k) =>
        `<span class="dd-key">${k}</span>${
          isColorKey(k)
            ? `<dd-color data-key="${k}" aria-label="${k}"></dd-color>`
            : `<input data-key="${k}" placeholder="the base theme's" spellcheck="false">`
        }`,
    ).join('')}</div>`;
    this.addEventListener('input', (e) => this.quiet(e));
    this.addEventListener('change', (e) => {
      if (e.target === this) return;
      e.stopPropagation();
      const control = e.target as HTMLElement & { value: string };
      const key = control.dataset.key;
      if (!key) return;
      const rows = propertyRows(this.text) as [string, string][];
      const value = control.value.trim();
      const at = rows.findIndex(([k]) => k === key);
      if (!value) {
        if (at >= 0) rows.splice(at, 1);
      } else if (at >= 0) {
        rows[at] = [key, value];
      } else {
        rows.push([key, value]);
      }
      this.commit(propertyText(rows));
    });
    this.addEventListener('keydown', (e) => {
      if (e.target === this || !(e.target instanceof HTMLInputElement)) return;
      if (e.key === 'Enter') {
        e.preventDefault();
        e.stopPropagation();
        e.target.blur();
      } else if (e.key === 'Escape') {
        e.stopPropagation();
        e.target.blur();
        this.render();
      }
    });
  }

  protected render(): void {
    const rows = propertyRows(this.text);
    for (const k of THEME_KEYS) {
      const control = this.querySelector<HTMLElement & { value: string }>(`[data-key="${k}"]`)!;
      if (control instanceof HTMLInputElement && typingIn(control)) continue;
      control.value = rows.find(([key]) => key === k)?.[1] ?? '';
    }
  }
}

const NODE_KEYS = [
  ['fill', 'Fill'],
  ['stroke', 'Line'],
  ['text', 'Text'],
] as const;

/**
 * `<dd-theme-nodes>`: a declared theme's node colours (`kind: fill=…;stroke=…;text=…` per line),
 * a row per kind with fill, line and text wells, and a kind picker to add one.
 */
export class ThemeNodesControl extends FieldControl {
  #rows: BlockRow[] = [];
  #list = `dd-node-kinds-${++lists}`;

  set kinds(kinds: readonly string[]) {
    const list = this.querySelector('datalist');
    if (list) list.innerHTML = kinds.map((k) => `<option value="${escape(k)}">`).join('');
    this.#kinds = kinds;
  }

  #kinds: readonly string[] = [];

  /** Drops kinds added but not given a colour yet, as when another theme is shown. */
  forget(): void {
    this.#rows = this.#rows.filter((r) => r.style);
  }

  protected build(): void {
    this.classList.add('dd-theme-nodes');
    this.innerHTML = `<datalist id="${this.#list}"></datalist><div class="dd-node-list"></div>
      <div class="dd-node-add"><input list="${this.#list}" placeholder="kind" spellcheck="false" aria-label="Kind to add"><button type="button" class="dd-add-node">Add</button></div>`;
    this.kinds = this.#kinds;
    const kind = this.querySelector<HTMLInputElement>('.dd-node-add input')!;
    const add = () => {
      const name = kind.value.trim();
      kind.value = '';
      if (!name || this.#rows.some((r) => r.selector === name)) return;
      this.#rows.push({ selector: name, style: '' });
      this.#build();
    };
    this.addEventListener('input', (e) => this.quiet(e));
    this.addEventListener('change', (e) => {
      if (e.target === this) return;
      e.stopPropagation();
      const color = e.target as HTMLElement;
      if (!(color instanceof ColorControl)) return;
      const i = Number(color.closest<HTMLElement>('.dd-node')!.dataset.index);
      const row = this.#rows[i]!;
      this.#rows[i] = {
        ...row,
        style: withStyleValue(row.style, color.dataset.key!, color.value || undefined),
      };
      this.commit(blockText(this.#rows));
    });
    kind.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') {
        e.preventDefault();
        e.stopPropagation();
        add();
      } else if (e.key === 'Escape') {
        e.stopPropagation();
        kind.value = '';
        kind.blur();
      }
    });
    this.addEventListener('click', (e) => {
      const button = (e.target as HTMLElement).closest('button');
      if (button?.classList.contains('dd-add-node')) add();
      else if (button?.classList.contains('dd-remove')) {
        this.#rows.splice(Number(button.closest<HTMLElement>('.dd-node')!.dataset.index), 1);
        this.#build();
        this.commit(blockText(this.#rows));
      }
    });
  }

  protected render(): void {
    const rows = blockRows(this.text);
    const sameShape =
      rows.length === this.#rows.length &&
      rows.every((r, i) => r.selector === this.#rows[i]!.selector);
    // A kind just added has no colours yet, so the text does not have it: keep it shown.
    const pending = this.#rows.filter(
      (r) => !r.style && !rows.some((t) => t.selector === r.selector),
    );
    this.#rows = [...rows, ...pending];
    if (!sameShape || !this.querySelector('.dd-node')) this.#build();
    else this.#show();
  }

  #build(): void {
    this.querySelector('.dd-node-list')!.innerHTML = this.#rows
      .map(
        (r, i) =>
          `<div class="dd-node" data-index="${i}"><span class="dd-node-kind">${escape(r.selector)}</span>${NODE_KEYS.map(
            ([key, label]) =>
              `<span class="dd-key">${label}</span><dd-color data-key="${key}" aria-label="${escape(`${r.selector} ${label}`)}"></dd-color>`,
          ).join(
            '',
          )}<button type="button" class="dd-remove" title="Remove this kind's colours" aria-label="Remove this kind's colours">−</button></div>`,
      )
      .join('');
    this.#show();
  }

  #show(): void {
    this.querySelectorAll<HTMLElement>('.dd-node').forEach((node, i) => {
      for (const [key] of NODE_KEYS)
        node.querySelector<ColorControl>(`[data-key="${key}"]`)!.value =
          styleValue(this.#rows[i]!.style, key) ?? '';
    });
  }
}

customElements.define('dd-style-blocks', StyleBlocksControl);
customElements.define('dd-theme-keys', ThemeKeysControl);
customElements.define('dd-theme-nodes', ThemeNodesControl);
