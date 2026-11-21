/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "latexsource.h"
#include <QColor>
#include <QStringList>

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

QString sourceForLayout(const QString &source)
{
    // Only discard a suffix made of ordinary whitespace and line-break tokens.
    // A control space and a verbatim delimiter are source, not layout padding.
    qsizetype lastContent = 0;
    qsizetype i = 0;
    int groupDepth = 0;
    bool trailingBreak = false;
    const auto keep = [&] {
        lastContent = i;
        trailingBreak = false;
    };
    while (i < source.size()) {
        const QChar ch = source.at(i++);
        if (ch == QLatin1Char('%')) {
            // Keep the comment terminator too, so later TeX cannot be commented out.
            while (i < source.size() && source.at(i) != QLatin1Char('\n') && source.at(i) != QLatin1Char('\r'))
                ++i;
            if (i < source.size() && source.at(i) == QLatin1Char('\r'))
                ++i;
            if (i < source.size() && source.at(i) == QLatin1Char('\n'))
                ++i;
            keep();
        } else if (ch == QLatin1Char('\\')) {
            const qsizetype start = i;
            if (i < source.size() && source.at(i).isLetter()) {
                while (i < source.size() && source.at(i).isLetter())
                    ++i;
            } else if (i < source.size()) {
                ++i;
            }
            const QString command = source.mid(start, i - start);
            if (command == QLatin1String("verb")) {
                if (i < source.size() && source.at(i) == QLatin1Char('*'))
                    ++i;
                if (i >= source.size() || source.at(i) == QLatin1Char('\n') || source.at(i) == QLatin1Char('\r'))
                    return source;
                const qsizetype end = source.indexOf(source.at(i), i + 1);
                if (end < 0)
                    return source;
                i = end + 1;
                keep();
            } else if (command == QLatin1String("catcode")) {
                // Token meanings can change; leave such snippets to TeX unchanged.
                return source;
            } else if (groupDepth == 0 && (command == QLatin1String("\\") || command == QLatin1String("newline") || command == QLatin1String("linebreak"))) {
                trailingBreak = true;
            } else {
                keep();
            }
        } else if (groupDepth == 0 && (ch == QLatin1Char(' ') || ch == QLatin1Char('\t') || ch == QLatin1Char('\n') || ch == QLatin1Char('\r'))) {
            trailingBreak |= ch == QLatin1Char('\n') || ch == QLatin1Char('\r');
        } else {
            if (ch == QLatin1Char('{'))
                ++groupDepth;
            else if (ch == QLatin1Char('}') && groupDepth > 0)
                --groupDepth;
            keep();
        }
    }
    // Ordinary trailing spaces alone do not add an empty line. Preserve them.
    return trailingBreak && lastContent > 0 ? source.left(lastContent) : source;
}
}
