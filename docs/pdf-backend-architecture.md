# PDF backend architecture

Status: **implemented** (2026-11-16) behind the phase-2 opt-in
`MENGSHEE_PDF_ENGINE_MODULES`. The existing `OFF` build still uses the legacy
plugins; the `ON` stage uses the single plugin and the runtime engine modules.
The interface drafts it refers to are
[`generators/pdf/pdfengine.h`](../generators/pdf/pdfengine.h),
[`generators/pdf/pdfenginemodule.h`](../generators/pdf/pdfenginemodule.h),
[`generators/pdf/pdfengineregistry.h`](../generators/pdf/pdfengineregistry.h),
[`generators/pdf/pdfgeneratorbase.h`](../generators/pdf/pdfgeneratorbase.h) and
[`generators/pdf/pdfutils.h`](../generators/pdf/pdfutils.h).

Implementation progress:

- [x] Retired the legacy expansion/`MengsheeLayoutBox` machinery.
- [x] `mengshee_pdf_frontend`: engine ABI, registry, selection and utilities.
- [x] Fake engine module + `mengshee_pdf_registry_test` (discovery, C ABI,
      ownership, selection precedence, utilities).
- [x] Real MuPDF engine module (`MengsheePdfEngineMuPdf.dll`); it loads and
      renders `autotests/data/file2.pdf` (`mupdf_engine_module_test`).
- [x] `PdfGeneratorBase` implements the full Okular surface through
      `PdfDocument` plus capability objects, and the single plugin
      `okularGenerator_mengshee_pdf` is loaded through `KPluginFactory` in
      `mupdf_plugin_test`.
- [x] Poppler engine module copy (`MengsheePdfEnginePoppler.dll`): the existing
      Poppler plugin DLL also exports the module ABI and is copied under the
      discoverable name. `poppler_engine_module_test` and `poppler_plugin_test`
      pass; `renderToImage()` no longer requires a Document-owned page model.
- [x] MuPDF narrow `PdfDocument` read path: open/geometry/render/text/metadata
      and per-page model creation; `mupdf_narrow_document_test` covers it.
- [x] Wrapper document bridge: the front end passes its `Okular::Document`
      through `PdfEngine::setWrapperDocument()` so the engine's internal
      generator sees the same document (printing, signing, page operations).
- [x] Phase 2 deployment rules prepared behind `MENGSHEE_PDF_ENGINE_MODULES`
      (default `OFF`): install rules for the single plugin and both engine
      modules, deploy/package/manifest/smoke updates, and a dual-tree
      deployment helper. A temp-prefix deploy/package/smoke with both engines
      has passed; see [PDF backend deployment](pdf-backend-deployment.md).
- [x] `PdfGeneratorBase` builds page models through
      `PdfDocument::createPageModel()`, so the front end owns the pages.
- [x] Engine discovery is lazy: modules are found by the
      `MengsheePdfEngine<Id>.dll` naming convention and only the selected
      module is loaded.
- [x] Poppler also implements the narrow `PdfDocument`; `PdfGeneratorBase`
      now loads both engines through open → `pageCount()` →
      `createPageModel()`, and page models belong to the front end/Document.
- [x] Narrow read path: document info, fonts/font data, embedded files,
      permissions, metadata, synopsis, annotation proxy, text pages,
      `renderToImage` and image/tile requests use `PdfDocument`. The PDF engine
      selection is exposed in the General settings page
      (`[Core General] PdfEngine`).
- [x] Full MuPDF 9/9 and Poppler 8/8 PDF test suites pass; a temp-stage
      dual-engine app smoke passes with only the selected engine module loaded.
- [x] `PdfSaver`, `PdfPrinter`, `PdfSigner`, `PdfLinkEditor`, `PdfOcr` and
      `PdfCustomMetadata` capability interfaces are implemented by both modules
      and preferred by the front end; legacy fallback remains for
      ReadingView generation and capabilities that are still absent.
- [x] Generator feature flags are mirrored from the legacy generator
      (text extraction, raw data, tiled rendering, swap backing file, print
      capabilities and cancellation).
- [x] A clean-prefix `cmake --install`, dual-engine deploy, package, smoke and
      Inno Setup compile have passed in a temporary workspace; the Poppler
      private-header path bug found by that run is fixed.
