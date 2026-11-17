# PDF backend capability parity

Status: 2026-11-17. This matrix describes the **product capability surface** of
the three backends we care about:

- **MuPDF module** — `MengsheePdfEngineMuPdf.dll`, upstream MuPDF plus the
  Okular-facing generator.
- **Poppler fork** — `MengsheePdfEnginePoppler.dll`, built against the local
  Poppler fork (`external/poppler`, Poppler 26.02.0 + ~7.6k lines of product
  patches).
- **Poppler upstream** — stock Poppler Qt6/core, used as a portability check.
- **PDFium / PDF.js** — forward-looking read-only candidates, not implemented
  in this repository.

Legend:

- ✅ implemented and covered by a test in this repository.
- ◐ partial, display-only, or not end-to-end verified.
- ✗ not implemented by that backend.
- n/a not meaningful for that backend.

The matrix is about **behavior**, not about whether the code compiles. A
capability is only marked ✅ when there is a test in `autotests/` or
`generators/*/autotests/` that exercises it through the unified front end.
The **PDFium / PDF.js** columns are feasibility assessments for future
adapters, not tested code in this repository.

## 1. Read / render / text

| Capability | MuPDF | Poppler fork | Poppler upstream | PDFium | PDF.js | Notes |
|---|---|---|---|---|---|---|
| Open file / data | ✅ | ✅ | ✅ | ✅ | ✅ | MuPDF `documenttest`, Poppler `poppler_plugin_test` |
| Password / locked document | ✅ | ✅ | ✅ | ✅ | ✅ | MuPDF narrow open maps `NeedsPassword` |
| Page count / geometry / rotation | ✅ | ✅ | ✅ | ✅ | ✅ | `mupdf_narrow_document_test`, `poppler_narrow_document_test` |
| Render page | ✅ | ✅ | ✅ | ✅ | ✅ | includes view rotation and DPR |
| Tiled rendering | ✅ | ✅ | ✅ | ◐ | ◐ | MuPDF/Poppler region tiles; PDFium/PDF.js have their own tile strategy |
| Text extraction | ✅ | ✅ | ✅ | ✅ | ✅ | MuPDF now emits word-level `TextEntity`s |
| Text search | ✅ | ✅ | ✅ | ✅ | ✅ | `searchtest` 21/21 |
| Document info / metadata | ✅ | ✅ | ✅ | ◐ | ◐ | PDFium/PDF.js expose basic metadata only |
| Fonts / font data | ✅ | ✅ | ✅ | ◐ | ◐ | embedded font bytes are not a first-class PDFium/PDF.js API |
| Embedded files | ✅ | ✅ | ✅ | ◐ | ◐ | PDFium has attachment APIs; PDF.js has limited support |
| Permissions | ✅ | ✅ | ✅ | ◐ | ◐ | |
| Synopsis / outline | ✅ | ✅ | ✅ | ✅ | ✅ | |
| Page model: annotations | ✅ | ✅ | ✅ | ✅ | ◐ | PDF.js can render annotations; write APIs are limited |
| Page model: links | ✅ | ✅ | ✅ | ✅ | ✅ | |
| Page model: forms | ✅ | ✅ | ✅ | ✅ | ◐ | |
| Annotation proxy | ✅ | ✅ | ✅ | ◐ | ✗ | editing annotations is not a PDF.js feature |
| Layers / optional content | ✅ | ✅ | ✅ | ◐ | ◐ | |
| Additional document actions | ✅ | ✅ | ✅ | ◐ | ◐ | |

## 2. Forms, signatures, printing

