/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "latexrenderguards.h"

#include <QFile>
#include <QFileInfo>

#include <cmath>

namespace GuiUtils
{
namespace LatexRenderGuards
{
InputError validateInput(const QString &source, double width, double fontSize)
{
    if (source.size() > 250000) {
        return InputError::SourceTooLarge;
    }
    if (source.contains(QChar(u'\0'))) {
        return InputError::EmbeddedNull;
    }
    if (source.trimmed().isEmpty()) {
        return InputError::EmptySource;
    }
    if (!std::isfinite(width) || width < 0 || width > 50000) {
        return InputError::InvalidWidth;
    }
    if (!std::isfinite(fontSize) || (fontSize != 0 && (fontSize < 1 || fontSize > 200))) {
        return InputError::InvalidFontSize;
    }
    return InputError::None;
}

bool isUsablePdfArtifact(const QString &fileName)
{
    const QFileInfo info(fileName);
    if (!info.isFile() || info.isSymLink() || !info.isReadable() || info.size() <= 0 || info.size() > maxPdfBytes) {
        return false;
    }
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > maxPdfBytes) {
        return false;
    }
    return file.read(5) == QByteArrayLiteral("%PDF-");
}
}
}