- [x] `PdfPageIdentity` resolves PDF page tokens for `GotoAction`; MuPDF uses
      its `/MengsheeViews /PageId` tokens and Poppler its page `Ref` tokens.
      The parttest live-link/destination cases pass through the unified engine.
- [x] `PdfPageEditor` is implemented by both modules and preferred by the front
      end for the full `PageInsertionInterface` surface (insert, duplicate,
      insert-PDF, append, detach/attach, move, rotate, adjust box, delete,
      combine and can* queries), with transitional `Okular::Page*` outputs.
- [x] `PdfCustomMetadata` also covers ReadingView generation (source snapshot,
      page list and progress callback) for both engines.
- [x] Configuration pages, print options and print preview are routed through
      `PdfConfigurable` / `PdfPrintConfigurator` / `PdfPrintPreviewProvider`;
      layers, opaque actions and additional document actions use
      `PdfBackendActions`. The single plugin no longer falls back to legacy
      interface pointers for these.
- [x] `PdfSigner` owns the active certificate backend; the base forwards the
      selection to the signer instead of the legacy generator.
- [x] Replaced the 72-DPI page-width assumption in `parttest`; the blank-page
      insertion case passes through the unified Poppler engine.
- [x] The external `legacyGenerator()` bridge and all `legacy*` forwarding methods
      are removed from `PdfEngine`; `PdfGeneratorBase` holds only the engine and
      uses `PdfDocument` plus capabilities. `EngineModuleApiVersion` is now 4.
- [x] High-zoom tiles render only the requested region at full page scale
      instead of allocating the whole zoomed page per tile; tiled rendering
      stays sharp at 1500%+ and no longer falls back to blank/low-res tiles.
- [x] The window caption shows the loaded backend, e.g. `[MuPDF]` or
      `[Poppler]`, so the actual engine is visible without opening logs.
- [x] Feature flags use `PdfEngine::hasFeature()`; the wrapper document is
      attached through `PdfEngine::setWrapperDocument()`.
- [x] Asynchronous Poppler pixmaps use a worker-safe render path with the page
      model; export applies the view rotation, so image export/rotation tests
      pass through the unified engine.
- [x] `takeLoadedPages()` is removed; page models always come from
      `createPageModel()`.
- [x] Explicit logical→native page mapping in `PdfGeneratorBase`: Page* →
      native is the authority; page insert/duplicate/insert-PDF/append/detach/
      attach/move/rotate/adjust update the mapping, and tokens resolve back to
      the current logical page.
- [ ] Replace the modules' internal legacy inheritance with direct backend
      implementations; this is a backend rewrite, not a front-end interface
      task.

## Scope decision

Mengshee builds **one PDF plugin** and loads **one PDF engine module at run
time**:

```text
okularGenerator_mengshee_pdf.dll        one Okular plugin, front end only
  └─ Mengshee::Pdf::PdfEngineRegistry   QLibrary + C ABI
       ├─ MengsheePdfEngineMuPdf.dll    MuPDF engine module
       └─ MengsheePdfEnginePoppler.dll  Poppler engine module
```

- Both engine modules are built and installed. `MENGSHEE_PDF_BACKEND` stops
  being a compile-time switch.
- Engine selection happens when a document is opened, in this order:
  1. explicit test override (`PdfEngineRegistry::setOverrideEngineId`),
  2. `MENGSHEE_PDF_ENGINE` environment variable (id or module path),
  3. the user setting (default engine id),
  4. the first available module, in MuPDF, Poppler order.
- Testing two backends needs no rebuild: run the same binary with a different
  `MENGSHEE_PDF_ENGINE`, or load each module directly in a contract test.
- One engine is active per process. Hot-swapping the engine of an open
  document, or mixing engines within one process, is out of scope.

A two-plugin design (each plugin exposing only the interfaces its backend
supports) is documented as the rejected alternative at the end.

## 1. Where the PDF code lives today

