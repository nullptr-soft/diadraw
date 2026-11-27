import { defineConfig, type Plugin } from 'vite';
import { contentSecurityPolicy } from './src/csp';

// The page's Content Security Policy, as a <meta> tag: GitHub Pages cannot send headers. The
// desktop app (Tauri sets TAURI_ENV_PLATFORM while it builds or serves the app) keeps the policy
// of src/apps/desktop/src-tauri/tauri.conf.json, which Tauri adds to for its own IPC.
function csp(): Plugin {
  return {
    name: 'diadraw-csp',
    transformIndexHtml: {
      order: 'pre',
      handler(_html, ctx) {
        if (process.env.TAURI_ENV_PLATFORM) return [];
        return [
          {
            tag: 'meta',
            attrs: {
              'http-equiv': 'Content-Security-Policy',
              content: contentSecurityPolicy({ dev: ctx.server !== undefined }),
            },
            injectTo: 'head-prepend',
          },
        ];
      },
    },
  };
}

export default defineConfig({
  // Where the app is served: `/` by default, `/<repo>/app/` on GitHub Pages (tools/pages/build.mjs).
  base: process.env.DIADRAW_WEB_BASE || '/',
  plugins: [csp()],
  // The C++ core loads its .wasm file through import.meta.url; Vite must not pre-bundle it.
  optimizeDeps: { exclude: ['@diadraw/wasm'] },
  server: {
    fs: { allow: ['../../..'] },
  },
});
