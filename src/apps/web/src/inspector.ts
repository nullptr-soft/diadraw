import type { Element, FieldProblems, Relationship } from '@diadraw/wasm';
import type { ElementFields } from './element-fields';
import { applyFields, fieldsOf, localId } from './element-fields';
import type { Choice, FieldChoices } from './field-editors';
import { fieldChoices } from './field-editors';
import type { TagsControl } from './controls';
import { typingIn } from './controls';
import type { RelationshipFields } from './relationship-fields';
import {
  applyRelationshipFields,
  relationshipFieldsOf,
  isName,
  relationshipTitle,
} from './relationship-fields';

interface FieldSpec<K extends string> {
  key: K;
  label: string;
  /** The control: a line of text unless given. */
  control?: 'area' | 'style' | 'tags' | 'props' | 'arrow';
  list?: string;
  hint?: string;
}

const FIELDS: readonly FieldSpec<keyof ElementFields>[] = [
  { key: 'id', label: 'Id' },
  { key: 'kind', label: 'Kind', list: 'dd-kinds' },
  { key: 'name', label: 'Name' },
  { key: 'technology', label: 'Tech', list: 'dd-technologies' },
  { key: 'shape', label: 'Shape', list: 'dd-shapes', hint: 'from the kind' },
  { key: 'tags', label: 'Tags', control: 'tags' },
  { key: 'description', label: 'Description', control: 'area' },
  { key: 'style', label: 'Style', control: 'style' },
  { key: 'props', label: 'Properties', control: 'props' },
];

const RELATIONSHIP_FIELDS: readonly FieldSpec<keyof RelationshipFields>[] = [
  { key: 'label', label: 'Label' },
  { key: 'technology', label: 'Tech', list: 'dd-technologies' },
  { key: 'arrow', label: 'Arrow', control: 'arrow' },
  { key: 'name', label: 'Name (optional id)' },
  { key: 'tags', label: 'Tags', control: 'tags' },
  { key: 'style', label: 'Style', control: 'style' },
  { key: 'props', label: 'Properties', control: 'props' },
];

/** A field's control: an <input>, a <textarea> or one of controls.ts, all with a text `value`. */
type Input = HTMLElement & { value: string };

const escape = (text: string): string =>
  text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');

function fieldHtml(panel: 'element' | 'relationship', f: FieldSpec<string>): string {
  const field = `data-field="${f.key}"`;
  const target = panel === 'relationship' ? 'relationships' : 'elements';
  switch (f.control) {
    case 'arrow':
      return `<div class="field">${f.label}<dd-arrow ${field}></dd-arrow></div>`;
    case 'tags':
      return `<div class="field">${f.label}<dd-tags ${field}></dd-tags></div>`;
    case 'props':
      return `<div class="field">${f.label}<dd-props ${field}></dd-props></div>`;
    case 'style':
      return `<details class="field style-field" open><summary>${f.label}<code class="style-summary"></code></summary><dd-style-editor ${field} target="${target}"></dd-style-editor></details>`;
    case 'area':
      return `<label>${f.label}<textarea ${field} rows="2" class="grow"></textarea></label>`;
    default:
      return `<label>${f.label}<input ${field} ${f.list ? `list="${f.list}"` : ''} placeholder="${f.hint ?? ''}" spellcheck="false"></label>`;
  }
}

/** `<option>`s of a datalist; a library's entries show their label and alias. */
const options = (choices: readonly Choice[]): string =>
  choices
    .map(
      (c) =>
        `<option value="${escape(c.value)}"${
          c.group && c.label !== c.value ? ` label="${escape(`${c.label} (${c.group})`)}"` : ''
        }>`,
    )
    .join('');

/**
 * `<dd-inspector>`: the properties of the selected element or relationship, as fields. Committing
 * a field (Enter, leaving it after a change, or a change in one of its controls) fires
 * `dd-update` with the whole updated element, the Shared styles field as typed and, when the Id
 * field changed, `rename` (the new last part of the id), or
 * `dd-update-relationship` with the updated relationship and its name; Escape puts the field
 * back. Set `choices` and `sharedStyles`, then `element`, or set `relationship` to show one;
 * undefined hides the panel.
 *
 * Kind, Tech and Shape suggest what the document and its libraries have, Tags are chips, Style is
 * the visual style editor (folded, with the style text as its summary), Properties a key/value
 * table, and Description grows with its text.
 *
 * While a field is typed in, `checkElement` or `checkRelationship` says which fields the edit
 * would refuse; those get a red outline (`aria-invalid`) and the reason as their tooltip.
 */
