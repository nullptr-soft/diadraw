# @diadraw/markdown

Draws ` ```diadraw ` code blocks in Markdown as diagrams, with the DiaDraw core
([`@diadraw/wasm`](../wasm/README.md)), the way Mermaid blocks are drawn. How to use it on a
site is in the user guide: [Diagrams in Markdown](../../docs/guide/markdown.md).

| Entry point                       | What it is                                                                           |
| --------------------------------- | ------------------------------------------------------------------------------------ |
| `@diadraw/markdown/satteri`       | Sätteri plugin, for Astro 7 and Starlight (`markdown.processor: satteri({ … })`)     |
| `@diadraw/markdown/remark`        | remark plugin: Docusaurus, Next.js with MDX, Gatsby, Astro with `unified()`, unified |
| `@diadraw/markdown/markdown-it`   | markdown-it plugin: VitePress, Eleventy; takes a loaded core                         |
| `@diadraw/markdown/browser`       | `renderDiagrams()`, for drawing blocks already on a page from bundled code           |
| `dist/embed/diadraw.js` + `.wasm` | One script for any page; draws the blocks when the page loads                        |
| `@diadraw/markdown`               | `renderBlock()` and `parseMeta()`, which all of the above share                      |

The build plugins replace each block with `<figure class="diadraw" data-view="…"><svg …></figure>`.
Ids in the SVG are prefixed per block, so several diagrams on one page keep their arrowheads
apart, and the SVG scales down to the column (`max-width: 100%`).

```bash
npm run build -w @diadraw/markdown   # after @diadraw/wasm; turbo builds both
npm test -w @diadraw/markdown
```

To try `diadraw.js`, serve this folder over HTTP (`python3 -m http.server -d src/markdown`) and
open `examples/browser.html`. The Pages build (`tools/pages/build.mjs`) publishes it under
`embed/` on the website, where pages anywhere can load it.

## Publishing

The package and `@diadraw/wasm` are marked private, so they are not on npm yet. Both are under
the MIT License. To publish, drop `"private": true` from both and publish `@diadraw/wasm` first;
this package depends on it.
