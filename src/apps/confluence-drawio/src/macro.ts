/**
 * The macro as it shows in a Confluence page: the saved drawing as an image, with buttons to save
 * it as an editable SVG (which draw.io opens again) or as PNG. Nothing of draw.io is loaded here;
 * the editor (editor.ts) opens from the macro's Edit button.
 */
import { view } from '@forge/bridge';
import { errorText } from './drawio.js';
import {
  fileBaseName,
  pageNames,
  readConfig,
  svgDataUri,
  unpackSvg,
  withColorScheme,
} from './diagram.js';
import { downloadPng, downloadSvg } from './download.js';

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
  // Lets the page's light or dark theme reach this frame (the --ds-* tokens, data-color-mode).
  await view.theme.enable().catch(() => undefined);
  const context = await view.getContext();
  const config = readConfig(context.extension?.config);
  if (!config.diagram) {
    root.replaceChildren(
      message('This diagram is empty. Edit the macro to draw one.', 'placeholder'),
    );
    return;
  }

  const svg = await unpackSvg(config.diagram);
  const dark = document.documentElement.dataset.colorMode === 'dark';
  const figure = document.createElement('figure');
  figure.className = 'diagram';
  const image = document.createElement('img');
  image.src = svgDataUri(withColorScheme(svg, dark ? 'dark' : 'light'));
  image.alt = pageNames(svg)[0] ?? 'Diagram';
  // A drawing is shown at its own size, and never wider than the page.
  image.decoding = 'async';
  figure.append(image);

  const status = message('', 'status');
  const fail = (error: unknown) => {
    status.textContent = `Could not save the file: ${errorText(error)}`;
  };
  const base = fileBaseName(svg);
  const tools = document.createElement('div');
  tools.className = 'tools';
  tools.append(
    button('SVG', 'Download this diagram as an SVG that draw.io can open and edit', () => {
      try {
        downloadSvg(svg, `${base}.drawio.svg`);
      } catch (error) {
        fail(error);
      }
    }),
    button('PNG', 'Download this diagram as PNG', () => {
      downloadPng(withColorScheme(svg, 'light'), `${base}.png`).catch(fail);
    }),
  );
  figure.append(tools);
  root.replaceChildren(figure, status);
}

main().catch((error: unknown) => {
  console.error('draw.io macro:', error);
  root.replaceChildren(message(`This diagram could not be shown: ${errorText(error)}`, 'error'));
});
