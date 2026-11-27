// The DiaDraw website: Astro with Starlight. The reference section is generated from docs/ by
// scripts/sync-docs.mjs before every build, so the repository's docs stay the one source.
//
// SITE_URL and SITE_BASE say where the site is served. tools/pages/build.mjs sets them for
// GitHub Pages (https://<owner>.github.io/<repo>/); without them the site serves from /.
import { existsSync, readFileSync } from 'node:fs';
import { satteri } from '@astrojs/markdown-satteri';
import starlight from '@astrojs/starlight';
import { defineConfig } from 'astro/config';
import csp from './scripts/csp.mjs';

const grammar = JSON.parse(
  readFileSync(
    new URL('../src/apps/vscode/syntaxes/diadraw.tmLanguage.json', import.meta.url),
    'utf8',
  ),
);

const repo = 'https://github.com/mykulyak/diadraw';

// DiaDraw's version, shown beside the site title (src/components/SiteTitle.astro).
const version = readFileSync(new URL('../VERSION', import.meta.url), 'utf8').trim();

// ```diadraw render blocks are drawn as diagrams when the site is built, by the Sätteri plugin in
// src/markdown (built with the core: `npx turbo run build --filter=@diadraw/markdown`). Only
// marked blocks are drawn: most DiaDraw blocks on the site teach the text and stay as code.
// Without the plugin built, the site still builds and those blocks show as code.
const diadrawPlugin = new URL('../src/markdown/dist/satteri.js', import.meta.url);
const mdastPlugins = [];
if (existsSync(diadrawPlugin)) {
  const { default: satteriDiadraw } = await import(diadrawPlugin.href);
  mdastPlugins.push(satteriDiadraw({ blocks: 'marked' }));
} else {
  console.warn('site: @diadraw/markdown is not built; ```diadraw render blocks show as code.');
}

export default defineConfig({
  site: process.env.SITE_URL || undefined,
  base: process.env.SITE_BASE || '/',
  trailingSlash: 'always',
  vite: { define: { __DIADRAW_VERSION__: JSON.stringify(version) } },
  markdown: { processor: satteri({ mdastPlugins }) },
  integrations: [
    starlight({
      title: 'DiaDraw',
      locales: {
        root: { label: 'English', lang: 'en' },
        pl: { label: 'Polski' },
        uk: { label: 'Українська' },
        fr: { label: 'Français' },
        de: { label: 'Deutsch' },
      },
      routeMiddleware: './src/localizedMetadata.ts',
      logo: { src: './src/assets/logo.svg', alt: '' },
      favicon: '/favicon.svg',
      // The title with the version; Give feedback in the header, beside the GitHub link.
      components: {
        SiteTitle: './src/components/SiteTitle.astro',
        SocialIcons: './src/components/SocialIcons.astro',
      },
      social: [{ icon: 'github', label: 'GitHub', href: repo }],
      editLink: { baseUrl: `${repo}/edit/main/site/` },
      expressiveCode: {
        shiki: { langs: [{ ...grammar, name: 'diadraw', aliases: ['draw'] }] },
      },
      sidebar: [
        {
          label: 'Getting started',
          slug: 'getting-started',
          translations: {
            pl: 'Pierwsze kroki',
            uk: 'Перші кроки',
            fr: 'Premiers pas',
            de: 'Erste Schritte',
          },
        },
        {
          label: 'Tutorial',
          slug: 'tutorial',
          translations: {
            pl: 'Samouczek',
            uk: 'Навчальний посібник',
            fr: 'Tutoriel',
            de: 'Tutorial',
          },
        },
        {
          label: 'AI agents',
          slug: 'reference/guide/agents',
          translations: { pl: 'Agenci AI', uk: 'Агенти ШІ', fr: 'Agents IA', de: 'KI-Agenten' },
        },
        {
          label: 'Reference',
          translations: { pl: 'Dokumentacja', uk: 'Довідник', fr: 'Référence', de: 'Referenz' },
          items: [
            {
              label: 'Overview',
              slug: 'reference',
              translations: { pl: 'Przegląd', uk: 'Огляд', fr: 'Vue d’ensemble', de: 'Übersicht' },
            },
            { label: 'DSL', slug: 'reference/dsl' },
            {
              label: 'Grammar',
              slug: 'reference/grammar',
              translations: { pl: 'Gramatyka', uk: 'Граматика', fr: 'Grammaire', de: 'Grammatik' },
            },
            {
              label: 'Libraries',
              slug: 'reference/libraries',
              translations: {
                pl: 'Biblioteki',
                uk: 'Бібліотеки',
                fr: 'Bibliothèques',
                de: 'Bibliotheken',
              },
            },
            {
              label: 'User guide',
              translations: {
                pl: 'Poradnik użytkownika',
                uk: 'Посібник користувача',
                fr: 'Guide d’utilisation',
                de: 'Benutzerhandbuch',
              },
              items: [{ autogenerate: { directory: 'reference/guide' } }],
            },
          ],
        },
        {
          label: 'Feedback',
          slug: 'feedback',
          translations: { pl: 'Opinie', uk: 'Відгуки', fr: 'Votre avis', de: 'Feedback' },
        },
        {
          label: 'Contact',
          slug: 'contact',
          translations: { pl: 'Kontakt', uk: 'Контакти', fr: 'Contact', de: 'Kontakt' },
        },
        {
          label: 'Credits',
          slug: 'credits',
          translations: { pl: 'Podziękowania', uk: 'Подяки', fr: 'Crédits', de: 'Danksagungen' },
        },
      ],
    }),
    // Last, so the policy covers the pages as Starlight wrote them (scripts/csp.mjs).
    csp(),
  ],
});
