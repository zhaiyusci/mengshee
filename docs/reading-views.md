# Reading and editing Views

A View defines only a **rectangle on a page** and a **positive integer label**.
Its saved definition does not prescribe zoom, scrolling or column detection.
An optional reading mode presents these definitions as pages.
Numbers need not be consecutive or unique. Each View has a separate page-local
UUID so changing a number never changes its identity.

## Editing

Choose **Reading Views** from the **Mode** selector at the far right of the
main toolbar. These Views are editable reading regions, not the general **View**
display-settings menu. The second-row mode toolbar is aligned to the **right**,
with **icon-only** Draw, Order, Generate, and Apply buttons. Apply's icon shows
one region becoming several regions. Annotation tools stay on the left; button
names and explanations appear on hover, not as toolbar text. The **Read by Views** display switch remains independent. Entering this mode shows existing ranges
without activating drawing or changing the normal mouse tool.
Choose **Draw** from the mode toolbar or page context menu to start
drawing. Its button stays checked while drawing is armed. Click the button again
or press Escape to finish; choosing another mouse tool also exits drawing.
**Draw** and **Order** are mutually exclusive tools on the same
Mode Tools toolbar. The reader does not need to know whether the document has columns.

- Drawing is continuous: after finishing one rectangle, draw the next without
  reactivating the tool. Its number defaults to the current page's maximum plus
  one (starting at 1); no number dialog interrupts drawing.
- Existing labels/borders remain draggable while drawing is armed. Selected
  Views have resize handles. Right-click a label/border to renumber or delete.
- Escape cancels an unfinished gesture and ends continuous drawing. **Draw**
  resumes drawing; the editor's existing rectangles remain available to adjust.
- Choose **Reading and Annotations** or another task mode to hide the View
  editor, without deleting definitions. See [editing modes](editing-modes.md).
- Edits participate in undo/redo. Save the PDF to keep them.

### Number Views by clicking

Choose **Order** on the Reading Views mode toolbar. The button stays checked and
the pointer changes to a hand. Click View interiors, labels or borders in the
order you want to read them. In Read-by-Views display, click the displayed View.

- The clicked sequence takes numbers 1, 2, 3… **on each physical page**.
- Unclicked Views follow that sequence in their previous relative order; numbers
  remain unique and consecutive. Repeating an already-counted View does nothing.
- Rectangles and identities are unchanged. Each effective click is one undo step.
- Click the tool again, press Escape, right-click, or choose another tool/mode to
  finish. Finishing keeps the numbering already applied. Undo/redo and changing
  active frames finish the tool too; entering it again starts a fresh sequence.
- The same **Click to Number** tool is available for Numbered Callouts in
  **Proofread** mode; it follows their document/per-page numbering setting.

### Generate editable Views automatically

In **Reading Views** mode, click the visible **Generate** toolbar
button (not the general View menu). Select the current physical PDF page, all pages, or a
page range. **Skip pages that already have Views** is enabled by default to
protect manual work. If you disable it, the confirmation reports how many
pages will have their Views replaced.

Detection runs locally on a temporary snapshot of the current document,
including unsaved page edits. It does not upload the document, recognize or
replace text, change page content, or change annotation permissions. Cancel
before applying to leave all Views unchanged. Pages with no detected regions
are also left unchanged. Confirm **Apply**, inspect the rectangles, and adjust
ranges or numbers as usual. The whole batch is one undo/redo operation; save the
PDF to persist it. **Read by Views** remains an independent display switch.

The detector uses Tesseract layout analysis without a language model, followed
by merging based on original block widths/spans and iterator order. Wide
regions followed by narrower columns are separated rather than swallowed by a
previously merged full-width rectangle. Tesseract support must be available in
the build; English OCR language data is not required for this action.

These are editable suggestions, not guaranteed document structure. Complex
formula pages, uncaptioned figures and unusual reading orders may need manual
correction. This implementation does not include PDFfigures, AI, or deskewing.
Normal pages are analysed at 300 DPI; oversized pages are downscaled within a
bounded image allocation or rejected if useful analysis would be unsafe.

### Apply one page's layout to the document

Draw and adjust the ranges on one page, then click **Apply**
on the Reading Views mode toolbar. Confirm before replacing other pages' View
definitions. The source page stays unchanged; each other page receives its own
editable copies, using the same numbers and relative page rectangles. Pages of
different sizes use the same proportional positions.

