/**
 * Draws the DiaDraw code blocks already on a page, in the browser: whatever rendered the Markdown
 * (a static site generator, a wiki, a CMS) left them as `<pre><code class="language-diadraw">`.
 */
import { loadDiaDraw } from '@diadraw/wasm';
import { defaultLanguages, hashText, renderBlock, type DiaDraw } from './index.js';

export interface RenderDiagramsOptions {
  /** Where to look for blocks. Default `document`. */
  readonly root?: ParentNode;
  /** Code block languages to draw. Default `['diadraw']`. */
  readonly languages?: readonly string[];
  /** A loaded core to use; loaded on first use otherwise. */
  readonly diadraw?: DiaDraw | Promise<DiaDraw>;
  /** The class of the `<figure>` around each diagram. Default `diadraw`. */
  readonly className?: string;
}

let rendered = 0;

/**
 * Replaces each DiaDraw code block under `root` with its diagram, and returns how many it drew.
 * A block's `data-view` attribute (on the `<pre>` or the `<code>`) picks the view. A block with
 * errors stays as code with the errors under it. Blocks already handled are skipped, so call it
 * again after the page adds content.
 */
export async function renderDiagrams(options: RenderDiagramsOptions = {}): Promise<number> {
  const root = options.root ?? document;
  const languages = options.languages ?? defaultLanguages;
  const className = options.className ?? 'diadraw';
  const selector = languages
    .flatMap((l) => [
      `code.language-${l}`,
      `code.lang-${l}`,
      `pre.language-${l}`,
      `pre[lang="${l}"]`,
    ])
    .join(',');
  const blocks = new Set<HTMLElement>();
  for (const element of root.querySelectorAll<HTMLElement>(selector)) {
    const pre = element.closest('pre') ?? element;
    if (pre.hasAttribute('data-diadraw')) continue;
    // Claimed before the core loads, so a second call while this one waits skips the block.
    pre.setAttribute('data-diadraw', '');
    blocks.add(pre);
  }
  if (!blocks.size) return 0;

  const dd = await (options.diadraw ?? loadDiaDraw());
  let count = 0;
  for (const pre of blocks) {
    const code = pre.querySelector('code') ?? pre;
    const text = code.textContent ?? '';
    const view = pre.dataset.view ?? (code as HTMLElement).dataset.view;
    const result = renderBlock(dd, text, { view, idPrefix: `dd${hashText(text)}${rendered++}-` });
    if (!result.ok) {
      const error = document.createElement('p');
      error.className = `${className}-error`;
      error.setAttribute('role', 'alert');
      error.textContent = `DiaDraw: ${result.message}`;
      pre.after(error);
      continue;
    }
    const figure = document.createElement('figure');
    figure.className = className;
    figure.dataset.view = result.view;
    figure.innerHTML = result.svg;
    pre.replaceWith(figure);
    count++;
  }
  return count;
}
