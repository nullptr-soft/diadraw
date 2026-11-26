/**
 * Renders the text of a ```diadraw code block as an SVG diagram with the DiaDraw core. The remark
 * and markdown-it plugins and diadraw.js all go through `renderBlock`, so a block looks the same
 * whichever of them draws it.
 */
import type { DiaDraw, Diagnostic } from '@diadraw/wasm';

export type { DiaDraw, Diagnostic };

/** What a block asks for in its info string: ```diadraw view=checkout */
export interface BlockOptions {
  /** The view to draw; the document's first view when absent. */
  readonly view?: string | undefined;
}

export type BlockResult =
  | { readonly ok: true; readonly svg: string; readonly view: string }
  | { readonly ok: false; readonly message: string; readonly diagnostics: readonly Diagnostic[] };

/** Words in a code block's info string (after the language) that the plugins understand. */
export interface BlockMeta extends BlockOptions {
  /** `render`: draw this block even when only marked blocks are drawn. */
  readonly render: boolean;
  /** `source` or `norender`: leave this block as code. */
  readonly source: boolean;
}

/** Reads `view=…`, `render` and `source` (or `norender`) from a code block's meta string. */
export function parseMeta(meta: string | null | undefined): BlockMeta {
  let view: string | undefined;
  let render = false;
  let source = false;
  for (const word of (meta ?? '').trim().split(/\s+/)) {
    if (word.startsWith('view=')) view = word.slice('view='.length).replace(/^(["'])(.*)\1$/, '$2');
    else if (word === 'render') render = true;
    else if (word === 'source' || word === 'norender') source = true;
  }
  return { view, render, source };
}

/** Code block languages drawn by default. */
export const defaultLanguages: readonly string[] = ['diadraw'];

/**
 * Parses `text` and draws one of its views as a standalone `<svg>` element. Errors in the text
 * (not warnings) fail the block, with every diagnostic as `line:column: message`.
 *
 * `idPrefix` is put in front of every id in the SVG (and every reference to one), so that several
 * diagrams on one page keep their arrowheads and other definitions apart.
 */
export function renderBlock(
  dd: DiaDraw,
  text: string,
  options: BlockOptions & { readonly idPrefix?: string } = {},
): BlockResult {
  let parsed;
  try {
    parsed = dd.parse(text);
  } catch (error) {
    return { ok: false, message: String(error), diagnostics: [] };
  }
  const { model, diagnostics } = parsed;
  const errors = diagnostics.filter((d) => d.severity === 'error');
  if (errors.length) {
    return {
      ok: false,
      message: errors.map((d) => `${d.line}:${d.column}: ${d.message}`).join('\n'),
      diagnostics,
    };
  }
  // A document without a view block has one view, `default`, showing every element.
  const declared = Object.keys(model.views);
  const views = declared.length ? declared : ['default'];
  const view = options.view ?? views[0]!;
  if (!views.includes(view)) {
    return {
      ok: false,
      message: `no view "${view}" (the document has ${views.map((v) => `"${v}"`).join(', ')})`,
      diagnostics,
    };
  }
  let svg;
  try {
    svg = dd.renderSvg(model, view);
  } catch (error) {
    return { ok: false, message: String(error), diagnostics };
  }
  return { ok: true, svg: embeddable(svg, options.idPrefix ?? ''), view };
}

/**
 * The SVG as an element for an HTML page: without the XML declaration, scaled down to fit narrow
 * columns, and with ids prefixed.
 */
function embeddable(svg: string, idPrefix: string): string {
  let out = svg.replace(/^\s*<\?xml[^>]*\?>\s*/, '').replace(/^\s*<!DOCTYPE[^>]*>\s*/i, '');
  if (idPrefix) {
    out = out
      .replace(/\sid="([^"]+)"/g, (_, id: string) => ` id="${idPrefix}${id}"`)
      .replace(/url\(#([^)]+)\)/g, (_, id: string) => `url(#${idPrefix}${id})`)
      .replace(
        /(\s(?:xlink:)?href)="#([^"]+)"/g,
        (_, attr: string, id: string) => `${attr}="#${idPrefix}${id}"`,
      );
  }
  // Keeps the intrinsic size from width and height, but never wider than the page's column.
  return out.replace(/^<svg\b/, '<svg style="max-width:100%;height:auto"');
}

/** A short hash of the text, for ids that stay the same from one build to the next. */
export function hashText(text: string): string {
  let h = 0x811c9dc5;
  for (let i = 0; i < text.length; i++) {
    h ^= text.charCodeAt(i);
    h = Math.imul(h, 0x01000193);
  }
  return (h >>> 0).toString(36);
}
