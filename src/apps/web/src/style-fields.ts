import type { Element, Model, Operation, StyleRule, ThemeDef } from '@diadraw/wasm';
import { parseStyleText, printStyleText } from './element-fields';
import { isName } from './relationship-fields';

// `style` blocks and themes as inspector fields, and the operations that write them back; the
// same rules as the native apps' host/styles.cpp. A block is one `selector: key=value;…` line:
// `container: fill=#438dd5`, `tag "external": dashed=1`, `relationship tag "async": dashed=1`.

type Style = Readonly<Record<string, string>>;

/** The themes the core draws without a declaration. */
export const BUILT_IN_THEMES = [
  'default',
  'dark',
  'mono',
  'ocean',
  'forest',
  'sunset',
  'lavender',
  'sand',
  'graphite',
  'pastel',
  'vivid',
  'contrast',
  'blueprint',
  'midnight',
  'aurora',
  'ember',
  'terminal',
] as const;

const quote = (text: string): string =>
  `"${text.replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/\n/g, '\\n')}"`;

const unquote = (literal: string): string =>
  literal.slice(1, -1).replace(/\\(.)/g, (_, c: string) => (c === 'n' ? '\n' : c));

/** What a block applies to, as written after `style`. */
export function printSelector(rule: StyleRule): string {
  let out = rule.target === 'relationships' ? 'relationship' : (rule.kind ?? (rule.tag ? '' : '*'));
  if (rule.tag !== undefined) out += `${out ? ' ' : ''}tag ${quote(rule.tag)}`;
  return out;
}

function parseSelector(text: string): StyleRule {
  const bad = new Error(
    `"${text.trim()}" is not a selector: write *, a kind, tag "…", a kind and a tag, or relationship`,
  );
  let rest = text.trim();
  const word = (w: string) => rest === w || rest.startsWith(`${w} `) || rest.startsWith(`${w}\t`);
  let target: StyleRule['target'] = 'elements';
  let kind: string | undefined;
  let tag: string | undefined;
  if (word('relationship')) {
    target = 'relationships';
    rest = rest.slice(12).trim();
  } else if (rest.startsWith('*')) {
    rest = rest.slice(1).trim();
  } else if (!word('tag')) {
    const end = rest.search(/[ \t]|$/);
    if (end === 0) throw bad;
    kind = rest.slice(0, end);
    rest = rest.slice(end).trim();
  }
  if (rest) {
    if (!word('tag')) throw bad;
    rest = rest.slice(3).trim();
    tag = rest.length >= 2 && rest.startsWith('"') && rest.endsWith('"') ? unquote(rest) : rest;
    if (!tag) throw bad;
  }
  return {
    target,
    ...(kind !== undefined && { kind }),
    ...(tag !== undefined && { tag }),
    style: {},
  };
}

/** Why the core would refuse to add a block: a kind or a key that is not a name. */
function ruleProblem(rule: StyleRule): string | undefined {
  if (rule.kind !== undefined && (!isName(rule.kind) || rule.kind === 'tag'))
    return `Kind "${rule.kind}" of a style block is not a name`;
  const key = Object.keys(rule.style).find((k) => !isName(k));
  return key === undefined ? undefined : `Key "${key}" of a style block is not a name`;
}

/** The first `:` outside double quotes, or -1. */
function colonOf(line: string): number {
  let quoted = false;
  for (let i = 0; i < line.length; i++) {
    if (line[i] === '\\' && quoted) i++;
    else if (line[i] === '"') quoted = !quoted;
    else if (line[i] === ':' && !quoted) return i;
  }
  return -1;
}

export function styleLines(rules: readonly StyleRule[], shown: readonly number[]): string {
  return shown
    .filter((i) => i < rules.length)
    .map((i) => `${printSelector(rules[i]!)}: ${printStyleText(rules[i]!.style)}`)
    .join('\n');
}

