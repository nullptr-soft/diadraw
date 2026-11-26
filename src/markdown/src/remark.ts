/**
 * A remark plugin that draws ```diadraw code blocks as inline SVG when the Markdown is built:
 * Astro and Starlight, Docusaurus, Next.js with MDX, Gatsby, Eleventy, or unified directly.
 *
 *   import remarkDiadraw from '@diadraw/markdown/remark';
 *   unified().use(remarkParse).use(remarkDiadraw).use(remarkRehype).use(rehypeStringify);
 */
import { fromHtml } from 'hast-util-from-html';
import type { Code, Parent, Root, RootContent } from 'mdast';
import { loadDiaDraw } from '@diadraw/wasm';
import { defaultLanguages, hashText, parseMeta, renderBlock, type DiaDraw } from './index.js';

/**
 * The errors of a block as one message, each `line:column` turned into the line in the Markdown
 * file: the block's text starts on the line after its fence.
 */
export function lineMessage(message: string, node: Pick<Code, 'position'>): string {
  const first = (node.position?.start.line ?? 0) + 1;
  return `DiaDraw block: ${message.replace(/^(\d+):/gm, (_, l: string) => `line ${first + Number(l) - 1}:`)}`;
}

/** A drawn block as HTML: the SVG in a `<figure>`, with no blank line that would end an HTML block. */
export function figure(svg: string, view: string, className = 'diadraw'): string {
  const attr = (value: string) => value.replace(/&/g, '&amp;').replace(/"/g, '&quot;');
  return `<figure class="${attr(className)}" data-view="${attr(view)}">${svg.replace(/\n\s*\n/g, '\n')}</figure>`;
}

export interface RemarkDiadrawOptions {
  /** Code block languages to draw. Default `['diadraw']`. */
  readonly languages?: readonly string[];
  /**
   * `all` (the default) draws every block in those languages, except those marked `source`;
   * `marked` draws only blocks marked `render` (```diadraw render), for pages where most blocks
   * are examples of the text rather than diagrams.
   */
  readonly blocks?: 'all' | 'marked';
  /**
   * What a block whose text has errors does: `fail` (the default) stops the build with the
   * errors at the block's lines; `warn` reports them and leaves the block as code.
   */
  readonly onError?: 'fail' | 'warn';
  /** A loaded core to use; loaded on first use otherwise. */
  readonly diadraw?: DiaDraw | Promise<DiaDraw>;
  /** The class of the `<figure>` around each diagram. Default `diadraw`. */
  readonly className?: string;
}

/** The part of a VFile the plugin reports to. */
interface File {
  message(reason: string, place?: unknown): unknown;
  fail(reason: string, place?: unknown): never;
}

export default function remarkDiadraw(options: RemarkDiadrawOptions = {}) {
  const languages = options.languages ?? defaultLanguages;
  const marked = options.blocks === 'marked';
  const className = options.className ?? 'diadraw';
  return async (tree: Root, file: File): Promise<void> => {
    const found: { parent: Parent; index: number; node: Code }[] = [];
    const walk = (parent: Parent) => {
      parent.children.forEach((child, index) => {
        if (child.type === 'code') {
          if (!child.lang || !languages.includes(child.lang)) return;
          const meta = parseMeta(child.meta);
          if (meta.source || (marked && !meta.render)) return;
          found.push({ parent, index, node: child });
        } else if ('children' in child) {
          walk(child);
        }
      });
    };
    walk(tree);
    if (!found.length) return;

    const dd = await (options.diadraw ?? loadDiaDraw());
    found.forEach(({ parent, index, node }, n) => {
      const { view } = parseMeta(node.meta);
      const result = renderBlock(dd, node.value, {
        view,
        idPrefix: `dd${hashText(node.value)}${n}-`,
      });
      if (!result.ok) {
        const reason = lineMessage(result.message, node);
        if ((options.onError ?? 'fail') === 'fail') file.fail(reason, node);
        file.message(reason, node);
        return;
      }
      const svg = fromHtml(result.svg, { fragment: true, space: 'svg' });
      parent.children[index] = {
        type: 'diadrawDiagram',
        data: {
          hName: 'figure',
          hProperties: { className: [className], dataView: result.view },
          hChildren: svg.children as never,
        },
      } as unknown as RootContent;
    });
  };
}
