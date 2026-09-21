# Flattened PDF export

Use **File → Export As → Flattened PDF…** to create a final copy of all pages.
Mengshee's editable annotations already display in standard PDF readers; this
operation instead makes supported visible annotation appearances ordinary page
content, independent of annotation visibility/printing settings.

## Preservation and safety

- Export snapshots the live document, including unsaved annotations, page edits,
  logical page order and native page rotations. It does not reopen the original
  disk PDF, change the undo stack, clear the modified flag or run PDF save actions.
- This is not print-to-PDF or whole-page rasterization. Existing appearance Form
  XObjects (including LaTeX stamps) are reused with their vector paths, fonts,
  transparency and images. An image appearance remains an image, not a vector.
- Normal page resources, contents, links, named destinations and bookmarks are
  retained. Hidden/Invisible/NoView annotations and form widgets are retained as
  annotations, not flattened; the result is **not guaranteed annotation-free**.
- The output must be different from the current source. Symlink outputs and
  source hard-link aliases are rejected. An existing destination requires UI
  confirmation. Snapshot/flattening failures leave it untouched; the final write
  uses `QSaveFile` without direct-write fallback.
- Export runs synchronously, with a busy cursor; there is no mid-export cancel.
  The dialog reports flattened and retained annotation counts on success.
- Reader-only view rotation is not a native PDF page edit and is not serialized.

## Supported appearances and explicit limits

Supported markup includes FreeText, Line, Square/Circle, Polygon/PolyLine,
Highlight/Underline/Squiggly/StrikeOut, Ink and Stamp. Valid supplied Text/Caret
appearances can also be used when their flags/behavior are supported. Missing
normal appearances for standard supported types are generated on the snapshot
using Poppler's vector appearance generators, never by rasterizing the page.
State dictionaries must explicitly select an appearance with `/AS`. The NoRotate
flag used by LaTeX notes is resolved against the current native page rotation and
baked into the appearance placement in the copy.

Export fails with an error rather than silently losing unsupported content:

- encrypted PDFs and signed PDFs/signature dictionaries;
- missing, malformed or ambiguous appearances that cannot be safely generated;
- NoZoom, ToggleNoView, optional-content or unsupported interactive
  annotation behavior, structural tagging associations and comment reply chains;
- attachments, rich media and other unsupported visible annotation types;
- unusual popup relationships (ordinary reciprocal popup containers are removed
  with their flattened parent).

The selected policy follows screen visibility. Visible annotations become page
content and therefore print even if their former Print flag was unset. Retained
links/widgets remain interactive; their appearance must not be silently reordered
relative to overlapping flattened content.

This is **not secure redaction, metadata sanitization, copy protection or signature
preservation**. Original annotation objects/metadata may remain as unreachable
PDF objects. PDF editors can still edit ordinary page content. Keep the editable
original when formula/template source or interactive annotation editing is needed.

## Implementation and tests

- `Document::exportFlattenedPdf()` checks thread affinity and destination identity.
- The Poppler generator saves the live state to a private snapshot, invokes the
  source-level `PdfAnnotationFlattener` helper, then atomically writes the result.
- `external/poppler/utils/PdfAnnotationFlattener.{h,cc}` uses the pinned Poppler
  Core API, with no new shared-library ABI. Its full-rewrite temporary output is
  reopened before no-replace publication; that internal step needs a filesystem
  supporting hard links (the normal Windows NTFS temporary directory does).
- `autotests/flattenedpdfexporttest.cpp` exercises the public API, visual output,
  non-destructive failure cases, retained PDF structures and modal UI paths.
- Build the helper against the same private headers/Core SDK as the existing page
  sequence editor. Use the canonical Windows CMake build paths from
  `windows-build/README.md`; rebuild core, part and PDF generator together.