/** Reads lines as `styleLines` writes them; throws with the line number when one cannot be read. */
export function parseStyleLines(text: string): StyleRule[] {
  const out: StyleRule[] = [];
  text.split('\n').forEach((raw, i) => {
    const line = raw.trim();
    if (!line) return;
    const colon = colonOf(line);
    if (colon < 0)
      throw new Error(
        `Line ${i + 1}: write a selector, a colon and the keys: container: fill=#438dd5`,
      );
    let rule: StyleRule;
    try {
      rule = parseSelector(line.slice(0, colon));
    } catch (e) {
      throw new Error(`Line ${i + 1}: ${(e as Error).message}`);
    }
    const parsed = { ...rule, style: parseStyleText(line.slice(colon + 1).trim()) };
    // As adding the block would refuse it (the core's style_rule_problem), so the line is named.
    const problem = ruleProblem(parsed);
    if (problem) throw new Error(`Line ${i + 1}: ${problem}`);
    out.push(parsed);
  });
  return out;
}

/** `after` with the keys `before` has first, in `before`'s order. */
function ordered(before: Style, after: Style): Record<string, string> {
  const out: Record<string, string> = {};
  for (const k of Object.keys(before)) if (k in after) out[k] = after[k]!;
  for (const [k, v] of Object.entries(after)) if (!(k in before)) out[k] = v;
  return out;
}

const one = (ops: Operation[]): Operation =>
  ops.length === 1 ? ops[0]! : { type: 'batch', operations: ops };

/**
 * The operation that turns the blocks at `shown` (ascending indices into `rules`) into `wanted`:
 * blocks are matched by selector, changed ones updated in place, dropped ones removed and new ones
 * added after the last shown block. Undefined when nothing changes.
 */
export function styleOperation(
  rules: readonly StyleRule[],
  shown: readonly number[],
  wanted: StyleRule[],
): Operation | undefined {
  const n = shown.length;
  const m = wanted.length;
  const sel = (r: StyleRule) => printSelector(r);
  const lcs = Array.from({ length: n + 1 }, () => new Array<number>(m + 1).fill(0));
  for (let a = n - 1; a >= 0; a--)
    for (let b = m - 1; b >= 0; b--)
      lcs[a]![b] =
        sel(rules[shown[a]!]!) === sel(wanted[b]!)
          ? lcs[a + 1]![b + 1]! + 1
          : Math.max(lcs[a + 1]![b]!, lcs[a]![b + 1]!);
  const match: (number | undefined)[] = new Array(n).fill(undefined);
  let a = 0;
  let b = 0;
  let gapA = 0;
  let gapB = 0;
  const pairGap = (toA: number, toB: number) => {
    for (; gapA < toA && gapB < toB; gapA++, gapB++) match[gapA] = gapB;
  };
  while (a < n && b < m) {
    if (sel(rules[shown[a]!]!) === sel(wanted[b]!)) {
      pairGap(a, b);
      match[a] = b;
      gapA = ++a;
      gapB = ++b;
    } else if (lcs[a + 1]![b]! >= lcs[a]![b + 1]!) a++;
    else b++;
  }
  pairGap(n, m);
  const placed = new Array<boolean>(m).fill(false);
  match.forEach((w, i) => {
    if (w === undefined) return;
    placed[w] = true;
    wanted[w] = { ...wanted[w]!, style: ordered(rules[shown[i]!]!.style, wanted[w]!.style) };
  });

  const slots: { old?: number; rule: StyleRule }[] = [];
  let next = 0;
  const flush = (upto: number) => {
    for (; next < upto; next++) if (!placed[next]) slots.push({ rule: wanted[next]! });
  };
  let at = 0;
  rules.forEach((rule, i) => {
    if (at < n && shown[at] === i) {
      const w = match[at];
      if (w !== undefined) {
        flush(w);
        slots.push({ old: i, rule: wanted[w]! });
        next = Math.max(next, w + 1);
      }
      if (++at === n) flush(m);
    } else slots.push({ old: i, rule });
  });
  flush(m);

  const ops: Operation[] = [];
  for (let i = n - 1; i >= 0; i--)
    if (match[i] === undefined) ops.push({ type: 'removeStyleRule', index: shown[i]! });
  slots.forEach((slot, p) => {
    if (slot.old === undefined) {
      ops.push({ type: 'addStyleRule', rule: slot.rule, index: p });
      return;
    }
    const was = rules[slot.old]!;
    const same =
      sel(was) === sel(slot.rule) && printStyleText(was.style) === printStyleText(slot.rule.style);
    if (!same) ops.push({ type: 'updateStyleRule', index: p, rule: slot.rule });
  });
  return ops.length ? one(ops) : undefined;
}

