// The web app's Content Security Policy (vite.config.ts puts it in index.html). It is the second
// line of defence against markup a document smuggles into the page: the core escapes what it
// writes into SVG, and this policy keeps any script that slips through from running or loading.
//
// - Scripts come from the app's own files only; 'wasm-unsafe-eval' lets the C++ core, which is
//   WebAssembly, compile. No inline scripts, no eval, no event handler attributes.
// - Styles may be inline: the canvas's shadow root and Vite's development server use <style>.
// - Network: the app itself, and the Google Drive and OneDrive APIs of the Storage dialog (src/cloud.ts),
//   including the hosts OneDrive hands out download links on.
// - Images: the app's files and the data: and blob: URLs of exports and previews.

/** Hosts the Storage dialog talks to. */
export const CLOUD_HOSTS = [
  'https://www.googleapis.com',
  'https://login.microsoftonline.com',
  'https://graph.microsoft.com',
  // `@microsoft.graph.downloadUrl`: personal OneDrive, OneDrive for Business.
  'https://*.1drv.com',
  'https://*.microsoftpersonalcontent.com',
  'https://*.sharepoint.com',
];

export interface PolicyOptions {
  /** Vite's development server: its client talks to it over a WebSocket. */
  dev?: boolean;
}

export function contentSecurityPolicy({ dev = false }: PolicyOptions = {}): string {
  const directives: [string, string[]][] = [
    ['default-src', ["'self'"]],
    ['script-src', ["'self'", "'wasm-unsafe-eval'"]],
    ['style-src', ["'self'", "'unsafe-inline'"]],
    ['img-src', ["'self'", 'data:', 'blob:']],
    ['font-src', ["'self'", 'data:']],
    ['connect-src', ["'self'", ...(dev ? ['ws:', 'wss:'] : []), ...CLOUD_HOSTS]],
    ['worker-src', ["'self'", 'blob:']],
    ['object-src', ["'none'"]],
    ['base-uri', ["'self'"]],
    ['form-action', ["'none'"]],
  ];
  return directives.map(([name, sources]) => `${name} ${sources.join(' ')}`).join('; ');
}
