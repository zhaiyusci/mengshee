# Bilingual user guide

Built with Astro Starlight. English and Simplified Chinese are equal editions,
not a primary site and a partial translation.

## Editing

- Edit paired files in `src/content/docs/en/` and `src/content/docs/zh-cn/` together.
- Use the same filename, chapter scope, examples, and sidebar order in both.
- Translate titles, instructions, image descriptions, and link labels. Use the
  corresponding application labels where known; do not invent shortcuts.
- Verify behavior against the application before documenting it.
- Keep development notes and implementation specifications outside this guide.
- Screenshots shared by both editions live in `../assets/`. Their application
  interface language may differ from the surrounding text.

`npm run dev` provides live editing. `npm run build` checks paired chapter
presence/order, creates the static site and Pagefind indexes, then checks output
routes, links, language switching and both search languages. Search is available
in the production build; preview the staged site to test it and shared images.
Automated checks cannot establish translation accuracy: review both editions
whenever behavior or instructions change.

The framework requires an internal default locale, but the public entry point
is a language chooser and every chapter must exist in both languages. The chooser
remembers the last selection without hiding either option. Starlight's language
selector keeps the current chapter. Missing translations fail the publication
build instead of silently falling back.

## Commands

```sh
npm ci
npm run build
node scripts/stage.mjs
node scripts/serve.mjs
```

Preview: http://localhost:4321/mengshee/guide/

The site is deployed by the repository Pages workflow. Generated `dist/`,
`.astro/`, dependencies and `../.site/` are ignored. Commit `package-lock.json`
with dependency changes for reproducible builds.
