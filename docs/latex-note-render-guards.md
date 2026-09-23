# LaTeX note rendering guards

## Reference and geometry policy

Reference (read-only): `C:\Users\jairy\Documents\LaTeXBlocks`, particularly
Word's `StemTeXRenderer.cs` and PowerPoint's `LaTeXBlockStyle.cs`.
The transferred cropping principle is **existing TeX logical bounds UNION
painted bounds**, not an ink-tight crop.

**Important correction after testing in a real boxed note:** the reference's
`FullLineHeight` option defaults to false. Its optional full-line struts are not
the default for a fixed-width display-plus-prose block. Word's Auto formula
wrapper is a different path and does not accept the mixed display/prose example.
Consequently we do not force a full em-height strut into a leading display.
The earlier revision did so and added approximately 4pt above a 10pt alpha.

The reference itself does not universally zero display skips: the unguarded
native source can still create the original extra paragraph. We retain our
leading-display fix rather than copying that behaviour.

## Leading-display fix and ordinary text boundaries

For `$$\alpha$$ test`, the original worker minipage creates an empty indentation
paragraph, then adds 7.57997 TeX points of baseline glue at 10pt. This occurs even
without the colour wrapper.

`part/latexsource.cpp` inserts only this render-time prefix after leading
comments/whitespace:

```tex
\hrule width\hsize height0pt depth0pt\relax\noindent%
```

The invisible width anchor preserves requested layout width, unlike `\noindent`
alone. It introduces no real blank paragraph. Source characters and authored
display skips are preserved; inline/prose-first/macro-first inputs are not
reclassified as literal leading displays.

There is **no injected math atom, phantom or vertical rule inside the formula**,
and no closing-delimiter lexer. Thus initial scripts, terminal punctuation,
infix fractions and author macros are not changed by an injected math item.

Ordinary text still uses the measured local vertical box: the first measurable
text line gets at least the selected base font's strut ascent by a real top kern;
a real final paragraph hlist gets at least its strut depth. Depth is applied
last, after reboxing. No unconditional trailing strut turns resumed empty
horizontal mode after a display into a new paragraph. Simple `a`, `A`, `g` and
inline alpha keep their normal line box. Author-local custom boxes/fonts retain
their natural metrics when they exceed these minimums.

This is not a TeX macro interpreter. Leading whatsits (such as an authored bare
colour declaration) can obscure first-line measurement; that boundary is left
alone rather than guessed. A tested `\vsplit` workaround regressed layout and
was rejected. Unusual existing colour/aftergroup-generated paragraphs are not
removed by deleting arbitrary author nodes.

At 10pt, removing the forced display strut leaves roughly 1bp of native preview
border above alpha, rather than roughly 5bp. A note's default 3bp padding is
separate: total top space is therefore roughly 4bp rather than 8bp. Native ink
precision protection may add a small fraction of a point. The actual leading
short-display skip is zero in this example; clearing it would not fix the extra
strut height. Display-to-prose and ordinary baseline distances are retained.

## Native PDF bounds union

`generators/poppler/pdfpagebounds.{h,cpp}` measures the **already typeset PDF**.
This is a Mengshee-owned adapter using the pinned backend's existing Core/Splash
interfaces, not a policy or helper added to the Poppler fork.
`part/latexpdfbounds.cpp` invokes it on the validated, caller-owned SDK artifact.

- A 1×1 Splash font provider obtains real glyph outlines, not whole-font ascent
  boxes. There is no page-sized raster allocation or raster replacement.
- A large inspection canvas finds exterior glyphs, native paths/rules, Type3,
  images and tiling patterns that the original page would otherwise clip.
- The existing MediaBox is unioned with conservative painted bounds plus a 1bp
  allowance. This is `union(logical, paddedInk)`, not padding the entire union
  again. Logical whitespace is not removed.
- Curve control hulls, stroke joins, image rectangles and complex clips/masks can
  overestimate bounds. Font precision allowances scale with transforms; unsafe
  anisotropic/near-singular transforms fail explicitly.
- Output is zero-origin, with translated original content streams/resources;
  selectable text and vectors remain intact. There is no second TeX pass.

SVG-only measurement was rejected: dvisvgm can successfully return bounds while
ignoring `pdf:literal`, including a tested 300×60bp rectangle.

The utility accepts valid generated single-page appearance fragments, not
arbitrary documents. Encryption/signatures, unsupported geometry/transforms,
filters and resource excesses fail explicitly. Annots/AcroForm are deliberately
removed under this appearance-only contract. Input is read-only; a verified new
output is published without replacing an existing destination. The application
then atomically replaces only its private SDK artifact using `QSaveFile`.
Limits include 64 MiB input/output and decoded non-image streams, 2,000,000
preflight nodes and paint/outline operations, nesting bounds, an inspection
canvas within ±1,000,000bp and output spans at most 50,000bp. The 15-second
deadline is cooperative: it cannot interrupt PDFDoc opening or an individual
font-library call. This is not a malicious-PDF sandbox or complete syntax validator.

## Fixed-frame clipping is a separate operation

