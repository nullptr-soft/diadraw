# Diagramy w Markdownie

Diagram możesz zapisać bezpośrednio na stronie Markdown jako blok kodu w języku `diadraw`, tak samo jak diagramy Mermaid, a DiaDraw narysuje go tam, gdzie strona jest wyświetlana:

````md
```diadraw
model {
  customer = person "Customer"
  shop = system "Shop"
  customer -> shop "places order"
}
```
````

Diagram pozostaje tekstem w dokumentacji, więc przechodzi przegląd i kontrolę wersji razem z resztą. Na tej stronie poniższy blok rysuje wtyczka Sätteri podczas budowania witryny:

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

Bloki można rysować na dwa sposoby. Oba korzystają z tego samego rdzenia co aplikacje, więc diagram wszędzie wygląda tak samo:

- **Podczas budowania witryny**: wtyczka do narzędzia Markdown zastępuje każdy blok diagramem w postaci SVG osadzonym w stronie. Czytelnik dostaje zwykły obraz, bez skryptu do załadowania. Tego sposobu używaj w witrynach z dokumentacją (Astro i Starlight, Docusaurus, VitePress, Next.js, Eleventy).
- **W przeglądarce czytelnika**: jeden skrypt, `diadraw.js`, znajduje bloki na stronie i je rysuje. Tego sposobu używaj, gdy nie możesz dodać wtyczki do tego, co renderuje Markdown, na przykład do wiki, silnika bloga albo CMS.

## Pisanie bloku

Językiem bloku jest `diadraw`. Słowa po nim w wierszu otwierającym zmieniają sposób rysowania:

| Wiersz otwierający         | Co rysuje                                                                   |
| -------------------------- | --------------------------------------------------------------------------- |
| ` ```diadraw `             | Pierwszy widok dokumentu (albo wszystkie elementy, gdy nie ma bloku `view`) |
| ` ```diadraw view=people ` | Widok o nazwie `people`                                                     |
| ` ```diadraw source `      | Nic: blok pozostaje kodem, aby pokazać sam tekst                            |
| ` ```diadraw render `      | Diagram, gdy wtyczka rysuje tylko oznaczone bloki (zobacz niżej)            |

Blok z błędami w tekście nie jest rysowany. Podczas budowania budowanie się zatrzymuje i zgłasza każdy błąd z numerem wiersza w pliku Markdown; w przeglądarce blok pozostaje kodem, a pod nim pojawiają się błędy.

## Podczas budowania witryny

Wtyczki znajdują się w [`src/markdown`](../../src/markdown/README.md) (`@diadraw/markdown`). Nie ma ich jeszcze w npm; do tego czasu zbuduj je z repozytorium (`npm ci`, a potem `npx turbo run build --filter=@diadraw/markdown`, co wymaga Emscripten dla rdzenia) i dodaj folder pakietu do swojego projektu.

**Astro i Starlight** korzystają z wtyczki Sätteri:

```js
// astro.config.mjs (Astro 7, whose default Markdown processor is Sätteri)
import { satteri } from '@astrojs/markdown-satteri';
import satteriDiadraw from '@diadraw/markdown/satteri';

export default defineConfig({
  markdown: { processor: satteri({ mdastPlugins: [satteriDiadraw()] }) },
});
```

**remark** rysuje bloki we wszystkich narzędziach zbudowanych na remark, także w Astro z procesorem `unified()`. W Docusaurus dodaj ją do opcji dokumentacji (i bloga): `remarkPlugins: [require('@diadraw/markdown/remark').default]`. W Next.js przekaż ją do `@next/mdx` jako `options: { remarkPlugins: [remarkDiadraw] }`.

**markdown-it** rysuje bloki w VitePress, Eleventy i wszystkim innym, co działa na markdown-it. Renderuje synchronicznie, więc najpierw załaduj rdzeń:

```js
// .vitepress/config.mjs
import { loadDiaDraw } from '@diadraw/wasm';
import markdownItDiadraw from '@diadraw/markdown/markdown-it';

const diadraw = await loadDiaDraw();
export default { markdown: { config: (md) => md.use(markdownItDiadraw, { diadraw }) } };
```

Obie przyjmują te same opcje:

| Opcja       | Co robi                                                                                                            |
| ----------- | ------------------------------------------------------------------------------------------------------------------ |
| `blocks`    | `all` (domyślnie) rysuje każdy blok `diadraw`; `marked` rysuje tylko bloki oznaczone `render`                      |
| `onError`   | remark i Sätteri: `fail` (domyślnie) albo `warn`, które zostawia blok jako kod; markdown-it: `throw` albo `source` |
| `languages` | Języki bloków kodu do rysowania; domyślnie `['diadraw']`                                                           |
| `className` | Klasa elementu `<figure>` wokół każdego diagramu; domyślnie `diadraw`                                              |

Użyj `blocks: 'marked'`, gdy większość bloków na stronach uczy języka, zamiast pokazywać diagram. Tak robi ta witryna.

## W przeglądarce czytelnika

Dodaj skrypt do strony, po wyrenderowanym Markdownie:

```html
<script type="module" src="https://mykulyak.github.io/diadraw/embed/diadraw.js"></script>
```

Skrypt znajduje każdy `<pre><code class="language-diadraw">` (oraz `lang-diadraw`, drugą pisownię używaną przez niektóre renderery), rysuje go i wstawia diagram na jego miejsce. Rdzeń, `diadraw.wasm`, ładuje się z tego samego miejsca co skrypt, raz na stronę. Aby udostępniać go samodzielnie, skopiuj oba pliki z `src/markdown/dist/embed/` do swojej witryny.

- Aby wybrać widok, nadaj elementowi `<pre>` lub `<code>` atrybut `data-view`.
- Gdy strona dodaje treść po załadowaniu, wywołaj ponownie `diadraw.render()`; narysowane już bloki są pomijane.
- Aby rysować tylko na żądanie, przed uruchomieniem skryptu ustaw `window.diadrawConfig = { startOnLoad: false }` i sam wywołaj `diadraw.render()`.
- We własnym kodzie budowanym bundlerem to samo robi `import { renderDiagrams } from '@diadraw/markdown/browser'`.

## Jeszcze nieobsługiwane

- **GitHub i GitLab** wyświetlają Markdown bez uruchamiania cudzego kodu, więc bloki `diadraw` pokazują jako tekst. Planowana jest akcja GitHub, która rysuje każdy blok do pliku SVG obok strony i wstawia do niego odnośnik.
- **MkDocs i Python-Markdown**: planowane jest rozszerzenie, które rysuje bloki narzędziem wiersza poleceń `diadraw`.
