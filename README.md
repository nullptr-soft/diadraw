# DiaDraw

Ultimate diagram drawing: a drawing and diagramming engine with a text DSL **and** a WYSIWYG
editor on one model, shipped as web and desktop apps, with integrations and strong auto-layout.

## Vision

- **One model, two editors.** Elements and relationships are defined once; each diagram is a view.
  The text DSL and the canvas both edit the model through typed operations, so every canvas edit
  is written back as a small text patch, and the text stays the source of truth in git.
- **Layout you can steer.** An in-house layered engine behind one `layout(view, hints) → geometry`
  interface, plus a constraint pass that honours every hint, an orthogonal router, and stability
  across edits; `layout fixed` keeps hand-placed geometry and `layout sequence` draws sequence
  diagrams. Canvas drags become hints (`pin a below b`), not coordinates. A benchmark corpus
  scores every change.
- **Everywhere.** One C++ core in every host: a native CLI, native shells (AppKit, gtkmm, WinUI),
  and one WebAssembly module behind the web app (Web Components), the Tauri 2 desktop app and
  VS Code; a render server later, and importers for PlantUML, Mermaid, Structurizr, draw.io and
  BPMN.

See the [user guide](docs/guide/README.md), [docs/architecture.md](docs/architecture.md) and [docs/dsl.md](docs/dsl.md).

```
model {
  customer = person "Customer"
  shop = system "Shop" {
    web = container "Web app" tech "React"
    db  = container "Orders DB" tech "Postgres" shape cylinder
  }
  customer -> shop.web "places order"
  shop.web -> shop.db "writes"
}

view checkout of shop {
  include *
  layout layered direction right
  pin shop.db below shop.web
}
```

## Repository map

| Path                                               | Package                      | What it does                                                                                                                                                                                                        |
| -------------------------------------------------- | ---------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/`                                             | —                            | The C++ core (`libdiadraw`): model, DSL, layout, render, editor, importers, libraries, language server; native CLI, C ABI, WebAssembly and Python. See [src/README.md](src/README.md)                               |
| `src/wasm`                                         | `@diadraw/wasm`              | The core as one WebAssembly module with a typed, event-based API: parse, print, patch, render, import and export, editing sessions and a language server that report to listeners                                   |
| `src/python`                                       | `diadraw` (Python)           | The core as a Python package (nanobind): check, format, render, import and edit diagrams from Python. See [src/python/README.md](src/python/README.md)                                                              |
| `src/rust`                                         | `diadraw` (crate)            | Rust bindings over the C ABI: check, format, lay out, render, import and edit documents from Rust. See [src/rust/README.md](src/rust/README.md)                                                                     |
| `src/markdown`                                     | `@diadraw/markdown`          | Draws ` ```diadraw ` blocks in Markdown: Sätteri (Astro), remark and markdown-it plugins for build time, and `diadraw.js` for the browser. See [the guide](docs/guide/markdown.md)                                  |
| `src/node`                                         | `@diadraw/node`              | The core as a native Node.js addon (Node-API) for scripts and servers: parse, check, render SVG, PNG and PDF, import and export. See [src/node/README.md](src/node/README.md)                                       |
| `src/apps/web`                                     | `@diadraw/web`               | The web app: Web Components on `@diadraw/wasm`; text and canvas side by side, shared undo, files, import, export, libraries, share links                                                                            |
| `src/apps/vscode`                                  | `diadraw-vscode`             | VS Code extension on `@diadraw/wasm`: the core's language server, grammar, live preview, SVG export                                                                                                                 |
| `src/apps/confluence`                              | `@diadraw/confluence`        | Confluence Cloud app (Atlassian Forge): a DiaDraw diagram macro with an editor, live preview, themes and import, drawn in the page by `@diadraw/wasm`. See [the guide](docs/guide/confluence.md)                    |
| `src/apps/confluence-drawio`                       | `@diadraw/confluence-drawio` | Confluence Cloud app (Atlassian Forge): a diagram macro that opens the open-source draw.io editor, bundled with it, and shows the saved drawing in the page. See [its README](src/apps/confluence-drawio/README.md) |
| `src/apps/desktop`                                 | `@diadraw/desktop`           | Tauri 2 desktop app around the web app (native build pending a newer Rust)                                                                                                                                          |
| `src/apps/macos`, `src/apps/gtk`, `src/apps/winui` | —                            | Native shells (AppKit, gtkmm, WinUI) on the C++ core                                                                                                                                                                |
| `assets/`                                          | —                            | Built-in library sources (`builtin/`, compiled into the core) and draw.io's converted stencil sets (`drawio/`, see `NOTICE`)                                                                                        |
| `corpus/`                                          | —                            | Sample `.draw` files and diagrams in other formats, used by tests, benchmarks and import coverage                                                                                                                   |
| `docs/`                                            | —                            | [User guide](docs/guide/README.md), [architecture](docs/architecture.md), [DSL](docs/dsl.md), [libraries](docs/libraries.md), [decisions](docs/decisions.md), [benchmarks](docs/benchmarks.md)                      |
| `site/`                                            | `@diadraw/site`              | The website (Astro + Starlight): getting started, tutorial, reference generated from `docs/`, feedback, contact. See [site/README.md](site/README.md)                                                               |

