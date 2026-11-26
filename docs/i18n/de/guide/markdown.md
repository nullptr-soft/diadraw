# Diagramme in Markdown

Schreib ein Diagramm direkt in eine Markdown-Seite, als Codeblock in der Sprache `diadraw`, genau wie Mermaid-Diagramme, und DiaDraw zeichnet es dort, wo die Seite angezeigt wird:

````md
```diadraw
model {
  customer = person "Customer"
  shop = system "Shop"
  customer -> shop "places order"
}
```
````

Das Diagramm bleibt Text in deiner Dokumentation und wird mit allem darum herum geprüft und versioniert. Auf dieser Website zeichnet das Sätteri-Plugin den folgenden Block beim Bauen der Website:

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

Es gibt zwei Wege, die Blöcke zu zeichnen. Beide nutzen denselben Kern wie die Apps, daher sieht ein Diagramm überall gleich aus:

- **Beim Bauen der Website**: Ein Plugin für das Markdown-Werkzeug ersetzt jeden Block durch das Diagramm als SVG in der Seite. Leser bekommen ein einfaches Bild, ohne ein Skript zu laden. Das passt für Dokumentationswebsites (Astro und Starlight, Docusaurus, VitePress, Next.js, Eleventy).
- **Im Browser des Lesers**: Ein einziges Skript, `diadraw.js`, findet die Blöcke auf der Seite und zeichnet sie. Das passt, wenn du dem, was das Markdown darstellt, kein Plugin hinzufügen kannst, etwa einem Wiki, einer Blog-Software oder einem CMS.

## Einen Block schreiben

Die Sprache des Blocks ist `diadraw`. Wörter dahinter in der Eröffnungszeile ändern, wie er gezeichnet wird:

| Eröffnungszeile            | Was gezeichnet wird                                                                   |
| -------------------------- | ------------------------------------------------------------------------------------- |
| ` ```diadraw `             | Die erste Ansicht des Dokuments (oder alle Elemente, wenn es keinen `view`-Block hat) |
| ` ```diadraw view=people ` | Die Ansicht namens `people`                                                           |
| ` ```diadraw source `      | Nichts: Der Block bleibt Code, um den Text selbst zu zeigen                           |
| ` ```diadraw render `      | Das Diagramm, wenn das Plugin nur markierte Blöcke zeichnet (siehe unten)             |

Ein Block mit Fehlern im Text wird nicht gezeichnet. Beim Bauen bricht der Build ab und meldet jeden Fehler mit seiner Zeile in der Markdown-Datei; im Browser bleibt der Block Code, und die Fehler stehen darunter.

## Beim Bauen der Website

Die Plugins liegen in [`src/markdown`](../../src/markdown/README.md) (`@diadraw/markdown`). Sie sind noch nicht auf npm; bis dahin baust du sie aus dem Repository (`npm ci`, dann `npx turbo run build --filter=@diadraw/markdown`, was Emscripten für den Kern braucht) und fügst den Paketordner deinem Projekt hinzu.

**Astro und Starlight** nutzen das Sätteri-Plugin:

```js
// astro.config.mjs (Astro 7, whose default Markdown processor is Sätteri)
import { satteri } from '@astrojs/markdown-satteri';
import satteriDiadraw from '@diadraw/markdown/satteri';

export default defineConfig({
  markdown: { processor: satteri({ mdastPlugins: [satteriDiadraw()] }) },
});
```

**remark** zeichnet Blöcke für jedes Werkzeug, das auf remark aufbaut, auch für Astro mit dem `unified()`-Prozessor. In Docusaurus fügst du es den Optionen der Dokumentation (und des Blogs) hinzu: `remarkPlugins: [require('@diadraw/markdown/remark').default]`. In Next.js übergibst du es an `@next/mdx` als `options: { remarkPlugins: [remarkDiadraw] }`.

**markdown-it** zeichnet Blöcke für VitePress, Eleventy und alles andere, was auf markdown-it aufbaut. Es rendert synchron, also lade zuerst den Kern:

```js
// .vitepress/config.mjs
import { loadDiaDraw } from '@diadraw/wasm';
import markdownItDiadraw from '@diadraw/markdown/markdown-it';

const diadraw = await loadDiaDraw();
export default { markdown: { config: (md) => md.use(markdownItDiadraw, { diadraw }) } };
```

Beide nehmen dieselben Optionen:

| Option      | Was sie bewirkt                                                                                                     |
| ----------- | ------------------------------------------------------------------------------------------------------------------- |
| `blocks`    | `all` (Standard) zeichnet jeden `diadraw`-Block; `marked` nur die mit `render` markierten                           |
| `onError`   | remark und Sätteri: `fail` (Standard) oder `warn`, das den Block als Code lässt; markdown-it: `throw` oder `source` |
| `languages` | Die Codeblock-Sprachen, die gezeichnet werden; standardmäßig `['diadraw']`                                          |
| `className` | Die Klasse des `<figure>` um jedes Diagramm; standardmäßig `diadraw`                                                |

Nutze `blocks: 'marked'`, wenn die meisten Blöcke auf deinen Seiten die Sprache erklären, statt ein Diagramm zu zeigen. Diese Website macht es so.

## Im Browser des Lesers

Füge das Skript nach dem gerenderten Markdown in die Seite ein:

```html
<script type="module" src="https://mykulyak.github.io/diadraw/embed/diadraw.js"></script>
```

Es findet jedes `<pre><code class="language-diadraw">` (und `lang-diadraw`, die andere Schreibweise mancher Renderer), zeichnet es und setzt das Diagramm an seine Stelle. Der Kern, `diadraw.wasm`, lädt einmal pro Seite vom selben Ort wie das Skript. Um ihn selbst auszuliefern, kopiere beide Dateien aus `src/markdown/dist/embed/` auf deine Website.

- Um eine Ansicht zu wählen, gib dem `<pre>` oder `<code>` ein `data-view`-Attribut.
- Fügt eine Seite nach dem Laden Inhalte hinzu, ruf `diadraw.render()` erneut auf; bereits gezeichnete Blöcke werden übersprungen.
- Um nur auf Anforderung zu zeichnen, setz `window.diadrawConfig = { startOnLoad: false }`, bevor das Skript läuft, und ruf `diadraw.render()` selbst auf.
- In deinem eigenen gebündelten Code macht `import { renderDiagrams } from '@diadraw/markdown/browser'` dasselbe.

## Noch nicht unterstützt

- **GitHub und GitLab** zeigen Markdown an, ohne fremden Code auszuführen, und zeigen `diadraw`-Blöcke daher als Text. Geplant ist eine GitHub Action, die jeden Block in eine SVG-Datei neben der Seite zeichnet und sie von der Seite aus verlinkt.
- **MkDocs und Python-Markdown**: Geplant ist eine Erweiterung, die Blöcke mit dem Kommandozeilenwerkzeug `diadraw` zeichnet.