export class ElementInspector extends HTMLElement {
  #element: Element | undefined;
  #relationship: Relationship | undefined;
  #choices: FieldChoices = fieldChoices(undefined);
  /** The `style` blocks that apply to the element, one `selector: key=value;…` per line. */
  sharedStyles = '';
  /**
   * Which fields an edit of the shown element would refuse, and why (see
   * `Session.elementProblems`): `element` with the typed fields applied, `id` and `sharedStyles`
   * as typed. Keys are field names (`id`, `kind`, `sharedStyles`, …).
   */
  checkElement:
    ((element: Element, typed: { id: string; sharedStyles: string }) => FieldProblems) | undefined;
  /** The same for the shown relationship, with its `name` as typed. */
  checkRelationship:
    ((relationship: Relationship, name: string | undefined) => FieldProblems) | undefined;

  connectedCallback(): void {
    if (this.childElementCount) return;
    this.classList.add('inspector');
    this.setAttribute('role', 'complementary');
    this.setAttribute('aria-label', 'Properties');
    this.innerHTML = `
      <div class="inspector-head"><span class="inspector-id"></span></div>
      <datalist id="dd-kinds"></datalist>
      <datalist id="dd-shapes"></datalist>
      <datalist id="dd-technologies"></datalist>
      <div class="inspector-fields" data-panel="element">${FIELDS.map((f) =>
        fieldHtml('element', f),
      ).join('')}
        <label>Shared styles<textarea data-field="sharedStyles" rows="2" class="grow" spellcheck="false"
          placeholder="container: fill=#438dd5 (one block per line)"></textarea></label></div>
      <div class="inspector-fields" data-panel="relationship" hidden>${RELATIONSHIP_FIELDS.map(
        (f) => fieldHtml('relationship', f),
      ).join('')}</div>`;
    this.#fillChoices();
    this.addEventListener('change', (e) => {
      if ((e.target as HTMLElement).dataset.field) this.#commit();
      // A refused commit keeps the typed text: it stays marked.
      this.#check();
    });
    this.addEventListener('input', (e) => {
      const area = e.target as HTMLElement;
      if (area instanceof HTMLTextAreaElement) grow(area);
      this.#check();
    });
    this.addEventListener('keydown', (e) => {
      const field = e.target as Input;
      if (!field.dataset.field) return;
      if (e.key === 'Escape') {
        this.#fill(true);
        field.blur();
      } else if (e.key === 'Enter' && (field.tagName === 'INPUT' || e.metaKey || e.ctrlKey)) {
        e.preventDefault();
        this.#commit();
      }
    });
    this.hidden = true;
  }

  /** What Kind, Shape, Tech and Tags suggest. */
  set choices(choices: FieldChoices) {
    this.#choices = choices;
    if (this.childElementCount) this.#fillChoices();
  }

