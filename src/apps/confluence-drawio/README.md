# draw.io diagrams for Confluence Cloud

An [Atlassian Forge](https://developer.atlassian.com/platform/forge/) app that adds a **Diagram
(draw.io editor)** macro to Confluence Cloud. Inserting or editing the macro opens the
open-source [draw.io](https://github.com/jgraph/drawio) editor in embed mode, filling the
dialog. draw.io is bundled with the app, so Atlassian serves it with the app's own files and
nothing is loaded from draw.io's servers; **Save & Exit** stores the diagram with the macro and the page shows it as an image.
The user guide is the "Drawing in the draw.io editor" section of
[docs/guide/confluence.md](../../../docs/guide/confluence.md).

draw.io is © JGraph Ltd under the Apache License 2.0. This app is not made, endorsed or supported
by JGraph or draw.io. It bundles their editor unchanged, from the `draw.war` of a pinned release,
with draw.io's `LICENSE` beside it in `dist/editor/drawio/`.

| File                           | What it is                                                                                                |
| ------------------------------ | --------------------------------------------------------------------------------------------------------- |
| `manifest.yml`                 | The Forge manifest: the `drawio-diagram` macro, its editor, the export function and the script policy     |
| `scripts/fetch-drawio.mjs`     | Downloads the pinned draw.io release, checks its SHA-256 and unpacks what the editor loads into `vendor/` |
| `src/editor.ts`                | The macro's editor (Custom UI resource `editor`): draw.io in a frame, and Save & Exit / Exit              |
| `src/drawio.ts`                | draw.io's embed URL and JSON message protocol: load the diagram, export an editable SVG on save           |
| `src/macro.ts`                 | The macro in the page (resource `macro`): the saved drawing as an image, with SVG and PNG download        |
| `src/diagram.ts`               | The stored diagram: one editable SVG (the drawing plus the `.drawio` file), deflated and Base64-encoded   |
| `src/index.ts`                 | The Forge function `adfExport`: what PDF and Word exports show in place of the macro                      |
| `src/dev/bridge.ts`, `static/` | The local preview's stand-in for `@forge/bridge`, and the HTML and CSS of both resources                  |

How it works: the editor posts `load` with the stored SVG when draw.io sends `init`. On Save &
Exit draw.io sends `save`, the editor asks for an `export` in `xmlsvg` format (the drawing with
the whole file, every page, in its `content` attribute) and stores that with `view.submit`; an
empty diagram stores nothing. Exit closes without storing. The macro in the page never loads
draw.io: it decodes the SVG and shows it as an `<img>`, so scripts in a stored SVG cannot run, and
pins draw.io's `light-dark()` colours to the page's theme. The SVG download opens in draw.io
again.

draw.io is part of the editor resource, at `dist/editor/drawio/`, so its frame has the same
origin as the editor and the diagram never leaves Atlassian's servers and the reader's browser.
The manifest declares no external frames, fetches or egress. It allows inline styles and
`unsafe-eval`, which draw.io needs; only the editor and draw.io run with them, and the macro in
the page draws an image.

## The bundled draw.io

`npm run build` runs `scripts/fetch-drawio.mjs`, which downloads `draw.war` of the release pinned
in that script from GitHub (about 53 MB, once: it is kept in `vendor/`), checks it against the
pinned SHA-256 and unpacks it without what an embedded editor never loads: the server side,
source maps, unminified sources, the offline service worker, the viewer and the bundles of
draw.io's other integrations. What is left is about 70 MB, 29 MB zipped, inside Forge's limit
for a resource. `npm run fetch-drawio` does the download alone.

Forge counts the files of each production deploy against a weekly quota (75 MB for a free app),
so deploy draw.io changes to production in batches; development deploys are not counted.

To move to a newer draw.io, change `VERSION` in `scripts/fetch-drawio.mjs`, clear the two hashes
and run `npm run fetch-drawio`, which prints the new ones to pin. Then build and try the editor
(`npm run dev`) before deploying.

## Building and trying it locally

```bash
npm run build -w @diadraw/confluence-drawio   # dist/macro and dist/editor
npm test -w @diadraw/confluence-drawio
npm run dev -w @diadraw/confluence-drawio     # http://localhost:5181/: the editor above the macro
```

The local preview runs both resources with a stand-in for Confluence that keeps the macro's
configuration in `localStorage`: Save & Exit in the editor redraws the macro below it.

### Loading draw.io from elsewhere

Build with `DRAWIO_URL` to load the editor from another copy of draw.io instead of bundling one:
`https://embed.diagrams.net/`, which keeps the app small and always current, or the
[jgraph/drawio](https://hub.docker.com/r/jgraph/drawio) Docker image on your network. Add that
origin to `manifest.yml` under `permissions.external.frames` (the manifest has it commented out
for embed.diagrams.net); Atlassian then tells the administrator who installs the app that it
exchanges data with that address.

```bash
DRAWIO_URL=https://embed.diagrams.net/ npm run build -w @diadraw/confluence-drawio
```

Do not add `offline=1` to that URL: draw.io hides the Save & Exit and Exit buttons in offline mode.

## Deploying to a Confluence site

You need an Atlassian account, a Confluence Cloud site where you are an administrator (a free
[developer site](https://go.atlassian.com/cloud-dev) works) and the Forge CLI:

```bash
npm install -g @forge/cli
forge login                            # with an Atlassian API token
cd src/apps/confluence-drawio
forge register                         # once: gives the app its own id in manifest.yml
npm run build && forge deploy          # to the development environment
forge install --product confluence --site <your-site>.atlassian.net
```

Give the app a name of your own when `forge register` asks, not "draw.io" alone. After a change,
`npm run build && forge deploy` updates the installed app; `forge deploy -e production` and
`forge install -e production` install the production environment, the one to use for real pages.

## Not done yet

- Listing the app in the Atlassian Marketplace.
- The PDF and Word export shows a note, not the picture.
- The page shows the draw.io page that was open when the diagram was saved; the others are kept
  in the file and open in the editor.
- Very large diagrams may be more than Confluence keeps in a macro's configuration; saving then
  fails with Confluence's message, and the diagram stays open in the editor.
