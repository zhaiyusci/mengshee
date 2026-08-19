# Local Poppler Fork

Mengshee does not build against an arbitrary system Poppler checkout. The
`external/poppler` submodule pins the locally maintained Poppler fork used by
the PDF generator. The submodule commit recorded by this repository is part of
the source and binary compatibility contract.

## Why the Fork Exists

The fork currently carries two categories of changes:

- annotation appearance, FreeText fallback, Windows/CJK font handling, and the
  Qt annotation behavior required by Mengshee's annotation features;
- a branding-neutral PDF page-sequence editor used to insert, import, delete,
  move, and reorder pages while preserving the page object graph and
  annotations.

The page editor is implemented by:

- `external/poppler/utils/PdfPageSequenceEditor.h`;
- `external/poppler/utils/PdfPageSequenceEditor.cc`;
- `external/poppler/utils/pdfpagesequence.cc`, the optional command-line
  frontend built when Poppler utilities are enabled.

These names must remain product-neutral. Code inside the Poppler repository
must not contain Mengshee branding or depend on Mengshee UI or document-model
types. Product integration belongs in `generators/poppler/`. The submodule's
consumer-neutral maintenance notes are in
`external/poppler/README.local-fork.md`.

## Build Boundary

`PdfPageSequenceEditor` uses Poppler Core and its unstable C++ API, including
`PDFDoc`, `XRef`, `Object`, and the page-object writing helpers. It deliberately
does not add a public Poppler ABI.

The Mengshee build creates the `pdf_page_sequence_editor` static target from
the files in the pinned submodule and links it into the PDF generator. The
source headers, generated private Poppler headers, and installed Poppler
library must therefore come from compatible revisions. Mixing the editor from
one submodule commit with an SDK built from another commit is unsupported.

The Windows SDK build uses `ENABLE_UTILS=OFF`, so the standalone
`pdfpagesequence` executable is not included in the Windows application. The
Mengshee PDF generator still compiles the editor source directly.

## Updating Poppler

Treat a Poppler update as a two-repository change:

1. Make and test the generic Poppler changes in `external/poppler`.
2. Commit and push the Poppler fork commit first.
3. Update the parent repository's submodule gitlink to that exact commit.
4. Rebuild the Poppler SDK and its generated private headers from a clean or
   otherwise verified build directory.
5. Rebuild the Mengshee PDF generator against that SDK.
6. Test blank-page insertion, PDF-page import, deletion, live page movement,
   saved page reordering, and annotation preservation/identity.
7. Confirm the Poppler worktree contains no product branding:

   ```sh
   git -C external/poppler grep -n -I -E \
     'Scholia|scholia|SCHOLIA|Mengshee|mengshee|MENGSHEE'
   ```

8. Commit the parent gitlink only after the Poppler commit is available to
   other clones and CI.

When rebasing the fork onto a newer upstream Poppler, review all uses of the
unstable Core API rather than assuming source compatibility. A successful
compile is not sufficient: page-tree serialization and preservation of
annotations, forms, named objects, optional content, and output intents require
behavioral tests.

## Ownership Boundary

Generic PDF mechanics that need Poppler internals may remain in the Poppler
fork. Mengshee-specific policy, UI, undo/redo, live page ordering, error
presentation, and save orchestration remain in the parent repository. If an
extension cannot be described and tested without referring to Mengshee, it does
not belong in the Poppler submodule.
