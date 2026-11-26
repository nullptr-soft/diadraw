/**
 * A markdown-it plugin that draws ```diadraw code blocks as inline SVG: VitePress, Eleventy with
 * markdown-it, or markdown-it directly. markdown-it renders synchronously, so load the core first:
 *
 *   import { loadDiaDraw } from '@diadraw/wasm';
 *   import markdownItDiadraw from '@diadraw/markdown/markdown-it';
 *   md.use(markdownItDiadraw, { diadraw: await loadDiaDraw() });
 */
import type MarkdownIt from 'markdown-it';
import { defaultLanguages, hashText, parseMeta, renderBlock, type DiaDraw } from './index.js';
import { figure } from './remark.js';

export interface MarkdownItDiadrawOptions {
  /** The loaded core (`await loadDiaDraw()` from `@diadraw/wasm`). */
  readonly diadraw: DiaDraw;
  /** Code block languages to draw. Default `['diadraw']`. */
  readonly languages?: readonly string[];
  /** `all` (the default) or `marked`: only blocks marked `render`. See the remark plugin. */
  readonly blocks?: 'all' | 'marked';
  /**
   * What a block whose text has errors does: `throw` (the default) stops rendering with the
   * errors; `source` shows the block as code with the errors under it.
   */
  readonly onError?: 'throw' | 'source';
  /** The class of the `<figure>` around each diagram. Default `diadraw`. */
  readonly className?: string;
}

export default function markdownItDiadraw(md: MarkdownIt, options: MarkdownItDiadrawOptions): void {
  const languages = options.languages ?? defaultLanguages;
  const className = options.className ?? 'diadraw';
  const fallback = md.renderer.rules.fence!;
  let count = 0;
  md.renderer.rules.fence = (tokens, idx, mdOptions, env, self) => {
    const token = tokens[idx]!;
    const [lang = '', ...rest] = token.info.trim().split(/\s+/);
    const meta = parseMeta(rest.join(' '));
    if (!languages.includes(lang) || meta.source || (options.blocks === 'marked' && !meta.render)) {
      return fallback(tokens, idx, mdOptions, env, self);
    }
    const result = renderBlock(options.diadraw, token.content, {
      view: meta.view,
      idPrefix: `dd${hashText(token.content)}${count++}-`,
    });
    if (result.ok) {
      return `${figure(result.svg, result.view, className)}\n`;
    }
    const line = (token.map?.[0] ?? 0) + 2;
    const message = result.message.replace(
      /^(\d+):/gm,
      (_, l: string) => `line ${line + Number(l) - 1}:`,
    );
    if ((options.onError ?? 'throw') === 'throw') throw new Error(`DiaDraw block: ${message}`);
    return (
      fallback(tokens, idx, mdOptions, env, self) +
      `<p class="${md.utils.escapeHtml(className)}-error" role="alert">${md.utils.escapeHtml(message)}</p>\n`
    );
  };
}
