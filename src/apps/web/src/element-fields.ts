import type { Element } from '@diadraw/wasm';

// An element's properties as the inspector's text fields, and back.

/** The inspector's fields, as text. */
export interface ElementFields {
  /** The last part of the id (`web` for `shop.web`); another one renames the element. */
  readonly id: string;
  readonly kind: string;
  readonly name: string;
  readonly technology: string;
  readonly shape: string;
  readonly description: string;
  /** Comma-separated. */
  readonly tags: string;
  /** `key=value;key=value`, as inline `style "…"` takes it. */
  readonly style: string;
  /** One `key=value` per line. */
  readonly props: string;
}

/** `k=v;k` → { k: 'v', k: '' }, as the core reads inline styles. */
export function parseStyleText(text: string): Record<string, string> {
  const style: Record<string, string> = {};
  for (const part of text.split(';')) {
    if (!part) continue;
    const eq = part.indexOf('=');
    if (eq < 0) style[part] = '';
    else style[part.slice(0, eq)] = part.slice(eq + 1);
  }
  return style;
}

export function printStyleText(style: Readonly<Record<string, string>>): string {
  return Object.entries(style)
    .map(([k, v]) => (v === '' ? k : `${k}=${v}`))
    .join(';');
}

/** The last part of an element's id, as its declaration writes it: `web` for `shop.web`. */
export const localId = (element: Element): string =>
  element.id.slice(element.id.lastIndexOf('.') + 1);

export function fieldsOf(element: Element): ElementFields {
  return {
    id: localId(element),
    kind: element.kind,
    name: element.name,
    technology: element.technology ?? '',
    shape: element.shape ?? '',
    description: element.description ?? '',
    tags: element.tags.join(', '),
    style: element.style ? printStyleText(element.style) : '',
    props: Object.entries(element.properties)
      .map(([k, v]) => `${k}=${v}`)
      .join('\n'),
  };
}

const optional = (text: string): string | undefined => text.trim() || undefined;

/**
 * The element with the fields applied; what the fields do not cover stays. The id is not applied
 * here: a new one renames the element through the session (see `Session.renameElement`).
 */
export function applyFields(element: Element, fields: ElementFields): Element {
  const local = localId(element);
  const properties: Record<string, string> = {};
  for (const line of fields.props.split('\n')) {
    const eq = line.indexOf('=');
    const key = (eq < 0 ? line : line.slice(0, eq)).trim();
    if (key) properties[key] = eq < 0 ? '' : line.slice(eq + 1).trim();
  }
  const style = fields.style.trim();
  const out: { -readonly [K in keyof Element]: Element[K] } = {
    ...element,
    kind: fields.kind.trim(),
    name: fields.name.trim() || local,
    tags: fields.tags
      .split(',')
      .map((t) => t.trim())
      .filter(Boolean),
    properties,
  };
  for (const key of ['technology', 'shape', 'description'] as const) {
    const value = optional(fields[key]);
    if (value === undefined) delete out[key];
    else out[key] = value;
  }
  // `style ""` still draws the element as its own shape; an empty field keeps that as it was.
  if (style) out.style = parseStyleText(style);
  else if (element.style && Object.keys(element.style).length > 0) delete out.style;
  return out;
}
