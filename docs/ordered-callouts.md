# Numbered Callout

Previously named Ordered Callout, this tool is based on LaTeX Callout. Select **Proofread** using the mode selector on the right of the main toolbar. The mode toolbar provides Numbered Callout, numbering format, document-wide renumbering, and CSV export. Body editing, LaTeX compilation, and leader-line interactions remain consistent across all three callout types. Legacy tool identifiers and files remain compatible. Existing numbered annotations can still be edited in other modes. The internal ID appears directly at the top of the annotation pop-up, without opening the properties dialog.

## Internal IDs and displayed numbers

- Each numbered callout has a separate positive-integer **internal ID** and **sequence counter**, both in the range 1–2147483647. The numbering format generates the displayed label.
- When creating or pasting a callout, its ID is one greater than the largest existing ID in the document. Its sequence counter is one greater than the largest counter in the current numbering scope: the document or the current page. Undo and redo preserve the assigned values.
- Edit the internal ID directly at the top of the annotation pop-up. Pressing Enter, moving focus away, or closing the pop-up commits a single undoable change. The adjacent current display label is read-only. The properties dialog retains a compatibility entry point. Changing an ID does not automatically reorder annotations or change their body text.
- In Proofread mode, use the mode toolbar's document-wide callout renumbering command, also available under Tools → Mode Tools. It sorts by **ascending physical page number, then ascending internal ID within each page**, and recalculates counters and labels using the current numbering format. Internal IDs do not change.
- Annotations with identical IDs on the same page retain their relative order in the PDF annotation list. Identical IDs on different pages do not conflict: page order always takes precedence.
- Ordinary callouts and other annotation types are excluded. Read by Views does not change the physical page numbers used for ordering or cause an annotation to be numbered more than once.
- Reordering is a single undoable operation. If any annotation that needs renumbering is locked or otherwise uneditable, the entire operation makes no changes.
- Deleting a callout does not automatically reorder the others. Run reordering manually to remove numbering gaps. After deleting the highest ID or sequence number, a new callout may reuse that value.
- Legacy Ordered Callouts without an ID use their stored display number as the initial ID. Loading does not change their displayed numbering.

## Numbering in click order

In **Proofread** mode, enable **Click to Number**. The button stays checked and the pointer becomes a hand. Click annotation body boxes or their external number labels in the desired order. These clicks reorder annotations rather than dragging them or opening their pop-ups.

- Clicked annotations receive positions 1, 2, 3, and so on. Unclicked annotations follow in their existing relative order, avoiding duplicate sequence numbers.
- The current document-wide or restart-per-page scope and label format are preserved. With per-page numbering, each physical page maintains its own click order.
- Internal IDs, body text, and geometry remain unchanged. Annotations are identified by object identity, so annotations with identical internal IDs can be ordered independently. Clicking the same object repeatedly does not count it again.
- Each click that actually changes numbering creates one undo step. Affected locked or read-only annotations block that change entirely; the operation is not applied partially.
- Exit by clicking the tool button again, pressing Esc, right-clicking, or switching tools or modes. Completed numbering changes remain. Undo, redo, or switching the active view pane also exits the tool; the next activation starts a new click sequence.
- The **Order** tool in the **Reading Views** toolbar applies the same interaction to views, but always numbers them separately on each physical page.

The original document-wide renumbering command remains separate and orders annotations automatically by page and internal ID. Applying a numbering format also recalculates numbering using that automatic order. To choose a custom order, set the format first, then use Click to Number.

## Numbering formats

Use the numbering-format button in the Proofread toolbar to choose a format in a single dialog:

| Format | Example |
| --- | --- |
| Continuous across the document | 1, 2, 3, … |
| Restart on each page | Start at 1 on every page |
| Page number plus per-page counter | 1-1, 1-2, 2-1, … |
| Custom | `P{page}-{n}`, `({n})`, `Page {page} ({n})` |

In a custom pattern, `{page}` is the 1-based physical page number and `{n}` is the sequence counter. Choose continuous document-wide numbering or restart-per-page numbering. The pattern must contain `{n}`, accepts only these two placeholders, is limited to 256 characters, and cannot contain line breaks. Chinese prefixes and suffixes are supported. If the system lacks the required glyphs, an error is reported instead of generating missing-glyph boxes.

