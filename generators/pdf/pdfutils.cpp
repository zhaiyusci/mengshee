// SPDX-License-Identifier: AGPL-3.0-or-later
#include "pdfutils.h"

#include "pdfengineregistry.h"

#include <QSizeF>

#include <utility>

namespace Mengshee
{
namespace Pdf
{

PageSizeResult firstPageLogicalSize(const QString &pdfFileName, const QString &engineId)
{
    PageSizeResult result;

    if (pdfFileName.isEmpty()) {
        result.errorText = QStringLiteral("The PDF file name is empty.");
        return result;
    }

    PdfEngineRegistry &registry = PdfEngineRegistry::instance();
    EnginePtr engine = engineId.isEmpty() ? registry.createSelectedEngine(&result.errorText) : registry.createEngine(engineId, &result.errorText);
    if (!engine) {
        if (result.errorText.isEmpty()) {
            result.errorText = QStringLiteral("No PDF engine is available.");
        }
        return result;
    }

    PdfDocument *document = engine->document();
    if (!document) {
        result.errorText = QStringLiteral("The PDF engine \"%1\" does not provide a document interface.").arg(engine->id());
        return result;
    }

    const OpenStatus status = document->openFile(pdfFileName, QString());
    if (status != OpenStatus::Success) {
        result.errorText = status == OpenStatus::NeedsPassword ? QStringLiteral("The PDF file is encrypted and needs a password.")
                                                               : QStringLiteral("The PDF file could not be opened by the \"%1\" engine.").arg(engine->id());
        document->close();
        return result;
    }

    if (document->pageCount() < 1) {
        result.errorText = QStringLiteral("The PDF file has no pages.");
        document->close();
        return result;
    }

    const PageGeometry geometry = document->pageGeometry(0);
    document->close();

    QSizeF size = geometry.cropBox;
    if (!size.isValid() || size.isEmpty()) {
        size = geometry.mediaBox;
    }
    if (!size.isValid() || size.isEmpty()) {
        result.errorText = QStringLiteral("The first PDF page has an invalid size.");
        return result;
    }

    const int rotation = ((geometry.rotation % 360) + 360) % 360;
    if (rotation == 90 || rotation == 270) {
        size.transpose();
    }

    result.sizePoints = size;
    result.ok = true;
    return result;
}

} // namespace Pdf
} // namespace Mengshee
