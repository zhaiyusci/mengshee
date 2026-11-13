// SPDX-License-Identifier: GPL-2.0-or-later
#include "aipageanalysis.h"
#include <KLocalizedString>
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>

namespace AiPageAnalysis
{
namespace
{
struct Block {
    QRectF rect;
    bool figure;
    bool caption;
};

// Estimate the inter-column gutter from facing text edges. A narrow centred
// equation on a single-column page is insufficient evidence for two columns.
double columnGutter(const QList<Block> &blocks)
{
    double best = 0, gutter = 0;
    for (int i = 30; i <= 70; ++i) {
        const double x = i / 100.0;
        double left = 0, right = 0, crossing = 0;
        for (const auto &b : blocks) {
            const auto &r = b.rect;
            if (b.figure || b.caption || r.width() < .17 || r.width() > .60)
                continue;
            if (r.right() <= x && x - r.right() < .10)
                left += r.height() * (1 - (x - r.right()) / .10);
            else if (r.left() >= x && r.left() - x < .10)
                right += r.height() * (1 - (r.left() - x) / .10);
            else if (r.left() < x && x < r.right())
                crossing += r.height();
        }
        const double score = std::min(left, right) - crossing;
        if (score > best) {
            best = score;
            gutter = x;
        }
    }
    return gutter;
}

QList<Region> mergeBlocks(const QList<Block> &blocks)
{
    const double gutter = columnGutter(blocks);
    QList<QRectF> bands;
    if (gutter) {
        // Join a figure's panels and caption before deciding whether it spans
        // the gutter. A caption ends its figure, even if the next one is close.
        QRectF panels;
        auto record = [&](const QRectF &r) {
            if (r.left() < gutter - .035 && r.right() > gutter + .035)
                bands.append(r);
        };
        for (qsizetype i = 0; i < blocks.size(); ++i) {
            const auto &b = blocks[i];
            const auto &r = b.rect;
            const bool adjacent = !panels.isEmpty() && r.top() <= panels.bottom() + .04 && r.bottom() >= panels.top() - .04;
            if (!adjacent) {
                record(panels);
                panels = {};
            }
            if (b.figure) {
                panels |= r;
                continue;
            }
            if (adjacent && (b.caption || (r.width() > .65 && r.height() < .10))) {
                record(panels | r);
                panels = {};
                continue;
            }
            record(panels);
            panels = {};
            if (b.caption && i + 1 < blocks.size() && blocks[i + 1].figure && std::abs(blocks[i + 1].rect.top() - r.bottom()) < .04) {
                panels = r;
            } else {
                record(r);
            }
        }
        record(panels);
    }
    std::sort(bands.begin(), bands.end(), [](const QRectF &a, const QRectF &b) { return a.top() < b.top(); });
    QList<QRectF> spans;
    for (auto band : bands) {
        if (!spans.isEmpty() && band.top() < spans.last().bottom())
            spans.last() |= band;
        else
            spans.append(band);
    }
    QList<QRectF> views(3 * spans.size() + 2);
    for (const auto &b : blocks) {
        qsizetype row = 0;
        while (row < spans.size() && b.rect.center().y() > spans[row].bottom())
            ++row;
        const bool inside = row < spans.size() && b.rect.center().y() >= spans[row].top();
        const auto slot = 3 * row + (inside ? 2 : int(gutter && b.rect.center().x() > gutter));
        views[slot] |= b.rect;
    }
    QList<Region> result;
    for (const auto &r : views) {
        if (!r.isEmpty())
            result.append({r.adjusted(-.004, -.004, .004, .004).intersected(QRectF(0, 0, 1, 1)), {}});
    }
    return result;
}
}

QList<Region> layoutViews(const QJsonObject &response, QString &error)
{
    const auto pages = response.value(QStringLiteral("layout_details")).toArray();
    if (pages.size() != 1 || !pages.first().isArray() || pages.first().toArray().size() > 8000) {
        error = i18n("Invalid GLM-OCR page layout.");
        return {};
    }
    QList<Block> blocks;
    for (const auto &item : pages.first().toArray()) {
        const auto obj = item.toObject();
        const auto coords = obj.value(QStringLiteral("bbox_2d")).toArray();
        const double w = obj.value(QStringLiteral("width")).toDouble(), h = obj.value(QStringLiteral("height")).toDouble();
        if (w <= 0 || h <= 0 || coords.size() != 4 || !std::all_of(coords.begin(), coords.end(), [](QJsonValue v) { return v.isDouble(); })) {
            error = i18n("The model returned invalid page coordinates.");
            return {};
        }
        const QRectF r(QPointF(coords[0].toDouble() / w, coords[1].toDouble() / h), QPointF(coords[2].toDouble() / w, coords[3].toDouble() / h));
        if (!r.isValid() || !QRectF(0, 0, 1, 1).contains(r)) {
            error = i18n("The model returned invalid page coordinates.");
            return {};
        }
        const auto label = obj.value(QStringLiteral("label")).toString();
        const auto native = obj.value(QStringLiteral("native_label")).toString();
        if (native.contains(QLatin1String("header")) || native.contains(QLatin1String("footer")) || native == QLatin1String("number"))
            continue;
        const bool caption = native.contains(QLatin1String("caption")) || native == QLatin1String("figure_title") || native == QLatin1String("table_title");
        // GLM-OCR can split a full-width caption into two columns. When
        // both follow the same spanning illustration, consume them together.
        if (caption && blocks.size() >= 2 && blocks.last().caption) {
            auto &left = blocks.last().rect;
            const auto &figure = blocks[blocks.size() - 2];
            const double overlap = std::min(left.bottom(), r.bottom()) - std::max(left.top(), r.top());
            if (figure.figure && figure.rect.right() > r.left() && std::abs(figure.rect.bottom() - left.top()) < .04
                && r.left() >= left.right() - .01 && r.left() - left.right() < .08 && overlap > .5 * std::min(left.height(), r.height())) {
                left |= r;
                continue;
            }
        }
        blocks.append({r, label == QLatin1String("image") || label == QLatin1String("table"), caption});
    }
    return mergeBlocks(blocks);
}
}
