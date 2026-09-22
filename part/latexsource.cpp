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
    // Set the foreground while still in vertical mode, before the leading
    // display guard. Prefer xcolor/color when present: a bare DVI special does
    // not update their current-colour state (e.g. \\color{.} or \\colorlet).
    // The bundled minimal profile has neither package, so fall back to the
    // driver's colour stack there. Remember that choice before reading source.
    const QString prefix = QStringLiteral("\\begingroup%\n"
                                          "\\expandafter\\let\\csname mengshee@note@endcolor\\endcsname\\relax%\n"
                                          "\\ifdefined\\color%\n"
                                          "\\color[rgb]{%1,%2,%3}%\n"
                                          "\\else%\n"
                                          "\\special{color push rgb %1 %2 %3}%\n"
                                          "\\expandafter\\def\\csname mengshee@note@endcolor\\endcsname{\\special{color pop}}%\n"
                                          "\\fi%\n")
                               .arg(red, green, blue);
    // Keep author whitespace/comments untouched; '%' belongs to the wrapper
    // and prevents a new interword space when the source has no final newline.
    // A trailing unpaired backslash must see an end-of-line, not turn our '%'
    // into an authored \\% command (e.g. while editing an unfinished command).
    qsizetype backslashes = 0;
    for (qsizetype i = source.size(); i > 0 && source.at(i - 1) == QLatin1Char('\\'); --i) {
        ++backslashes;
    }
    const QString boundary = backslashes % 2 ? QStringLiteral("\n") : QStringLiteral("%\n");
    const QString preparedSource = prepareLeadingDisplay(source);
    const bool leadingDisplay = preparedSource != source;
    // Allocate private registers once per worker, not once per render. All box
    // assignments and helper aliases remain local to the outer snippet group.
    const QString boxPrefix = QStringLiteral(
        "\\ifcsname mengshee@note@box\\endcsname\\else\\expandafter\\newbox\\csname mengshee@note@box\\endcsname\\fi%\n"
        "\\ifcsname mengshee@note@scratch\\endcsname\\else\\expandafter\\newbox\\csname mengshee@note@scratch\\endcsname\\fi%\n"
        "\\ifcsname mengshee@note@top\\endcsname\\else\\expandafter\\newdimen\\csname mengshee@note@top\\endcsname\\fi%\n"
        "\\expandafter\\let\\expandafter\\MengsheeNoteBox\\csname mengshee@note@box\\endcsname%\n"
        "\\expandafter\\let\\expandafter\\MengsheeNoteScratch\\csname mengshee@note@scratch\\endcsname%\n"
        "\\expandafter\\let\\expandafter\\MengsheeNoteTop\\csname mengshee@note@top\\endcsname%\n"
        "\\let\\MengsheeNoteDepth\\relax%\n"
        "\\def\\MengsheeNoteLastLine{\\def\\MengsheeNoteDepth{\\ifdim\\dp\\MengsheeNoteBox<\\dp\\strutbox\\relax\\dp\\MengsheeNoteBox=\\dp\\strutbox\\relax\\fi}}%\n"
        "\\setbox\\MengsheeNoteBox=\\vbox{\\begingroup%\n");
    // Test the final node BEFORE the source group's aftergroup colour-pop
    // whatsits. Two levels of aftergroup record a local depth action after the
    // source group AND the vbox have closed; colour pops stay inside the box.
    // Apply that action after top-edge repacking, which otherwise loses an
    // artificially extended depth when unboxing the original natural list.
    // A display's trailing glue is not a text line. Inserting an hmode strut
    // there would instead create a real, unwanted empty trailing paragraph.
    const QString boxSuffix = QStringLiteral(
        "\\par%\n"
        "\\ifnum\\lastnodetype=1\\relax\\aftergroup\\aftergroup\\aftergroup\\MengsheeNoteLastLine\\fi%\n"
        "\\endgroup}%\n");
    QString topEdge;
    if (!leadingDisplay) {
        // vtop exposes the first node's measured height. Only a positive first
        // box/rule height is measurable here; leave authored leading glue,
        // specials and macro-owned layout alone rather than guessing a line.
        // Use a real kern: merely changing a vbox's reported height does not
        // move its internal ink away from the top in DVI output.
        topEdge = QStringLiteral(
            "\\setbox\\MengsheeNoteScratch=\\vtop{\\unvcopy\\MengsheeNoteBox}%\n"
            "\\ifdim\\ht\\MengsheeNoteScratch>0pt\\relax%\n"
            "\\MengsheeNoteTop=\\ht\\strutbox\\relax%\n"
            "\\advance\\MengsheeNoteTop by -\\ht\\MengsheeNoteScratch\\relax%\n"
            "\\ifdim\\MengsheeNoteTop>0pt\\relax%\n"
            "\\setbox\\MengsheeNoteBox=\\vbox{\\kern\\MengsheeNoteTop\\relax\\unvbox\\MengsheeNoteBox}%\n"
            "\\fi\\fi%\n");
    }
    return prefix + boxPrefix + preparedSource + boundary + boxSuffix + topEdge
        + QStringLiteral("\\MengsheeNoteDepth%\n\\box\\MengsheeNoteBox%\n\\csname mengshee@note@endcolor\\endcsname%\n\\endgroup");
}
}
}
