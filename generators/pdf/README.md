# Unified PDF backend (draft)

This directory is the proposed home of the single PDF front end shared by the
MuPDF and Poppler backends. It is being built in phases; see the progress note
at the end.

- `pdfengine.h` — the engine seam: shared value types plus the capability
  interfaces a backend implements.
- `pdfenginemodule.h` — the C ABI a dynamically loaded engine module exports.
- `pdfengineregistry.h/.cpp` — runtime discovery and selection of engine
  modules (built as part of `mengshee_pdf_frontend`).
- `pdfutils.h/.cpp` — the neutral utility API for `part/` and `gui/` (built,
  not linked into the application yet).
- `pdfgeneratorbase.h` — the one class that implements `Okular::Generator` and
  the optional PDF interfaces by delegating to an engine (draft, not built).
- `autotests/` — a fake engine module and a registry/ABI test.

Read [`docs/pdf-backend-architecture.md`](../../docs/pdf-backend-architecture.md)
for the current-state analysis, the capability split, the semantic
normalization decisions and the migration phases.

Decision recorded with the draft:

- Mengshee builds **one PDF plugin** (`okularGenerator_mengshee_pdf`).
- The plugin loads **one engine module at run time** through
  `PdfEngineRegistry` (`MengsheePdfEngineMuPdf.dll` or
  `MengsheePdfEnginePoppler.dll`).
- Selection order: explicit test override, `MENGSHEE_PDF_ENGINE` environment
  variable, user configuration, then the first available module (MuPDF first).
- Both engine modules are built and installed; `MENGSHEE_PDF_BACKEND` stops
  being a compile-time switch.
- The plugin exposes the union of the optional Okular interfaces and gates each
  one on the active engine.

When the draft is approved, phase 1 adds the front-end target and the engine
module ABI, renames `MuPDFGenerator`/`PDFGenerator` to engine implementations
in their existing directories, and adds the single plugin wrapper. Source
directory paths stay unchanged.

## Implementation progress

- Done: `mengshee_pdf_frontend` builds the engine ABI, registry, utilities and
  `PdfGeneratorBase`; `pdfengineregistry.cpp`, `pdfutils.cpp` and
  `pdfgeneratorbase.cpp` all compile.
- Done: `mengshee_pdf_engine_fake` exercises the C ABI and
  `mengshee_pdf_registry_test` covers discovery, selection, ownership and the
  utility path.
- Done: `MengsheePdfEngineMuPdf.dll` wraps the existing `MuPDFGenerator` as an
  internal implementation detail, exposes it only through `PdfDocument` and the
  capability objects, and passes `mupdf_engine_module_test` (open, page count,
  render, capabilities and forwarded feature flags).
- Done: the single plugin `okularGenerator_mengshee_pdf.dll` is built (not
  installed) and `mupdf_plugin_test` loads it through `KPluginFactory`, lets it
  select the MuPDF module through the registry, and opens/renders a PDF.
- Done: Poppler implements the narrow `PdfDocument` too; both engines load via
  `createPageModel()` and the front end owns the page models. The narrow read
  path covers info/fonts/embedded/permissions/metadata/synopsis/annotation
  proxy, text pages, `renderToImage`, and image/tile requests.
- Done: the PDF engine is selectable from the General settings page
  (`[Core General] PdfEngine`); the environment variable still overrides it.
- Done: MuPDF 9/9 and Poppler 8/8 PDF test suites pass, and a temp-stage
  dual-engine app smoke passes with only the selected engine module loaded.
- Done: `PdfSaver`, `PdfPrinter`, `PdfSigner`, `PdfLinkEditor`, `PdfOcr` and
  `PdfCustomMetadata` capabilities are implemented by both modules and preferred
  by the front end; feature flags are mirrored from the legacy generator.
- Done: a clean-prefix install, dual-engine deploy, package, smoke and Inno
  Setup compile passed in a temporary workspace.
- Done: `PdfPageIdentity` maps `GotoAction` page tokens for both engines; the
  parttest live-link/destination cases pass through the unified engine.
- Done: `PdfPageEditor` covers the full page-insertion surface for both engines
  and is preferred by the front end.
- Done: ReadingView generation is part of `PdfCustomMetadata` and routed for
  both engines.
- Done: configuration pages, print options, print preview, layers and
  opaque/additional document actions are available as capabilities; the single
  plugin no longer falls back to legacy interface pointers for them.
- Done: certificate-backend selection is part of `PdfSigner`; the 72-DPI
  blank-page test assumption is fixed.
- Done: the external `PdfEngine::legacyGenerator()` bridge and its `legacy*`
  wrappers are deleted; `PdfGeneratorBase` has no legacy pointer and no fallback
  path. The modules keep their `MuPDFGenerator`/`PDFGenerator` internally and
  expose `hasFeature()` plus `setWrapperDocument()` to the front end.
  `EngineModuleApiVersion` is 4.
- Done: high-zoom tiles render only the requested region at full page scale;
  the window caption shows the loaded engine (`[MuPDF]` / `[Poppler]`).
- Done: Poppler asynchronous display rendering uses a worker-safe narrow path;
  export applies the view rotation. `imageexporttest` passes through the unified
  engine.
- Done: `mupdf_generator_base_test` checks `PdfGeneratorBase` loading, page
  ownership, features and the no-engine error path.
- Done: phase 2 deploy/package/smoke rules exist behind
  `MENGSHEE_PDF_ENGINE_MODULES=ON` (default `OFF`) and passed in a temp prefix.
- Done: explicit logical→native page mapping in `PdfGeneratorBase`; page
  topology changes and page tokens are exercised by the reorder/insert/rotate
  tests.
- Done: MuPDF now matches Poppler on view-session search, form calculation and
  signature fields: word-level text entities, `/AA/C` + `/CO` calculation
  order, and `PDF_WIDGET_TYPE_SIGNATURE` mapping.
- Next: page editing parity is complete enough that Poppler can be considered
  for a stock/read-only build; keep the fork only for features that are still
  fork-specific.
- The old `okularGenerator_mupdf` / `okularGenerator_poppler` plugins remain the
  active ones unless a phase-2 stage is deployed with
  `MENGSHEE_PDF_ENGINE_MODULES=ON`.