| Layer | Target(s) | PDF-specific today |
| --- | --- | --- |
| `core/` | `okularcore` (shared) | No. Contains the `Generator` contract and the optional PDF interfaces, but links no PDF engine. |
| `interfaces/` | headers | Plugin interfaces (`saveinterface.h`, `printinterface.h`, ...). |
| `generators/mupdf/` | `mengshee_pdf_runtime` (DLL) + `okularGenerator_mupdf` (plugin) | The default backend. ~9.3k lines. Engine code, Qt-facing generator, LaTeX appearance, printing, OCR, page editing, resources. |
| `generators/poppler/` | `okularGenerator_poppler` (plugin) + static helper targets | ~9.6k lines. Engine code in `generator_pdf.cpp`; local-fork page editor (`PdfPageSequenceEditor`) and flattener (`PdfAnnotationFlattener`); signatures, PDF settings page, form fields, link editing. |
| `part/`, `gui/` | `okularpart` | `GuiUtils::pdfPageSizeInPoints()` (backend `#ifdef`). Everything else goes through `Okular::Document`. |
| `windows-build/` | CMake drivers | One backend per build, from SDK bootstrap through deployment, packaging, manifests and smoke tests. |

Build entry points that matter for this work:

- `CMakeLists.txt:225` selects the backend and finds `MuPDF` or `Poppler`;
- `generators/CMakeLists.txt:14` adds exactly one generator subdirectory;
- `CMakeLists.txt:640-646` links `okularpart` against `mengshee_pdf_runtime` or
  `Poppler::Qt6` and defines `MENGSHEE_USE_MUPDF`;
- `windows-build/cmake/deploy-runtime.cmake` deploys one backend DLL set and
  removes the other.

## 2. What is already unified, and what is missing

The **plugin contract is unified** in `core/generator.h` and the two interface
headers next to it. Both backends implement the same optional interfaces:

| Core interface | MuPDF | Poppler |
| --- | --- | --- |
| `Okular::Generator` | yes | yes |
| `Okular::SaveInterface` | yes | yes |
| `Okular::PageInsertionInterface` | yes | yes |
| `Okular::PdfLinkEditingInterface` | yes | yes |
| `Okular::PdfOcrInterface` | yes | yes |
| `Okular::ReadingViewEditingInterface` | yes | yes |
| `Okular::NumberedCalloutNumberingInterface` | yes | yes |
| `Okular::ConfigInterface` | – | yes |
| `Okular::PrintInterface` | – | yes |
| `Okular::PrintPreviewInterface` | yes | – |
| signature API (`canSign`/`sign`/certificate store) | – | yes |
| text export (`exportFormats`/`exportTo`) | – | yes |
| layers model, opaque actions, additional document actions | – | yes |
| batch append (`canAppendPdfFile`) | yes | – |
| rotate with annotations (`rotatePageWithAnnotationsInDocument`) | yes | – |

The missing piece is the **shared implementation behind that contract**. The
two classes ([`generator_mupdf.h`](../generators/mupdf/generator_mupdf.h),
[`generator_pdf.h`](../generators/poppler/generator_pdf.h)) each implement the
full surface, and the duplicated behavior has already drifted:

- Page size metric: MuPDF reports `Points`, Poppler reports `Pixels`
  (`generator_mupdf.h:37`, `generator_pdf.h:75`).
- Page model: Poppler keeps a logical/native page order and edits the live
  `/Pages` tree; MuPDF reorders native page objects directly.
- Undo state: Poppler stores `shared_ptr<void>` page snapshots, MuPDF keeps
  detached `pdf_obj` references.
- LaTeX appearance bounds: the two independent
  `PdfPageBounds::expandAppearance()` implementations (709 lines of Poppler
  Core versus 134 lines of MuPDF) were **retired**. Their production caller was
  removed in 2026.0.26.10; the remaining writer, the MuPDF
  `/MengsheeLayoutBox` reader and the expansion tests have been deleted. This
  is no longer a backend difference; see semantic normalization item 10.
- Custom metadata (`MengsheeViews`, `MengsheeNumberedCalloutNumbering`,
  `MengsheeOCRWords`, annotation-level LaTeX keys) is written and validated
  twice.
- Tests exist for MuPDF (`generators/mupdf/autotests/`) but there is no
  contract suite that both backends must pass.

So the goal is not a new UI API. It is a **shared front end, a runtime engine
seam, and one behavior implemented once**.

## 3. Target architecture

```text
                      core/ (okularcore, no PDF dependency)
                                ▲
             Okular::Generator / Save / PageInsertion / ...
                                │
              okularGenerator_mengshee_pdf.dll
        ┌───────────────────────┴────────────────────────┐
        │ PdfGeneratorBase                               │
        │   logical page model, native<->logical mapping │
        │   edit ids / undo, error policy, progress      │
        │   custom-metadata schema, OCR/flatten policy   │
        └───────────────────────┬────────────────────────┘
                                │ PdfEngine capability getters
                     PdfEngineRegistry  (QLibrary + C ABI)
             ┌──────────────────┴──────────────────┐
             │                                     │
  MengsheePdfEngineMuPdf.dll            MengsheePdfEnginePoppler.dll
  MuPdfEngine                           PopplerEngine
             │                                     │
        MuPDF 1.x                            Poppler (local fork)

        part/gui ──► Mengshee::Pdf::firstPageLogicalSize()
                     (front-end utility; uses the registry)
```