This is copying a layout, not detecting columns or recognizing page content.
You can adjust individual pages afterward. One undo restores all replaced
layouts, and redo reapplies them. Unsupported metadata is checked before any
page is changed. No changes reach the PDF file until you save.

Overlapping regions are allowed. Editing definitions does not itself turn on
reading mode.

## Reading

Toggle **Read by Views** in the View menu or the main toolbar. Each View becomes
one displayed page, using the existing zoom, single/facing-page and continuous
reading controls. No automatic fit or forced scrolling policy is introduced.
Turning the switch off restores the original PDF pages.

- Original PDF page order is retained. Within a page, Views are displayed by
  ascending number; equal numbers retain their stored order and remain distinct.
- A page without usable View definitions is displayed whole, not omitted.
- Previous/next, first/last, page-number input and the progress indicator address
  the displayed pages. The original page/View label is shown separately.
- Main and auxiliary frames can use different modes independently.
- Annotations remain editable while reading Views. Creating, modifying or
  pasting an annotation uses the original PDF page and coordinates, so it also
  appears in other Views of that region and in normal page mode. Annotation
  saving and undo/redo continue to work normally.
- Other PDF operations retain their normal permissions and semantics. A View
  is a display range, not a read-only object or a separate kind of PDF page.
- Text selection is limited to displayed regions and copying follows their
  displayed order. PDF links and searches still use real PDF page coordinates.
  The switch remains under the user's control; navigation does not rewrite the
  defined ranges or automatically disable the mode.

Switching this screen-only projection on or off does not split the PDF, rewrite
its page count or change its content, definitions, save state, printing or PDF
export. Edits made while using a View are ordinary edits to the original PDF.
Each visible View requests the original PDF at its current display scale,
including the screen's pixel density. Views at different scales use separate
rendering caches; high magnification renders the visible region rather than
permanently enlarging a low-resolution whole-page image. Temporary previews
while rendering are not treated as the final-resolution result.

## Persistence and coordinates

Mengshee owns the model, editing policy and PDF adapter. No Poppler fork changes
are needed. Each PDF Page dictionary can contain an application-private value:

```
/MengsheeViews <<
  /Version 1
  /PageId (page-uuid)
  /Items [
    << /Id (view-uuid) /Number 7 /Rect [x1 y1 x2 y2] >>
  ]
>>
```

`Rect` uses PDF user coordinates, including the actual CropBox origin. Public
model rectangles use normalized top-left coordinates in the page's native
PDF rotation. Temporary viewer rotation is applied only by the UI and reversed
before an edit is committed. All four rectangle corners are transformed.

Views are not PDF annotations, links, named destinations, page content, crop
operations or appearance streams. Other PDF viewers can ignore the private
metadata. Rendering, printing and PDF-derived image export are unchanged.

The data lives with the page, not at a document-global page-number index.
Moving, detaching and restoring pages preserves it. Copies/imports receive a
new page identity while keeping their View labels and page-local definitions.
Undo resolves a persistent page identity instead of blindly editing an old
page number. Writing uses fresh dictionaries to avoid mutating shared copies.

Reading metadata never creates or edits it. Unsupported versions and malformed
records are rejected rather than silently discarded on the next edit.

## Tests

`readingviewlayouttest` freezes the original-block-width merger against the
validated full-width-table/two-column case, earlier normal and tilted layouts,
relative scaling, padding and invalid inputs. `parttest` additionally covers
automatic detection on native rotations and offset CropBoxes, blank pages,
cancellation, batch undo/redo, save/reopen and generation-dialog defaults.

`parttest` covers native rotations with an offset CropBox/UserUnit, unchanged
PDF rendering, save/reopen, duplicate labels, invalid data, unknown schema,
undo/redo, page identity across topology changes, and mouse creation/editing/
cancellation. Reading-mode tests cover duplicate/overlapping Views, source versus
displayed page identity, navigation and history, rotation, continuous/facing
rendering at different scales on the same source page, independent frames,
external destinations, crop-limited text selection, and real annotation drawing,
copy/paste, undo/redo and save/reopen through rotated View ranges. These exercise
the application and its PDF backend together.
