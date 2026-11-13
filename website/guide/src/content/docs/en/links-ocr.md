---
title: Edit links and searchable text
sidebar:
  order: 8
---

## Repair navigation

Use **Cross-references** mode to work with links and destinations. A link is the clickable area; its destination determines where it goes. Check both when repairing a reference. Test the link after editing it, then save the PDF. External links also depend on the target file or website remaining available.

## Recognize a scanned page

In **Content Recognition** mode, choose **Recognize Content** and select **Add searchable text (OCR)**. Tesseract runs locally with the bundled English model. GLM-OCR and Mathpix send the selected page images to the configured service. Select **Generate Reading Views** as well to create both outputs in one operation. Pages with selectable text skip OCR by default.

OCR does not reconstruct the original mathematical source. Its searchable layer lets you search or select recognized words while retaining the scanned page appearance.

## Correct the text layer

Enable **Edit OCR Text Layer**. Click a word to correct its text; double-click a blank area to add a word. Clear a word's text to delete it. Adjust its box to match the word's location on the page.

The editing overlay is a working view of the text layer. Turn it off to return to the normal PDF appearance, then check search and selection before saving. If the source already has selectable text, first determine whether OCR is needed; recognition errors can make searching less reliable.
