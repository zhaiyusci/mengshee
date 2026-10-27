import { readdir, readFile } from 'node:fs/promises';
import assert from 'node:assert/strict';
const root = new URL('../src/content/docs/', import.meta.url);
const locales = ['en', 'zh-cn'];
const pages = await Promise.all(locales.map(locale => readdir(new URL(`${locale}/`, root))));
assert.deepEqual(pages[0].sort(), pages[1].sort(), 'Every page must exist in both languages; fallback publication is forbidden.');
for (const name of pages[0]) {
  const texts = await Promise.all(locales.map(locale => readFile(new URL(`${locale}/${name}`, root), 'utf8')));
  for (const text of texts) {
    assert.match(text, /^---\r?\ntitle: .+\r?\n/m, `Missing title: ${name}`);
    assert.ok(text.split('---')[2]?.trim().length > 200, `Empty page: ${name}`);
  }
  const order = text => text.match(/order: (\d+)/)?.[1];
  assert.equal(order(texts[0]), order(texts[1]), `Different chapter order: ${name}`);
}
console.log(`Locale parity: ${pages[0].length} paired pages.`);
const dictionaries = await Promise.all(['en', 'zh-CN'].map(async lang =>
  JSON.parse(await readFile(new URL(`../src/content/i18n/${lang}.json`, import.meta.url), 'utf8'))));
assert.deepEqual(Object.keys(dictionaries[0]).sort(), Object.keys(dictionaries[1]).sort(), 'UI translations must have matching keys.');

