# @diadraw/site

The DiaDraw website, built with [Astro](https://astro.build) and
[Starlight](https://starlight.astro.build): a home page, getting started, a tutorial, the
reference, feedback and contact. GitHub Pages serves it with the web app under `app/`; see
[Website and GitHub Pages](../README.md#website-and-github-pages) for building and deploying both.

It has its own dependencies and lockfile, outside the npm workspaces, because Astro needs a newer
Node (22) than the rest of the repository supports (20).

```bash
npm ci --prefix site
npm run dev --prefix site     # http://localhost:4321
npm run build --prefix site   # site/dist
```

| Path                          | What it is                                                                                                                                                                                                                                   |
| ----------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/content/docs/`           | The pages, in Markdown and MDX. The sidebar is in `astro.config.mjs`.                                                                                                                                                                        |
| `scripts/sync-docs.mjs`       | Runs before every build: copies `docs/dsl.md`, `docs/diadraw.grammar`, `docs/libraries.md` and `docs/guide/` into `src/content/docs/reference/`, rewriting links. Edit those, not the copies.                                                |
| `src/tutorial/`               | The tutorial's documents, one per step. The page shows each with an Open in DiaDraw link that carries it in the app's share format (`src/share.ts`).                                                                                         |
| `scripts/render-tutorial.mjs` | Checks the tutorial's documents with the core (any diagnostic fails) and renders their views to `public/tutorial/`, where the tutorial shows them. Needs `src/wasm` built; the Pages build runs it.                                          |
| `src/components/`             | `TryIt` (a tutorial step), `AppLink` (a button to the app, under the site's base), `SiteTitle` (the title with the version from `VERSION`) and `SocialIcons` (the header's Give feedback button and GitHub link, replacing Starlight's own). |

DiaDraw code blocks (`diadraw` or `draw`) are highlighted with the VS Code extension's grammar,
`src/apps/vscode/syntaxes/diadraw.tmLanguage.json`.

## Maintaining translations

English lives at the site root. Polish (`pl`), Ukrainian (`uk`), French (`fr`) and German
(`de`) use matching paths under their language code. Keep page slugs the same across languages
so that the language picker can open the equivalent page.

- For an authored page, edit the English MDX in `src/content/docs/` and its counterparts in
  `src/content/docs/<locale>/`. Translate frontmatter, link labels and body text. Links to other
  pages stay in the current language; links to the editor and tutorial images use the shared
  `app/` and `tutorial/` destinations under the deployment base.
- For the DSL reference, libraries and user guide, edit the English Markdown in `../docs/`
  and the matching source in `../docs/i18n/<locale>/`. Use the same filenames and relative
  repository paths as in English, but link to the **translated heading** for in-page fragments.
  `npm run sync-docs` regenerates the reference pages; do not edit those copies.
- Keep DSL syntax and runnable examples intact. The grammar listing, tutorial documents and
  rendered diagrams are shared by all five languages.

To check a change, run `npm run build --prefix site` from the repository root. Also try
`SITE_BASE=/diadraw/ npm run build --prefix site` to catch links that assume a root deployment.
