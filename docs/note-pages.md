# Note pages

A note page is a blank PDF page appended to the document that holds a margin
note for a position on an existing page. It is an ordinary page plus an
ordinary internal link; no new PDF object type is introduced.

## Creating one

Choose **Add Note Page** on the annotation toolbar, next to **Add Template
Note**. It is a placement tool like the other annotation tools, so it stays
checked while armed and finishes after one click, on Esc, on right-click, or
when another tool is chosen.

Click the position the note belongs to. One undoable operation then:

1. appends a blank page at the **end** of the document, as large as the page
   that was clicked, headed with the note it holds;
2. adds a named destination on the new page;
3. writes a **Link** at the clicked position pointing to that destination, whose
   visible appearance is the label `see note 1.`;
4. opens the new page in an **auxiliary frame**, so the note can be written
   immediately without losing the reading position.

The button is on the always-visible annotation toolbar, so it is available in
every editing mode. It is enabled only when the document supports both blank
page insertion and PDF link editing, which in practice means a local PDF file.
The change reaches the file only when the document is saved.

## Numbering and wording

Destinations are named `mnote-<source page>-<sequence>`, for example
`mnote-3-1` for the first note attached to page 3. The sequence is the next
unused number for that source page, so names stay readable, stable, and usable
in **Cross-References** mode like any other named destination.

That same sequence is what the reader sees:

- the link label reads `see note 1.`;
- the note page starts with the heading `note 1`.

The wording, the font and the label's box size live together in
`core/numberedcalloutgeometry_p.h`, next to the Base14 advance widths they use,
so the box a caller reserves and the text the generator draws cannot drift
apart. Numbering is per source page, matching the destination name; two notes on
different pages can therefore both be "note 1".

The text is plain ASCII and is not translated: it is written into the document,
where it has to stay stable and where a viewer needs no font beyond the standard
14. A non-ASCII label is rejected instead of being written as unreadable bytes.

## The link label

A PDF link is invisible by default: it has no appearance stream and a
zero-width border. The note link therefore carries a self-contained `/AP`
normal appearance stream:

```pdf
<<
  /Type /Annot
  /Subtype /Link
  /Rect [100 500 141 512]
  /Border [0 0 0]
  /Dest (mnote-3-1)
  /AP << /N 24 0 R >>
>>
```

The link itself is created by the page editor exactly as any other internal
link. The label is added afterwards by the PDF generator
(`generators/poppler/generator_pdf.cpp`), which reaches Poppler Core directly,
like the other source-level adapters in that directory. The backend fork stays
unchanged: generic link mechanics live there, the label policy lives here.

The appearance is a Form XObject whose `/BBox` is the annotation rectangle
itself, so the text is drawn at its natural size and `Gfx::drawAnnot` maps it
without scaling. It draws a pale note sheet, a thin border and the label in
Helvetica-Bold from the standard 14, so the saved file embeds no font and has no
resource dependencies beyond that face.

The generator finds the new link by its normalized rectangle, using the same
matching and tolerance the editor uses for its other link operations, and calls
`Annot::setNewAppearance()` rather than editing the dictionary directly, because
that also refreshes Poppler's cached appearance so the label is drawn in the
live session instead of only after reopening the file.

That call registers the appearance stream itself, so the stream is handed over
unregistered (`GeneratedStream`); going through `XRef::addStreamObject()` first
would register the same bytes twice and leave an unreferenced copy behind on
every save, including every undo/redo cycle. One note link adds exactly one Form
XObject to the file.

The zero-width border is unchanged, so the label does not depend on a viewer
that renders link borders. Readers that do not draw link appearances still get a
working hotspot.

## The page heading

The heading is **page content**, not an annotation: it is written as part of the
same page-insertion edit, so it survives annotation removal, prints with the
document, and is visible in every viewer. It is one short line at the top-left
of the page, in Helvetica at 12pt from the standard 14, with WinAnsi text.

If writing the heading fails, the page insertion is rolled back as well, so
`Document::insertBlankPage()` never reports success for a half-made page.

## Coordinates and size

The click position comes from the viewport, which is in the page's displayed
orientation. Link geometry is written in the page's native PDF origin and
rotation, so the position is un-rotated first, using the same conversion the
named-destination tool applies. The label is centred on the click and kept
inside the page, so a click near an edge still produces a fully visible label.

Because the label is drawn in the page's own coordinate system, its long axis
follows the page on a page with a native rotation: the box is transposed before
normalizing, so the text is never clipped, and it then reads with the page, like
the page's other content.

The blank page is sized from `Document::pageSizeInPoints()`, which is the
clicked page's size in PDF points in canonical page axes. This matters because
`Page::width()` and `Page::height()` are device pixels at the document DPI, not
points; passing them straight to `Document::insertBlankPage()` would produce a
page that is `dpi / 72` times too large. For a page with a native rotation the
blank page keeps the unrotated page size, like the rest of the page editor. The
same conversion is used by **Insert Blank Page** and by the size fields of the
**Insert Page** dialog, which are labelled in points.

## Undo and redo

The blank page, its heading, its named destination, and the note link are one
undo step. Undo removes the link, then the destination, then the page, in that
order; redo re-attaches the page, recreates the destination and recreates the
label. Opening the auxiliary frame is a view action, not part of the edit, so
undo and redo never open or close tabs.

Undo and redo are not navigation: no frame is moved by a note page appearing or
disappearing, including the frame the reader is looking at while another frame
is active, and the back/forward history is untouched. A frame that was showing
the note page itself has no valid position left and follows the page the
page-level helper chooses.

The link label lives in the page image rather than in the annotation overlay, so
that image has to be replaced instead of repainted when the link goes away. The
refresh is therefore the last thing the undo step does: removing the page waits
for the running renders and discards the queued ones, so a refresh made earlier
in the step would be thrown away with them and the label would stay on screen
while its link is gone. A pixmap request that outlives the page it was queued for
is dropped instead of being dispatched, both when the page is retired and when
the request is taken off the queue.

## Tests

`parttest` covers the full operation: an unchanged page count before the
click, the appended page size in points, `mnote-<page>-<n>` naming and sequence,
the link's destination and rectangle, the label and heading literals in the
saved file, the heading actually being drawn on the new page, the auxiliary
frame opening on the new page, single-step undo and redo, and save/reopen
persistence. `testInsertBlankPageMatchesReferenceSize` covers the point-size
fix for the ordinary blank page insertion.
