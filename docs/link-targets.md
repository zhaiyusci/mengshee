# Link targets are resolved when they are needed

A link in a PDF points at a destination in one of two ways:

- it **names** a destination, which the file's name tree resolves; or
- it carries an **explicit** destination, which references the target **page object**
  (`/D [pageRef /XYZ …]`) together with a position and zoom.

The first form is independent of page numbers, so inserting, removing, moving, rotating
or resizing pages cannot change what it means. The second form does not carry a page
number either: it carries a page *object*, which keeps naming the same page when other
pages change around it. Renaming a destination only rewrites the places that spell the
old name.

**A page number is therefore never stored as the target of a link.** The document model
holds either the destination name or an opaque **page identity** (the page object
reference), plus the position, and resolves the page number when the target is actually
needed:

- `Okular::GotoAction` keeps `destinationName()`, or `pageToken()` for an explicit page;
- `Okular::Document::gotoTarget()` is the single place that turns either of those into a
  `DocumentViewport`, asking the generator which page an identity now refers to
  (`Generator::pageNumberForPageToken()`), and resolving a name through the document's
  named viewports;
- navigation, the auxiliary-pane link handling and the tests use that one entry point.

Consequences:

- editing pages never invalidates a link, so nothing has to be wiped or rebuilt: the
  page's link objects, their rectangles and any selection stay exactly as they were;
- a link whose target page **was removed** resolves to nothing rather than to a wrong
  page, which is visible instead of silent;
- renaming a named destination rebuilds the link objects of the affected pages, because
  the actions spell the name; their rectangles come from the same sources, so they do not
  move;
- the outline, the named-destination metadata and the script API already resolve on
  demand and are untouched by page edits.

## Cross-file links

A link can also point at a page in **another file**, written as a `/GoToR` action
(`/A << /S /GoToR /F (other.pdf) /D [2 /XYZ …] >>`). Such a link comes from the tool that
produced the document — LaTeX, most often — and not from this application, which only
creates links inside the file it has open.

The two forms differ in a way that matters:

- the file is named **as the producing tool wrote it**, usually a plain file name relative
  to the document, and it is what the format stores. Renaming the other file therefore
  breaks the link, and so does moving it away from the document;
- the page is a plain **page number** inside that other file, not a page reference: a
  reference would name a page of *this* document, which means nothing in the other one. It
  is written zero-based, as `/D [pageNumber - 1 /XYZ …]`.

Because the target belongs to another file, the page identity machinery described above
does not apply: `Okular::GotoAction::fileName()` carries the file name, and
`Okular::Document::gotoTarget()` hands its stored viewport on unchanged instead of
resolving it here. Following such a link when the file is not where the name points
reports the missing file rather than doing nothing.

An existing cross-file link can be **re-pointed** from the link context menu ("Edit Link
Destination…"), which shows the stored file name and the page and offers "Locate File…"
to pick the renamed file. Nothing in the interface creates a new one.