### Directory layout

```text
generators/pdf/                 new: shared front end only
  CMakeLists.txt                mengshee_pdf_frontend (STATIC), plugin, utilities
  pdfengine.h                   engine seam and shared value types (draft)
  pdfenginemodule.h             C ABI descriptor for engine modules (draft)
  pdfengineregistry.h/.cpp      QLibrary discovery and selection (draft)
  pdfgeneratorbase.h/.cpp       the only Okular::Generator implementation
  pdfpageidentity.h/.cpp        logical order, native mapping, edit identities
  pdfcustommetadata.h/.cpp      key names, JSON schemas, limits
  pdfocrpolicy.h/.cpp           page selection, skip/progress/cancel policy
  pdfutils.h/.cpp               neutral utilities for part/gui (draft)
  mengsheeplugin.cpp            single K_PLUGIN_CLASS_WITH_JSON wrapper

generators/mupdf/               stays where it is
  mupdfengine.h/.cpp            MuPdfEngine : PdfEngine (+ capabilities)
  mupdfmodule.cpp               exports mengshee_pdf_engine_module()
  ...                           existing files, class bodies unchanged
  target: mengshee_pdf_engine_mupdf (SHARED)

generators/poppler/             stays where it is
  popplerengine.h/.cpp          PopplerEngine : PdfEngine (+ capabilities)
  popplermodule.cpp             exports mengshee_pdf_engine_module()
  ...                           existing files, class bodies unchanged
  target: mengshee_pdf_engine_poppler (SHARED)
```

Keeping the backend directories in place is intentional. Moving them would
touch include paths, JSON metadata paths and existing build trees for no
architectural gain. Phase 1 renames the generator classes in place, adds the
module entry point, and puts the shared code in the front-end target.

### Layering rules

1. `pdfengine.h` and `pdfenginemodule.h` include only standard headers, Qt
   value types and Okular `core/`/`interfaces/` types. No `poppler-qt6.h`, no
   `mupdf/*.h`.
2. `PdfGeneratorBase` never includes a backend header. It knows engines only
   through `PdfEngine` capability getters.
3. The plugin links the front end, not an engine. The engine module links its
   own backend library and exports only the C ABI entry point plus the C++
   interfaces the front end calls.
4. `part/` and `gui/` depend on `Mengshee::Pdf` utility functions, never on a
   backend header, a backend SDK, or `MENGSHEE_USE_MUPDF`.

### Engine module ABI

```cpp
// generated/consumed by each module
struct EngineModuleDescriptor {
    std::uint32_t apiVersion;   // EngineModuleApiVersion
    const char *id;             // "mupdf" / "poppler"
    const char *displayName;
    const char *buildInfo;      // compiler, Qt version, build type (diagnostics)
    void *(*create)();          // returns PdfEngine*
    void (*destroy)(void *);    // destroys it; never `delete` across modules
};
extern "C" MENGSHEE_PDF_ENGINE_MODULE_EXPORT
const EngineModuleDescriptor *mengshee_pdf_engine_module();
```

- The registry rejects a module whose `apiVersion` differs, whose id/create/
  destroy are empty, or whose id is already provided by an earlier search path.
- Module and front end are built together; mixing artifacts from different
  builds is unsupported (same rule as the Poppler fork).
- `create`/`destroy` must run on the same thread unless an engine documents
  otherwise. `buildInfo` is logged, not enforced.
- The registry keeps `QLibrary` handles for the process lifetime and never
  unloads an engine module: MuPDF contexts, Poppler settings and static
  initializers are not guaranteed to survive unload/reload.

### Runtime selection

Search paths, in order:

1. explicit paths added with `PdfEngineRegistry::addSearchPath` (tests),
2. `MENGSHEE_PDF_ENGINE_PATH` (directories),
3. `<app dir>/plugins/mengshee_pdf`,
4. `<app dir>/mengshee_pdf`.

