import type { Relationship } from '@diadraw/wasm';
import { parseStyleText, printStyleText } from './element-fields';

// A relationship's properties as the inspector's text fields, and back.

/** The arrows a relationship can be written with. */
export const ARROWS = ['->', '-->', '<->'] as const;
export type Arrow = (typeof ARROWS)[number];

/** The inspector's fields, as text. */
export interface RelationshipFields {
  readonly label: string;
  readonly technology: string;
  readonly arrow: Arrow;
  /** The explicit id in `name = a -> b`; empty for an id derived from the ends. */
  readonly name: string;
  /** Comma-separated. */
  readonly tags: string;
  /** `key=value;key=value`, as inline `style "…"` takes it. */
  readonly style: string;
  /** One `key=value` per line. */
  readonly props: string;
}

/** `[A-Za-z_][A-Za-z0-9_]*`: an id that can be written as a name. Derived ids (`a->b`) are not. */
export const isName = (id: string): boolean => /^[A-Za-z_][A-Za-z0-9_]*$/.test(id);

export function arrowOf(relationship: Relationship): Arrow {
  if (relationship.bidirectional) return '<->';
  return relationship.line === 'dashed' ? '-->' : '->';
}

/** How the inspector titles a relationship: `name = a -> b`, or `a -> b` without a name. */
export function relationshipTitle(relationship: Relationship): string {
  const end = (e: Relationship['source']) => (e.port ? `${e.element}.${e.port}` : e.element);
  const line = `${end(relationship.source)} ${arrowOf(relationship)} ${end(relationship.target)}`;
  return isName(relationship.id) ? `${relationship.id} = ${line}` : line;
}

export function relationshipFieldsOf(relationship: Relationship): RelationshipFields {
  return {
    label: relationship.label ?? '',
    technology: relationship.technology ?? '',
    arrow: arrowOf(relationship),
    name: isName(relationship.id) ? relationship.id : '',
    tags: relationship.tags.join(', '),
    style: relationship.style ? printStyleText(relationship.style) : '',
    props: Object.entries(relationship.properties)
      .map(([k, v]) => `${k}=${v}`)
      .join('\n'),
  };
}

const optional = (text: string): string | undefined => text.trim() || undefined;

/**
 * The relationship with the fields applied (its id and ends stay), and the name it should have:
 * the core applies the name, since a new one can renumber other lines' derived ids.
 */
export function applyRelationshipFields(
  relationship: Relationship,
  fields: RelationshipFields,
): { relationship: Relationship; name: string | undefined } {
  const properties: Record<string, string> = {};
  for (const line of fields.props.split('\n')) {
    const eq = line.indexOf('=');
    const key = (eq < 0 ? line : line.slice(0, eq)).trim();
    if (key) properties[key] = eq < 0 ? '' : line.slice(eq + 1).trim();
  }
  const out: { -readonly [K in keyof Relationship]: Relationship[K] } = {
    ...relationship,
    tags: fields.tags
      .split(',')
      .map((t) => t.trim())
      .filter(Boolean),
    properties,
  };
  if (ARROWS.includes(fields.arrow)) {
    out.line = fields.arrow === '-->' ? 'dashed' : 'solid';
    out.bidirectional = fields.arrow === '<->';
  }
  for (const key of ['label', 'technology'] as const) {
    const value = optional(fields[key]);
    if (value === undefined) delete out[key];
    else out[key] = value;
  }
  // `style ""` still marks a line as styled; an empty field keeps that as it was.
  const style = fields.style.trim();
  if (style) out.style = parseStyleText(style);
  else if (relationship.style && Object.keys(relationship.style).length > 0) delete out.style;
  return { relationship: out, name: optional(fields.name) };
}
