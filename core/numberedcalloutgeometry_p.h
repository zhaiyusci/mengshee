/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef OKULAR_NUMBEREDCALLOUTGEOMETRY_P_H
#define OKULAR_NUMBEREDCALLOUTGEOMETRY_P_H

#include <QFontDatabase>
#include <QGlyphRun>
#include <QPainterPath>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QTextLayout>
#include <QTransform>
#include <algorithm>
#include <iterator>

namespace Okular::NumberedCalloutGeometry
{
struct BadgeLabel {
    QString text;
    double width = 0;
    bool ascii = true;
    bool valid = true;
    QPainterPath outline;
};

// Standard Helvetica-Bold WinAnsi advances, ASCII 32..126 (1/1000 em).
inline constexpr int helveticaBoldAdvances[] = {
    278,333,474,556,556,889,722,238,333,333,389,584,278,333,278,278,
    556,556,556,556,556,556,556,556,556,556,333,333,584,584,584,611,
    975,722,722,722,722,667,611,778,722,278,556,722,611,833,722,778,
    667,778,722,667,611,722,667,944,667,667,611,333,278,333,584,556,
    333,556,611,556,611,556,333,611,611,278,278,556,278,889,611,611,
    611,611,389,556,333,611,556,778,556,556,500,389,280,389,584
};
static_assert(std::size(helveticaBoldAdvances) == 95);

// Advance width of an ASCII label in the standard Helvetica-Bold face at an
// arbitrary size, in PDF points. Every consumer that has to size a label before
// its appearance exists shares this, so the box and the drawn text agree.
// @p text must contain only characters 32..126.
inline double asciiHelveticaBoldWidth(const QString &text, double fontSizePt)
{
    int advance = 0;
    for (QChar c : text) advance += helveticaBoldAdvances[c.unicode() - 32];
    return advance * fontSizePt / 1000.0;
}

// A short, self-describing "note" label. It is used both for the link that
// points at a note page and for the heading written on the note page itself, so
// the wording and the face live here instead of in either caller.
namespace NoteLabel
{
inline constexpr double markFontSizePt = 7.0;
inline constexpr double markPaddingPt = 2.5;
inline constexpr double headingFontSizePt = 12.0;
inline constexpr double headingMarginPt = 36.0;

/** Visible text of the link that points at note @p number. */
inline QString markText(int number)
{
    return QStringLiteral("see note %1.").arg(number);
}

/** Destination-name prefix of the notes attached to @p sourcePageOneBased. */
inline QString destinationPrefix(int sourcePageOneBased)
{
    return QStringLiteral("mnote-%1-").arg(sourcePageOneBased);
}

/**
 * Note number carried by a note destination name, or 0 when
 * @p destinationName is not one. The destination is the durable identity of a
 * note link: its appearance is dropped whenever the link itself is edited, so
 * anything that has to restore the label reads it back from here.
 */
inline int noteNumberForDestination(const QString &destinationName)
{
    const QStringList parts = destinationName.split(QLatin1Char('-'));
    if (parts.size() != 3 || parts.at(0) != QLatin1String("mnote")) {
        return 0;
    }
    const auto positiveNumber = [](const QString &text) {
        if (text.isEmpty()) {
            return 0;
        }
        for (QChar c : text) {
            if (c < QLatin1Char('0') || c > QLatin1Char('9')) {
                return 0;
            }
        }
        return text.toInt();
    };
    // The page part has to be a real page number too, so a name that only looks
    // like a note name is not labelled as one.
    if (positiveNumber(parts.at(1)) <= 0) {
        return 0;
    }
    return positiveNumber(parts.at(2));
}

/** Visible text of a link that points at @p destinationName, or an empty string when that is not a note destination. */
inline QString markTextForDestination(const QString &destinationName)
{
    const int number = noteNumberForDestination(destinationName);
    return number > 0 ? markText(number) : QString();
}

/** Visible heading written at the top of note page @p number. */
inline QString pageHeading(int number)
{
    return QStringLiteral("note %1").arg(number);
}

/**
 * Size of the link's label box in PDF points. The mark is a link, so this is
 * also the hotspot: keep it as small as the text allows.
 */
inline QSizeF markBoxSize(const QString &text)
{
    return QSizeF(asciiHelveticaBoldWidth(text, markFontSizePt) + 2.0 * markPaddingPt, markFontSizePt + 2.0 * markPaddingPt);
}
}

inline BadgeLabel badgeLabel(int order, const QString &formatted)
{
    BadgeLabel result;
    result.text = formatted.isEmpty() ? QString::number(order) : formatted;
    if (result.text.size() > 4096 || QString::fromUtf8(result.text.toUtf8()) != result.text) { result.valid = false; return result; }
    for (QChar c : result.text) {
        if (c.unicode() < 32 || c.unicode() > 126) result.ascii = false;
    }
    if (result.ascii) {
        result.width = asciiHelveticaBoldWidth(result.text, 9.0);
        return result;
    }
    // Shape Unicode with Qt's font fallback, then store actual glyph outlines
    // in the PDF. Reopening never depends on a viewer's font substitution.
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPixelSize(90);
    font.setWeight(QFont::Bold);
    QTextLayout layout(result.text, font);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    if (line.isValid()) line.setLineWidth(1000000);
    layout.endLayout();
    if (!line.isValid() || line.textLength() != result.text.size()) { result.valid = false; return result; }
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    for (const QGlyphRun &run : layout.glyphRuns()) {
        const auto indexes = run.glyphIndexes();
        const auto positions = run.positions();
        for (qsizetype i = 0; i < indexes.size(); ++i) {
            if (indexes[i] == 0) { result.valid = false; return result; }
            const QPainterPath glyph = run.rawFont().pathForGlyph(indexes[i]);
            const auto space = run.rawFont().glyphIndexesForString(QStringLiteral(" "));
            if (glyph.isEmpty() && (space.isEmpty() || indexes[i] != space.front())) { result.valid = false; return result; }
            path.addPath(QTransform::fromTranslate(positions[i].x(), positions[i].y()).map(glyph));
        }
    }
    const QRectF bounds = path.boundingRect();
    if (bounds.isEmpty()) { result.valid = false; return result; }
    const double scale = std::min(0.1, 9.0 / bounds.height());
    result.outline = QTransform::fromScale(scale, -scale).map(path);
    result.width = result.outline.boundingRect().width();
    return result;
}

// Native PDF-point dimensions shared by appearance generation and hit testing.
inline QSizeF badgeSize(int order, const QString &label)
{
    if (order <= 0) return {};
    const BadgeLabel shaped = badgeLabel(order, label);
    if (!shaped.valid) return {};
    return QSizeF(std::max(14.0, shaped.width + 6.0), 14.0);
}
}

#endif
