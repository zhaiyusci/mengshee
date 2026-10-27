# Mengshee website

The product homepage is plain HTML/CSS. The bilingual user guide uses Astro
Starlight and is built as a static site. Neither requires a server-side runtime.

## Build and preview

Use Node 26 and npm:

```sh
cd website/guide
npm ci
npm run build
node scripts/stage.mjs
node scripts/serve.mjs
```

Open http://localhost:4321/mengshee/ to preview the complete staged website.
The guide starts at `/mengshee/guide/`, with equal English and Simplified Chinese
editions at `/en/` and `/zh-cn/`. See [guide/README.md](guide/README.md) for editing.

## Deployment

Public site: https://www.zhaiyusci.net/mengshee/

`.github/workflows/pages.yml` builds and deploys on changes to `website/**` on
master, or by manual dispatch. GitHub Pages must use GitHub Actions as its source.
Only the staged public files in `.site/` are uploaded; dependencies, Markdown
sources, and maintenance scripts are excluded. Upload that directory's contents
for another static host, adjusting the guide base and site URL if necessary.

## Assets

`assets/` contains genuine user-supplied screenshots and the Mengshee logo/favicon.
Screenshots retain their proportions; they illustrate application behavior, not
fabricated interfaces. Review publication rights and visible personal information
when adding images. The homepage remains directly previewable as `index.html`.
