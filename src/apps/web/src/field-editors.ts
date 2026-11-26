import type { Model, PaletteItem } from '@diadraw/wasm';
import { BUILT_IN_THEMES } from './style-fields';

// What the inspector's controls need beyond the fields themselves: the choices its pickers offer
// and the keys of the visual style editor. The same rules as the native apps'
// host/field_editors.cpp. The fields stay text, so a control writes the same string a person
// could have typed.

// ── Choices ──────────────────────────────────────────────────────────────────────────────────

/** One entry of a picker. */
export interface Choice {
  /** What the field is set to: `container`, `cylinder`, `aws.lambda`. */
  readonly value: string;
  /** What the picker shows; the value itself for built-in kinds and shapes. */
  readonly label: string;
  /** Built-in entries have none; a library's entries have its alias, to group them under. */
  readonly group: string;
}

export interface FieldChoices {
  /** The built-in kinds, then the libraries' (`alias.kind`), then kinds only the model uses. */
  readonly kinds: readonly Choice[];
  /** The base shapes, then the libraries' (`alias.shape`). */
  readonly shapes: readonly Choice[];
  /** Every tag used on an element or a relationship, sorted, without repeats. */
  readonly tags: readonly string[];
  /** Every `tech` used on an element or a relationship, sorted, without repeats. */
  readonly technologies: readonly string[];
  /** The built-in themes, then the ones the document declares (group `Declared`). */
  readonly themes: readonly Choice[];
}

/** The built-in element kinds, in the order a picker lists them. */
export const BUILTIN_KINDS: readonly string[] = [
  'person',
  'system',
  'container',
  'component',
  'database',
  'process',
  'decision',
  'start',
  'end',
  'note',
  'class',
  'object',
  'event',
  'task',
  'subprocess',
  'gateway',
  'data',
  'annotation',
  'group',
  'pool',
  'lane',
  'conversation',
];

/** The base shapes `shape` takes (docs/dsl.md). */
export const BASE_SHAPES: readonly string[] =
  'rect rounded ellipse circle rhombus triangle hexagon octagon cylinder parallelogram trapezoid document cloud actor person note card step arrow process stadium cube swimlane folder queue interface text image invisible frame module lifeline'.split(
    ' ',
  );

const plain = (values: readonly string[]): Choice[] =>
  values.map((v) => ({ value: v, label: v, group: '' }));

/** The choices for a document's inspector: built-in, its libraries' (`palette`) and its own. */
export function fieldChoices(
  model: Model | undefined,
  palette: readonly PaletteItem[] = [],
): FieldChoices {
  const kinds = plain(BUILTIN_KINDS);
  const shapes = plain(BASE_SHAPES);
  for (const item of palette) {
    const choice = { value: item.ref, label: item.label || item.id, group: item.alias };
    if (item.type === 'kind') kinds.push(choice);
    else if (item.type === 'shape' || item.type === 'composite') shapes.push(choice);
  }
  const tags = new Set<string>();
  const technologies = new Set<string>();
  const known = new Set(kinds.map((k) => k.value));
  for (const e of Object.values(model?.elements ?? {})) {
    for (const t of e.tags) tags.add(t);
    if (e.technology) technologies.add(e.technology);
    // A kind no entry has (a typo, or a library not loaded), so the picker still shows it.
    if (e.kind && !known.has(e.kind)) {
      known.add(e.kind);
      kinds.push({ value: e.kind, label: e.kind, group: 'Used' });
    }
  }
  for (const r of Object.values(model?.relationships ?? {})) {
    for (const t of r.tags) tags.add(t);
    if (r.technology) technologies.add(r.technology);
  }
  const themes = plain(BUILT_IN_THEMES);
  for (const t of model?.themes ?? [])
    if (!themes.some((c) => c.value === t.name))
      themes.push({ value: t.name, label: t.name, group: 'Declared' });
  const sorted = (set: Set<string>) => [...set].sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
  return { kinds, shapes, tags: sorted(tags), technologies: sorted(technologies), themes };
}

/** A Tags field (`external, legacy`) as a list, trimmed, without empty entries or repeats. */
export function splitTags(text: string): string[] {
  const out: string[] = [];
  for (const part of text.split(',')) {
    const tag = part.trim();
    if (tag && !out.includes(tag)) out.push(tag);
  }
  return out;
}

