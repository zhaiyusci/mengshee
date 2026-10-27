/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef OKULAR_LATEXSOURCE_H
#define OKULAR_LATEXSOURCE_H
#include <QString>
class QColor;
namespace GuiUtils::LatexSource
{
// Executed in the worker before the unchanged source reaches its layout box.
QString requestSetup(const QColor &textColor);
}
#endif
