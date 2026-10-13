# Changing a page's size

Page Editing mode can change the visible size of any page, including the pages
**Add Note Page** appends.

## Dragging a border

The page under the pointer shows handles on its four borders and its four corners.
Drag one, and a dashed preview follows the pointer; release, and the page takes
that shape.

- A handle moves the borders it belongs to and leaves the opposite ones where they
  are: the page's content keeps its coordinates, so it does not move relative to
  the borders that stay. Dragging the left border, for instance, moves that border
  and pins the right one, whether that means the page grows or shrinks.
- Dragging inwards makes the page smaller, which only hides what falls outside the
  visible box; enlarging it again brings that back.
- One drag is one undo step, and the new shape reaches the file when the document
  is saved.
- The handles belong to Page Editing mode and follow the page you point at, so
  reading, annotating and the other editing modes are unaffected.

**Page Size…** in the page context menu — and the same button in the page-edit
toolbar — performs the same operation with exact numbers instead of a drag. It
keeps the lower-left corner, and its values are the page's **unrotated** width and
height in PDF points, like the blank-page size in the page insertion dialog, so
they keep their meaning when the page is rotated.

Sizes stay between 1 and 20000 points per side. A drag that would leave that range
stops at the limit instead of being rejected, and a rotation is taken into account:
the borders you drag are the ones you see, whichever way the page is turned.

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

- the crop box moves the edges the gesture moved, by the amounts the gesture asked
  for, and leaves the other edges alone,
- the media box keeps its own extent and grows when the new crop box would leave
  it, because the format requires the crop box to stay inside the media box,
- both are written as the page's own entries, which override a box the page tree
  would otherwise contribute to an inherited attribute.

Writing both also keeps the operation predictable: a page whose visible area is
its media box, a page with a smaller crop box, and a page that inherits either box
all behave the same way.

## Existing content does not move

Only the box that decides what is visible changes, so page coordinates are
unchanged and content keeps its position: it does not move relative to the borders
that stay put, so a page can be grown in any direction.

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