## Using the CLI

The CLI is the native `diadraw` binary, built from the repository root (see [src/README.md](src/README.md)) into
`diadraw-build/<preset>` next to the checkout:

```bash
cmake --preset release && cmake --build --preset release
```

```bash
../diadraw-build/release/diadraw render corpus/c4-02-banking.draw --view containers -o banking.svg
```

```bash
../diadraw-build/release/diadraw import diagram.mmd -o diagram.draw
```

`render` writes SVG, PNG or PDF by the output extension. `check` reports errors as
`file:line:column`, `format` prints canonical source, and `import` converts Mermaid, PlantUML,
Structurizr and draw.io files (reporting anything it could not carry over). PlantUML covers
class, object, component, deployment, use case, state, activity, sequence, C4-PlantUML, mindmap
and WBS diagrams, with the preprocessor and a vendored C4-PlantUML. BPMN 2.0 files import with every diagram as a fixed view, and `render -o out.bpmn` writes BPMN back. Structurizr covers the DSL
(local `!include`s and `workspace extends`, archetypes, groups, deployment, every view type, with
dynamic views as sequence views) and workspace JSON, whose positions become fixed views. draw.io files
(`.drawio`, `.xml`, `.drawio.svg`, `.drawio.png`) keep their positions, sizes and routes unless
you pass `--geometry drop`, and `render -o out.drawio` exports views back to draw.io.

`diadraw mcp` serves the same commands to AI agents over the Model Context Protocol: Claude Code,
Claude Desktop and other agents can check a diagram, render it, look at the image and fix it
(`claude mcp add diadraw -- /path/to/diadraw mcp`). See
[Drawing with AI agents](docs/guide/agents.md).

Documents can use shape libraries (`use "c4@^1"`, then `shape c4.database`). `diadraw lib`
creates, builds, packs and installs them, pinning versions in `diadraw-lock.json`; rendering
never fetches. See [docs/libraries.md](docs/libraries.md).

## Drawing with Claude Code

Because diagrams are text, coding agents can draw them. The [DiaDraw plugin](plugins/diadraw/README.md)
gives Claude Code a skill that writes the DSL, runs `diadraw check` and `diadraw render`, looks at
the picture and fixes it:

```
/plugin marketplace add mykulyak/diadraw
/plugin install diadraw@diadraw
```

It needs the `diadraw` CLI on `PATH` (or `DIADRAW` set to it).

## Development

Requires Node 20+ and npm (npm workspaces; `packageManager` in `package.json` names the version).

```bash
npm install
```

