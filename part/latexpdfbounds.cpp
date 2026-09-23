/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "latexpdfbounds.h"
#include "latexrenderguards.h"

#include <pdfpagebounds.h>
#include <KLocalizedString>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>

namespace GuiUtils::LatexPdfBounds
{
bool expandInPlace(const QString &pdfFileName, QString &error)
{
    error.clear();
    QTemporaryDir staging;
    if (!staging.isValid()) {
        error = i18n("Could not create a temporary directory for LaTeX note appearances.");
        return false;
    }
    const QString output = staging.filePath(QStringLiteral("appearance.pdf"));
    // This is the existing preview-border allowance, not a replacement for
    // logical line metrics. The native helper unions rather than tight-crops.
    const auto result = PdfPageBounds::expandAppearance(pdfFileName.toUtf8().toStdString(), output.toUtf8().toStdString(), 1.0);
    if (!result.ok) {
        error = i18n("Could not preserve the LaTeX note's logical and painted bounds: %1", QString::fromUtf8(result.message.c_str()));
        return false;
    }
    if (!LatexRenderGuards::isUsablePdfArtifact(output)) {
        error = i18n("The LaTeX renderer returned an unreadable, invalid, or oversized PDF (limit: 64 MiB).");
        return false;
    }
    QFile expanded(output);
    QSaveFile destination(pdfFileName);
    // Never use a direct-write fallback: a failed expansion must leave the
    // caller-owned SDK artifact intact, just as a failed render leaves the
    // annotation's previously accepted appearance intact.
    destination.setDirectWriteFallback(false);
    if (!expanded.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly)) {
        error = i18n("Could not save the rendered LaTeX note PDF.");
        return false;
    }
    char buffer[64 * 1024];
    qint64 count;
    while ((count = expanded.read(buffer, sizeof(buffer))) > 0) {
        if (destination.write(buffer, count) != count) {
            error = i18n("Could not save the rendered LaTeX note PDF.");
            return false;
        }
    }
    if (count < 0 || !destination.commit()) {
        error = i18n("Could not save the rendered LaTeX note PDF.");
        return false;
    }
    return true;
}
}
