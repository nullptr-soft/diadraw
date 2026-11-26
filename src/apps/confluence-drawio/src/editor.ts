/**
 * The macro's editor, which Confluence opens when the macro is inserted and from its Edit button:
 * the draw.io editor in a frame that fills the dialog. Save & Exit stores the diagram with the
 * macro (`view.submit`); Exit leaves the macro as it was (`view.close`).
 */
import { view } from '@forge/bridge';
import { isEmptyDiagram, packSvg, readConfig, svgFromDataUri, unpackSvg } from './diagram.js';
import { BUNDLED_DRAWIO_PATH, Session, editorUrl, errorText, parseEvent } from './drawio.js';

declare const DRAWIO_URL: string | undefined;
const drawioUrl = new URL(
  typeof DRAWIO_URL === 'string' && DRAWIO_URL ? DRAWIO_URL : BUNDLED_DRAWIO_PATH,
  location.href,
).href;

const frame = document.getElementById('drawio') as HTMLIFrameElement;
const status = document.getElementById('status') as HTMLParagraphElement;
const cancel = document.getElementById('cancel') as HTMLButtonElement;

function show(text: string, error = false): void {
  status.textContent = text;
  status.classList.toggle('error', error);
  document.body.classList.toggle('failed', error);
}

function isDark(theme: unknown): boolean {
  const mode = (theme as { colorMode?: unknown } | undefined)?.colorMode;
  if (mode === 'dark' || mode === 'light') return mode === 'dark';
  return document.documentElement.dataset.colorMode === 'dark';
}

async function main(): Promise<void> {
  await view.theme.enable().catch(() => undefined);
  const context = await view.getContext();
  const config = readConfig(context.extension?.config);
  let svg = '';
  if (config.diagram) {
    try {
      svg = await unpackSvg(config.diagram);
    } catch (error) {
      // Opens an empty diagram rather than none; Exit keeps the stored one as it was.
      console.error('draw.io macro:', error);
    }
  }

  const origin = new URL(drawioUrl).origin;
  const session = new Session(
    {
      post: (action) => frame.contentWindow?.postMessage(JSON.stringify(action), origin),
      save: async (diagram) => {
        await view.submit({ config: diagram ? { diagram: await packSvg(diagram) } : {} });
      },
      close: () => void view.close(),
      fail: (message) => {
        frame.contentWindow?.postMessage(
          JSON.stringify({ action: 'dialog', title: 'Error', message, button: 'OK' }),
          origin,
        );
      },
    },
    svg,
    isEmptyDiagram,
    svgFromDataUri,
  );

  let ready = false;
  addEventListener('message', (event) => {
    if (event.source !== frame.contentWindow || event.origin !== origin) return;
    const message = parseEvent(event.data);
    if (!message) return;
    if (message.event === 'init') {
      ready = true;
      // Also clears a slow-loading warning: draw.io can take long the first time.
      document.body.classList.remove('loading', 'failed');
    }
    void session.handle(message);
  });

  frame.src = editorUrl(drawioUrl, {
    lang: (context as { locale?: string }).locale,
    dark: isDark((context as { theme?: unknown }).theme),
  });
  // draw.io is about 17 MB, which the first open downloads and the browser then caches, so a slow
  // start is not a failure: say what is happening, and give up only after a long wait.
  setTimeout(() => {
    if (!ready)
      show(
        'Still loading the draw.io editor. The first time it downloads about 17 MB; after that your browser keeps it.',
      );
  }, 10_000);
  setTimeout(() => {
    if (!ready)
      show(
        `The draw.io editor has not started after two minutes (${drawioUrl}). Reload the page, or check that the app is installed in full.`,
        true,
      );
  }, 120_000);
}

cancel.addEventListener('click', () => void view.close());

main().catch((error: unknown) => {
  console.error('draw.io macro:', error);
  show(`The editor could not start: ${errorText(error)}`, true);
});