| Capability | MuPDF | Poppler fork | Poppler upstream | PDFium | PDF.js | Notes |
|---|---|---|---|---|---|---|
| Edit text/button/choice fields | ✅ | ✅ | ✅ | ✅ | ◐ | `editformstest` |
| Form calculation | ✅ | ✅ | ✅ | ◐ | ◐ | MuPDF now exposes `/AA/C` + `/CO`; `calculatetexttest` |
| Signature field display | ✅ | ✅ | ✅ | ◐ | ◐ | MuPDF maps `PDF_WIDGET_TYPE_SIGNATURE`; `signatureformtest` |
| Certificate validation | ◐ | ✅ | ✅ | ◐ | ◐ | MuPDF currently reports `SignatureNotVerified`; no chain validation |
| Actual signing | ✗ | ◐ | ◐ | ◐ | ✗ | MuPDF `canSign()` is false; Poppler has the legacy signing path, but no real certificate is exercised in this repository |
| Native print | ✅ | ✅ | ✅ | n/a | n/a | `mupdf_print_test` |
| Print options widget | ✗ | ✅ | ✅ | n/a | n/a | MuPDF uses the default widget |
| Print preview renderer | ✅ | ✗ | ✗ | n/a | n/a | MuPDF provides a snapshot renderer |
| Export text formats | ✗ | ✅ | ✅ | ◐ | ✗ | Poppler exposes text export; MuPDF does not |

## 3. Save / editing / product metadata

| Capability | MuPDF | Poppler fork | Poppler upstream | PDFium | PDF.js | Notes |
|---|---|---|---|---|---|---|
| Save changes | ✅ | ✅ | ◐ | ◐ | ✗ | PDFium can save a loaded document with `FPDF_SaveAsCopy`; PDF.js has no general save |
| Save copy | ✅ | ✅ | ✅ | ◐ | ✗ | |
| Swap backing file | ✅ | ✅ | ✗ | ✗ | ✗ | |
| Flatten annotations | ✅ | ✅ | ✗ | ◐ | ✗ | MuPDF implements this in `export.cpp`; Poppler fork has `PdfAnnotationFlattener` |
| Insert blank page | ✅ | ✅ | ✗ | ◐ | ✗ | `capabilitytest` |
| Duplicate page | ✅ | ✅ | ✗ | ◐ | ✗ | |
| Insert page from PDF | ✅ | ✅ | ✗ | ◐ | ✗ | |
| Append / combine PDF files | ✅ | ✅ | ✗ | ◐ | ✗ | |
| Delete / detach / attach page | ✅ | ✅ | ✗ | ◐ | ✗ | |
| Move page | ✅ | ✅ | ✗ | ◐ | ✗ | |
| Rotate page | ✅ | ✅ | ✗ | ✅ | ✗ | PDFium has page rotation APIs |
| Resize / adjust page box | ✅ | ✅ | ✗ | ◐ | ✗ | |
| Named destination editing | ✅ | ✅ | ✗ | ◐ | ✗ | MuPDF native destinations; Poppler fork has `PdfPageSequenceEditor` |
| Internal / external / remote links | ✅ | ✅ | ✗ | ◐ | ✗ | |
| Link rectangle edit / delete | ✅ | ✅ | ✗ | ◐ | ✗ | |
| Page token identity | ✅ | ✅ | ✗ | ◐ | ✗ | `PdfPageIdentity`; parttest live-link cases |
| OCR text layer read/replace | ✅ | ◐ | ✗ | ✗ | ✗ | MuPDF save-with-OCR round-trip is covered by `capabilitytest`; Poppler is only contract-tested |
| ReadingView get/set/token | ✅ | ✅ | ✗ | ✗ | ✗ | |
| Numbered callout metadata | ✅ | ✅ | ✗ | ✗ | ✗ | |
| LaTeX note metadata | ✅ | ✅ | ✗ | ✗ | ✗ | Poppler fork adds Qt annotation custom properties; MuPDF implements it in its generator |

## 4. What the local Poppler fork actually adds

The fork is based on **Poppler 26.02.0** and has **54 product commits**,
**28 files**, **about +7.6k lines**. The important ones for this matrix:

- `utils/PdfPageSequenceEditor.cc` (~3.1k lines): page sequence editing,
  named destinations, cross-file links, page-box changes.
