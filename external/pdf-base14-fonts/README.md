# Application-private PDF Base14 fonts

Fourteen PDFium/Foxit raw CFF font programs, extracted without outline or encoding
changes from revision `a84323421e94f484faca52dd9d027934eba42ab8`.
Runtime font bytes total **264,741 bytes**. The two generic multiple-master fonts
are deliberately excluded. No PDFium library or OS font installation is needed.

## Provenance and license

See `NOTICE`, the complete upstream `LICENSE`, and the original headers retained
in `upstream/*.cpp`. All fourteen selected arrays explicitly carry PDFium's
BSD-style license notice and the original Foxit attribution. Preserve copyright,
conditions, disclaimer, and non-endorsement provisions in binary distributions.
Do not apply licenses from unrelated PDFium third-party font families to these
arrays. The full upstream LICENSE also contains appended Apache 2.0 text and is
retained verbatim rather than rewritten.

`manifest.json` pins the revision and SHA-256 hashes of every upstream input and
CFF output. `prepare_fonts.py --check` verifies all inputs, the extraction, and
every output without writing. `prepare_fonts.py` reproduces fonts offline from
vendored sources. `prepare_fonts.py --fetch` retrieves only the pinned inputs and
reproduces outputs; when a manifest exists it rejects upstream/hash drift.
Updating the pin and hashes is an explicit dependency review, not a build step.
The script uses only the Python standard library. Normal builds never download.

## Runtime contract

Install the fourteen files from `fonts/` **flat** under `<prefix>/share/fonts`.
Poppler's Windows DLL in `<prefix>/bin` discovers this location automatically.
Copy `LICENSE` as `LICENSE.pdfium`, `NOTICE` as `NOTICE.pdfium`, and the manifest
as `base14-manifest.json` beside the fonts. Keep this directory in the mandatory
core installation, independent of optional StemTeX and system/user font stores.

| PDF Base14 name | CFF file |
|---|---|
| Courier | FoxitFixed.cff |
| Courier-Bold | FoxitFixedBold.cff |
| Courier-Oblique | FoxitFixedItalic.cff |
| Courier-BoldOblique | FoxitFixedBoldItalic.cff |
| Helvetica | FoxitSans.cff |
| Helvetica-Bold | FoxitSansBold.cff |
| Helvetica-Oblique | FoxitSansItalic.cff |
| Helvetica-BoldOblique | FoxitSansBoldItalic.cff |
| Times-Roman | FoxitSerif.cff |
| Times-Bold | FoxitSerifBold.cff |
| Times-Italic | FoxitSerifItalic.cff |
| Times-BoldItalic | FoxitSerifBoldItalic.cff |
| Symbol | FoxitSymbol.cff |
| ZapfDingbats | FoxitDingbats.cff |

Poppler must map canonical Base14 names to these filenames before OS fallback,
retain embedded-font priority, and identify CFF contents as `fontType1C` rather
than infer TrueType from the fact that a file is not `.ttc`. CFF glyph-name
selection preserves PDF Symbol/Zapf encodings; these fonts are not generic
Unicode replacements or general-purpose CID/CJK fallback fonts.

Required render regressions include the user's original PDF, all fourteen
faces, Symbol `Phi`/`phi`/`phi1`, the entire Symbol and Zapf encoded repertoires,
Differences encodings, and embedded-font controls. Asset integrity checks alone
do not establish rendering correctness. Run clean-machine, relocated-install,
and core-only installer tests without optional TeX resources or installed fonts.
