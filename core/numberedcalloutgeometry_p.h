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

inline BadgeLabel badgeLabel(int order, const QString &formatted)
{
    BadgeLabel result;
    result.text = formatted.isEmpty() ? QString::number(order) : formatted;
    if (result.text.size() > 4096 || QString::fromUtf8(result.text.toUtf8()) != result.text) { result.valid = false; return result; }
    for (QChar c : result.text) {
        if (c.unicode() < 32 || c.unicode() > 126) result.ascii = false;
    }
    if (result.ascii) {
        // Standard Helvetica-Bold WinAnsi advances, ASCII 32..126 (1/1000 em).
        static constexpr int widths[] = {
            278,333,474,556,556,889,722,238,333,333,389,584,278,333,278,278,
            556,556,556,556,556,556,556,556,556,556,333,333,584,584,584,611,
            975,722,722,722,722,667,611,778,722,278,556,722,611,833,722,778,
            667,778,722,667,611,722,667,944,667,667,611,333,278,333,584,556,
            333,556,611,556,611,556,333,611,611,278,278,556,278,889,611,611,
            611,611,389,556,333,611,556,778,556,556,500,389,280,389,584
        };
        static_assert(std::size(widths) == 95);
        int advance = 0;
        for (QChar c : result.text) advance += widths[c.unicode() - 32];
        result.width = advance * 9.0 / 1000.0;
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
