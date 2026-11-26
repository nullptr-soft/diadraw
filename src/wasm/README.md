# @diadraw/wasm

The DiaDraw C++ core (`src/`) as one WebAssembly module, with a typed API for JavaScript hosts:
the web app, the VS Code extension and the Tauri desktop app. All diagram logic is the core's;
this package only converts values (JSON, UTF-16 offsets) and fans events out to listeners.

```bash
npm run build -w @diadraw/wasm   # needs Emscripten (EMSDK, or emsdk in ~/emsdk or ~/Software/emsdk)
```

The build runs the `wasm` CMake preset into `diadraw-build/wasm` next to the checkout (or
`$DIADRAW_BUILD_DIR/wasm`) and copies `diadraw.mjs` and `diadraw.wasm` into `dist/core/`.

## Calls and events

```ts
import { loadDiaDraw } from '@diadraw/wasm';

const dd = await loadDiaDraw();

// One-shot calls for pure work.
const { model, diagnostics } = dd.parse(text);
const svg = dd.renderSvg(model, 'default');
const { model: imported, report } = dd.importDiagram(mermaid, 'flow.mmd');

// Long-lived objects report what happens to them.
const session = dd.createSession(text);
session.on('change', ({ update, state }) => {
  // `update`: the text edits a gesture made (absent when the host set the text);
  // `state`: model, diagnostics and views after the change.
});
session.on('libraries', () => redraw());
session.setSource(edited); // emits change
session.connect('a', 'b', 'calls'); // emits change with the update

const server = dd.createLanguageServer();
server.on('message', (message) => client.send(message)); // replies and diagnostics
server.send({ jsonrpc: '2.0', id: 1, method: 'initialize', params: {} });
```

`on` returns a function that removes the listener. `dispose()` frees a session or a server.

## Libraries on disk

In Node, give the core the file system so sessions of saved files and the language server
resolve libraries like the native CLI: `diadraw-lock.json` next to the document, the library
cache and draw.io's converted stencil sets.

```ts
import { nodeFileSystem } from '@diadraw/wasm/node';

dd.setFileSystem(nodeFileSystem(), { drawio: '/path/to/diadraw/assets/drawio' });
const session = dd.createSession(text, { path: '/work/diagram.draw' });
```

In the browser there is no file system; documents use the built-in libraries and those added
with `addLibrary`.
