import { defineCollection } from 'astro:content';
import { readFile } from 'node:fs/promises';
import { docsLoader } from '@astrojs/starlight/loaders';
import { docsSchema, i18nSchema } from '@astrojs/starlight/schema';
export const collections = {
  docs: defineCollection({ loader: docsLoader(), schema: docsSchema() }),
  i18n: defineCollection({
    // Explicit language IDs avoid filesystem separator differences between
    // Windows previews and Linux publication builds.
    loader: async () => Promise.all(['en', 'zh-CN'].map(async id => ({
      id, ...JSON.parse(await readFile(new URL(`./content/i18n/${id}.json`, import.meta.url), 'utf8')),
    }))),
    schema: i18nSchema(),
  }),
};
