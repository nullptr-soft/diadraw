import { defineRouteMiddleware } from '@astrojs/starlight/route-data';

// Starlight's global description accepts only a string. Fill missing tags per
// locale, preserving any description or head overrides supplied by the page.
export const onRequest = defineRouteMiddleware(({ locals }) => {
  const { head } = locals.starlightRoute;
  const content = locals.t('diadraw.description');
  if (!head.some(({ tag, attrs }) => tag === 'meta' && attrs?.name === 'description')) {
    head.push({ tag: 'meta', attrs: { name: 'description', content } });
  }
  if (!head.some(({ tag, attrs }) => tag === 'meta' && attrs?.property === 'og:description')) {
    head.push({ tag: 'meta', attrs: { property: 'og:description', content } });
  }
});
