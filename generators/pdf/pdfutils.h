// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Draft: the neutral PDF utility API for code outside a document, such as the
// LaTeX note pipeline in part/ and the GUI helpers in gui/.
//
// With runtime engine modules, these functions use PdfEngineRegistry to pick
// the same engine the PDF plugin would use (explicit id > MENGSHEE_PDF_ENGINE
// > configured default > first available). part/ and gui/ therefore link the
// front end only, never MuPDF or Poppler.
//
// History: the former expansion API (PdfPageBounds::expandAppearance and
// LatexPdfBounds::expandInPlace) was retired. Its production caller was
// removed in 2026.0.26.10; the writer, the two implementations, the tests and
// the private /MengsheeLayoutBox reader were deleted. Appearances saved by
// 2026.0.26.4 through 2026.0.26.9 therefore use their page box as-is when
// re-edited. Do not reintroduce that machinery without a live caller.
//
// This header is not part of the build yet.

#include <QSizeF>
#include <QString>

namespace Mengshee
{
namespace Pdf
{

struct PageSizeResult {
    bool ok = false;
    /** Page size of the first page, in PDF points, including /Rotate. */
    QSizeF sizePoints;
    QString errorText;
};

/**
 * Returns the size of the first page of a PDF file.
 *
 * The rule is the page's visible box (CropBox, falling back to MediaBox),
 * rotated by the page's /Rotate; the result does not depend on which engine
 * produced the file.
 *
 * engineId empty uses PdfEngineRegistry::selectedEngineId(). The function
 * opens the file through that engine, reads page 0 and closes it again; the
 * file stays read-only. Encrypted or malformed files return ok=false with a
 * user-presentable message.
 */
PageSizeResult firstPageLogicalSize(const QString &pdfFileName, const QString &engineId = {});

} // namespace Pdf
} // namespace Mengshee
