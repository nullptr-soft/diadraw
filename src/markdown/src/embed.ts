/**
 * diadraw.js: one script for any page with DiaDraw code blocks. It draws them when the page has
 * loaded, unless the page sets `window.diadrawConfig = { startOnLoad: false }` first; call
 * `diadraw.render()` to draw blocks added later.
 *
 *   <script type="module" src="diadraw.js"></script>
 *
 * The core, diadraw.wasm, is loaded from beside the script.
 */
import { renderDiagrams, type RenderDiagramsOptions } from './browser.js';

export { renderDiagrams };

declare global {
  interface Window {
    diadraw?: { render: (options?: RenderDiagramsOptions) => Promise<number> };
    diadrawConfig?: { startOnLoad?: boolean } & RenderDiagramsOptions;
  }
}

window.diadraw = { render: (options) => renderDiagrams({ ...window.diadrawConfig, ...options }) };

if (window.diadrawConfig?.startOnLoad !== false) {
  const start = () => {
    window.diadraw!.render().catch((error: unknown) => console.error('DiaDraw:', error));
  };
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start);
  else start();
}
