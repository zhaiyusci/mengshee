import { cp, mkdir } from 'node:fs/promises';
const site = new URL('../../', import.meta.url);
const output = new URL('.site/', site);
await mkdir(output, { recursive: true });
for (const file of ['index.html', 'styles.css', 'assets']) {
  await cp(new URL(file, site), new URL(file, output), { recursive: true });
}
await cp(new URL('../dist/', import.meta.url), new URL('guide/', output), { recursive: true });
console.log('Public site staged in website/.site/');
