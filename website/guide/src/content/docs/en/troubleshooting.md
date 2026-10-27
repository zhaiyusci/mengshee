---
title: Troubleshooting
sidebar:
  order: 10
---

## I cannot find a tool

Check the editing mode. Reading and Annotations, Cross-references, OCR, Page Editing, Reading Views, and Proofread expose different controls. Hover over an icon to read its name. Right-click an annotation tool to configure its defaults; select an existing annotation to edit that object's properties.

## A LaTeX note will not render

Check the source for unmatched delimiters or unsupported commands. Confirm that the LaTeX runtime and required fonts or packages are installed. Viewing an already rendered note does not require compiling it again. Keep a copy of the source before making large changes.

## Text cannot be selected

The page may be a scan. Try [OCR](../links-ocr/), then check the recognized words. Formulas and unusual layouts need manual review.

## Another reader shows something different

Readers may generate ordinary text annotations differently. Rendered LaTeX notes carry an appearance, but their source editor is specific to Mengshee. Test important documents in the recipient's reader. A [flattened copy](../printing-export/) can preserve visible annotations for delivery; retain the editable original.

## Saving or opening fails

Check that the file is locally available and its folder is writable. Try saving a copy to a writable local folder. For a crash or repeatable display problem, report the Mengshee version, exact steps, and a small example PDF with private information removed. Include a screenshot if it helps explain the mismatch.

Report issues on [GitHub](https://github.com/zhaiyusci/mengshee/issues). Do not upload confidential documents to a public issue.