```bash
npm run lint      # ESLint + Prettier check, and the version check below
```

```bash
npm run typecheck # tsc in every package
```

```bash
npm test          # Vitest in every package
```

```bash
npm run build     # tsc builds + Vite build of the web app
```

```bash
npm run dev:web   # web app on http://localhost:5173
```

```bash
npm run format    # Prettier --write
```

Turborepo runs tasks in dependency order and caches them; packages build to `dist/` and
dependents consume the built output.

### Version

The `VERSION` file holds DiaDraw's version (now `0.0.1`). CMake reads it for the core
(`dd_version()`, which the CLI's `--version`, the language server, BPMN export, the WASM module
and the web app report), the macOS bundle and the Windows package; the website reads it at build
time and shows it beside its title. Each app shows it in Help › About (macOS: DiaDraw › About;
the web app: beside its title). The package manifests and a few other files keep a copy:
`npm run lint` checks them against `VERSION`, and `npm run version:sync` updates them.

To release a new version, run `npm run version:bump -- patch` (or `minor`, `major`, or an explicit
`X.Y.Z` later than the current one). It writes `VERSION`, adds the release with today's date to
the GTK app's AppStream metadata, updates every copy, and commits exactly those files as "Bump
the version to X.Y.Z", leaving any other change in the working tree or the index out of the
commit. It stops before changing anything when one of those files already has uncommitted changes.
Pass `--no-commit` to only update the files. It does not tag.

## Website and GitHub Pages

The website (`site/`) and the web app are published together on GitHub Pages: the site at the
root, the app under `app/`. Every push to `main` builds and deploys both
(`.github/workflows/pages.yml`); pull requests that touch them build them as a check.

```bash
npm run dev:site      # the website on http://localhost:4321 (npm ci --prefix site first)
npm run pages:build   # site + app into ../diadraw-build/pages (needs Emscripten, like the web app)
npm run pages:preview # serve that folder on http://localhost:4321
npm run pages:deploy  # run the Pages workflow on main now (GitHub CLI)
```

`pages:build -- --url https://<owner>.github.io/<repo>/` builds for that address instead, as the
workflow does. Pages must be set to deploy from GitHub Actions once, in the repository's
Settings → Pages.

### Publishing without GitHub Actions

When the workflow cannot run, publish from your own machine to a `gh-pages` branch instead:

```bash
npm run pages:publish                # build for https://<owner>.github.io/<repo>/ and push gh-pages
npm run pages:publish -- --dry-run   # build and commit, but do not push
npm run pages:publish -- --no-build  # push the last pages:build as it is
```

It builds as `pages:build` does (Emscripten needed), adds `.nojekyll`, and force-pushes the
folder as the only commit of `gh-pages`, so the branch never holds anything but the latest
build. Set Settings → Pages → Source to **Deploy from a branch**, `gh-pages`, `/ (root)`, once.
`--url`, `--remote` and `--branch` change where it goes; it refuses `main` and `master`.

## Licensing

DiaDraw's own code, documentation and assets are under the [MIT License](LICENSE), except:

- `assets/drawio/`: draw.io's stencil sets, converted. Apache-2.0, with draw.io's restriction on
  using them in Atlassian products; see [assets/NOTICE](assets/NOTICE).
- `third_party/`: vendored code under its own licence (C4-PlantUML, MIT).
- `corpus/` other than its own `*.draw` examples: diagrams from other projects, used only as test
  data, each folder under the licence of its source (see the READMEs there).
- `src/apps/confluence-drawio`: the draw.io editor it bundles is fetched at build time and keeps
  draw.io's Apache-2.0 licence.

[docs/credits.md](docs/credits.md) lists every third-party library and asset the apps ship, with
its licence; it is the website's Credits page, and the apps show their part of it in their About
dialogs. It is generated: edit [tools/credits/components.json](tools/credits/components.json), or
change a dependency, and run `node tools/gen/credits.mjs` (`npm run lint` fails when it is stale).
