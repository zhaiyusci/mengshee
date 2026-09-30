# Unified editing modes

A mode selector is available at the far right of the top toolbar and in the View menu. It replaces the former Advanced Mode and separate Edit View toggles.

| Mode | Tools and editing indicators |
| --- | --- |
| Read / Annotate | Default mode for reading, selection, and annotations |
| Cross-References | Named destinations, internal links, and table-of-contents editing, including adding entries |
| OCR | Text recognition and editing of existing OCR text layers |
| Page Editing | Page insertion, duplication, deletion, rotation, templates, and page operations in the thumbnail panel |
| Reading Views | Icon-only tools for drawing reading regions, generating regions automatically, ordering them by clicking, and applying them to other pages; regions can be moved and resized |
| Proofread | Numbered Callout, numbering formats (document-wide, per-page, page-and-counter, or custom), document-wide reordering by page and internal ID, and CSV proofreading checklist export |

**Reading Views** are reading regions within a page, not the display settings in the traditional **View** menu. Selecting this mode places an **icon-only** group for Draw, Order, Generate, and Apply on the right of the second toolbar row. The Apply icon depicts one region being copied to multiple regions. Annotation tools remain on the left. Tool names and descriptions appear in tooltips, so there is no need to look for these tools in the View menu.

Modes organize tools and editing indicators; they do not change the file format or grant permissions:

- Standard annotation tools remain available in every mode, without additional annotation restrictions.
- Selecting a mode does not automatically activate a mouse tool for drawing regions, placing destinations, or editing OCR text. Activate the corresponding tool explicitly.
- Leaving Cross-References, OCR, or Reading Views cancels that mode's unfinished gestures and hides its editing indicators without deleting committed data. Existing edits remain available for undo and redo.
- Proofread groups the commands for creating numbered annotations and reordering them throughout the document. Existing numbered annotations remain editable in other modes, and switching modes does not automatically select an annotation tool.
- Document modification permissions and backend capabilities still apply; selecting a mode does not bypass them.
- **Read by Views** remains a separate display toggle in the top toolbar. It is available in every mode and is not changed by switching modes.
- The main and auxiliary panes of a document share its editing mode. Newly opened auxiliary panes inherit the current mode, while each pane independently chooses between original-page display and Read by Views. Different document tabs retain their own modes.

## Implementation boundaries

`EditingMode` in `part/editingmode.h` defines six states. The `Part` action `editing_mode_selector` (`KSelectAction`) handles selection and synchronization. PageView's Cross-References, OCR, and Reading Views states each control their own indicators and interactions rather than enabling one another through a generic advanced mode.

The legacy `view_toggle_named_destinations` action and advanced API remain hidden Cross-References compatibility aliases, not alternative mode entry points. The `advancedToolBar` object name is retained for layout compatibility, but its display title is Mode Tools and it shows only the current tool group.

Buttons and the mode selector share the toolbar's natural height, adapting to fonts, icon sizes, styles, and DPI without replacing the original button labels or icons.
