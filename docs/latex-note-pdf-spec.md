# Okular LaTeX Note PDF Specification

This document defines the intended PDF representation for Okular LaTeX notes.
The goal is to keep the PDF annotation itself standard and self-contained,
while keeping Okular-specific editing state in one private JSON payload.

## Design Goals

- A LaTeX note is a normal PDF stamp annotation.
- Other PDF readers must be able to display it through the normal appearance
  stream.
- Okular-specific state should not be spread across many custom PDF dictionary
  keys.
- The user-facing variants should be expressed by one explicit `type`
  field, not by many boolean marker fields.

## Annotation Shape

All LaTeX notes are stored as `/Stamp` annotations:

```pdf
<<
  /Type /Annot
  /Subtype /Stamp
  /Rect [100 500 260 550]
  /Contents (editable LaTeX source)
  /LatexNoteData (...JSON string...)
  /AP << /N 21 0 R >>
>>
```

Required fields:

`/Subtype`
: Must be `/Stamp`.

`/Rect`
: The annotation rectangle. For callouts, this rectangle may cover the whole
visual appearance, including the note box and leader line.

`/Contents`
: The editable LaTeX source. This remains outside the JSON so ordinary PDF
tools can still expose the annotation text in a familiar place.

`/LatexNoteData`
: A PDF string containing UTF-8 JSON. This is the single Okular private field
that identifies and restores a LaTeX note.

`/AP /N`
: The normal appearance stream. It must be self-contained and display the
rendered note without requiring TeX or a temporary source PDF file.

Optional fields:

`/Name`
: May be present as a normal stamp name, but it is not part of the LaTeX note
identity. New files should not require `/Name /latex-notes`.

## Page and Annotation Orientation

Page rotation changes the PDF page `/Rotate`; it does not bake the page rotation
into page content or existing annotation appearances. Annotations follow the
page, including their frames and callout leaders. There is no follow-page toggle;
previous saved UI preferences do not affect this behavior. Undo/redo restores
the same page and annotation geometry.

New notes do not set `NoRotate`. An explicit page rotation normalizes existing
stamp and FreeText `NoRotate` flags while preserving their current appearance
before applying the page turn. This is not a file-open migration. Painted bounds
and hit regions must stay aligned regardless of imported flags.

The interaction model is writing on a sheet of paper: create content upright in
the current view, then keep it attached to the page. Turning the page turns all
existing content together; independently turning a note changes only that note's
orientation relative to the page. Moving, selecting and resizing use the same
content-to-page-to-view transform as painting.

A new note or image is authored upright relative to the current page view,
including its native PDF page rotation and temporary reader view rotation.
Creating it must not change the page orientation. Temporary reader rotation is
not written to the PDF page. Saving and reopening must preserve appearance,
position and editing direction without the original image or temporary LaTeX PDF.

### LaTeX stamp appearance and editable direction

A LaTeX note remains a `/Stamp`. Its self-contained `/AP /N` encodes its visible
orientation through standard appearance transformations. Do not depend on an
annotation-level `/Rotate` entry for stamps. Other readers need only the AP to
display the note; they need not understand LaTeX or its private metadata.

`/LatexNoteData.rotation` separately preserves the direction needed to regenerate
that AP. It is a required authored angle, normalized to 0, 90, 180 or 270 degrees.
In the current coordinate convention, let P be the native page's clockwise
`/Rotate` angle and A the note's clockwise direction in native displayed-page
axes (before temporary reader rotation). Then:

`rotation = (P - A) mod 360`.

The AP uses this angle as a rotation in PDF's y-up coordinate system; a positive
PDF-space angle is counterclockwise. Do not interpret it as an additional page
rotation. For example, an upright note created on a page with `/Rotate 90` has
`rotation: 90`; this compensates for the page's clockwise rotation. A later page
turn changes P and the displayed A together, leaving the authored rotation intact.

Changing text, font, colors or layout width must reuse this stored authored angle
when rebuilding the AP. Do not recompute it from the current viewing direction or
reset it to zero: editing a few characters must never turn the note. Independent
note rotation updates both the AP and private rotation consistently; page rotation
alone updates neither for a note already following the page. Undo/redo and
save/reopen preserve both representations.

`callout.pointsNorm` and `callout.boxRectPt` are expressed in that authored
orientation; loading and editing transform them to/from current native page axes.
Frames, leaders and numbered labels use the same transform as the note content.
Image stamps likewise store their visible orientation in AP; the current
implementation retains `/MengsheeStampRotation` as editing metadata when rebuilding
an image appearance, not as a field other readers must interpret.

This change does not migrate annotations produced by older versions. No legacy
rotation-flag conversion is performed when opening a document.

