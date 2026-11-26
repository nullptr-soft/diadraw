# Діаграми в Markdown

Діаграму можна записати просто на сторінці Markdown як блок коду мовою `diadraw`, так само як діаграми Mermaid, і DiaDraw намалює її там, де показують сторінку:

````md
```diadraw
model {
  customer = person "Customer"
  shop = system "Shop"
  customer -> shop "places order"
}
```
````

Діаграма лишається текстом у документації, тож її переглядають і зберігають у системі контролю версій разом з усім іншим. На цьому сайті блок нижче малює плагін Sätteri під час збирання сайту:

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

Блоки можна малювати двома способами. Обидва використовують те саме ядро, що й застосунки, тому діаграма всюди виглядає однаково:

- **Під час збирання сайту**: плагін для інструмента Markdown замінює кожен блок діаграмою у форматі SVG просто на сторінці. Читач отримує звичайне зображення без жодного скрипту. Цей спосіб підходить для сайтів документації (Astro і Starlight, Docusaurus, VitePress, Next.js, Eleventy).
- **У браузері читача**: один скрипт, `diadraw.js`, знаходить блоки на сторінці й малює їх. Цей спосіб підходить, коли до того, що рендерить Markdown, не можна додати плагін, наприклад до вікі, рушія блогу чи CMS.

## Як написати блок

Мова блоку — `diadraw`. Слова після неї в рядку відкриття змінюють те, як блок малюється:

| Рядок відкриття            | Що малює                                                            |
| -------------------------- | ------------------------------------------------------------------- |
| ` ```diadraw `             | Перше подання документа (або всі елементи, якщо блоку `view` немає) |
| ` ```diadraw view=people ` | Подання з назвою `people`                                           |
| ` ```diadraw source `      | Нічого: блок лишається кодом, щоб показати сам текст                |
| ` ```diadraw render `      | Діаграму, коли плагін малює лише позначені блоки (див. нижче)       |

Блок із помилками в тексті не малюється. Під час збирання воно зупиняється й повідомляє кожну помилку з номером рядка у файлі Markdown; у браузері блок лишається кодом, а під ним з'являються помилки.

## Під час збирання сайту

Плагіни лежать у [`src/markdown`](../../src/markdown/README.md) (`@diadraw/markdown`). У npm їх ще немає; доти зберіть їх із репозиторію (`npm ci`, потім `npx turbo run build --filter=@diadraw/markdown`, для ядра потрібен Emscripten) і додайте теку пакета до свого проєкту.

**Astro і Starlight** використовують плагін Sätteri:

```js
// astro.config.mjs (Astro 7, whose default Markdown processor is Sätteri)
import { satteri } from '@astrojs/markdown-satteri';
import satteriDiadraw from '@diadraw/markdown/satteri';

export default defineConfig({
  markdown: { processor: satteri({ mdastPlugins: [satteriDiadraw()] }) },
});
```

**remark** малює блоки в усіх інструментах, побудованих на remark, зокрема в Astro з процесором `unified()`. У Docusaurus додайте його до параметрів документації (і блогу): `remarkPlugins: [require('@diadraw/markdown/remark').default]`. У Next.js передайте його в `@next/mdx` як `options: { remarkPlugins: [remarkDiadraw] }`.

**markdown-it** малює блоки у VitePress, Eleventy та всьому іншому, що працює на markdown-it. Він рендерить синхронно, тому спершу завантажте ядро:

```js
// .vitepress/config.mjs
import { loadDiaDraw } from '@diadraw/wasm';
import markdownItDiadraw from '@diadraw/markdown/markdown-it';

const diadraw = await loadDiaDraw();
export default { markdown: { config: (md) => md.use(markdownItDiadraw, { diadraw }) } };
```

Обидва приймають ті самі параметри:

| Параметр    | Що робить                                                                                            |
| ----------- | ---------------------------------------------------------------------------------------------------- |
| `blocks`    | `all` (типово) малює кожен блок `diadraw`; `marked` малює лише блоки, позначені `render`             |
| `onError`   | remark і Sätteri: `fail` (типово) або `warn`, що лишає блок кодом; markdown-it: `throw` або `source` |
| `languages` | Мови блоків коду, які треба малювати; типово `['diadraw']`                                           |
| `className` | Клас елемента `<figure>` навколо кожної діаграми; типово `diadraw`                                   |

Використовуйте `blocks: 'marked'`, коли більшість блоків на сторінках навчає мови, а не показує діаграму. Так робить цей сайт.

## У браузері читача

Додайте скрипт на сторінку після відрендереного Markdown:

```html
<script type="module" src="https://mykulyak.github.io/diadraw/embed/diadraw.js"></script>
```

Скрипт знаходить кожен `<pre><code class="language-diadraw">` (і `lang-diadraw`, інше написання, яке використовують деякі рендерери), малює його й ставить діаграму на його місце. Ядро, `diadraw.wasm`, завантажується з того самого місця, що й скрипт, один раз на сторінку. Щоб роздавати його самостійно, скопіюйте обидва файли з `src/markdown/dist/embed/` на свій сайт.

- Щоб вибрати подання, додайте до `<pre>` або `<code>` атрибут `data-view`.
- Якщо сторінка додає вміст після завантаження, знову викличте `diadraw.render()`; уже намальовані блоки пропускаються.
- Щоб малювати лише на вимогу, до запуску скрипту задайте `window.diadrawConfig = { startOnLoad: false }` і самі викличте `diadraw.render()`.
- У власному коді, який збирає бандлер, те саме робить `import { renderDiagrams } from '@diadraw/markdown/browser'`.

## Поки що не підтримується

- **GitHub і GitLab** показують Markdown, не запускаючи чужого коду, тому блоки `diadraw` вони показують як текст. Заплановано GitHub Action, яка малює кожен блок у файл SVG поруч зі сторінкою й додає на нього посилання.
- **MkDocs і Python-Markdown**: заплановано розширення, яке малює блоки інструментом командного рядка `diadraw`.