export function styleApplies(rule: StyleRule, element: Element): boolean {
  return (
    rule.target === 'elements' &&
    (rule.kind === undefined || rule.kind === element.kind) &&
    (rule.tag === undefined || element.tags.includes(rule.tag))
  );
}

export function stylesFor(model: Model, element: Element): number[] {
  return (model.styles ?? []).flatMap((rule, i) => (styleApplies(rule, element) ? [i] : []));
}

/** The element inspector's Shared styles field. */
export function sharedStyles(model: Model, element: Element): string {
  return styleLines(model.styles ?? [], stylesFor(model, element));
}

/** The operation for a typed Shared styles field; throws when a line cannot be read or does not apply. */
export function sharedStylesOperation(
  model: Model,
  element: Element,
  typed: string,
): Operation | undefined {
  const wanted = parseStyleLines(typed);
  for (const rule of wanted)
    if (!styleApplies(rule, element))
      throw new Error(
        `style ${printSelector(rule)} does not apply to ${element.id}; edit it with nothing selected`,
      );
  return styleOperation(model.styles ?? [], stylesFor(model, element), wanted);
}

/** The document inspector's fields, as text. */
export interface DocumentFields {
  /** The document's `theme`. */
  readonly theme: string;
  /** The current view's own `theme`. */
  readonly viewTheme: string;
  /** Every `style` block. */
  readonly styles: string;
  /** The declared theme the view is drawn with (not editable); empty for built-in ones. */
  readonly declared: string;
  readonly extends: string;
  /** Theme-wide keys, one `key=value` per line. */
  readonly themeKeys: string;
  /** Node colours, one `kind: fill=…;stroke=…;text=…` per line. */
  readonly themeNodes: string;
}

const THEME_META = new Set(['name', 'extends', 'nodes']);

/** A theme's own keys (`background`, `edge`, …), without its name, base and nodes. */
function themeValues(theme: ThemeDef): Record<string, string> {
  const out: Record<string, string> = {};
  for (const [k, v] of Object.entries(theme)) if (!THEME_META.has(k)) out[k] = String(v);
  return out;
}

/** The theme a view is drawn with: its own, else the document's. */
export function viewTheme(model: Model, view: string | undefined): string | undefined {
  return (view !== undefined ? model.views[view]?.theme : undefined) ?? model.theme;
}

export function documentFields(model: Model, view: string | undefined): DocumentFields {
  const styles = model.styles ?? [];
  const name = viewTheme(model, view);
  const declared = (model.themes ?? []).find((t) => t.name === name);
  return {
    theme: model.theme ?? '',
    viewTheme: (view !== undefined ? model.views[view]?.theme : undefined) ?? '',
    styles: styleLines(
      styles,
      styles.map((_, i) => i),
    ),
    declared: declared?.name ?? '',
    extends: declared?.extends ?? '',
    themeKeys: declared
      ? Object.entries(themeValues(declared))
          .map(([k, v]) => `${k}=${v}`)
          .join('\n')
      : '',
    themeNodes: declared
      ? Object.entries(declared.nodes ?? {})
          .map(([kind, look]) => `${kind}: ${printStyleText(look as Style)}`)
          .join('\n')
      : '',
  };
}

