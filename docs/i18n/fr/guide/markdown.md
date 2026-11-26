# Diagrammes dans Markdown

Écrivez un diagramme directement dans une page Markdown, sous forme de bloc de code dans le langage `diadraw`, comme les diagrammes Mermaid, et DiaDraw le dessine là où la page s’affiche :

````md
```diadraw
model {
  customer = person "Customer"
  shop = system "Shop"
  customer -> shop "places order"
}
```
````

Le diagramme reste du texte dans votre documentation : il est relu et versionné avec tout ce qui l’entoure. Sur ce site, le bloc ci-dessous est dessiné par le plugin Sätteri lors de la construction du site :

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

Il existe deux façons de dessiner les blocs. Les deux utilisent le même cœur que les applications, si bien qu’un diagramme a partout le même aspect :

- **À la construction du site** : un plugin pour l’outil Markdown remplace chaque bloc par le diagramme en SVG dans la page. Les lecteurs obtiennent une simple image, sans script à charger. C’est la solution pour les sites de documentation (Astro et Starlight, Docusaurus, VitePress, Next.js, Eleventy).
- **Dans le navigateur du lecteur** : un seul script, `diadraw.js`, trouve les blocs de la page et les dessine. C’est la solution lorsque vous ne pouvez pas ajouter de plugin à ce qui affiche le Markdown, par exemple un wiki, un moteur de blog ou un CMS.

## Écrire un bloc

Le langage du bloc est `diadraw`. Les mots qui le suivent sur la ligne d’ouverture changent la façon dont il est dessiné :

| Ligne d’ouverture          | Ce qui est dessiné                                                             |
| -------------------------- | ------------------------------------------------------------------------------ |
| ` ```diadraw `             | La première vue du document (ou tous les éléments s’il n’a pas de bloc `view`) |
| ` ```diadraw view=people ` | La vue nommée `people`                                                         |
| ` ```diadraw source `      | Rien : le bloc reste du code, pour montrer le texte lui-même                   |
| ` ```diadraw render `      | Le diagramme, quand le plugin ne dessine que les blocs marqués (voir plus bas) |

Un bloc dont le texte contient des erreurs n’est pas dessiné. À la construction, celle-ci s’arrête et signale chaque erreur avec sa ligne dans le fichier Markdown ; dans le navigateur, le bloc reste du code et les erreurs s’affichent dessous.

## À la construction du site

Les plugins se trouvent dans [`src/markdown`](../../src/markdown/README.md) (`@diadraw/markdown`). Ils ne sont pas encore sur npm ; d’ici là, construisez-les depuis le dépôt (`npm ci`, puis `npx turbo run build --filter=@diadraw/markdown`, qui demande Emscripten pour le cœur) et ajoutez le dossier du paquet à votre projet.

**Astro et Starlight** utilisent le plugin Sätteri :

```js
// astro.config.mjs (Astro 7, whose default Markdown processor is Sätteri)
import { satteri } from '@astrojs/markdown-satteri';
import satteriDiadraw from '@diadraw/markdown/satteri';

export default defineConfig({
  markdown: { processor: satteri({ mdastPlugins: [satteriDiadraw()] }) },
});
```

**remark** dessine les blocs pour tous les outils construits sur remark, y compris Astro avec le processeur `unified()`. Dans Docusaurus, ajoutez-le aux options de la documentation (et du blog) : `remarkPlugins: [require('@diadraw/markdown/remark').default]`. Dans Next.js, passez-le à `@next/mdx` sous la forme `options: { remarkPlugins: [remarkDiadraw] }`.

**markdown-it** dessine les blocs pour VitePress, Eleventy et tout ce qui repose sur markdown-it. Le rendu est synchrone, chargez donc d’abord le cœur :

```js
// .vitepress/config.mjs
import { loadDiaDraw } from '@diadraw/wasm';
import markdownItDiadraw from '@diadraw/markdown/markdown-it';

const diadraw = await loadDiaDraw();
export default { markdown: { config: (md) => md.use(markdownItDiadraw, { diadraw }) } };
```

Les deux acceptent les mêmes options :

| Option      | Effet                                                                                                            |
| ----------- | ---------------------------------------------------------------------------------------------------------------- |
| `blocks`    | `all` (par défaut) dessine chaque bloc `diadraw` ; `marked` ne dessine que ceux marqués `render`                 |
| `onError`   | remark et Sätteri : `fail` (par défaut) ou `warn`, qui garde le bloc en code ; markdown-it : `throw` ou `source` |
| `languages` | Les langages de blocs de code à dessiner ; `['diadraw']` par défaut                                              |
| `className` | La classe du `<figure>` autour de chaque diagramme ; `diadraw` par défaut                                        |

Utilisez `blocks: 'marked'` quand la plupart des blocs de vos pages enseignent le langage plutôt qu’ils ne montrent un diagramme. C’est ce que fait ce site.

## Dans le navigateur du lecteur

Ajoutez le script à la page, après le rendu du Markdown :

```html
<script type="module" src="https://mykulyak.github.io/diadraw/embed/diadraw.js"></script>
```

Il trouve chaque `<pre><code class="language-diadraw">` (et `lang-diadraw`, l’autre graphie qu’emploient certains moteurs de rendu), le dessine et met le diagramme à sa place. Le cœur, `diadraw.wasm`, se charge depuis le même emplacement que le script, une fois par page. Pour l’héberger vous-même, copiez les deux fichiers de `src/markdown/dist/embed/` sur votre site.

- Pour choisir une vue, donnez au `<pre>` ou au `<code>` un attribut `data-view`.
- Pour une page qui ajoute du contenu après son chargement, appelez de nouveau `diadraw.render()` ; les blocs déjà dessinés sont ignorés.
- Pour ne dessiner qu’à la demande, définissez `window.diadrawConfig = { startOnLoad: false }` avant l’exécution du script et appelez `diadraw.render()` vous-même.
- Dans votre propre code empaqueté, `import { renderDiagrams } from '@diadraw/markdown/browser'` fait la même chose.

## Pas encore pris en charge

- **GitHub et GitLab** affichent le Markdown sans exécuter le code d’autrui ; ils montrent donc les blocs `diadraw` comme du texte. Une GitHub Action qui dessine chaque bloc dans un fichier SVG à côté de la page, et y fait un lien depuis la page, est prévue.
- **MkDocs et Python-Markdown** : une extension qui dessine les blocs avec l’outil en ligne de commande `diadraw` est prévue.
