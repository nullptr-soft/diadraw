import { loadDiaDraw } from '@diadraw/wasm';
import './app';

// The C++ core loads once; the app starts when it is ready.
const app = document.querySelector('dd-app')!;
loadDiaDraw()
  .then((dd) => app.start(dd))
  .catch((error: unknown) => {
    app.textContent = `DiaDraw could not start: ${error instanceof Error ? error.message : String(error)}`;
  });
