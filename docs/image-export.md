# Export PDF pages as images

Use **File → Export As → Images…** to export the currently open PDF, including unsaved native PDF edits. Each selected page produces one PNG or JPEG file; this is full-page rendering, not a screenshot or extraction of embedded images.

## Options

- Current page, all pages, or a comma-separated range such as `1-3, 5, 8-10`. Numbers refer to the current document order and start at 1. Ranges are sorted and deduplicated; invalid or reversed ranges are rejected.
- PNG (lossless) or JPEG, when the corresponding Qt writer is available.
- Resolution: 36–1200 DPI, default 300. JPEG quality defaults to 90.
- Include annotations (default on). This controls native PDF annotations, not selections, search highlights, or editing handles. Form widgets remain visible in the no-annotations mode.
- An existing output directory and a safe filename prefix. Page 5 becomes `prefix_0005.png` (or `.jpg`), even when exporting a subset.

Existing output files require confirmation. Each image is written atomically using `QSaveFile`; failures do not intentionally truncate an existing output. Canceling or encountering an error keeps pages already written and reports the count and directory. Cancellation is checked **between pages**; rendering and encoding one page are synchronous and may temporarily make the dialog unresponsive for complex PDFs.

## Rendering contract

`Document::renderToImage()` delegates to the current generator. The PDF backend uses the live Poppler document and logical-to-native page mapping, so unsaved annotation edits, inserted pages, reordered pages and native page rotation are retained. View rotation is applied separately. The PDF crop box defines the page extent. Rendering uses white paper and antialiasing, independently of the view's paper color. Temporary backend settings are restored while holding the rendering mutex.

The implementation rejects images larger than 64 million pixels or 32768 pixels on either axis, rather than silently lowering DPI. A 64-million-pixel RGBA image alone needs about 256 MB; Poppler may use additional intermediate memory. Lower DPI if an image is too large.

The API must be called on the document's owning GUI thread. It does not pump events while rendering. The UI revalidates document lifetime between pages and stops on document close/reload. Do not substitute reopening the on-disk PDF: it may not contain the current edits.

Adding virtual methods to `Generator` requires rebuilding the core and generator plugins together; do not mix old generator binaries with the new core.

## Tests

`autotests/imageexporttest.cpp` exercises rendered dimensions, PNG/JPEG encoding, invalid requests, unsaved annotations, page order, native plus view rotation, blank insertion, page-range parsing and filename validation. It also requests an asynchronous display pixmap after export, checking that the ordinary viewer rendering path still works with the updated generator interface. `autotests/imageexportuitest.cpp` drives the real dialog through PNG/JPEG export, cancellation and overwrite refusal. The tests generate small PDFs in temporary directories rather than modifying repository fixtures.

Build the `imageexporttest`, `imageexportuitest` and `okularGenerator_poppler` targets together, with `BUILD_TESTING=ON`. Run `ctest -R '^imageexport(ui)?test$' --output-on-failure` from the build directory, using the freshly built core, part and PDF generator plugin on the runtime library/plugin paths. Both test targets have a 60-second CTest timeout.
