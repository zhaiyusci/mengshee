# Local component submodules

This fork pins local component sources as git submodules:

- `external/poppler`: the pinned, locally maintained Poppler fork used for
  annotation behavior and the generic PDF page-sequence editor.
- `external/mupdf`: the upstream MuPDF checkout the default PDF backend builds
  from.

Initialize them after cloning:

```sh
git submodule update --init --recursive
```

The Windows build is the only maintained build path. It builds the Poppler and
MuPDF libraries from these checkouts into its own SDK prefix
(`windows-build/cmake/build-poppler-sdk.cmake` and
`windows-build/cmake/build-mupdf-sdk.cmake`); see
[Windows build from scratch](docs/windows-build-from-scratch.md). MuPDF is the
default backend, and Poppler remains available with
`-DMENGSHEE_PDF_BACKEND=Poppler`.

The Poppler submodule is not an unmodified upstream checkout. Its maintenance
policy, page-editor boundary, two-repository update procedure, and required
regression checks are documented in `docs/local-poppler-fork.md`. In particular,
the Poppler library, generated private headers, and page-editor sources used by
one Mengshee build must come from compatible submodule revisions.

LaTeX note rendering uses the StemTeX renderer, which is **not** a submodule:
Mengshee consumes StemTeX as a prebuilt runtime. On Windows,
`windows-build/cmake/prepare-stemtex-runtime.cmake` uses an installed StemTeX in
place (or installs the pinned release when there is none) and fetches the
QScintilla headers and import library matching that runtime from the same StemTeX
tag; see [Windows build from scratch](docs/windows-build-from-scratch.md).
`STEMTEX_STAGE_ROOT` selects a runtime explicitly, and
`MENGSHEE_QSCINTILLA_ROOT` overrides the QScintilla location when Mengshee's
CMake is configured directly.