`MENGSHEE_PDF_ENGINE` accepts an engine id or a module path. The user setting
is a default id (no path) so an installation can move without breaking it.
Selection is applied when the plugin creates its engine for a document; an
already open document keeps its engine.

If no module is found or the selected module fails to load, the plugin still
loads: opening a document emits `Generator::error` with the registry error and
returns `OpenError`, instead of failing plugin loading. Utilities return their
`ok=false` result with the same message.

### Capability exposure in the single plugin

One plugin class must expose the union of the optional Okular interfaces and
gate each one on the active engine:

```cpp
class MengsheePdfGenerator final
    : public Mengshee::Pdf::PdfGeneratorBase,
      public Okular::ConfigInterface,
      public Okular::PrintInterface,
      public Okular::PrintPreviewInterface
{
    Q_OBJECT
    Q_INTERFACES(Okular::ConfigInterface Okular::PrintInterface)
    // Generator and SaveInterface are declared by PdfGeneratorBase.
public:
    MengsheePdfGenerator(QObject *parent, const QVariantList &args)
        : PdfGeneratorBase(Mengshee::Pdf::PdfEngineRegistry::instance()
                               .createSelectedEngine(), parent, args) {}

    // ConfigInterface: add nothing when the engine has no PdfConfigurable.
    bool reparseConfig() override;
    void addPages(KConfigDialog *dialog) override;

    // PrintInterface: engine widget, otherwise a DefaultPrintOptionsWidget.
    Okular::PrintOptionsWidget *printConfigurationWidget() const override;

    // PrintPreviewInterface: engine provider, otherwise an empty renderer.
    Okular::PrintPreviewRenderer createPrintPreviewRenderer(QString *error) override;
};
```

`PrintPreviewInterface` has no `Q_DECLARE_INTERFACE`, so it is inherited but not
listed in `Q_INTERFACES` (the current MuPDF plugin does the same).

Behavior deltas that must be verified in phase 5:

| Interface | MuPDF active | Poppler active |
| --- | --- | --- |
| `ConfigInterface` | no PDF settings page (`addPages` adds nothing) | existing PDF settings page |
| `PrintInterface` | `DefaultPrintOptionsWidget` (because `Document::canConfigurePrinter()` becomes true) | existing widget |
| `PrintPreviewInterface` | existing preview renderer | empty renderer until a generic snapshot preview is added; the print dialog already handles an empty renderer |
| signature API | `canSign() == false` | existing signing |
| layers/opaque/additional actions | defaults (null/no-op) | existing |

If a behavior delta proves unacceptable, the fallback is the two-plugin
alternative at the end of this document, not a second front end.

### Why capability getters instead of `dynamic_cast`

The engine now always lives in a different DLL from the front end. Cross-module
`dynamic_cast` on interface types is avoidable and fragile on MSVC. Each
concrete engine returns `this` (statically cast to the capability base) or
`nullptr`; callers only make virtual calls. No RTTI is required across the
module boundary.

## 4. Semantic normalization decisions

These are the decisions that make one front end possible. Each one must be
verified against both engines during its migration phase.

1. **Page size metric.** The interface exposes PDF points only.
   `PdfGeneratorBase::pagesSizeMetric()` returns `Points`, and the base builds
   `Okular::Page` from `PageGeometry::cropBox`. Poppler's current
   `Pixels` + DPI multiplication moves into the base. This changes the numbers
   stored in `Okular::Page` for Poppler but should not change the rendered
   result; verify `Document::localizedSize()` and template-note geometry.

2. **Page order and identity.** The base owns the logical page model: an
   ordered list of page identities, each resolved to a native index. Engines
   only see native indices and native insert positions. Detach/attach and live
   moves return edit identities; the base keeps the undo map and rebuilds
   `Okular::Page` objects. Poppler's `m_pageOrder` moves into the front end;
   its annotation wrappers are rebound by PDF object reference, not by old page
   index (the current `resetPageTopologyCaches()` logic).

3. **Rendering.** `RenderRequest` stays in output pixels, including the tile
   rectangle, annotation flag and abort callback. MuPDF keeps its display-list
   cache inside `MuPdfEngine`; Poppler keeps its thread/render-hint handling
   inside `PopplerEngine`. The base only computes the DPI-to-pixel request and
   forwards `PixmapRequest` flags.

