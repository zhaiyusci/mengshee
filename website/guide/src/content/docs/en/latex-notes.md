---
title: Write LaTeX notes
sidebar:
  order: 4
---

## Create a note

Choose **LaTeX Note** and place a frame beside the passage. A new note starts with `\LaTeX` as editable sample content. Replace it with your explanation, wait for rendering, and save the PDF when satisfied. Use a LaTeX callout when you want a leader pointing at a precise location.

Try this source:

```latex
The image of the projection operator is
$\operatorname{im}P(q_0)=\{P(q_0)v:\ v\in\mathcal H_e\}$.
```

Use `$…$` for an inline formula and `$$…$$` for a displayed formula. Ordinary text can surround the mathematics. Available fonts and packages depend on the installed StemTeX profile.

## Configure the LaTeX environment

Open **Configure Annotations…** from the Settings menu (or the **Annotations** page in the application settings), then find **LaTeX Notes**. These settings choose the rendering environment; a note's color, border, and text size are configured through the annotation's properties.

| Setting | What it controls |
| --- | --- |
| **StemTeX profile** | The fonts, loaded packages, and preamble used for typesetting. **Auto (recommended bundled profile)** selects the recommended bundled profile. Entries are marked **(bundled)** or **(user)**. |
| **Create…** | Opens StemTeX Profile Creator to make a user profile using the selected TeX tree. |
| **Open Folder** | Opens Mengshee's user-profile directory. Use this button to locate your profiles rather than the separate StemTeX application's profile folder. |
| **StemTeX TeX tree** | The package/font tree. Leave it empty for the bundled tree, or select a local TeX Live installation as described below. |
| **StemTeX status** | Shows readiness and rendering progress. If initialization fails, read the accompanying message. |

### Start with the bundled environment

For Latin text and basic mathematics, leave **StemTeX TeX tree** empty and select **Auto**. The bundled `unicodemath` profile uses Latin Modern text and mathematics with `unicode-math`. You do not need to install a complete TeX Live distribution for this setup.

The bundled tree is deliberately small: it does not include CJK support or the full collection of optional packages. To write Chinese, use a suitable local TeX Live tree and create a profile with a **CJK font**. Installing a Chinese Windows font alone does not supply the required `xeCJK` package.

### Use your local TeX Live

1. Install TeX Live locally, including the packages and fonts you intend to use.
2. In **StemTeX TeX tree**, browse to the directory **containing** `texmf-dist`. For example, if the file tree includes `C:\texlive\2026\texmf-dist\web2c`, select **`C:\texlive\2026`**. Use your actual installation location and year.
3. Do not select `texmf-dist` itself, a `bin` directory, or `xelatex.exe`. This field selects resources, not an executable.
4. Click **Create…** to make a profile for that tree, then select the resulting user profile in Mengshee. Apply the settings with **Apply** or **OK**.
5. Create a short test note. Try ordinary text and `$x^2$`, then add Chinese or package-specific commands as needed.

Mengshee continues to use **StemTeX's own engine and converters**. Selecting local TeX Live supplies packages and fonts; it does not switch to running the system's `xelatex` command. Keep the StemTeX support components installed.

### Create a font and package profile

In Profile Creator, check **TeX Live tree**, enter a **Profile name**, and keep **Save to** at the Mengshee directory supplied by the application.

- **Fonts:** select **Text font**, **Math font**, and, for Chinese text, **CJK font**. The available choices depend on the selected tree and required packages.
- **Packages:** enable the features you need, for example `mhchem` for `\ce{H2O}` or `siunitx` for quantities and units. Optional packages are not all enabled automatically. The creator adds dependencies for supported selections.
- **Custom:** add extra preamble commands, such as the example below. Do not include `\documentclass` or `\begin{document}` / `\end{document}`. Custom commands still require their packages to be available and compatible.
- **Preamble:** inspect the generated preamble, then click **Create Profile**. Close the creator to return to Mengshee; the profile list refreshes. If exactly one new profile was created, Mengshee selects it automatically. Otherwise choose it yourself, then apply the settings.

```latex
\newcommand{\R}{\mathbb{R}}
```

With that profile selected, a note can use `$x\in\R$`. Put package loads and shared definitions in the profile; the note editor contains the text and mathematics to display.

### Resolve environment problems

If the TeX tree is unavailable, check that the chosen directory contains `texmf-dist\web2c`. If **Create…** reports that Profile Creator is missing, repair the Mengshee StemTeX support installation. For missing commands, fonts, or packages, check both the selected tree and the selected profile: having a package on disk does not necessarily load it into your note.

To return to the bundled setup, clear **StemTeX TeX tree**, select **Auto**, and apply. Notes requiring additional packages or Chinese support still need their original environment when edited again. Other readers can display their saved appearance without that environment, but the PDF does not install your local TeX Live or user profile on another computer.

## Width controls wrapping; height fits the content

Change the note's width to change line wrapping. Mengshee fits its height to the rendered content. On a rotated note, use the width handles in the note's own layout direction: the text baseline may no longer be horizontal on screen. Editing a few words should retain the note's orientation.

![A LaTeX note and its editable source](/mengshee/assets/latex-note.png)

## Keep source and appearance together

Saving stores the editable source and the rendered appearance in the PDF. Other readers can display the appearance without StemTeX. They may not expose Mengshee's source editor. Keep the editable PDF if you plan to revise the note; flattening turns its visible appearance into page content.

If rendering fails, inspect the error and check the source, packages, and runtime installation. Changing paper orientation in the print dialog does not rotate the saved note or alter its source.
