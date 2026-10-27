---
title: Install and get started
sidebar:
  order: 1
---

## Install on Windows

1. Open the [release downloads](https://github.com/zhaiyusci/mengshee/releases/latest).
2. Download the `Mengshee-…-Setup.exe` installer and run it.
3. Follow the installer prompts. Keep the offered LaTeX support components if you plan to create or edit LaTeX notes.
4. Start Mengshee and use **File → Open** to choose a PDF.

Opening an existing LaTeX note does not require compiling it again: the PDF contains its rendered appearance. Creating or changing one needs the rendering runtime.

For Chinese text, additional packages, or a local TeX Live installation, follow [LaTeX environment setup](../latex-notes/#configure-the-latex-environment).

## Find your tools

The mode selector on the main toolbar groups tools by task: Reading and Annotations, Cross-references, OCR, Page Editing, Reading Views, and Proofread. Changing mode reveals the relevant controls; it does not automatically activate every tool in that mode. Hover over an icon for its name.

Try a short workflow: open a PDF, fit it to the window width, highlight a sentence, and save a copy. Reopen the copy to confirm that your annotation is there.

## Save your work

Use **Save** to update the current PDF, or **Save As** to keep the source and write a separate edited copy. A modified-document indicator means there are unsaved changes. Undo and redo apply to supported editing operations in the current session; they are not a substitute for keeping an original copy.

If a tool is unavailable, check that you opened a local PDF and that the document permits editing. Switching modes does not override document permissions. See [troubleshooting](../troubleshooting/).