Applying a format updates labels and appearance streams (APs) throughout the document in a single undoable operation, including the numbering settings. Any affected annotation that cannot be modified blocks the entire operation. Settings are saved in the PDF even when no numbered annotations exist, and subsequent creation or paste operations use the same settings. After pages are moved, inserted, or deleted, manual renumbering refreshes labels using the new physical page numbers.

## Exporting a CSV proofreading checklist

In Proofread mode, use the numbered-callout CSV export command, also available under Tools → Mode Tools. It exports every Numbered Callout in the entire PDF, not just those on the current page, in the selection, or within a reading view.

- Columns are `Page` (1-based physical page number), `Internal ID`, `Number` (current displayed label), and `LaTeX` (body source).
- `Number` contains the current display label, not merely the numeric counter. Rows are sorted by physical page number and current sequence counter; equal counters retain their original annotation-list order. Export does not automatically renumber annotations.
- Custom label cells receive a leading apostrophe to preserve them as text, preventing Excel from interpreting `1-1` as a date or dropping leading zeros. Labels in the PDF remain unchanged.
- A UTF-8 BOM helps Excel recognize Chinese text. Commas, double quotes, and line breaks in the body are preserved using CSV quoting rules.
- To prevent Excel formula execution, body cells starting with `=`, `+`, `-`, or `@` (including after leading whitespace), or starting with a tab or line break, receive a protective apostrophe prefix. Source text in the PDF is unchanged.
- Locked or read-only annotations can also be exported. Export requires neither modification permission nor temporary LaTeX appearance files, and does not change annotations, reading mode, or undo history.
- If no numbered callouts exist, export produces a CSV containing only column headers. Canceling the file dialog writes nothing. Saving is atomic, and overwriting the source PDF is prohibited.

## PDF appearances

The number box sits outside the upper-left edge of the body box, without shrinking or obscuring the LaTeX body:

- The number box's fill and outline use the callout's outline color.
- The label text uses the callout's fill color. The renderer does not substitute black or white or otherwise increase contrast. A transparent fill therefore makes the label text transparent as well.
- The number box and complete label are written into `/AP /N` of the same Stamp annotation, not painted as a UI overlay. ASCII labels use the PDF standard font Helvetica-Bold; Unicode labels, including Chinese, use vector glyph outlines from font fallback. Viewing and printing the saved PDF do not require the original font to be installed.
- Color and displayed-number changes update the AP. Saving and reopening, printing, and flattened export all use that appearance.

Body-box metadata retains its original dimensions, while the PDF `/Rect` and AP `/BBox` also encompass the number box. Rebuilding the AP preserves the original LaTeX `Fm0` rather than repeatedly incorporating an old number box into the body. Renumbering remains possible after the temporary source PDF has been deleted.

New PDF annotation metadata stores `type: "numbered-callout"`, a positive-integer `id`, a positive-integer `order` (sequence counter), and a string `label` (display label). The PDF Catalog entry `MengsheeNumberedCalloutNumbering` stores UTF-8 JSON containing `version: 1`, `pattern`, and `restartPerPage`. Without global settings, numbering defaults to document-wide decimal numbers. Legacy annotations without `label` display their `order`. Reading remains compatible with the old `"ordered-callout"` type; legacy internal enum values and tool action IDs are retained as compatibility identifiers.

## Shared interactions across all three callout types

Ordinary Callout, LaTeX Callout, and Numbered Callout use the same handle rules:

- The first endpoint—the arrow tip—can be dragged freely.
- The bend point can be dragged. The segment nearest the body box remains perpendicular to the edge it connects to.
- The box connection point snaps to the midpoint of the corresponding edge. It can switch edges even during slow dragging.
- Moving or resizing the body box leaves the arrow tip fixed while realigning the bend and connection points with the box.
- Handle manipulation, moving, and resizing support full geometry undo and redo. Internal IDs and displayed numbers remain unchanged.
