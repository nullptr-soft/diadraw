# @diadraw/node

The DiaDraw C++ core (`src/`) as a native Node.js addon (Node-API), for scripts, build tools and
servers that work with diagrams outside a browser. It covers the one-shot calls of
`@diadraw/wasm` with the same value shapes (models, operations, diagnostics with UTF-16 offsets),
and adds what only a native build has: PNG and PDF output, and libraries resolved like the
`diadraw` command line resolves them (the `diadraw-lock.json` next to the file, the library cache,
the built-in libraries and draw.io's stencil sets, all offline).

```bash
npm run build -w @diadraw/node   # CMake 3.25+ and a C++20 compiler (MSVC on Windows)
npm test -w @diadraw/node
```

The build runs [cmake-js](https://github.com/cmake-js/cmake-js) on the repository root with
`DIADRAW_BUILD_NODE=ON` (tests, the CLI and the apps off) into `diadraw-build/node` next to the
checkout (or `$DIADRAW_BUILD_DIR/node`), and copies `diadraw.node` into `dist/`. `--debug` builds
the Debug configuration; `--CDNAME=value` passes `-DNAME=value` to CMake. The addon uses Node-API
version 8, so one build runs on every Node from 20 on, and in worker threads. It is tested on
Linux, macOS and Windows with Node 20 and 22 (the `node` job in `.github/workflows/cpp.yml`).

## Calls

Everything is synchronous and throws on failure (a `TypeError` for an argument of the wrong type).

```ts
import { check, format, importDiagram, parse, print, renderPng, renderSvg } from '@diadraw/node';
import { readFileSync, writeFileSync } from 'node:fs';

const file = 'docs/architecture.draw';
const source = readFileSync(file, 'utf8');

// Diagnostics and library warnings, as `diadraw check` reports them.
const { ok, diagnostics, libraryWarnings } = check(source, { file });

// Render a view (the first one by default) from source or from a parsed model.
writeFileSync('containers.svg', renderSvg(source, { file, view: 'containers' }));
writeFileSync('containers.png', renderPng(source, { file, view: 'containers', scale: 2 }));

// Import Mermaid, PlantUML, Structurizr, draw.io or BPMN, and write DiaDraw source.
const { model, report } = importDiagram(readFileSync('flow.mmd', 'utf8'), 'flow.mmd');
writeFileSync('flow.draw', print(model));
```

| Call                                                      | Returns                                                                               |
| --------------------------------------------------------- | ------------------------------------------------------------------------------------- |
| `version`                                                 | the core's version                                                                    |
| `parse(source)`                                           | `{ model, diagnostics, sourceMap, operations }`                                       |
| `print(model)`                                            | canonical DSL source                                                                  |
| `format(source)`                                          | the source reprinted canonically, as `diadraw format`; throws when it has errors      |
| `check(source, { file })`                                 | `{ ok, diagnostics, libraryWarnings }`                                                |
| `highlight(text)`                                         | `[from, to, tag]` syntax runs                                                         |
| `updateSource(source, operation)`                         | the text update an operation makes (`{ source, edits, model, minimal, diagnostics }`) |
| `renderSvg(input, { file, view })`                        | a standalone SVG document                                                             |
| `renderPng(input, { …, scale, background })`              | the PNG file's bytes (a `Buffer`)                                                     |
| `renderPdf(input, { file, view })`                        | a one-page vector PDF (a `Buffer`)                                                    |
| `exportDrawio(input, { file, view })`                     | the view, or every view one page each, laid out as a draw.io file                     |
| `exportBpmn(input, { file, view })`                       | the view, or every view, laid out as BPMN 2.0 XML                                     |
| `importDiagram(data, fileName, { format, keepGeometry })` | `{ model, report }`; `data` is text or bytes, includes are read next to `fileName`    |

`input` is DSL source or a model. `file` is where the document is (or would be) saved, which
decides the lockfile its libraries are pinned by; it defaults to a file in the current directory.
Source with errors is refused by the calls that draw or export it. What `diadraw render` warns
about (libraries it could not supply, layout hints it could not honour) is emitted as a process
warning named `DiaDrawWarning`; Node prints those to stderr unless you listen for `warning` events
(`process.on('warning', …)`) or run with `--no-warnings`.

## Not here yet

The editing `Session` and the `LanguageServer` of `@diadraw/wasm` (UI-side objects with
listeners), adding libraries at run time, and prebuilt binaries for publishing to npm: the
package builds from this repository.