  #fillChoices(): void {
    const set = (id: string, html: string) => {
      const list = this.querySelector(`#${id}`)!;
      if (list.innerHTML !== html) list.innerHTML = html;
    };
    set('dd-kinds', options(this.#choices.kinds));
    set('dd-shapes', options(this.#choices.shapes));
    set(
      'dd-technologies',
      options(this.#choices.technologies.map((t) => ({ value: t, label: t, group: '' }))),
    );
    for (const tags of this.querySelectorAll<TagsControl>('dd-tags'))
      tags.suggestions = this.#choices.tags;
  }

  get element(): Element | undefined {
    return this.#element;
  }

  set element(element: Element | undefined) {
    const same = element !== undefined && element.id === this.#element?.id;
    this.#element = element;
    if (element) this.#relationship = undefined;
    this.#show(same);
  }

  get relationship(): Relationship | undefined {
    return this.#relationship;
  }

  set relationship(relationship: Relationship | undefined) {
    const same = relationship !== undefined && relationship.id === this.#relationship?.id;
    this.#relationship = relationship;
    if (relationship) this.#element = undefined;
    this.#show(same);
  }

  #show(same: boolean): void {
    this.hidden = !this.#element && !this.#relationship;
    this.#panel('element').hidden = !this.#element;
    this.#panel('relationship').hidden = !this.#relationship;
    if (!this.hidden) this.#fill(!same);
  }

  #panel(name: 'element' | 'relationship'): HTMLElement {
    return this.querySelector(`[data-panel="${name}"]`)!;
  }

  #field(panel: 'element' | 'relationship', key: string): Input {
    return this.#panel(panel).querySelector(`[data-field="${key}"]`)!;
  }

  /** Shows the item's values; a field being typed in keeps its text unless `all`. */
  #fill(all: boolean): void {
    const put = <K extends string>(
      panel: 'element' | 'relationship',
      specs: readonly FieldSpec<K>[],
      fields: Readonly<Record<K, string>>,
    ) => {
      for (const f of specs) {
        const input = this.#field(panel, f.key);
        if (!all && typingIn(input)) continue;
        input.value = fields[f.key];
        if (input instanceof HTMLTextAreaElement) grow(input);
        if (f.control === 'style') {
          const summary = input.closest('details')!.querySelector('.style-summary')!;
          summary.textContent = fields[f.key] || 'none';
        }
      }
    };
    const id = this.querySelector('.inspector-id')!;
    if (this.#element) {
      id.textContent = this.#element.id;
      put('element', FIELDS, fieldsOf(this.#element));
      const shared = this.#field('element', 'sharedStyles');
      if (all || !typingIn(shared)) shared.value = this.sharedStyles;
      grow(shared as HTMLTextAreaElement);
    } else if (this.#relationship) {
      id.textContent = relationshipTitle(this.#relationship);
      put('relationship', RELATIONSHIP_FIELDS, relationshipFieldsOf(this.#relationship));
      // Without a name, the field shows the id the core derives from the ends, greyed.
      (this.#field('relationship', 'name') as HTMLInputElement).placeholder = isName(
        this.#relationship.id,
      )
        ? ''
        : this.#relationship.id;
    }
    this.#check();
  }

  #typed<K extends string>(panel: 'element' | 'relationship', specs: readonly FieldSpec<K>[]) {
    return Object.fromEntries(specs.map((f) => [f.key, this.#field(panel, f.key).value]));
  }

  /** Marks the fields of the shown panel that the edit as typed would refuse. */
  #check(): void {
    let panel: 'element' | 'relationship';
    let problems: FieldProblems = {};
    if (this.#element) {
      panel = 'element';
      const typed = this.#typed('element', FIELDS) as unknown as ElementFields;
      problems =
        this.checkElement?.(applyFields(this.#element, typed), {
          id: typed.id,
          sharedStyles: this.#field('element', 'sharedStyles').value,
        }) ?? {};
    } else if (this.#relationship) {
      panel = 'relationship';
      const typed = this.#typed(
        'relationship',
        RELATIONSHIP_FIELDS,
      ) as unknown as RelationshipFields;
      const updated = applyRelationshipFields(this.#relationship, typed);
      problems = this.checkRelationship?.(updated.relationship, updated.name) ?? {};
    } else {
      return;
    }
    for (const input of this.#panel(panel).querySelectorAll<HTMLElement>('[data-field]')) {
      const problem = problems[input.dataset.field!];
      if (problem === undefined) {
        if (!input.hasAttribute('aria-invalid')) continue;
        input.removeAttribute('aria-invalid');
        input.removeAttribute('title');
      } else {
        input.setAttribute('aria-invalid', 'true');
        input.title = problem;
      }
    }
  }

  #commit(): void {
    const element = this.#element;
    if (element) {
      const typed = this.#typed('element', FIELDS) as unknown as ElementFields;
      const updated = applyFields(element, typed);
      const sharedStyles = this.#field('element', 'sharedStyles').value;
      const id = typed.id.trim();
      const rename = id === localId(element) ? undefined : id;
      if (
        JSON.stringify(updated) === JSON.stringify(element) &&
        sharedStyles === this.sharedStyles &&
        rename === undefined
      )
        return;
      this.dispatchEvent(
        new CustomEvent('dd-update', {
          detail: { element: updated, sharedStyles, rename },
          bubbles: true,
        }),
      );
      return;
    }
    const relationship = this.#relationship;
    if (!relationship) return;
    const typed = this.#typed('relationship', RELATIONSHIP_FIELDS) as unknown as RelationshipFields;
    const updated = applyRelationshipFields(relationship, typed);
    const was = relationshipFieldsOf(relationship).name || undefined;
    if (
      JSON.stringify(updated.relationship) === JSON.stringify(relationship) &&
      updated.name === was
    )
      return;
    this.dispatchEvent(
      new CustomEvent('dd-update-relationship', { detail: updated, bubbles: true }),
    );
  }
}

/** Fits a text area to its text, up to a limit (CSS `field-sizing` where the browser has it). */
function grow(area: HTMLTextAreaElement): void {
  if (CSS.supports?.('field-sizing', 'content')) return;
  area.style.height = 'auto';
  area.style.height = `${Math.min(area.scrollHeight + 2, 180)}px`;
}

customElements.define('dd-inspector', ElementInspector);
