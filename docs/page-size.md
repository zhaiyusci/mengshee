# Changing a page's size

Page Editing mode can change the visible size of any page, including the pages
**Add Note Page** appends. Right-click a page (or use the page-edit toolbar) and
choose **Page Size…**, then enter a width and a height in PDF points.

The values are the page's **unrotated** size, like the blank-page size in the page
insertion dialog, so they keep their meaning when the page is rotated.

## What the user sees grows

A PDF page carries several boxes, and what a reader displays is the **crop box**
when the page has one and the **media box** otherwise:

- `/MediaBox` — the extent of the physical page, and the coordinate space the page
  content is drawn in.
- `/CropBox` — the region a viewer or printer uses; when it is absent, it is the
  media box.

Changing only the media box would therefore be invisible on a page that carries a
crop box, and changing only the crop box would be invalid when the crop box leaves
the media box. The operation writes both boxes onto the page itself:

- the crop box keeps its lower-left corner and takes the requested width and
  height,
- the media box keeps its own extent and grows when the new crop box would leave
  it, because the format requires the crop box to stay inside the media box,
- both are written as the page's own entries, which override a box the page tree
  would otherwise contribute to an inherited attribute.

Writing both also keeps the operation predictable: a page whose visible area is
its media box, a page with a smaller crop box, and a page that inherits either box
all behave the same way.

## Existing content does not move

The lower-left corner of the visible box is kept, so page coordinates are
unchanged: content stays where it is and the new room appears to the right of and
above it, which is where a reader goes looking for it.

A **smaller** size is allowed and only hides what falls outside the visible box.
Nothing is deleted — the content stream, the annotations, and the rest of the file
are untouched — so enlarging the page again brings the hidden part back.

`/Rotate` and `/UserUnit` are not touched. Rotated content, annotation rectangles,
link targets, named destinations, note-page headings, reading views, and
numbering metadata all keep working because only the two boxes change.

## Undo, save, and limits

- One undo step per change. Undo and redo rebuild the page model, so the view and
  the thumbnails follow immediately.
- The change reaches the file when the document is saved, like the other page
  edits.
- Sizes are limited to 1–20000 pt, the same range as blank-page insertion.
- Only local PDF files can be edited this way, like the other page operations.

## Tests

`parttest` covers the operation in `testChangePageSize`: growing a page leaves the
neighbouring pages and the rendered content exactly as they were (the old
rendering is the bottom-left part of the new one), undo and redo restore both
sizes, saving and reopening keeps the new size, and a page whose visible area is a
crop box inside a larger media box grows that crop box while its media box still
contains it.
