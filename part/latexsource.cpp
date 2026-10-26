/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "latexsource.h"

#include <QColor>
#include <QRegularExpression>

namespace GuiUtils
{
namespace LatexSource
{
namespace
{
bool isLeadingSpace(QChar character)
{
    return character == QLatin1Char(' ') || character == QLatin1Char('\t') || character == QLatin1Char('\r') || character == QLatin1Char('\n');
}

QString withoutCommentsForDetection(const QString &source)
{
    QString result;
    result.reserve(source.size());
    qsizetype backslashes = 0;
    for (qsizetype index = 0; index < source.size(); ++index) {
        const QChar character = source.at(index);
        if (character == QLatin1Char('%') && backslashes % 2 == 0) {
            while (index + 1 < source.size() && source.at(index + 1) != QLatin1Char('\r') && source.at(index + 1) != QLatin1Char('\n')) {
                ++index;
            }
            backslashes = 0;
            continue;
        }
        result.append(character);
        backslashes = character == QLatin1Char('\\') ? backslashes + 1 : 0;
    }
    return result;
}

}

QString prepareLeadingDisplay(const QString &source)
{
    qsizetype firstToken = 0;
    while (firstToken < source.size()) {
        if (isLeadingSpace(source.at(firstToken))) {
            ++firstToken;
        } else if (source.at(firstToken) == QLatin1Char('%')) {
            // Only whitespace/comments precede this position, so '%' is unescaped.
            while (firstToken < source.size() && source.at(firstToken) != QLatin1Char('\r') && source.at(firstToken) != QLatin1Char('\n')) {
                ++firstToken;
            }
        } else {
            break;
        }
    }
    if (firstToken == source.size()) {
        return source;
    }

    const QString detection = withoutCommentsForDetection(source.mid(firstToken));
    static const QRegularExpression displayEnvironment(QStringLiteral("^\\\\begin[ \\t\\r\\n]*\\{[ \\t\\r\\n]*(displaymath|equation\\*?|align\\*?|gather\\*?|multline\\*?|flalign\\*?|alignat\\*?)[ \\t\\r\\n]*\\}"));
    if (!detection.startsWith(QStringLiteral("$$")) && !detection.startsWith(QStringLiteral("\\[")) && !displayEnvironment.match(detection).hasMatch()) {
        return source;
    }

    // Like LaTeXBlocks, detect only a literal leading display, not macro expansion,
    // catcode changes or verbatim syntax. Here the fix is specific to our TeX
    // paragraph-indent box: suppress it, not display skips or line spacing. Keep
    // every authored character (including comments and blank lines) unchanged.
    QString prepared = source;
    // Keep the display's natural height: a full-line strut is an optional
    // typography choice, not a default for a leading display. Do not insert
    // rules or math atoms into the author's math list.
    // Removing that empty paragraph also removes its full-width box from
    // preview's measured extent. A zero-height/depth rule retains the requested
    // layout width without ink or a line box, and resets the interline state.
    prepared.insert(firstToken, QStringLiteral("\\hrule width\\hsize height0pt depth0pt\\relax\\noindent%\n"));
    return prepared;
}

QString prepareSnippet(const QString &source, const QColor &textColor)
{
    const QColor color = textColor.isValid() ? textColor : QColor(Qt::black);
    const QString red = QString::number(color.redF(), 'f', 6);
    const QString green = QString::number(color.greenF(), 'f', 6);
    const QString blue = QString::number(color.blueF(), 'f', 6);
    // StemTeX 0.15 owns the logical block height. Set colour on entry to
    // the first paragraph so a leading DVI colour special does not hide the
    // first measurable line from its ascent calculation. Restore everypar
    // immediately, preserving authored colour changes in later paragraphs.
    const QString prefix = QStringLiteral(
        "\\ifdefined\\PreviewBorder\\global\\PreviewBorder=0pt\\relax\\fi%\n"
        "\\tracinglostchars=3\\relax%\n"
        "\\ifdefined\\MengsheeNotePopColor\\else\\gdef\\MengsheeNotePopColor{\\special{color pop}}\\fi%\n"
        "\\ifdefined\\color%\n"
        "\\expandafter\\let\\expandafter\\MengsheeNoteSetColor\\csname set@color\\endcsname%\n"
        "\\expandafter\\let\\csname set@color\\endcsname\\relax%\n"
        "\\color[rgb]{%1,%2,%3}%\n"
        "\\expandafter\\let\\csname set@color\\endcsname\\MengsheeNoteSetColor%\n"
        "\\def\\MengsheeNoteStartColor{\\csname set@color\\endcsname}%\n"
        "\\else\\def\\MengsheeNoteStartColor{\\special{color push rgb %1 %2 %3}\\aftergroup\\MengsheeNotePopColor}\\fi%\n"
        "\\expandafter\\def\\expandafter\\MengsheeNoteEveryPar\\expandafter{\\the\\everypar}%\n"
        "\\everypar{\\everypar\\expandafter{\\MengsheeNoteEveryPar}\\MengsheeNoteStartColor%\n"
        "\\MengsheeNoteEveryPar}%\n").arg(red, green, blue);
    // The host setup precedes the source, so retain the literal-display width
    // guard here. Do not add a second vbox, strut, or height/depth correction.
    const QString preparedSource = prepareLeadingDisplay(source);
    qsizetype backslashes = 0;
    for (qsizetype i = source.size(); i > 0 && source.at(i - 1) == QLatin1Char('\\'); --i) ++backslashes;
    return prefix + preparedSource + (backslashes % 2 ? QStringLiteral("\n") : QStringLiteral("%\n"));
}
}
}
