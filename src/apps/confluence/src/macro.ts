/**
 * The DiaDraw macro as it shows in a Confluence page: the diagram drawn from the text saved with
 * the macro, with buttons to save it as SVG or PNG. The text is edited in the macro's editor
 * (editor.ts), which Confluence opens from the macro's Edit button.
 */
import { view } from '@forge/bridge';
import { loadDiaDraw } from '@diadraw/wasm';
import { draw, readConfig } from './diagram.js';
import { downloadPng, downloadSvg, fileNameFor } from './download.js';

const root = document.getElementById('root')!;

function message(text: string, className: string): HTMLElement {
  const p = document.createElement('p');
  p.className = className;
  p.textContent = text;
  return p;
}

function button(label: string, title: string, onClick: () => void): HTMLButtonElement {
  const b = document.createElement('button');
  b.type = 'button';
  b.textContent = label;
  b.title = title;
  b.addEventListener('click', onClick);
  return b;
}

async function main(): Promise<void> {
  // Lets the page's light or dark theme reach this frame's CSS (the --ds-* tokens).
  await view.theme.enable().catch(() => undefined);
  const context = await view.getContext();
  const config = readConfig(context.extension?.config);
  if (!config.source.trim()) {
    root.replaceChildren(
      message('This DiaDraw diagram is empty. Edit the macro to draw one.', 'placeholder'),
    );
    return;
  }

  const dd = await loadDiaDraw();
  const { result } = draw(dd, config, 'dd-');
  if (!result.ok) {
    const pre = document.createElement('pre');
    pre.className = 'source';
    pre.textContent = config.source;
    root.replaceChildren(message(`DiaDraw: ${result.message}`, 'error'), pre);
    return;
  }

  const figure = document.createElement('figure');
  figure.className = 'diagram';
  figure.dataset.view = result.view;
  figure.innerHTML = result.svg;

  const status = message('', 'status');
  const fail = (error: unknown) => {
    status.textContent = `Could not save the file: ${error instanceof Error ? error.message : String(error)}`;
  };
  const tools = document.createElement('div');
  tools.className = 'tools';
  tools.append(
    button('SVG', 'Download this diagram as SVG', () => {
      try {
        downloadSvg(result.svg, fileNameFor(result.view, 'svg'));
      } catch (error) {
        fail(error);
      }
    }),
    button('PNG', 'Download this diagram as PNG', () => {
      downloadPng(result.svg, fileNameFor(result.view, 'png')).catch(fail);
    }),
  );
  figure.append(tools);
  root.replaceChildren(figure, status);
}

main().catch((error: unknown) => {
  console.error('DiaDraw:', error);
  root.replaceChildren(
    message(
      `DiaDraw could not draw this diagram: ${error instanceof Error ? error.message : String(error)}`,
      'error',
    ),
  );
});