/** The other way: `external, legacy`. */
export const joinTags = (tags: readonly string[]): string =>
  tags
    .map((t) => t.trim())
    .filter(Boolean)
    .join(', ');

// ── Properties ───────────────────────────────────────────────────────────────────────────────

export type Row = readonly [key: string, value: string];

/**
 * A Properties field (one `key=value` per line) as rows of a key/value table, as the inspector
 * reads it: trimmed, a line without `=` a key with an empty value, lines without a key skipped,
 * a repeated key's later value winning in the first one's place.
 */
export function propertyRows(text: string): Row[] {
  const out: [string, string][] = [];
  for (const raw of text.split('\n')) {
    const line = raw.trim();
    const eq = line.indexOf('=');
    const key = (eq < 0 ? line : line.slice(0, eq)).trim();
    if (!key) continue;
    const value = eq < 0 ? '' : line.slice(eq + 1).trim();
    const at = out.find((r) => r[0] === key);
    if (at) at[1] = value;
    else out.push([key, value]);
  }
  return out;
}

/** The other way. Rows without a key are left out; `=` in a key becomes `_`, a line break a space. */
export function propertyText(rows: readonly Row[]): string {
  const clean = (s: string, key: boolean) => {
    const out = s.trim().replace(/[\r\n]/g, ' ');
    return key ? out.replace(/=/g, '_') : out;
  };
  return rows
    .map(([k, v]) => [clean(k, true), clean(v, false)] as const)
    .filter(([k]) => k)
    .map(([k, v]) => `${k}=${v}`)
    .join('\n');
}

// ── Style blocks and themes ──────────────────────────────────────────────────────────────────

/**
 * One line of the Style blocks field (`container: fill=#438dd5`) or of a theme's Node colours
 * (`person: fill=#08427b;text=#ffffff`): what comes before the first colon outside quotes, and
 * the `key=value;…` after it, both trimmed.
 */
export interface BlockRow {
  readonly selector: string;
  readonly style: string;
}

/** Empty lines are skipped; a line without a colon is all selector. */
export function blockRows(text: string): BlockRow[] {
  const out: BlockRow[] = [];
  for (const raw of text.split('\n')) {
    const line = raw.trim();
    if (!line) continue;
    let colon = -1;
    let quoted = false;
    for (let i = 0; i < line.length && colon < 0; i++) {
      if (line[i] === '\\' && quoted) i++;
      else if (line[i] === '"') quoted = !quoted;
      else if (line[i] === ':' && !quoted) colon = i;
    }
    out.push(
      colon < 0
        ? { selector: line, style: '' }
        : { selector: line.slice(0, colon).trim(), style: line.slice(colon + 1).trim() },
    );
  }
  return out;
}

/** The other way, one `selector: style` per line; rows without a selector are left out. */
export const blockText = (rows: readonly BlockRow[]): string =>
  rows
    .filter((r) => r.selector.trim())
    .map((r) => `${r.selector.trim()}: ${r.style.trim()}`)
    .join('\n');

export type StyleTarget = 'elements' | 'relationships';

/** Whether a style block's selector picks relationships (`relationship`, `relationship tag "…"`). */
export const selectorTarget = (selector: string): StyleTarget =>
  /^relationship(?:[ \t]|$)/.test(selector.trim()) ? 'relationships' : 'elements';

/** The selectors a block picker offers: `*`, `relationship`, every kind, and tag selectors. */
export function selectorChoices(choices: FieldChoices): string[] {
  const quoted = (tag: string) => `"${tag.replace(/["\\]/g, '\\$&')}"`;
  return [
    '*',
    'relationship',
    ...choices.kinds.map((k) => k.value),
    ...choices.tags.map((t) => `tag ${quoted(t)}`),
    ...choices.tags.map((t) => `relationship tag ${quoted(t)}`),
  ];
}

/** Whether a theme-wide key (`background`, `fontFamily`, …) holds a colour. */
export const isColorKey = (key: string): boolean =>
  key !== 'fontFamily' && key !== 'fontSize' && key !== 'smallFontSize';

// ── Style editor ─────────────────────────────────────────────────────────────────────────────

export type StyleControl = 'color' | 'number' | 'flag' | 'choice' | 'fontStyle';

