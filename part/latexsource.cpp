/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "latexsource.h"
#include <QColor>

namespace GuiUtils::LatexSource
{
QString requestSetup(const QColor &textColor)
{
    const QColor color = textColor.isValid() ? textColor : QColor(Qt::black);
    // Set xcolor's current colour without emitting a whatsit into the layout.
    // The worker then runs the SDK's normalcolor at its original location.
    // Author colour commands can still override this request-local default.
    return QStringLiteral(
        "\\ifdefined\\color%\n"
        "\\expandafter\\let\\expandafter\\MengsheeSetColor\\csname set@color\\endcsname%\n"
        "\\expandafter\\let\\csname set@color\\endcsname\\relax%\n"
        "\\color[rgb]{%1,%2,%3}%\n"
        "\\expandafter\\let\\csname set@color\\endcsname\\MengsheeSetColor%\n"
        "\\expandafter\\let\\csname default@color\\expandafter\\endcsname\\csname current@color\\endcsname%\n"
        "\\ifcsname XC@current@color\\endcsname%\n"
        "\\expandafter\\let\\csname XC@default@color\\expandafter\\endcsname\\csname XC@current@color\\endcsname%\n"
        "\\fi%\n"
        "\\else%\n"
        "\\AddToHookNext{shipout/background}{\\special{color push rgb %1 %2 %3}}%\n"
        "\\AddToHookNext{shipout/foreground}{\\special{color pop}}%\n"
        "\\fi%\n")
        .arg(QString::number(color.redF(), 'f', 6), QString::number(color.greenF(), 'f', 6), QString::number(color.blueF(), 'f', 6));
}
}
