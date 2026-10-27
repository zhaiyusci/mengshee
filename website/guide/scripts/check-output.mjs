import { readdir, readFile, access } from 'node:fs/promises';
import assert from 'node:assert/strict';
const dist = new URL('../dist/', import.meta.url);
const base = '/mengshee/guide/';
const pages = await readdir(new URL('../src/content/docs/en/', import.meta.url));
for (const locale of ['en', 'zh-cn']) {
  for (const page of pages) {
    const slug = page === 'index.md' ? '' : `${page.replace(/\.md$/, '')}/`;
    const route = `${locale}/${slug}`;
    const html = await readFile(new URL(`${route}index.html`, dist), 'utf8');
    if (locale === 'zh-cn') {
      assert.ok(html.includes('清空') && html.includes('找到 [COUNT]'), `Missing Chinese search UI: ${route}`);
    }
    assert.match(html, new RegExp(`<html[^>]*lang="${locale === 'en' ? 'en' : 'zh-CN'}"`, 'i'));
    const nav = html.match(/<nav[^>]*aria-label="(?:Main|主要)"[\s\S]*?<\/nav>/)?.[0];
    assert.ok(nav, `Missing chapter navigation: ${route}`);
    for (const chapter of pages) {
      const chapterSlug = chapter === 'index.md' ? '' : `${chapter.replace(/\.md$/, '')}/`;
      assert.ok(nav.includes(`${base}${locale}/${chapterSlug}`), `Missing sidebar chapter ${chapter}: ${route}`);
    }
    const other = locale === 'en' ? 'zh-cn' : 'en';
    assert.ok(html.includes(`${base}${other}/${slug}`), `Missing corresponding language link: ${route}`);
    for (const [, href] of html.matchAll(/(?:href|src)="([^"]+)"/g)) {
      if (/^(https?:|data:|#)/.test(href)) continue;
      const url = new URL(href.replaceAll('&amp;', '&'), `https://local.test${base}${route}`);
      if (url.pathname.startsWith('/mengshee/assets/')) {
        await access(new URL(`../../assets/${url.pathname.slice('/mengshee/assets/'.length)}`, import.meta.url));
      }
      if (!url.pathname.startsWith(base)) continue;
      let file = decodeURIComponent(url.pathname.slice(base.length));
      if (file.endsWith('/')) file += 'index.html';
      await access(new URL(file, dist)).catch(() => { throw new Error(`Broken local link in ${route}: ${href}`); });
    }
  }
}
const chooser = await readFile(new URL('index.html', dist), 'utf8');
assert.ok(chooser.includes('data-locale="en"') && chooser.includes('data-locale="zh-cn"'), 'Missing equal language choices');
const search = await readFile(new URL('pagefind/pagefind-entry.json', dist), 'utf8');
const languages = JSON.parse(search).languages;
assert.ok(languages.en && (languages['zh-cn'] || languages.zh || languages['zh-CN']), 'Both search languages must be indexed');
console.log(`Built routes, local guide links, language switching and search indexes verified (${pages.length} pages per language).`);
