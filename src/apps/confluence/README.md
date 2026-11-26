# DiaDraw for Confluence Cloud

An [Atlassian Forge](https://developer.atlassian.com/platform/forge/) app that adds a **DiaDraw
diagram** macro to Confluence Cloud. The macro's DiaDraw text is stored with the macro, and the
diagram is drawn in the browser by the core as WebAssembly (`@diadraw/wasm`), so the app has no
server and no egress. The user guide is [docs/guide/confluence.md](../../../docs/guide/confluence.md).

| File                           | What it is                                                                                          |
| ------------------------------ | --------------------------------------------------------------------------------------------------- |
| `manifest.yml`                 | The Forge manifest: the `diadraw` macro, its editor, the export function and the script policy      |
| `src/macro.ts`                 | The macro in the page (Custom UI resource `macro`): draws the saved text, with SVG and PNG download |
| `src/editor.ts`                | The macro's editor (resource `editor`): text, live preview, view and theme pickers, import          |
| `src/diagram.ts`               | What both share: the stored configuration, drawing, themes and import                               |
| `src/publish.ts`, `src/cli.ts` | The publisher: `.draw` files drawn as SVG and attached to a page (`dist/cli.js publish`)            |
| `action.yml`                   | The GitHub Action `DiaDraw to Confluence`, which builds the core and runs the publisher             |
| `src/index.ts`                 | The Forge function `adfExport`: the macro in PDF and Word exports, as a code block of the text      |
| `src/dev/bridge.ts`, `static/` | The local preview's stand-in for `@forge/bridge`, and the HTML and CSS of both resources            |

The core is compiled to WebAssembly, which a Custom UI page may compile only when its script
policy allows it, so the manifest adds `unsafe-eval` to `permissions.content.scripts`.

## Building and trying it locally

```bash
npm run build -w @diadraw/wasm         # the core as WebAssembly (needs Emscripten)
npm run build -w @diadraw/markdown
npm run build -w @diadraw/confluence   # dist/macro and dist/editor, the two Custom UI resources
npm test -w @diadraw/confluence
npm run dev -w @diadraw/confluence     # http://localhost:5180/: the editor above the macro
```

The local preview runs both resources with a stand-in for Confluence that keeps the macro's
configuration in `localStorage`: Save in the editor redraws the macro below it.

## Deploying to a Confluence site

You need an Atlassian account, a Confluence Cloud site where you are an administrator (a free
[developer site](https://go.atlassian.com/cloud-dev) works) and the Forge CLI:

```bash
npm install -g @forge/cli
forge login                            # with an Atlassian API token
cd src/apps/confluence
forge register                         # once: gives the app its own id in manifest.yml
npm run build && forge deploy          # to the development environment
forge install --product confluence --site <your-site>.atlassian.net
```

After a change, `npm run build && forge deploy` updates the installed app. `forge deploy -e production` and
`forge install -e production` install the production environment, which is the one to use for
real pages.

## Publishing `.draw` files to a page

`npm run build` also writes `dist/cli.js`, which attaches each view of `.draw` files to a page as
SVG and adds a new version only when a drawing changed:

```bash
export CONFLUENCE_BASE_URL=https://example.atlassian.net CONFLUENCE_EMAIL=you@example.com
export CONFLUENCE_API_TOKEN=…
node dist/cli.js publish ../../../corpus/c4-02-banking.draw --page 123456 --dry-run
```

It uses the REST API with an API token: v1 attachments (`PUT …/child/attachment`, which creates
an attachment or adds a version) and, for `--insert`, v2 pages. The attachment's comment holds a
hash of the SVG, which is how an unchanged drawing is skipped. `action.yml` runs the same in
GitHub Actions; workflows in other repositories can use it while this repository is public, or
when its Actions settings share it with them. The user guide shows a workflow.

## Not done yet

- Listing the app in the Atlassian Marketplace.
- The PDF and Word export shows the text, not the picture.
