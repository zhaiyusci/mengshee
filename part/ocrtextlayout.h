/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef OKULAR_OCRTEXTLAYOUT_H
#define OKULAR_OCRTEXTLAYOUT_H

#include "core/generator.h"
#include <QFontMetricsF>
#include <algorithm>
#include <numeric>

namespace OcrTextLayout
{
struct Placement
{
    // Both values are fractions of the page height, independent of zoom.
    double fontSize = 0;
    double baseline = 0;
};

inline QFont font()
{
    return QFont(QStringLiteral("Arial"));
}

inline QList<Placement> arrange(const QList<Okular::OcrTextWord> &words)
{
    QList<Placement> result(words.size());
    QFont referenceFont = font();
    referenceFont.setPixelSize(1000);
    const QFontMetricsF metrics(referenceFont);
    QList<QRectF> glyphs;
    for (const auto &word : words) {
        glyphs.append(metrics.tightBoundingRect(word.text));
    }
    QList<int> order(words.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&words](int a, int b) { return words[a].rectangle.center().y() < words[b].rectangle.center().y(); });
    struct Line
    {
        QRectF anchor;
        QList<int> words;
    };
    QList<Line> lines;
    for (int index : order) {
        const QRectF rect = words[index].rectangle;
        int best = -1;
        double distance = 1;
        for (int line = lines.size() - 1; line >= 0; --line) {
            const QRectF anchor = lines[line].anchor;
            const double overlap = qMin(anchor.bottom(), rect.bottom()) - qMax(anchor.top(), rect.top());
            const double delta = qAbs(anchor.center().y() - rect.center().y());
            if (overlap > 0.5 * qMin(anchor.height(), rect.height()) && delta < 0.45 * qMax(anchor.height(), rect.height()) && delta < distance) {
                best = line;
                distance = delta;
            }
        }
        if (best < 0) {
            lines.append({rect, {index}});
        } else {
            lines[best].words.append(index);
        }
    }
    const auto median = [](QList<double> values) {
        std::sort(values.begin(), values.end());
        const int middle = values.size() / 2;
        return values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2;
    };
    for (const auto &line : lines) {
        QList<double> sizes;
        for (int index : line.words) {
            if (glyphs[index].height() > 0) {
                sizes.append(words[index].rectangle.height() * 1000 / glyphs[index].height());
            }
        }
        const double size = sizes.isEmpty() ? line.anchor.height() : median(sizes);
        QList<double> baselines;
        for (int index : line.words) {
            baselines.append(words[index].rectangle.top() - glyphs[index].top() * size / 1000);
        }
        const double baseline = median(baselines);
        for (int index : line.words) {
            result[index] = {size, baseline};
        }
    }
    return result;
}
}
#endif