Regression coverage must include creation at 0/90/180/270 degrees, a subsequent
page rotation, and saving/reopening, for both LaTeX and image stamps. Left then
right (or right then left) must restore the previous orientation and appearance;
four consecutive quarter-turns must also restore them. Tests must also edit text
and rebuild AP after page rotation and independent note rotation, checking that
the baseline direction and local layout width remain unchanged. Relative rotation actions
must use the page's current native orientation, including after reopening.

## Content, Page, and View Coordinates

Content has its own layout axes: x follows the text baseline (or image rows),
y follows the lines of text (or image columns). Layout width controls wrapping;
layout height controls the available frame. Both are physical point distances.
Annotation placement maps these axes into page space; page display then maps
page space into the viewport. For column vectors:

`p_view = M_page_to_view * M_content_to_page * p_content`.

Pointer deltas are mapped back through the inverse linear transforms; translation
must not affect a delta. Never rotate normalized x/y distances before converting
them to physical page points, since page width and height may differ.

The current UI offers independent left/right quarter-turns around the annotation
frame center for LaTeX notes, callouts, image stamps and FreeText. The frame and
callout leader geometry use the same transform. Left then right is an identity.
The shared AnnotationFrame helper defines physical size and point/rectangle
transformations. The model appearanceRotation is clockwise in native displayed
page axes, before temporary reader rotation. It is not another PDF page Rotate.
At 90/270 degrees, local layout width maps to the vertical page extent,
and local frame height maps to the horizontal page extent. The format does
not prescribe resize handles or an automatic-height editing policy.

LaTeX APs and `/LatexNoteData.rotation` encode the content direction defined above.
Image stamps encode equivalent orientation in their AP and editing metadata.
Saving/reopening must preserve this without source images or temporary TeX PDFs.

### Standard FreeText annotations

FreeText uses standard Rect, DA and Contents entries, with AP optional under
this authoring policy. When present, AP determines the painted result, including
baseline orientation; DA describes default text format.
The annotation-level Rotate used by MuPDF/Adobe is an implementation extension,
not the standard page Rotate or a standardized FreeText field. Mengshee persists
Rotate for subsequent text layout. For ordinary FreeText with
no existing AP, MuPDF generates a transient display appearance in its local xref;
that generated AP must not be persisted on save, including after content edits,
resizing or rotation. This retains the former Poppler transient-appearance policy
while adding the de facto Rotate field supported by Acrobat, PDF.js and MuPDF.
Readers that ignore Rotate may display such annotations in a different direction.

Imported existing APs are preserved on open/save; explicit edits may regenerate
those APs through MuPDF. Template notes retain their separate self-contained AP
contract. LaTeX and image stamps always keep their APs.
At creation, compensate for the current page/view direction; later page turns do
not change the authored direction relative to the page. Editing text or resizing
preserves Rotate and uses MuPDF's text layout, not LaTeX.
Do not replace FreeText with a LaTeX stamp or require LaTeX metadata to display it.

