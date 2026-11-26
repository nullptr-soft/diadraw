import { defineCollection } from 'astro:content';
import { docsLoader, i18nLoader } from '@astrojs/starlight/loaders';
import { docsSchema, i18nSchema } from '@astrojs/starlight/schema';
import { z } from 'astro/zod';

export const collections = {
  docs: defineCollection({ loader: docsLoader(), schema: docsSchema() }),
  i18n: defineCollection({
    loader: i18nLoader(),
    schema: i18nSchema({ extend: z.object({
      'diadraw.description': z.string(),
      'diadraw.version': z.string(),
      'diadraw.feedback': z.string(),
      'diadraw.feedbackTitle': z.string(),
      'diadraw.open': z.string(),
      'diadraw.openStep': z.string(),
      'diadraw.viewAlt': z.string(),
    }) }),
  }),
};