- `utils/PdfAnnotationFlattener.cc` (~0.6k lines): flattened PDF export.
- `utils/PdfPageBounds.cc` (~0.7k lines): page bounds helpers.
- `poppler/Annot.cc` (+1.4k lines) and Qt6 annotation changes (~1k lines):
  LaTeX stamp appearances, custom properties, appearance preservation.
- `poppler/Catalog`, `PDFDoc`, `Form`, `Outline`, `GlobalParamsWin`:
  destination/page/field/font behaviors.
- Qt6 `Document::namedDestinationNames()` and signature/form additions.

Therefore:

- **No fork required**: open, render, text, basic metadata, basic forms,
  basic annotations, printing, and a read-only PDFium/PDF.js adapter.
- **Fork required for full parity**: page editing, named destinations,
  internal/external/remote link editing, flattening, LaTeX/callout metadata,
  OCR text layers, and some page-size/rotation behavior.

## 5. Switching to MuPDF

After the 2026-11-17 parity fixes, the three previously failing MuPDF tests
pass:

- `searchtest`: word-level text entities, 21/21.
- `calculatetexttest`: `/AA/C` + `/CO` calculation, 3/3.
- `signatureformtest`: signature widget mapping, 3/3.

MuPDF is therefore a credible **single read + edit backend** for PDFs.
Remaining parity gaps are narrower:

1. **Actual signing** is not implemented (`canSign()` false); display of
   signed fields is implemented.
2. **Text export formats** exist only in Poppler.
3. **Poppler-specific configuration page** has no MuPDF equivalent.
4. Some behavior differences remain in annotation appearance/font fallback,
   media links and page-bound handling; they need a per-document corpus, not
   only unit tests.

If the product does not care about signing, MuPDF can be the
canonical backend and Poppler can become read-only or be dropped.

## 6. Adding PDFium or PDF.js

The unified front end is already capability-based, so a future engine only
needs to return the capabilities it can honestly provide:

- **PDFium**: a good read/render/forms/annotations backend; page creation and
  save exist, but page-sequence editing, named-destination editing,
  cross-file link editing, flattening and LaTeX/callout metadata do not map
  1:1. Expect a read-only adapter plus a small set of editing extensions.
- **PDF.js**: a good read/render/text/search/forms-display backend, but no
  general document mutation/save. It should be a read-only engine module and
  never be the editor.
- **MuPDF**: the only backend currently covering the full edit surface
  natively.
- **Poppler fork**: full parity today, but it is a second implementation of
  the edit surface and its fork cost is the highest.

Recommended split:

```text
core read contract      open, page geometry, render, text, search,
                        metadata, annotations, links, forms
optional edit contract  save, flatten, page editor, link editor,
                        OCR layer, ReadingView/callout metadata
```

The front end already gates both contracts through `PdfEngine` getters; the
missing piece is a UI/feature matrix that hides edit actions when the active
engine returns `nullptr` for an optional capability.

## 7. Verification commands

```powershell
ctest -R "searchtest|calculatetexttest|signatureformtest|editformstest" --output-on-failure
ctest -R "mengshee_pdf_registry_test|mupdf_engine_module_test|mupdf_generator_base_test|mupdf_plugin_test|mupdf_narrow_document_test|mupdf_capability_test|mupdf_backend_test|mupdf_print_test|mupdf_latex_rotation_test" --output-on-failure
```

Poppler equivalents are in the Poppler build tree; `imageexporttest` also
covers async display rendering and view rotation.

## 8. Decision summary

| Goal | Recommendation |
|---|---|
| One full-featured PDF backend | MuPDF |
| Keep Poppler rendering without fork maintenance | Poppler read-only against upstream |
| Full Poppler editing compatibility | Keep the local Poppler fork |
| Future PDFium/PDF.js | Read-only engine modules; do not expect editing parity |
| Minimum maintenance | MuPDF canonical + PDFium/PDF.js read-only experiments |
| Maximum legacy compatibility | Keep both MuPDF and Poppler fork |