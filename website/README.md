# Mengshee promotional website

A standalone English-language product page organized around the five research
workflows in the main project README. No build step, framework, JavaScript,
external fonts, analytics, or CDN is required.

## Preview

Open `index.html` directly in a browser. Keep `styles.css` and `assets/`
alongside it. The layout adapts to desktop and mobile screens and respects
reduced-motion preferences. Download links point to the latest GitHub release.

## Screenshots

Six genuine, user-supplied screenshots illustrate cross-references and auxiliary
panes, LaTeX source editing, numbered proofreading notes, reading by Views, and
the annotated document displayed in Sumatra PDF and Adobe Acrobat.
They retain their original pixels and proportions; each links to its full-size
image. No placeholders or fabricated application interfaces remain.

See [SCREENSHOTS.md](SCREENSHOTS.md) for provenance and placement.
Screenshots use bounded display widths to keep the page compact; every image
links to its original resolution. The Sumatra PDF and Adobe Acrobat images illustrate portability,
not flattened exports.

## Deployment

Public site: https://www.zhaiyusci.net/mengshee/

GitHub Pages entry: https://zhaiyusci.github.io/mengshee/ (inherits the account's
existing custom domain).

`.github/workflows/pages.yml` deploys this directory through GitHub Actions.
Pushes to `master` that change `website/**` or the workflow trigger deployment;
it can also be run manually with `workflow_dispatch`. GitHub Pages must use
**GitHub Actions** as its build source. Only `website/` is uploaded, not the
application source, local test outputs, or submodules.

For another static host, upload this directory's contents while preserving
relative paths. Review publication rights and visible author information when
adding or replacing screenshots.

Files:

- `index.html`: content, navigation, screenshot links, download links.
- `styles.css`: responsive layout, typography, focus and print styles.
- `assets/`: the six user-provided PNG screenshots, plus the application logo
  and favicon copied from `icons/256-apps-mengshee.png` and `icons/mengshee.ico`.
- `SCREENSHOTS.md`: image provenance and placement notes in Chinese.
