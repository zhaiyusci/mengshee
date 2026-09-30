# PDF font interpretation and Windows standard-font deployment

## Specification boundaries

Based on [ISO 32000-1:2008](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf):

- Section 9.6.2.2 requires a conforming reader to provide the Standard 14 fonts, or their metrics and suitable substitutes. This includes Symbol and ZapfDingbats, not just Latin text faces.
- Section 9.6.6 defines glyph selection for simple fonts through character codes and Encoding/Differences. Type 1 fonts use `.notdef` when the encoding names an unavailable glyph; extracted text must not become a separate drawing instruction.
- Section 9.7 defines the character-code → CMap → CID → font-glyph relationship for composite fonts. Identity-H/V does not mean that character codes are Unicode values.
- Section 9.10 defines ToUnicode mappings for text semantics and extraction. Preserve Poppler's legitimate Unicode-assisted mappings during font parsing and substitute construction, but do not select another system font at paint time to override the resolved glyph.
- Section 12.5.5 defines annotation appearance streams. Preserve an existing AP when reading a document rather than regenerating it simply because a new font is available or Contents differs from the appearance. Updating an appearance after an explicit content edit is a separate operation.

## Rendering changes

1. Remove the local BMP non-ASCII paint override from `SplashOutputDev.cc`, including the subsequent `hasGlyph` mitigation. Restore the upstream `font/code` fill, stroke, and clip paths without character-specific allowlists.
2. Retain the established `GfxFont` Encoding, CMap, and CIDToGID parsing. Resolve external font files through `getExternalFont` to identify the actual program format instead of treating files inferred from registry entries or extensions as TrueType. Preserve the TTC face index.
3. Prefer application-private CFF resources for unembedded Standard 14 fonts on Windows. Embedded fonts remain authoritative and are not replaced by these resources.
4. For nonstandard simple fonts with no system match or explicit alias, use the upstream serif/fixed/bold/italic substitution policy rather than forcing every miss to Helvetica. Preserve CID collection matching and explicit cidfmap aliases.
5. Use the same font and character code for glyph bounds and painting. Remove the blanket rejection of unembedded non-ASCII text left over from the old paint override.
6. Resolve DLL-relative resource paths through the Unicode Windows API and convert them to UTF-8, so non-ASCII installation paths do not hide the private fonts.

There are no branches specific to a document, an individual Greek letter, or a font size observed in a screenshot.

## Reproducible font resources

`external/pdf-base14-fonts/` pins PDFium commit `a84323421e94f484faca52dd9d027934eba42ab8`. It includes the 14 original font-array source files, the original license, provenance information, an offline extraction tool, and a checksum manifest. The 14 CFF files total 264741 bytes.

- Upstream: [PDFium font sources](https://github.com/chromium/pdfium/tree/a84323421e94f484faca52dd9d027934eba42ab8/core/fxge/fontdata/chromefontdata). Each font source declares a BSD-style license and retains the Foxit attribution. The complete upstream LICENSE is preserved verbatim.
- Runtime location: `<parent of the bin directory containing poppler.dll>/share/fonts/`.
- The SDK, development runtime, and core installer must include all 14 fonts and their LICENSE/NOTICE/manifest files. These resources are independent of optional StemTeX and do not require system-wide font or TeX installation.
- `windows-build/cmake/pdf-base14-fonts.cmake` checks source and deployed-resource hashes. Missing or corrupt resources fail packaging. Both SDK build entry points deploy the same resources.
- No proprietary Windows fonts are redistributed, and no system fonts are registered.

## Validation

Windows validation for 2026.0.22.4:

- Focused font QtTest totals: **66 passed / 0 failed / 0 skipped**, including initialization and cleanup. Coverage includes separation of character encoding from ToUnicode, blank glyphs, all 14 standard faces, embedded CFF comparisons, Symbol/Zapf glyph names and PDF-specified widths, 12 missing-font family/style combinations, horizontal and vertical CIDToGID mappings with embedded TrueType, Type3, and existing FreeText AP save/reload roundtrips.
- Focused glyph-bounds totals: **4 passed / 0 failed / 0 skipped**, including initialization and cleanup.
- The same tests were rerun against the complete staged runtime. Pages 3 and 12 of the real-document regression sample matched the SDK output pixel-for-pixel in RGBA. Visual comparison with PDFium confirmed the expected formula glyph shapes. Different renderers are not required to produce identical antialiasing pixels.
- Relocation testing copied only DLLs, the platform plugin, and private fonts into a directory containing Chinese characters and spaces, without SDK/TeX search paths. Fonts resolved from that directory and rendering remained pixel-identical.
- Resource deployment tests cover rejection of missing/corrupt fonts and missing licenses, as well as relocation. Independent staged-application startup and module-origin checks passed.
- No unrelated mode-switch or toolbar matrices were run. The user's PDF and running installed application were not modified.

## Scope limitations

This is not a completeness certification for arbitrary malformed PDFs or every font-layout feature. Unembedded fonts outside Standard 14 may still require system fonts or approximate substitutes.

Unicode layout and font embedding for newly created or edited FreeText annotations are separate authoring concerns. This change does not rewrite `Annot.cc`, `Form.cc`, or the Qt annotation-generation logic. The audit identified existing concerns with transient font generation, assigning subsequent non-ASCII characters to the font chosen for the first one, and TTC/complex-script handling. Those issues require separate work; this change neither claims to fix them nor conceals them with cross-font page-painting overrides. Reading and saving existing appearance streams have been specifically tested.