/** One key the style editor has a control for. */
export interface StyleKey {
  /** `fill`, `strokeWidth`, `endArrow` */
  readonly key: string;
  readonly label: string;
  readonly control: StyleControl;
  /** For `choice`: the values, in order; the first is the default, which the key's absence means. */
  readonly choices?: readonly Choice[];
  /** For `number`: the range and the step. */
  readonly min?: number;
  readonly max?: number;
  readonly step?: number;
  /**
   * What the key's absence means, for `flag` (`0` or `1`) and as a hint for `number`. A control
   * set back to it removes the key, so the text stays as short as before.
   */
  readonly fallback?: string;
  /** The section the editor shows it under. */
  readonly section: string;
}

/**
 * The key of the Line style pop-up, which stands for two style keys: `dashed` and `dashPattern`.
 * Its values are an empty value (solid), `dashed` (the plain dashes) and the dash patterns.
 */
export const LINE_STYLE_KEY = 'lineStyle';
const PLAIN_DASHES = 'dashed';

const pick = (...pairs: [string, string][]): Choice[] =>
  pairs.map(([value, label]) => ({ value, label, group: '' }));

const grouped = (group: string, ...pairs: [string, string][]): Choice[] =>
  pairs.map(([value, label]) => ({ value, label, group }));

// draw.io's names, so a file it wrote shows its ends here; the group sections the pop-up. As the
// native apps' host/field_editors.cpp lists them.
const ARROWHEADS = [
  ...pick(['', 'Default'], ['none', 'None']),
  ...grouped(
    'Arrows',
    ['classic', 'Arrow'],
    ['block', 'Triangle'],
    ['open', 'Open arrow'],
    ['async', 'Half arrow'],
    ['openAsync', 'Open half arrow'],
    ['doubleBlock', 'Double triangle'],
  ),
  ...grouped(
    'Shapes',
    ['diamond', 'Diamond'],
    ['oval', 'Circle'],
    ['box', 'Square'],
    ['circlePlus', 'Circle with plus'],
    ['halfCircle', 'Socket'],
  ),
  ...grouped('Lines', ['baseDash', 'Bar'], ['dash', 'Slash'], ['cross', 'Cross']),
  ...grouped(
    "Crow's foot",
    ['ERone', 'One'],
    ['ERmandOne', 'Exactly one'],
    ['ERzeroToOne', 'Zero or one'],
    ['ERmany', 'Many'],
    ['ERoneToMany', 'One or many'],
    ['ERzeroToMany', 'Zero or many'],
  ),
];

// As the native apps' host/field_editors.cpp lists them.
const LINE_STYLES = pick(
  ['', 'Solid'],
  [PLAIN_DASHES, 'Dashed'],
  ['1 3', 'Dotted'],
  ['3 3', 'Short dashes'],
  ['12 6', 'Long dashes'],
  ['8 3 1 3', 'Dash and dot'],
  ['8 3 1 3 1 3', 'Dash and two dots'],
);

const ELEMENT_KEYS: readonly StyleKey[] = [
  { key: 'fill', label: 'Fill', control: 'color', section: 'Fill and line' },
  { key: 'stroke', label: 'Line', control: 'color', section: 'Fill and line' },
  {
    key: 'strokeWidth',
    label: 'Line width',
    control: 'number',
    min: 0,
    max: 20,
    step: 0.5,
    fallback: '1',
    section: 'Fill and line',
  },
  {
    key: LINE_STYLE_KEY,
    label: 'Line style',
    control: 'choice',
    choices: LINE_STYLES,
    section: 'Fill and line',
  },
  { key: 'rounded', label: 'Rounded', control: 'flag', fallback: '0', section: 'Fill and line' },
  {
    key: 'opacity',
    label: 'Opacity',
    control: 'number',
    min: 0,
    max: 100,
    step: 5,
    fallback: '100',
    section: 'Fill and line',
  },
  { key: 'fontColor', label: 'Text colour', control: 'color', section: 'Text' },
  {
    key: 'fontSize',
    label: 'Size',
    control: 'number',
    min: 6,
    max: 96,
    step: 1,
    fallback: '12',
    section: 'Text',
  },
  { key: 'fontStyle', label: 'Style', control: 'fontStyle', fallback: '0', section: 'Text' },
  {
    key: 'align',
    label: 'Align',
    control: 'choice',
    choices: pick(['', 'Default'], ['left', 'Left'], ['center', 'Centre'], ['right', 'Right']),
    section: 'Text',
  },
  {
    key: 'verticalAlign',
    label: 'Vertical',
    control: 'choice',
    choices: pick(['', 'Default'], ['top', 'Top'], ['middle', 'Middle'], ['bottom', 'Bottom']),
    section: 'Text',
  },
  {
    key: 'labelPosition',
    label: 'Label beside',
    control: 'choice',
    choices: pick(['', 'Inside'], ['left', 'Left'], ['right', 'Right']),
    section: 'Text',
  },
  {
    key: 'verticalLabelPosition',
    label: 'Label above/below',
    control: 'choice',
    choices: pick(['', 'Inside'], ['top', 'Above'], ['bottom', 'Below']),
    section: 'Text',
  },
];