4. **Text.** Phase 1 keeps `Okular::TextPage *textPage(int)` as the seam.
   If the shared builder proves useful, phase 3 replaces it with a neutral
   `QList<TextWord>` and moves Poppler's `abstractTextPage()` and MuPDF's
   structured-text loop into the front end. Do not do both at once.

5. **Annotations.** `Okular::AnnotationProxy` remains the native-annotation
   seam and stays engine-owned. The base returns it from
   `Generator::annotationProxy()`. `nativeId()` remains opaque;
   `LatexNoteData`, stamp naming and appearance rules stay in the engines until
   a shared appearance writer exists. Page-topology changes must notify the
   proxy before it is used again.

6. **Links.** Interface rectangles are normalized, unrotated page
   coordinates. Destination pages are native indices; the base converts the
   public one-based logical numbering on the way in and out. Link appearance
   (`Okular::PdfLinkAppearance`) is passed through unchanged.

7. **Custom metadata.** Key names, JSON schema versions, size limits and
   validation move into `pdfcustommetadata.h/.cpp`:
   `MengsheeViews`, `MengsheeNumberedCalloutNumbering`, `MengsheeOCRWords`,
   plus annotation-level LaTeX keys. Engines only read and write
   strings/dictionaries; policy is shared. This removes the current risk of the
   two backends accepting different payloads. `/MengsheeLayoutBox` is not part
   of this set; it was retired with the expansion code (item 10).

8. **OCR.** The base owns page selection, "skip pages with text", progress and
   cancellation. The engine owns recognition and writing the copy. MuPDF keeps
   its linked Tesseract; Poppler keeps its external Tesseract runtime. Shipping
   both modules means both Tesseract arrangements coexist in one installation;
   see the risk table.

9. **Flattening.** The base owns eligibility policy (which annotation types can
   be flattened, hidden annotations and links preserved, counts reported).
   Poppler writes through `PdfAnnotationFlattener`; MuPDF through its exporter.

10. **PDF utilities and retired appearance bounds.** The expansion machinery
    around `PdfPageBounds::expandAppearance()` no longer ran in production
    after 2026.0.26.10, and it has been **retired**:
    - deleted both `PdfPageBounds` implementations, the `pdf_page_bounds`
      target, the test-only `LatexPdfBounds` wrapper and its test;
    - deleted the MuPDF `/MengsheeLayoutBox` write/copy/read branches and the
      MuPDF test coverage for expansion;
    - appearances saved by 2026.0.26.4–2026.0.26.9 keep their expanded page box
      when re-edited; that compatibility loss is accepted.

    `Mengshee::Pdf::firstPageLogicalSize()` is simply "the visible page box,
    rotated"; there is no legacy branch. It uses the registry-selected engine,
    so `part/` and `gui/` no longer link a backend and the old
    `MENGSHEE_USE_MUPDF` switch disappears.

11. **Printing.** The engine prints. The preview provider and the print options
    widget are engine capabilities, gated by the single plugin as described
    above.

12. **Signing.** Capability interface only; MuPDF keeps returning
    `canSign() == false`.

13. **Save and swap.** Engine-owned. The base only forwards options and maps
    the result enum. Live unsaved annotation state remains a save-time concern
    of each engine.

14. **Actions and page references (open point).** Document- and page-level
    actions produced by the engine can contain `GoTo` page numbers (Poppler's
    `createLinkFromPopplerLink`, page opening/closing actions, additional
    document actions). Native page numbers must not leak into the model. The
    clean target is for the engine to return a neutral action description and
    for the base to build `Okular::Action` with logical pages; until that is
    implemented, action construction stays on the engine side for the current
    document and the base only re-resolves the pages it can see. This is the
    main design point to settle at the start of phase 3.

15. **Engine identity and settings.** The engine id is a stable string
    (`"mupdf"`, `"poppler"`), not a C++ enum, because modules are discovered at
    runtime. The user setting stores the id; old backend-specific settings
    remain the engine's own concern (Poppler's PDF settings page stays in the
    Poppler module).

## 5. Migration plan

Each phase is independently shippable. Phases 1–2 change how the application
is assembled; phases 3–6 change where PDF behavior lives.

### Phase 1 — runtime scaffolding

1. Add `mengshee_pdf_frontend` (STATIC): `pdfgeneratorbase.cpp`,
   `pdfengineregistry.cpp`, `pdfutils.cpp`, and the draft headers.
