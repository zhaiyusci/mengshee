import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';
export default defineConfig({
  site: 'https://www.zhaiyusci.net',
  base: '/mengshee/guide',
  trailingSlash: 'always',
  integrations: [starlight({
    title: { en: 'Mengshee User Guide', 'zh-CN': 'Mengshee 使用指南' },
    // Starlight requires a fallback locale internally. Publication checks require
    // complete paired content, so neither language is a fallback for readers.
    defaultLocale: 'en',
    favicon: '/favicon.ico',
    locales: { en: { label: 'English', lang: 'en' }, 'zh-cn': { label: '简体中文', lang: 'zh-CN' } },
    social: [{ icon: 'github', label: 'GitHub', href: 'https://github.com/zhaiyusci/mengshee' }],
    customCss: ['./src/styles.css'],
    components: { Head: './src/components/Head.astro' },
  })],
});


