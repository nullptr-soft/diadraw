# Diagrams in Markdown

Write a diagram straight into a Markdown page as a fenced code block in the `diadraw` language,
the same way Mermaid diagrams are written, and DiaDraw draws it where the page is shown:

````md
```diadraw
model {
  customer = person "Customer"
  shop = system "Shop"
  customer -> shop "places order"
}
```
````

The diagram stays text in your documentation, so it is reviewed and versioned with everything
around it. On this website, the block below is drawn by the Sätteri plugin when the site is built:

```diadraw render
model {
  customer = person "Customer"
  shop = system "Shop" {
    web = container "Web app" tech "React"
    db = container "Orders DB" tech "Postgres" shape cylinder
  }
  customer -> shop.web "places order"
  shop.web -> shop.db "writes"
}

view checkout of shop {
  include *
  layout layered direction right
}
```

There are two ways to draw the blocks, and both use the same core as the apps, so a diagram
looks the same everywhere:

- **When the site is built**: a plugin for the Markdown tool replaces each block with the
  diagram as SVG in the page. Readers get a plain picture, with no script to load. Use this for
  documentation sites (Astro and Starlight, Docusaurus, VitePress, Next.js, Eleventy).
- **In the reader's browser**: one script, `diadraw.js`, finds the blocks on the page and draws
  them. Use this when you cannot add a plugin to whatever renders the Markdown, such as a wiki, a
  blog engine or a CMS.

## Writing a block

The block's language is `diadraw`. Words after it on the fence line change how it is drawn:

| Fence                      | What it draws                                                              |
| -------------------------- | -------------------------------------------------------------------------- |
| ` ```diadraw `             | The document's first view (or every element, when it has no `view` block)  |
| ` ```diadraw view=people ` | The view named `people`                                                    |
| ` ```diadraw source `      | Nothing: the block stays as code, for showing the text itself              |
| ` ```diadraw render `      | The diagram, when the plugin is set to draw only marked blocks (see below) |

A block whose text has errors is not drawn. At build time the build stops and reports each error
with its line in the Markdown file; in the browser the block stays as code with the errors under
it.

## When the site is built

The plugins live in [`src/markdown`](../../src/markdown/README.md) (`@diadraw/markdown`). They are
not on npm yet; until they are, build them from the repository (`npm ci`, then
`npx turbo run build --filter=@diadraw/markdown`, which needs Emscripten for the core) and add
the package folder to your project.

**Astro and Starlight** use the Sätteri plugin:

```js
// astro.config.mjs (Astro 7, whose default Markdown processor is Sätteri)
import { satteri } from '@astrojs/markdown-satteri';
import satteriDiadraw from '@diadraw/markdown/satteri';

export default defineConfig({
  markdown: { processor: satteri({ mdastPlugins: [satteriDiadraw()] }) },
});
```

**remark** draws blocks for every tool built on remark, including Astro with the `unified()` processor. In Docusaurus, add it to the docs (and blog) options:
`remarkPlugins: [require('@diadraw/markdown/remark').default]`. In Next.js, pass it to
`@next/mdx` as `options: { remarkPlugins: [remarkDiadraw] }`.

**markdown-it** draws blocks for VitePress, Eleventy and anything else on markdown-it. It renders
synchronously, so load the core first:

```js
// .vitepress/config.mjs
import { loadDiaDraw } from '@diadraw/wasm';
import markdownItDiadraw from '@diadraw/markdown/markdown-it';

const diadraw = await loadDiaDraw();
export default { markdown: { config: (md) => md.use(markdownItDiadraw, { diadraw }) } };
```

Both take the same options:

| Option      | What it does                                                                                                        |
| ----------- | ------------------------------------------------------------------------------------------------------------------- |
| `blocks`    | `all` (the default) draws every `diadraw` block; `marked` draws only those marked `render`                          |
| `onError`   | remark and Sätteri: `fail` (the default) or `warn`, which keeps the block as code; markdown-it: `throw` or `source` |
| `languages` | The code block languages to draw; `['diadraw']` by default                                                          |
| `className` | The class of the `<figure>` around each diagram; `diadraw` by default                                               |

Use `blocks: 'marked'` when most blocks on your pages teach the language rather than show a
diagram. This website does that.

## In the reader's browser

Add the script to the page, after the Markdown is rendered:

```html
<script type="module" src="https://mykulyak.github.io/diadraw/embed/diadraw.js"></script>
```

It finds every `<pre><code class="language-diadraw">` (and `lang-diadraw`, the other spelling
renderers use), draws it and puts the diagram in its place. The core, `diadraw.wasm`, loads from
beside the script, once per page. To serve it yourself, copy both files from
`src/markdown/dist/embed/` to your site.

- To pick a view, give the `<pre>` or `<code>` a `data-view` attribute.
- For a page that adds content after loading, call `diadraw.render()` again; blocks already
  drawn are skipped.
- To draw only when you say so, set `window.diadrawConfig = { startOnLoad: false }` before the
  script runs, and call `diadraw.render()` yourself.
- In your own bundled code, `import { renderDiagrams } from '@diadraw/markdown/browser'` does the
  same.

## Not yet supported

- **GitHub and GitLab** show Markdown without running anyone else's code, so they show
  `diadraw` blocks as text. A GitHub Action that draws each block into an SVG file next to the
  page, and links it from the page, is planned.
- **MkDocs and Python-Markdown**: an extension that draws blocks with the `diadraw` command-line
  tool is planned.