References: [ISO 32000-2 corrections, 12.5.5 and 12.5.6.6](https://pdf-issues.pdfa.org/32000-2-2020/clause12.html),
[PDF Association FreeText dictionary model](https://github.com/pdf-association/arlington-pdf-model/blob/master/tsv/latest/AnnotFreeText.tsv).

## Identity Rule

A PDF annotation is an Okular LaTeX note when:

- `/Subtype` is `/Stamp`;
- `/LatexNoteData` is present;
- `/LatexNoteData` parses as JSON;
- the parsed JSON has `"version": 20260610`.

`/OkularLatex true` is not required in the new format.

## JSON Payload

`/LatexNoteData` stores one JSON object.

Minimal schema:

```json
{
  "version": 20260610,
  "rotation": 0,
  "type": "plain",
  "layout": {
    "widthPt": 0,
    "paddingPt": 3
  },
  "style": {
    "textColor": "#ff000000",
    "fontSizePt": 0
  }
}
```

Fields:

`version`
: Required integer. For this specification, the value is `20260610`.

`type`
: Required string. One of `"plain"`, `"boxed"`, `"callout"`, or `"numbered-callout"`.
Readers also accept the legacy `"ordered-callout"` spelling.

`id`
: Required for `"numbered-callout"`: a positive integer up to `2147483647`.
An editable sorting ID, independent of the displayed number. New annotations,
including pasted copies, receive the document-wide maximum ID plus one.
Legacy `"ordered-callout"` records lacking this field initialize ID from `order`.

`order`
: Required for numbered callouts: a positive integer up to `2147483647`.
This is the sequence counter, not necessarily the visible text. New annotations
receive the maximum counter plus one within the configured scope (document or page).
Explicit renumbering sorts by physical page then numeric ID and assigns counters
1..N, resetting on each page when configured. IDs stay unchanged; equal IDs retain
page annotation-list order. The whole format/renumber operation is undoable together.

`label`
: Optional string containing the actual visible badge text. Missing/empty labels
fall back to decimal `order` for old documents. Readers reject non-string or over
4096-character labels. New writers store the formatted label alongside `order`.

Document-wide numbering settings are separate from annotation JSON: the Catalog
key `/MengsheeNumberedCalloutNumbering` is a UTF-8 JSON string with `version: 1`,
`pattern` and boolean `restartPerPage`. The pattern must contain `{n}`, may also
contain `{page}` (1-based physical page), and is at most 256 characters with no
control characters, line breaks or other brace placeholders. Missing settings
mean `{n}` and `restartPerPage: false`. Settings persist even without annotations.

The complete display label is drawn in a badge above the body's top-left corner
inside the same AP: badge fill/stroke use the callout border color, and digits
use its fill color, with their respective alpha and annotation opacity.
Its extent is included in `/Rect` and `/BBox`; the body rectangle and LaTeX
layout do not change. ASCII labels use escaped Helvetica-Bold text with exact font
widths. Non-ASCII labels use shaped fallback-font glyph outlines in the AP, so saved
PDFs do not depend on those fonts at viewing time. Unsupported glyphs are rejected
before applying a numbering format rather than silently writing replacement boxes.
See [Numbered Callouts](ordered-callouts.md).

`layout.widthPt`
: Optional number in PDF points. `0` or absence means natural width. A positive
value is the TeX paragraph width used for reflow.
The unit is a PDF point (1/72 inch), not a TeX point (1/72.27 inch).
Renderer adapters must convert units and disable the profile's preview border;
preview margins must not be added to the requested paragraph width or padding.

`layout.paddingPt`
: Optional non-negative number in PDF points. Defaults to `3`. It is applied
independently on every side between the annotation frame and LaTeX content.
Positioning and reflow use the logical typesetting box, not glyph outlines or
an expanded paint bounding box. Expanding the source PDF to retain overhangs
must preserve that logical origin and size. Mengshee's source appearance Form
may retain this box in its private `MengsheeLayoutBox` entry; it does not change
the standard `BBox` used for painting and clipping.

`style.textColor`
: Optional CSS-style ARGB hex string, `#aarrggbb`. Defaults to opaque black.

`style.fontSizePt`
: Optional number in PDF points in the range `1` through `200`. `0` or absence
leaves the base font size to the LaTeX source and StemTeX profile. A positive
value is passed unchanged to StemTeX's per-request font-size API; StemTeX uses
`\fontsize` with a baseline skip of `1.2 * fontSizePt` before evaluating
`/Contents`. Font-size commands inside `/Contents` may still override it
locally.

`style.fontSizePt` is a TeX layout input, not an appearance zoom. Mengshee must
render glyphs at the requested size and must not scale the completed `/AP`.

`style.fillColor`
: Optional ARGB hex string. Used by boxed and callout notes. Transparent means
no fill.

`style.borderColor`
: Optional ARGB hex string. Used by boxed and callout notes. Transparent means
no stroke.

`style.borderWidthPt`
: Optional non-negative number in PDF points. Defaults to `0` for plain notes
and `1` for boxed/callout notes.

`callout`
: Required when `type` is `"callout"`, `"numbered-callout"`, or legacy `"ordered-callout"`. Stores the editable callout
geometry.

Unknown JSON fields must be preserved when possible. Readers may ignore fields
they do not understand.

## Variant: Plain Note

A plain note is unboxed rendered LaTeX content.

```pdf
<<
  /Type /Annot
  /Subtype /Stamp
  /Rect [100 500 220 530]
  /Contents (E = mc^2)
  /LatexNoteData ({"version":20260610,"rotation":0,"type":"plain","layout":{"widthPt":0,"paddingPt":3},"style":{"textColor":"#ff000000","fontSizePt":0}})
  /AP << /N 21 0 R >>
>>
```

Plain-note JSON:

```json
{
  "version": 20260610,
  "rotation": 0,
  "type": "plain",
  "layout": {
    "widthPt": 0,
    "paddingPt": 3
  },
  "style": {
    "textColor": "#ff000000",
    "fontSizePt": 0
  }
}
```

## Variant: Boxed Note

A boxed note corresponds to the current LaTeX Inline Note. The PDF annotation
is still a stamp; the box is part of the appearance stream and its editing
style is stored in JSON.

```pdf
<<
  /Type /Annot
  /Subtype /Stamp
  /Rect [100 500 260 550]
  /Contents (\int_a^b f(x)\,dx)
  /LatexNoteData ({"version":20260610,"rotation":0,"type":"boxed","layout":{"widthPt":140,"paddingPt":3},"style":{"textColor":"#ff000000","fontSizePt":0,"fillColor":"#ffffff00","borderColor":"#ff000000","borderWidthPt":1}})
  /AP << /N 31 0 R >>
>>
```

Boxed-note JSON:

```json
{
  "version": 20260610,
  "rotation": 0,
  "type": "boxed",
  "layout": {
    "widthPt": 140,
    "paddingPt": 3
  },
  "style": {
    "textColor": "#ff000000",
    "fontSizePt": 0,
    "fillColor": "#ffffff00",
    "borderColor": "#ff000000",
    "borderWidthPt": 1
  }
}
```

## Variant: Callout

A callout is a boxed LaTeX note plus a three-point leader line. It is one stamp
annotation, not a FreeText callout and not multiple annotations.

The appearance stream must draw:

- the rendered LaTeX content;
- the optional fill and border box;
- the callout leader line and arrow.

Callout JSON:

```json
{
  "version": 20260610,
  "rotation": 0,
  "type": "callout",
  "layout": {
    "widthPt": 100,
    "paddingPt": 3
  },
  "style": {
    "textColor": "#ff000000",
    "fontSizePt": 0,
    "fillColor": "#ffffffff",
    "borderColor": "#ff000000",
    "borderWidthPt": 1
  },
  "callout": {
    "boxRectPt": [120, 500, 260, 545],
    "pointsNorm": [
      [0.12, 0.42],
      [0.18, 0.48],
      [0.22, 0.48]
    ]
  }
}
```

Callout fields:

`callout.boxRectPt`
: The editable text-box rectangle in PDF page coordinates:
`[x1, y1, x2, y2]`.

`callout.pointsNorm`
: Three points in Okular normalized page coordinates. Point 0 is the arrow tip,
point 1 is the elbow, and point 2 is the connection point on the box.

`callout.pointsPt`
: Optional fallback array of three points in PDF page coordinates. Writers may
include it if useful, but `pointsNorm` is the preferred editing geometry.

## Appearance Stream Contract

The saved PDF must not depend on a temporary file path such as
`latex-notes/*.pdf`. Okular may use such files while rendering, but saving must
embed a self-contained normal appearance stream.

The normal appearance may be an outer Form XObject that draws Okular note
geometry and places an inner Form XObject containing the rendered LaTeX page.
That is an implementation detail; readers should treat `/AP /N` as the source
of visual truth.

Poppler should not need to know the `/LatexNoteData` schema. Okular owns JSON
parsing and passes only generic appearance geometry to the PDF backend when
building `/AP`.

## Layout And Frame Geometry

The paragraph layout and annotation frame are distinct. `layout.widthPt`
specifies the TeX paragraph width; padding and border width are physical
PDF-point measurements. The annotation frame describes the visible area,
with annotation rotation mapping local layout axes into page coordinates.
Content outside the inner frame may be clipped when composing the final AP.
The final AP remains the authoritative visible appearance.

This format does not require automatic height or fixed height. Editors may
fit the frame to rendered content or allow an independently specified frame
height. No automatic-height mode field is defined by this specification.
Mengshee's current editing policy is documented in
[LaTeX note rendering guards](latex-note-render-guards.md#automatic-height-editing-policy).

All dimensions ending in `Pt` are PDF points. Values ending in `Norm` are
normalized page coordinates.

## Rendering Triggers

Mengshee should re-render a LaTeX note only when the rendered LaTeX appearance
can change:

- when creating a new LaTeX note with non-empty source;
- after editing the LaTeX source;
- after changing render-affecting style such as text color, base font size,
  boxed/plain state, padding, or paragraph layout width;
- after resizing along the local layout-width axis changes `layout.widthPt`;
- when a resize operation needs the appearance but the runtime appearance PDF
  path or page size is unavailable.

Mengshee should not invoke TeX or StemTeX for operations that only move or reuse
the existing appearance:

- moving the annotation on the page;
- reordering, inserting, or deleting PDF pages;
- changing ordinary metadata such as author or modification date;
- changing annotation opacity or other PDF annotation state that does not alter
  the rendered LaTeX page;
- opening a document that already has a valid self-contained normal appearance;
- changing only the clipping frame height while reusing the rendered content.

Changing StemTeX configuration, such as the selected profile or TeX tree, should
restart the renderer for future work. It should not automatically re-render all
existing notes in the document.
