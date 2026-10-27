# LaTeX note rendering guards

## Source and block geometry

Mengshee passes the original note source unchanged to StemTeX 0.15.2's
`stemtex_renderer_render_block`. The SDK owns uniform block layout,
fixed-width layout, natural display spacing and minimum text-line metrics.
Do not prepend a host colour wrapper, width anchor, strut or paragraph hook:
that can alter the SDK's paragraph and box layout or add empty vertical space.
Do not trim authored display skips or crop to glyph outlines.

The synchronous SDK call runs in the background note job and is serialized per
session. Width is converted from PDF points to TeX points. Annotation padding
remains separate from the content's logical bounds.

Mengshee sets `\abovedisplayskip`, `\belowdisplayskip`,
`\abovedisplayshortskip`, and `\belowdisplayshortskip` to `0pt` for each
request, after font selection and before the source. This application default
removes display-math separation; profile borders and annotation padding remain
independent. Explicit settings in the source can override the default.
Each generated setting uses `\setlength{...}{0pt}%`; the trailing percent
prevents its line ending from introducing a space into TeX input.

## Request settings outside the source

`part/latexworker.tex` uses the SDK's supported `worker_template_utf8` option.
It follows the StemTeX 0.15.2 worker protocol; review it when upgrading the runtime.
The profile preview border is retained exactly as in direct SDK output. Mengshee's annotation padding is an additional, separately configured frame inset.
Before the unchanged source is typeset, the worker reads a session-local settings
file. Mengshee atomically writes that file under the same mutex as rendering.
Each session has its own temporary directory, retained until its workers stop.

`part/latexsource.cpp` prepares a request-local default colour without emitting
layout material. The worker retains the SDK's original `normalcolor` call at
its original position; both color and xcolor default/current-colour state are
kept consistent. For profiles without color support, shipout hooks supply the
colour without changing layout. No host `everypar` or `everydisplay` hook is
needed. Explicit source colours and grouped colour changes remain effective.

The native regression in `autotests/stemtexblocktest.py` renders fixtures exported
by `latexsourcepreparationtest`, using the same worker and request settings as
the application. It checks leading displays, display-plus-prose, terminal
whitespace, ordinary line metrics, current/explicit colours and Chinese text.

## Preserve the SDK artifact

The LaTeX rendering path consumes the SDK PDF without rewriting its page boxes.
It no longer calls `LatexPdfBounds::expandInPlace`. Logical bounds, profile
preview border and content positions come from StemTeX, not glyph-bound cropping
or host geometry correction. The native regression compares page dimensions and
text origins against a second renderer using the SDK's default worker with the same four
zero display skips supplied in its source.


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

## StemTeX block integration check

Export the unchanged source and request settings with `MENGSHEE_LATEX_FIXTURES=<file.json>`
and run `latexsourcepreparationtest exportFixtures`. Then run
`autotests/stemtexblocktest.py --runtime <StemTeX-root> --profile <profile-dir>
--texmf <TeX-Live-root> --fixtures <file.json> --output <temporary-directory>`.
The opt-in test requires Python with pypdf and a Chinese-capable profile with
xcolor. It checks native PDF output against the default SDK worker (page bounds and text origins), simple-line metrics,
tall formulas, multiple paragraphs, Chinese, and inherited/overridden colours.

## Automatic-height editing policy

Mengshee currently always fits LaTeX note height to the rendered logical height
plus twice the annotation padding. This is an application editing policy, not
an obligation of the PDF note format; no fixed/automatic-height mode is stored.

New notes start with `\LaTeX` and render asynchronously without loading a
prebuilt appearance. Creation, content edits and width reflow fit the local
frame height while preserving the local top-left origin and width. The pending
render uses an empty source appearance so backend registration succeeds without
synthesizing a stock Draft stamp.

Only the two local-width resize handles are exposed. At 90/270 degrees these
appear on the top and bottom edges; the automatically fitted height changes the
horizontal page extent. Callout tips stay fixed while frame attachments follow
the updated frame. Padding and border width remain physical PDF-point values.
