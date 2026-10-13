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
