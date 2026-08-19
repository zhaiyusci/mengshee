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
- The three user-facing variants should be expressed by one explicit `type`
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
: Required string. One of `"plain"`, `"boxed"`, or `"callout"`.

`layout.widthPt`
: Optional number in PDF points. `0` or absence means natural width. A positive
value is the TeX paragraph width used for reflow.

`layout.paddingPt`
: Optional non-negative number in PDF points. Defaults to `3`. It is applied
independently on every side between the annotation frame and LaTeX content.

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
: Required only when `type` is `"callout"`. Stores the editable callout
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
  /LatexNoteData ({"version":20260610,"type":"plain","layout":{"widthPt":0,"paddingPt":3},"style":{"textColor":"#ff000000","fontSizePt":0}})
  /AP << /N 21 0 R >>
>>
```

Plain-note JSON:

```json
{
  "version": 20260610,
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
  /LatexNoteData ({"version":20260610,"type":"boxed","layout":{"widthPt":140,"paddingPt":3},"style":{"textColor":"#ff000000","fontSizePt":0,"fillColor":"#ffffff00","borderColor":"#ff000000","borderWidthPt":1}})
  /AP << /N 31 0 R >>
>>
```

Boxed-note JSON:

```json
{
  "version": 20260610,
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

## Layout And Resize Rules

The annotation rectangle is the authoritative outer frame. A render result
must never replace or enlarge a rectangle chosen by the user.

- dragging any resize handle changes the outer frame exactly as indicated by
  the pointer, without preserving the rendered content's aspect ratio;
- `layout.widthPt` is the last TeX paragraph width used for reflow and is
  derived from the frame's inner width and padding;
- `layout.paddingPt` and `style.borderWidthPt` remain physical PDF-point
  measurements;
- content outside the inner frame is clipped rather than expanding the
  annotation rectangle;
- horizontal resizing may re-render for paragraph reflow, but the asynchronous
  result must keep the exact frame selected by the user;
- vertical-only resizing changes only the clipping frame and does not invoke
  TeX or StemTeX.

All dimensions ending in `Pt` are PDF points. Values ending in `Norm` are
normalized page coordinates.

## Rendering Triggers

Mengshee should re-render a LaTeX note only when the rendered LaTeX appearance
can change:

- when creating a new LaTeX note with non-empty source;
- after editing the LaTeX source;
- after changing render-affecting style such as text color, base font size,
  boxed/plain state, padding, or paragraph layout width;
- after horizontal resize when the resize changes `layout.widthPt`;
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
- vertical-only resize that only changes the clipping frame.

Changing StemTeX configuration, such as the selected profile or TeX tree, should
restart the renderer for future work. It should not automatically re-render all
existing notes in the document.
