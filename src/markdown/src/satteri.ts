/**
 * A Sätteri plugin that draws ```diadraw code blocks as inline SVG when the Markdown is built.
 * Sätteri is the Markdown processor Astro uses by default from Astro 7 (Starlight included):
 *
 *   import { satteri } from '@astrojs/markdown-satteri';
 *   import satteriDiadraw from '@diadraw/markdown/satteri';
 *   export default defineConfig({
 *     markdown: { processor: satteri({ mdastPlugins: [satteriDiadraw()] }) },
 *   });
 */
import type { Code } from 'mdast';
import { loadDiaDraw } from '@diadraw/wasm';
import { defaultLanguages, hashText, parseMeta, renderBlock, type DiaDraw } from './index.js';
import { figure, lineMessage, type RemarkDiadrawOptions } from './remark.js';

export type SatteriDiadrawOptions = RemarkDiadrawOptions;

/** The part of Sätteri's mdast visitor context the plugin uses. */
interface Context {
  report(diagnostic: { message: string; node?: unknown; severity?: 'error' | 'warning' }): void;
}

/** The plugin, as Sätteri's `mdastPlugins` takes it (`defineMdastPlugin` without the import). */
export interface SatteriDiadrawPlugin {
  readonly name: string;
  readonly options: { readonly position: true };
  code(
    node: Readonly<Code>,
    context: Context,
  ): Promise<{ raw: string; mdxExpressions: false } | undefined>;
}

export default function satteriDiadraw(options: SatteriDiadrawOptions = {}): SatteriDiadrawPlugin {
  const languages = options.languages ?? defaultLanguages;
  const marked = options.blocks === 'marked';
  let core: Promise<DiaDraw> | undefined;
  let count = 0;
  return {
    name: 'diadraw',
    options: { position: true },
    async code(node, context) {
      if (!node.lang || !languages.includes(node.lang)) return undefined;
      const meta = parseMeta(node.meta);
      if (meta.source || (marked && !meta.render)) return undefined;
      core ??= Promise.resolve(options.diadraw ?? loadDiaDraw());
      const result = renderBlock(await core, node.value, {
        view: meta.view,
        idPrefix: `dd${hashText(node.value)}${count++}-`,
      });
      if (!result.ok) {
        const reason = lineMessage(result.message, node);
        if ((options.onError ?? 'fail') === 'fail') throw new Error(reason);
        context.report({ message: reason, node, severity: 'warning' });
        return undefined;
      }
      // Spliced back in as an HTML block, which ends at the first blank line.
      return { raw: figure(result.svg, result.view, options.className), mdxExpressions: false };
    },
  };
}
