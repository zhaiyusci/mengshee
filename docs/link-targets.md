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

A link can also point at a destination in **another file**, written as a `/GoToR` action
(`/A << /S /GoToR /F (other.pdf) /D [2 /XYZ …] >>`).

The two forms differ in a way that matters:

- the file is named **as the producing tool wrote it**, usually a plain file name relative
  to the document, and it is what the format stores. Renaming the other file therefore
  breaks the link, and so does moving it away from the document;
- the destination is either a plain **page number** inside that other file, not a page
  reference (a reference would name a page of *this* document, which means nothing in the
  other one; it is written zero-based, as `/D [pageNumber - 1 /XYZ …]`), or a **name** of a
  destination in that file, written as `/D (destinationName)`.

Because the target belongs to another file, the page identity machinery described above
does not apply: `Okular::GotoAction::fileName()` carries the file name, and
`Okular::GotoAction::destinationName()` carries the name when there is one.
`Okular::Document::gotoTarget()` hands a stored viewport on unchanged instead of resolving
it here, and the Document does not check either form against its own pages or its own named
destinations, because neither is its to know. Following such a link when the file is not
where the name points reports the missing file rather than doing nothing; a link that names
a destination is resolved in the file that is opened, through the same
`metaData("NamedViewport", name)` lookup an internal named link uses.

**Cross-file links are not recommended.** The format stores only a file name, so renaming
the other file, moving it away, or sending this document without it all break the link,
silently until someone follows it. Merging the pages into one document and using internal
links (page identities or named destinations) is the robust option: nothing outside the
file can break it. The application supports cross-file links anyway, because they come from
other tools and because a document that arrived with one has to be repaired and sometimes
produced.

An existing cross-file link can be **re-pointed** from the link context menu ("Edit Link
Destination…"), which shows the stored file name and offers "Locate File…" to pick the
renamed file. The dialog also chooses whether the link addresses a **page** or a **named
destination** of that file, and the same dialog creates a new cross-file link. The kind
carries a small warning marker next to its label, whose tooltip states the warning above
(the option itself carries the same tooltip), so the choice says what it costs without a
paragraph of explanation beside it.

