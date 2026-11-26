import { defineConfig } from 'vite';

export default defineConfig({
  // Where the app is served: `/` by default, `/<repo>/app/` on GitHub Pages (tools/pages/build.mjs).
  base: process.env.DIADRAW_WEB_BASE || '/',
  // The C++ core loads its .wasm file through import.meta.url; Vite must not pre-bundle it.
  optimizeDeps: { exclude: ['@diadraw/wasm'] },
  server: {
    fs: { allow: ['../../..'] },
  },
});
