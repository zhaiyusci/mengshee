# Mengshee

Mengshee is a PDF reader and editor derived from KDE Okular, focused on
scientific and engineering reading. It combines richer annotations, tools for
working with older literature, and a dedicated proofreading workflow while
keeping documents portable as ordinary PDFs.

[Download Mengshee](https://github.com/zhaiyusci/mengshee/releases)

## What Mengshee Is For

Mengshee is built for professional scientists and engineers who need to read
papers closely. It is not a shortcut around reading or a substitute for
understanding an argument. Its purpose is to support the work itself: following
a derivation, checking a claim against its references, recording a precise
objection, and preparing corrections that another researcher can act on.

Its tools address practical situations that arise in that work:

- **Work through technical arguments.** Write formulas, derivations, and
  explanations as LaTeX annotations alongside the passage you are studying.
- **Read across references and older sources.** Improve navigation and OCR text
  in older literature, and use fully functional auxiliary panes to consult
  referenced material without losing your place.
- **Prepare a paper for correction.** Place numbered proofreading notes in
  context and export a structured checklist for communicating corrections.
- **Read closely on a smaller screen.** Follow selected page regions with Views
  while retaining normal annotation and navigation tools.
- **Share the results of careful reading.** Save an ordinary PDF that standard
  readers can display without flattening. When extra compatibility assurance
  is needed, export images or a flattened copy and keep the editable original.

These features reduce the friction of handling documents, not the intellectual
work of reading them.

## Main Features

### Richer annotations for scientific and engineering reading

LaTeX Notes let you add typeset formulas, derivations, and technical explanations
directly to a PDF. They use [StemTeX](https://github.com/zhaiyusci/stemtex) for
rendering and are stored as standard PDF **Stamp annotations**, with both the
editable LaTeX source and a self-contained appearance preserved in the document.
Other PDF readers can display them without installing Mengshee PDF.

Alongside ordinary annotations, Mengshee provides LaTeX callouts and template
notes for contextual text such as page numbers. Callouts support direct editing
of their frames and leaders, with undo/redo. Font, package, and preamble choices
can be customized through StemTeX profiles.

### Better access to older papers and classic literature

Older documents often lack the navigation and searchable text expected from a
modern PDF. Mengshee lets you edit cross-references—including internal links,
named destinations, and the table of contents—and use OCR to recognize text or
edit an existing OCR text layer. These tools help make scanned and older
literature easier to navigate and read.

Internal links can open in **fully functional auxiliary reading panes**, not
just static previews. Read, navigate, select text, and annotate in a reference
pane without losing your place in the main document. Panes can be split and can
independently display original pages or Views while working with the same
document.

### Dedicated tools for proofreading papers

The **Proofread** mode brings together Numbered Callouts, numbering controls,
and checklist export. Add LaTeX-backed correction notes beside the text, edit
their internal IDs directly in the annotation pop-up, and choose how their
visible numbers should appear: continuous throughout the document, restarted
on each page, combined with page numbers, or formatted with custom patterns.

Explicit renumbering orders notes by physical page and internal ID. Export the
whole document's notes as an Excel-compatible CSV containing the page, internal
ID, displayed number, and LaTeX source. The annotated PDF and exported checklist
provide complementary documents for communicating and tracking corrections.
Numbering settings and visible labels are saved in the PDF.

### Read by Views on smaller screens

Define **Views**—numbered rectangular regions on a page—and read those regions
as a sequence of displayed pages. This is useful for following a paper's columns
or focusing on portions of a large page on a smaller screen. Draw and adjust a
layout yourself, then optionally apply it across the document; View definitions
are saved with the PDF.

Views change only the displayed range, not the underlying PDF pages. Reading by
Views does not split or crop the file, and text selection, navigation, and
annotation remain available. Switch back to whole-page reading whenever you
want, independently of the selected editing mode.

### Portable PDFs, with additional export options

**Flattening is not required to share a PDF edited in Mengshee.** Our design
contract is that saved documents remain readable in standards-compliant PDF
readers: annotations carry standard, self-contained appearances, including
rendered LaTeX notes and numbered badges. Recipients should not need Mengshee,
StemTeX, or the original temporary files to see them.

Image and flattened PDF exports are additional safeguards, not prerequisites
for portability. Use them when you want extra assurance about what a collaborator
will see, particularly if their reader has limited annotation support or hides
annotations through its display settings.

Export the current page, all pages, or a selected page range as **PNG or JPEG**,
with control over resolution and whether annotations are included. Image export
uses the live document, including unsaved edits and the current page order.

**Flattened PDF export** creates a separate copy with supported visible
annotation appearances merged into page content, preserving vectors instead of
rasterizing entire pages. Keep the editable original for further work. Hidden
annotations, links, and form widgets are retained.

## Working with Documents

The toolbar organizes tools into six modes: **Reading and Annotations**,
**Cross-references**, **OCR**, **Page Editing**, **View Editing**, and
**Proofread**. Modes organize the interface rather than restrict annotation:
ordinary annotation tools and editing existing Numbered Callouts remain
available across modes, subject to the document's normal permissions. Switching
modes does not automatically start a drawing tool.

Page editing includes inserting blank pages, deleting or duplicating pages,
and reordering them in the thumbnail sidebar. Page moves preserve annotations
and live edit state.

Mengshee stores editable annotation metadata together with standard PDF
annotation appearances. LaTeX Notes, including their numbered badges, remain
visible in other PDF readers without temporary files or a local TeX installation.
Private features such as View definitions can be ignored by other readers
without changing the underlying page content.

## Documentation

### Features and workflows

- [Numbered Callouts and proofreading](docs/ordered-callouts.md)
- [Reading and editing Views](docs/reading-views.md)
- [Editing modes](docs/editing-modes.md)
- [Image export](docs/image-export.md)
- [Flattened PDF export](docs/flattened-pdf-export.md)

### Implementation and development

- [LaTeX Note PDF representation](docs/latex-note-pdf-spec.md)
- [LaTeX rendering safeguards](docs/latex-note-render-guards.md)
- [Template Note PDF representation](docs/template-note-pdf-spec.md)
- [Page editing and annotation preservation](docs/page-editing-annotation-model.md)
- [Local components and submodules](README.local-components.md)
- [Local Poppler fork](docs/local-poppler-fork.md)
- [Local Linux build notes](README.local-linux-build.md)

The application shell lives in `shell/`; viewer and annotation UI in `part/`;
the shared document model in `core/`; and PDF backend integration in
`generators/poppler/`. Pinned dependencies live under `external/`.

## About the Project

The name **Mengshee** is a deliberately adapted spelling of **Mengxi**
(`Mèngxī`, 梦溪, in Hanyu Pinyin). It pays tribute to the Northern Song
polymath Shen Kuo (沈括), traditionally also referred to as Shen Mengxi
(沈梦溪), and to his encyclopedic work
[*Mengxi Bitan*](https://zh.wikisource.org/zh-hans/%E6%A2%A6%E6%BA%AA%E7%AC%94%E8%B0%88)
(`Mèngxī Bǐtán`, 《梦溪笔谈》), commonly known in English as *Dream Pool
Essays*. The name reflects the project's interest in reading, technical
inquiry, careful observation, and the practical recording of knowledge.

Mengshee is derived from [KDE Okular](https://invent.kde.org/graphics/okular),
but is not an official KDE release and is not affiliated with or endorsed by
KDE. Okular's original license and copyright notices remain in the inherited
source files. Mengshee-specific changes follow the same licensing terms as the
surrounding code unless a file states otherwise.

Please report Mengshee-specific issues in
[this repository](https://github.com/zhaiyusci/mengshee/issues), not to KDE Okular,
unless the problem has also been confirmed in upstream Okular.