2. Add the C ABI entry point to the two backend targets:
   `mengshee_pdf_engine_mupdf` and `mengshee_pdf_engine_poppler` (SHARED).
   Rename `MuPDFGenerator` to `MuPdfEngine` and `PDFGenerator` to
   `PopplerEngine` in place; make each implement `PdfEngine` and the
   capabilities it supports. Move method bodies unchanged.
3. Add one plugin `okularGenerator_mengshee_pdf`, `K_PLUGIN_CLASS_WITH_JSON`,
   that creates the engine from `PdfEngineRegistry`. Keep the old plugin
   targets buildable behind a temporary option until the new plugin passes
   smoke, then remove them.
4. Build both modules in one build; select with `MENGSHEE_PDF_ENGINE`.
5. Implement the basic capability gating needed by the single plugin (no
   settings page for MuPDF, default print options, empty preview for engines
   without a provider). Phase 5 verifies and polishes these flows.
6. No PDF behavior change beyond engine selection and that gating. Acceptance:
   both modules open/render/save the smoke PDFs; the existing MuPDF tests run
   against the module; a first Poppler contract smoke runs against its module.

### Phase 2 — deployment, settings and packaging

- Install both engine modules under `plugins/mengshee_pdf/`, the single plugin
  under `okular_generators/`, and both SDK runtime sets (Poppler DLLs and the
  MuPDF-backed module).
- Update `deploy-runtime.cmake`, `package-windows.cmake`, required-component
  lists, manifests and the staged smoke test to check both engines. Remove the
  old single-backend filtering and the obsolete plugin DLLs.
- Add the user-facing default engine setting and a way to switch it; the test
  env override stays.
- Acceptance: the staged runtime can open the smoke PDF with each engine
  selected by env; the installer ships both modules and their licenses.

### Phase 3 — shared page model and metadata

- Move page construction (`PageGeometry` -> `Okular::Page`, annotations, links,
  forms, transition, page actions) into `PdfGeneratorBase`.
- Move the logical/native page mapping and edit identities into
  `pdfpageidentity`.
- Switch Poppler to `Points` and delete its DPI multiplication.
- Move custom-metadata key/schema/validation into `pdfcustommetadata`.

Acceptance: page labels, sizes, transitions, actions, reading views, callout
numbering and LaTeX note sizes behave identically with both engines.

### Phase 4 — page editing, links and undo parity

- Move page-sequence bookkeeping (insert/import/duplicate/detach/attach/move/
  rotate/resize box) into the base; engines expose native operations.
- Move undo/edit state ownership into the base; engines supply opaque snapshots
  where needed.
- Normalize link editing: one-based logical API at the top, native indices
  below, shared appearance handling.
- Settle the action/page-reference design point (item 14) and remove native
  page numbers from model-facing actions.

Acceptance: the page-editing and link regression lists from
`docs/page-editing-annotation-model.md` pass with both engines, including
annotation identity after move/rotate and destination renaming.

### Phase 5 — OCR, flatten, print and capability gating

- Move OCR page selection/progress/cancel and flatten eligibility/counts into
  shared code.
- Leave recognition, PDF writing and printing in the engines.
- Implement and verify the single-plugin gating: engine config page, default
  print options, preview provider, signature gating.
- Add a generic snapshot print preview over the engine seam if the empty
  renderer proves insufficient for Poppler.

### Phase 6 — utilities seam and contract tests

- Implement `pdfutils.cpp` over the registry; move
  `mengshee_pdf_runtime/runtimehelpers` behind it.
- Change `gui/guiutils.cpp` and its CMake to the neutral API; delete
  `MENGSHEE_USE_MUPDF` from `part/`, `gui/` and `CMakeLists.txt`.
- Run one shared scenario suite against both modules (open/render/text/save/
  annotations/pages/links/views/OCR/print). Register two CTest entries,
  one per engine. Tests that are genuinely engine-specific move to a
  per-engine suite with a documented reason.

## 6. Build and deployment impact

New target graph:

```text
mengshee_pdf_frontend (STATIC)
  ├─ linked by okularGenerator_mengshee_pdf (plugin DLL)
  └─ linked by okularpart (utilities only; no backend SDK)

mengshee_pdf_engine_mupdf (SHARED)     -> MengsheePdfEngineMuPdf.dll
mengshee_pdf_engine_poppler (SHARED)   -> MengsheePdfEnginePoppler.dll
```