const RELATIONSHIP_KEYS: readonly StyleKey[] = [
  { key: 'stroke', label: 'Line', control: 'color', section: 'Line' },
  {
    key: 'strokeWidth',
    label: 'Line width',
    control: 'number',
    min: 0,
    max: 20,
    step: 0.5,
    fallback: '1.25',
    section: 'Line',
  },
  {
    key: LINE_STYLE_KEY,
    label: 'Line style',
    control: 'choice',
    choices: LINE_STYLES,
    section: 'Line',
  },
  { key: 'double', label: 'Double line', control: 'flag', fallback: '0', section: 'Line' },
  {
    key: 'edgeStyle',
    label: 'Routing',
    control: 'choice',
    choices: pick(['', 'Default'], ['orthogonalEdgeStyle', 'Right angles']),
    section: 'Line',
  },
  { key: 'curved', label: 'Curved', control: 'flag', fallback: '0', section: 'Line' },
  { key: 'rounded', label: 'Rounded corners', control: 'flag', fallback: '0', section: 'Line' },
  { key: 'startArrow', label: 'Start', control: 'choice', choices: ARROWHEADS, section: 'Arrows' },
  { key: 'startFill', label: 'Filled start', control: 'flag', fallback: '1', section: 'Arrows' },
  { key: 'endArrow', label: 'End', control: 'choice', choices: ARROWHEADS, section: 'Arrows' },
  { key: 'endFill', label: 'Filled end', control: 'flag', fallback: '1', section: 'Arrows' },
  { key: 'fontColor', label: 'Text colour', control: 'color', section: 'Label' },
  {
    key: 'fontSize',
    label: 'Size',
    control: 'number',
    min: 6,
    max: 96,
    step: 1,
    fallback: '11',
    section: 'Label',
  },
];

/** The keys the editor shows for elements (and decorations) or for relationships. */
export const styleKeys = (target: StyleTarget): readonly StyleKey[] =>
  target === 'relationships' ? RELATIONSHIP_KEYS : ELEMENT_KEYS;

type Entry = [key: string, value: string];

/** A `key=value;…` style as ordered pairs, as the core reads it: a later repeat wins in place. */
export function styleEntries(style: string): Entry[] {
  const out: Entry[] = [];
  for (const part of style.split(';')) {
    if (!part) continue;
    const eq = part.indexOf('=');
    const key = eq < 0 ? part : part.slice(0, eq);
    const value = eq < 0 ? '' : part.slice(eq + 1);
    const at = out.find((e) => e[0] === key);
    if (at) at[1] = value;
    else out.push([key, value]);
  }
  return out;
}

export const styleText = (entries: readonly (readonly [string, string])[]): string =>
  entries.map(([k, v]) => (v === '' ? k : `${k}=${v}`)).join(';');

/** The value of `key` in `style`, or undefined. */
export const styleValue = (style: string, key: string): string | undefined =>
  styleEntries(style).find((e) => e[0] === key)?.[1];

/**
 * `style` with `key` set to `value` (in place when present, appended otherwise), or removed when
 * `value` is undefined. Every other entry stays as written.
 */
export function withStyleValue(style: string, key: string, value: string | undefined): string {
  const entries = styleEntries(style);
  const at = entries.findIndex((e) => e[0] === key);
  if (value === undefined) {
    if (at < 0) return style;
    entries.splice(at, 1);
  } else if (at >= 0) {
    if (entries[at]![1] === value) return style;
    entries[at]![1] = value;
  } else {
    entries.push([key, value]);
  }
  return styleText(entries);
}

