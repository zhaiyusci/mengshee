/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef OKULAR_LATEXSOURCE_H
#define OKULAR_LATEXSOURCE_H

#include <QString>

class QColor;

namespace GuiUtils
{
namespace LatexSource
{
// Preserve source bytes and natural display height; insert only the leading
// width/noindent guard, without adding math atoms or overriding display skips.
QString prepareLeadingDisplay(const QString &source);
// Adaptive colour plus measured TeX edge metrics; no glyph-ink cropping policy.
QString prepareSnippet(const QString &source, const QColor &textColor);
}
}

#endif