function knownTheme(model: Model, name: string): boolean {
  if ((BUILT_IN_THEMES as readonly string[]).includes(name)) return true;
  if ((model.themes ?? []).some((t) => t.name === name)) return true;
  const dot = name.indexOf('.');
  return dot > 0 && (model.libraries ?? []).some((u) => u.alias === name.slice(0, dot));
}

/**
 * The operation for typed document fields (compared with the model's). A theme name that is
 * neither built in, declared nor a library's declares a new theme extending the one it replaces.
 */
export function documentOperation(
  model: Model,
  view: string | undefined,
  typed: DocumentFields,
): Operation | undefined {
  const now = documentFields(model, view);
  const ops: Operation[] = [];
  const declaring: string[] = [];
  const pick = (target: string | undefined, typedName: string, was: string) => {
    const name = typedName.trim();
    if (name === was) return;
    if (name && !knownTheme(model, name) && !declaring.includes(name)) {
      const base = target !== undefined ? viewTheme(model, target) : model.theme;
      ops.push({
        type: 'addTheme',
        theme: {
          name,
          ...(base && base !== name && base !== 'default' && { extends: base }),
        },
      });
      declaring.push(name);
    }
    ops.push({
      type: 'setTheme',
      ...(target !== undefined && { view: target }),
      ...(name && { theme: name }),
    });
  };
  pick(undefined, typed.theme, now.theme);
  if (view !== undefined && model.views[view]) pick(view, typed.viewTheme, now.viewTheme);

  if (typed.styles !== now.styles) {
    const styles = model.styles ?? [];
    const op = styleOperation(
      styles,
      styles.map((_, i) => i),
      parseStyleLines(typed.styles),
    );
    if (op) ops.push(op);
  }

  const declared = typed.declared
    ? (model.themes ?? []).find((t) => t.name === typed.declared)
    : undefined;
  if (
    declared &&
    (typed.extends !== now.extends ||
      typed.themeKeys !== now.themeKeys ||
      typed.themeNodes !== now.themeNodes)
  ) {
    const values: Record<string, string> = {};
    for (const raw of typed.themeKeys.split('\n')) {
      const line = raw.trim();
      if (!line) continue;
      const eq = line.indexOf('=');
      const key = (eq < 0 ? line : line.slice(0, eq)).trim();
      if (key) values[key] = eq < 0 ? '' : line.slice(eq + 1).trim();
    }
    const typedNodes: [string, Record<string, string>][] = [];
    typed.themeNodes.split('\n').forEach((raw, i) => {
      const line = raw.trim();
      if (!line) return;
      const colon = line.indexOf(':');
      if (colon < 0)
        throw new Error(
          `Node colours, line ${i + 1}: write a kind, a colon and the keys: person: fill=#08427b`,
        );
      const kind = line.slice(0, colon).trim();
      const look = parseStyleText(line.slice(colon + 1).trim());
      const same = typedNodes.find(([k]) => k === kind);
      if (same) Object.assign(same[1], look);
      else typedNodes.push([kind, look]);
    });
    // Kinds already declared keep their place and key order; new ones follow.
    const before = (declared.nodes ?? {}) as Record<string, Style>;
    const nodes: Record<string, Record<string, string>> = {};
    for (const [kind, look] of Object.entries(before)) {
      const typedLook = typedNodes.find(([k]) => k === kind);
      if (typedLook) nodes[kind] = ordered(look, typedLook[1]);
    }
    for (const [kind, look] of typedNodes) if (!(kind in nodes)) nodes[kind] = look;
    const base = typed.extends.trim();
    const theme: ThemeDef = {
      name: declared.name,
      ...(base && { extends: base }),
      ...ordered(themeValues(declared), values),
      ...(Object.keys(nodes).length > 0 && { nodes }),
    };
    if (JSON.stringify(theme) !== JSON.stringify(declared))
      ops.push({ type: 'updateTheme', theme });
  }
  return ops.length ? one(ops) : undefined;
}