/**
 * What the control for `key` shows for `style`: the key's value, or for the Line style pop-up the
 * value its `dashed` and `dashPattern` add up to; undefined when the key is absent (solid).
 */
export function styleControlValue(style: string, key: StyleKey): string | undefined {
  if (key.key !== LINE_STYLE_KEY) return styleValue(style, key.key);
  const entries = styleEntries(style);
  if (entries.find((e) => e[0] === 'dashed')?.[1] !== '1') return undefined;
  return entries.find((e) => e[0] === 'dashPattern')?.[1] || PLAIN_DASHES;
}

/** Whether the control for `key` edits the style key `styleKey`, so Other leaves it out. */
export const styleKeyCovers = (key: StyleKey, styleKey: string): boolean =>
  key.key === LINE_STYLE_KEY
    ? styleKey === 'dashed' || styleKey === 'dashPattern'
    : key.key === styleKey;

/**
 * What a control for `key` sets the key to: undefined when `value` is the key's fallback (or,
 * for a choice, its first value), so returning a control to its default removes the key.
 */
export function styleSetting(key: StyleKey, value: string): string | undefined {
  switch (key.control) {
    case 'choice':
      if (value === '' || value === key.choices?.[0]?.value) return undefined;
      break;
    case 'flag':
    case 'fontStyle':
      if (value === key.fallback) return undefined;
      break;
    case 'color':
    case 'number':
      if (value === '') return undefined;
      break;
  }
  return value;
}

/**
 * `style` with `key` set as its control sets it to `value` (see styleSetting). The Line style
 * pop-up sets `dashed` and `dashPattern` together: solid removes both, a pattern sets both.
 */
export function withStyleSetting(style: string, key: StyleKey, value: string): string {
  if (key.key !== LINE_STYLE_KEY) return withStyleValue(style, key.key, styleSetting(key, value));
  const dashed = value !== '';
  const patterned = dashed && value !== PLAIN_DASHES;
  const out = withStyleValue(style, 'dashed', dashed ? '1' : undefined);
  return withStyleValue(out, 'dashPattern', patterned ? value : undefined);
}

const known = (target: StyleTarget, key: string) =>
  styleKeys(target).some((k) => styleKeyCovers(k, key));

/** The entries of `style` the editor has no control for, as `key=value;…`, for its Other field. */
export const otherStyle = (style: string, target: StyleTarget): string =>
  styleText(styleEntries(style).filter(([k]) => !known(target, k)));

/**
 * `style` with those entries replaced by `other` (typed in the Other field); the entries with a
 * control stay. An entry typed in Other for a key with a control sets that key.
 */
export function withOtherStyle(style: string, target: StyleTarget, other: string): string {
  const typed: Entry[] = styleEntries(other)
    .map(([k, v]): Entry => [k.trim(), v.trim()])
    .filter(([k]) => k);
  const out: Entry[] = [];
  for (const entry of styleEntries(style)) {
    if (!known(target, entry[0])) continue;
    const t = typed.findIndex((e) => e[0] === entry[0]);
    if (t >= 0) {
      entry[1] = typed[t]![1];
      typed.splice(t, 1);
    }
    out.push(entry);
  }
  for (const entry of typed) {
    const t = out.find((e) => e[0] === entry[0]);
    if (t) t[1] = entry[1];
    else out.push(entry);
  }
  // Nothing changed: keep the text as written, entry order included.
  const sorted = (entries: Entry[]) =>
    entries
      .map((e) => e.join('='))
      .sort()
      .join(';');
  return sorted(styleEntries(style)) === sorted(out) ? style : styleText(out);
}

/** A colour as `#rrggbb` (lower case) when it is one: `#abc` and `#AABBCC` are read, `none` is not. */
export function normalizedColor(value: string): string | undefined {
  const v = value.trim();
  if (!/^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6})$/.test(v)) return undefined;
  const hex = v.slice(1).toLowerCase();
  return hex.length === 3 ? `#${[...hex].map((c) => c + c).join('')}` : `#${hex}`;
}

/** The bits of a `fontStyle` value (bold 1, italic 2, underline 4); 0 when it is not a number. */
export function fontStyleBits(value: string): number {
  const v = value.trim();
  if (!/^\d{1,7}$/.test(v)) return 0;
  const bits = Number(v);
  return bits > 1 << 20 ? 0 : bits;
}