- Plugin: `bin/plugins/okular_generators/okularGenerator_mengshee_pdf.dll`.
- Modules: `bin/plugins/mengshee_pdf/MengsheePdfEngineMuPdf.dll` and
  `MengsheePdfEnginePoppler.dll`.
- Poppler runtime DLLs (`poppler.dll`, `poppler-qt6.dll`, Tesseract and
  Leptonica as today) stay in `bin`; the MuPDF library is statically linked
  into its module.
- `okularpart` no longer links `mengshee_pdf_runtime` or `Poppler::Qt6` from
  phase 6; until then it keeps the current page-size helper link. In the final
  graph it links the front end and reaches engines through the registry.
- The installer grows by the second engine's DLL set and must ship both
  licenses. This is the main cost of the runtime-selection decision.
- `windows-build/cmake/deploy-runtime.cmake` currently removes the other
  backend; phase 2 reverses that: both modules, both SDK sets, updated
  manifests, required-components, smoke test and packaging checks.

For local development, both configurations can still be built separately while
the dual build stabilizes. The canonical build driver currently passes
`-DMENGSHEE_PDF_BACKEND`; phase 1 adds a both-engines mode and phase 2 makes
it the default.

## 7. Risks

| Risk | Why it matters | Mitigation |
| --- | --- | --- |
| Mechanical split changes behavior | ~19k lines change owner; include order, static init, DLL boundaries, moc classes | Move bodies unchanged in phase 1; diff-driven review; smoke both engines; no logic edits in the same commit |
| Engine module ABI mismatch | Front end and module are separate DLLs; Qt/compiler/debug builds must agree | `apiVersion` + `buildInfo` check; build both from the same tree; document unsupported mixing |
| Module load/search failure | A missing or blocked DLL must not make the plugin unloadable | Registry returns errors; plugin opens documents with `OpenError`; explicit search paths and logged paths |
| Two engines in one runtime | Poppler uses Tesseract/Leptonica DLLs, MuPDF's Windows archive embeds Tesseract; duplicate or conflicting copies are possible | Both live in separate modules; validate in the staged smoke test; ship license files; check DLL search order |
| Capability union changes UI | MuPDF gains Config/Print/Print-Preview interface presence | Return no settings page, `DefaultPrintOptionsWidget`, empty preview; verify print/preview/settings flows with both engines |
| Page-index normalization bugs | Wrong page edited/rendered after insert/move | Central `PageIdentity` with assertions; contract tests for insert/move/rotate/undo |
| Page size metric change | Poppler UI page size labels, template notes | Verify `localizedSize()`, template-note geometry, print scaling before deleting the pixel path |
| Annotation wrapper rebinding | Poppler wrappers cache page binding | Rebind by PDF Ref on every topology change; test annotations after move/rotate/save |
| Divergent custom metadata | Users exchange files between engines/builds | Shared schema + validation; round-trip tests with both engines |
| Retired `/MengsheeLayoutBox` | Notes saved by 2026.0.26.4–2026.0.26.9 keep an expanded AP that is no longer recovered exactly | Accepted; reader, writer and tests were removed. Mention the affected releases if a user reports changed padding on those old notes |
| Engine globals never unloaded | MuPDF contexts and Poppler settings may outlive a document | One engine per process; handles cached for process lifetime; no hot-swap |

## 8. Non-goals

- Changing `core/generator.h` or the plugin ABI.
- Per-document engine mixing or hot-swapping an open document's engine.
- Crash isolation between the front end and an engine module (a module crash
  still takes the process down).
- Rewriting the annotation model or the LaTeX renderer.
- Unifying the two Poppler fork helpers (`PdfPageSequenceEditor`,
  `PdfAnnotationFlattener`) into the front end; they stay engine-side.
- Replacing Tesseract, MuPDF or Poppler.

## Alternatives considered

### Two plugins, both built and installed

Each plugin keeps its exact current interface set (`ConfigInterface` and
`PrintInterface` on Poppler, `PrintPreviewInterface` on MuPDF), so no capability
gating is needed, and Okular's own backend setting switches engines. Rejected
because there is no single environment switch for tests, the user sees two
plugins for the same format, and the capability split remains baked into the
plugin classes. It remains the fallback if phase 5 gating proves unacceptable.

### Build-time selection (the previous draft)

One backend compiled per build, with the same shared front end. Rejected
because comparing engines or running the contract suite requires two builds and
two runtimes; the user asked for runtime loading for exactly this reason.