A complete source PDF does not mean content may paint over the note frame.
Mengshee's `generators/poppler/latexappearance.cpp` authors the frame, leader,
content placement and content-only clip. It uses the existing generic importer
only to copy a raw source PDF into the destination document, then submits its
own Form through Core's existing `Annot::setNewAppearance` interface.
Poppler's default custom-stamp behaviour is not modified.

The source-only `/Fm0 Do` is surrounded by a local graphics-state save,
frame-inner rectangle clip and restore. `/Fm0` always retains the **unclipped
source**, not a previously composed outer appearance. Shrinking, saving,
reopening without the temporary source PDF and growing the frame therefore
cannot accumulate clips or opacity. Unsupported appearances without recoverable
raw source are preserved rather than guessed; recompile those notes.

The existing private Qt Document-to-Core bridge is centralized in
`popplercorebridge.h`; it still requires matching pinned headers and library.
No additional Qt annotation-memory-layout adapter or backend API is introduced.
The annotation adapter obtains the real bound Core handle via a protected
member pointer declared by the pinned Qt wrapper, not an object downcast or
name-based search. Empty/duplicate `/NM` names remain valid. Wrong-document,
wrong-page and unbound handles are rejected, and raw resources cannot be
carried across document owners.

The frame path is inset by half the stroke width, so its **inner edge is a full
visible stroke width** inside the frame rectangle. Transparent/zero-width
borders contribute no stroke inset. An exhausted inner rectangle paints no
content. Padding still places content; this change does not redefine padding,
move/scale text or resize the user's fixed frame.

Clipping uses the frame rectangle, not the outer appearance BBox, which may also
contain a callout leader. The leader and frame are outside the content-only
clip. Repainting a border on top is not used as a workaround, since translucent
content would still blend into it. GUI page rendering and saved PDF appearances
use this same composition path.

The explicit-render cache identity is now `content-v9-app-appearance`, so a
rerender cannot take the unchanged-path shortcut instead of running the
application-owned appearance composer. Existing saved AP streams are not rewritten on PDF load: explicitly
recompile an old note to obtain both the new layout and the new content clip.

## Other retained guards

- Nonblank source, no NUL, at most 250000 UTF-16 code units.
- Finite width `[0,50000]` (zero/default), font size zero/default or `[1,200]`.
- Readable regular nonsymlink nonempty `%PDF-` artifact, at most 64 MiB. The magic
  header alone is not full PDF validation.
- Same colour wrapper for black and other colours: standard `\color` when
  available, otherwise driver colour push/pop. Current-colour semantics remain.
- Space-neutral boundaries preserve comments, CRLF and terminal backslashes;
  stored source is not rewritten.
- Native job-ID checks and existing render/conversion deadlines remain.

Private registers are allocated once per worker and locally reused. Explicit
TeX `\global` assignments can still leak between successful requests; grouping
is not successful-request isolation and no such sandbox is claimed.

## Verification evidence

Automated tests: `latexrenderguardstest`, `latexsourcepreparationtest`,
`latexpdfboundstest`, and `latexframeclippingtest`.

The new frame regression uses synthetic vector content, independent of TeX:
widths 0/1/4, padding 0/2, boxed/callout, opacity 1/.5, zero content offset and
exhausted inner frame. It checks protected border pixels, preserved leaders,
unchanged bounds/source hashes and save/reopen output. All 26 data rows now use
the application-owned composer. Additional tests verify the unchanged backend
as a negative control, recovery after source deletion and repeated shrinking /
saving / reopening / growing, and native-handle/document ownership. These are
31 QtTest results including setup/cleanup.

`parttest::testLatexAppearanceResizeHistory` exercises the actual application
annotation proxy for boxed/callout notes on normal/rotated pages, including
property updates, undo/redo and source-free growth after reopening.

The following `tmp/` paths are local development evidence, not files shipped in
source archives or release assets. Application-ownership follow-up evidence is
in `tmp/latex-app-appearance/`. Initial 2026.0.19.3 evidence lives in
`tmp/latex-frame-feedback/`, including the
new actual C++ fixture export, native comparisons, original/fixed frame tests
and read-only analysis of the reported PDF. Reference/default-policy evidence
is in `tmp/latex-head-diagnostic/REPORT.md`.

Earlier native-bounds evidence remains useful for the unchanged union helper:
`tmp/latex-ink-probe/NATIVE-REPORT.md`, its `anisotropy/REPORT.md`, and
`tmp/latex-linebox-probe/final-native/normalized/REPORT.md` (308 artifacts).
Those older display-height screenshots describe the **superseded forced-height
revision**, not the current default. Do not use them to assert current top space.

The initial 2026.0.19.3 release implemented the frame clip in Poppler. The
follow-up architecture correction moves that decision and the bounds helper
into Mengshee and restores the previous backend dependency. At the maintainer's
request, version 2026.0.19.3 is reissued with the corrected commit, tag and release
assets; the original commit remains in history. Existing notes need explicit
recompilation or an appearance rebuild; opening an older PDF alone does not
update its saved appearance.
