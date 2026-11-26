# DiaDraw for VS Code

Diagrams as code in `.draw` files: syntax highlighting (also inside ` ```diadraw ` fences
in Markdown), diagnostics, outline, go to definition, hover, completion, a live preview beside
the editor (**DiaDraw: Open Preview to the Side**), and **DiaDraw: Export View as SVG**.

Everything runs on the DiaDraw core compiled to WebAssembly (`@diadraw/wasm`), so the extension
has no native binaries: the language server is the core's (`dist/server.cjs` frames its messages
over stdio), and previews and exports render through a core session. Libraries resolve like the
CLI resolves them: `diadraw-lock.json` next to the file, the library cache (`$DIADRAW_CACHE` or
`~/.cache/diadraw/libraries`) and the built-in libraries; set `DIADRAW_DRAWIO` to a folder of
converted draw.io stencil sets to use those too.

## Develop

```bash
npm run build -w @diadraw/wasm   # needs Emscripten; see src/wasm
npm run build -w diadraw-vscode
```

Then open `src/apps/vscode` in VS Code and press F5 to start an Extension Development Host.

To make a `.vsix`, install `@vscode/vsce` and run `vsce package --no-dependencies` in this
folder (everything is bundled into `dist/`). Publishing needs a Marketplace publisher id.
