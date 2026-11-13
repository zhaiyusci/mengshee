#include <QPolygonF>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "aipageanalysis.h"
#include <KLocalizedString>
#include <QJsonArray>
#include <QJsonObject>
#include <QImage>
#include <QJsonDocument>
#include <QTextBoundaryFinder>
#include <QTextDocumentFragment>
#include <QRegularExpression>
#include <cmath>
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

// GLM can return a whole paragraph as one string. Recover printed baselines
// from the same page image submitted to the layout service.
static QList<QRect> printedRows(const QImage &gray, const QRect &box)
{
    QList<QRect> rows;
    QList<QRect> scanlines;
    QList<int> density;
    int peak = 0;
    for (int y = box.top(); y <= box.bottom(); ++y) {
        const auto *pixels = gray.constScanLine(y);
        int left = box.right(), right = box.left(), count = 0;
        for (int x = box.left(); x <= box.right(); ++x) {
            if (pixels[x] < 180) {
                left = std::min(x, left);
                right = x;
                ++count;
            }
        }
        density.append(count);
        peak = std::max(count, peak);
        scanlines.append(QRect(left, y, right - left + 1, 1));
    }
    // Sparse descenders and scan specks can bridge successive baselines.
    // Compare ink coverage with this block's strongest row.
    const int threshold = std::max(1, qRound(peak * .06));
    QRect band;
    for (qsizetype i = 0; i < density.size(); ++i) {
        if (density[i] >= threshold) {
            band |= scanlines[i];
        } else if (!band.isEmpty()) {
            rows.append(band);
            band = {};
        }
    }
    if (!band.isEmpty())
        rows.append(band);
    return rows;
}

static void appendTextLines(QList<Region> &result, const QImage &gray, const QRectF &region, QString content)
{
    static const QRegularExpression markup(QStringLiteral(R"(<(?:/?(?:p|div|span|sub|sup|strong|em|b|i|br|table|tr|td|th|font|u|li|ol|ul|h[1-6]))[\s/>])"), QRegularExpression::CaseInsensitiveOption);
    if (content.contains(markup))
        content = QTextDocumentFragment::fromHtml(content).toPlainText();
    const auto lines = content.trimmed().split(QChar::LineFeed, Qt::SkipEmptyParts);
    const QString text = content.simplified();
    if (text.isEmpty())
        return;
    const QRect box = QRectF(region.x() * gray.width(), region.y() * gray.height(), region.width() * gray.width(), region.height() * gray.height()).toAlignedRect().intersected(gray.rect());
    if (box.isEmpty())
        return;
    QList<QRect> rows = printedRows(gray, box);
    QTextBoundaryFinder boundaries(QTextBoundaryFinder::Line, text);
    QList<qsizetype> breaks;
    for (auto pos = boundaries.toNextBoundary(); pos >= 0; pos = boundaries.toNextBoundary())
        breaks.append(pos);
    if (breaks.isEmpty())
        breaks.append(text.size());
    const int estimate = qRound(std::sqrt(.55 * text.size() * box.height() / box.width()));
    if (rows.isEmpty() || rows.size() > breaks.size() || (rows.size() == 1 && (estimate > 2 || lines.size() > 1))) {
        // Fall back to explicit lines or a wrap estimate for noisy scans.
        const int n = std::clamp<int>(std::max<int>(lines.size(), estimate), 1, std::min<int>(box.height(), breaks.size()));
        rows.clear();
        for (int i = 0; i < n; ++i)
            rows.append(QRect(box.x(), box.y() + i * box.height() / n, box.width(), std::max(1, box.height() / n)));
    }
    qint64 remainingWidth = 0;
    for (const auto &row : rows)
        remainingWidth += row.width();
    qsizetype start = 0, nextBreak = 0;
    for (qsizetype i = 0; i < rows.size(); ++i) {
        const auto &row = rows[i];
        QString line;
        if (lines.size() == rows.size()) {
            line = lines[i].simplified();
        } else {
            const auto target = start + qRound64(double(text.size() - start) * row.width() / remainingWidth);
            const auto limit = breaks.size() - (rows.size() - i);
            while (nextBreak < limit && std::abs(breaks[nextBreak + 1] - target) <= std::abs(breaks[nextBreak] - target))
                ++nextBreak;
            const auto end = breaks[nextBreak++];
            line = text.mid(start, end - start).trimmed();
            start = end;
        }
        remainingWidth -= row.width();
        if (!line.isEmpty())
            result.append({QRectF(double(row.x()) / gray.width(), double(row.y()) / gray.height(),
                                  double(row.width()) / gray.width(), double(row.height()) / gray.height()), line});
    }
}

QList<Region> siliconRegions(const QString &answer, bool views, QString &error)
{
    static const QRegularExpression tag(QStringLiteral(R"(<\|ref\|>(.*?)<\|/ref\|>\s*<\|det\|>(.*?)<\|/det\|>)"), QRegularExpression::DotMatchesEverythingOption);
    QList<Block> blocks;
    QList<Region> words;
    auto matches = tag.globalMatch(answer);
    int count = 0;
    while (matches.hasNext()) {
        const auto match = matches.next();
        ++count;
        const auto text = match.captured(1).simplified();
        const auto boxes = QJsonDocument::fromJson(match.captured(2).toUtf8()).array();
        if (boxes.size() != 1 || text.isEmpty()) {
            error = i18n("The model returned invalid page coordinates.");
            return {};
        }
        const auto b = boxes.first().toArray();
        if (b.size() != 4 || !std::all_of(b.begin(), b.end(), [](QJsonValue v) { return v.isDouble() && v.toDouble() >= 0 && v.toDouble() <= 1000; })) {
            error = i18n("The model returned invalid page coordinates.");
            return {};
        }
        const QRectF r(QPointF(b[0].toDouble()/1000, b[1].toDouble()/1000), QPointF(b[2].toDouble()/1000, b[3].toDouble()/1000));
        if (!r.isValid()) {
            error = i18n("The model returned invalid page coordinates.");
            return {};
        }
        if (views)
            blocks.append({r, text == QLatin1String("image") || text == QLatin1String("table") || text == QLatin1String("figure"), text.contains(QLatin1String("caption"))});
        else
            words.append({r, text});
    }
    if (!count || count != answer.count(QLatin1String("<|det|>"))) {
        error = i18n("The model did not return the requested list of regions.");
        return {};
    }
    return views ? mergeBlocks(blocks) : words;
}

static QList<Region> parseLayout(const QJsonObject &response, QString &error, const QImage &gray)
{
    const auto pages = response.value(QStringLiteral("layout_details")).toArray();
    if (pages.size() != 1 || !pages.first().isArray() || pages.first().toArray().size() > 8000) {
        error = i18n("Invalid GLM-OCR page layout.");
        return {};
    }
    QList<Block> blocks;
    QList<Region> recognized;
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
        if (!gray.isNull()) {
            if (label != QLatin1String("text")) continue;
            appendTextLines(recognized, gray, r, obj.value(QStringLiteral("content")).toString());
            continue;
        }
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
    return gray.isNull() ? mergeBlocks(blocks) : recognized;
}
QList<Region> layoutTextLines(const QJsonObject &response, const QImage &image, QString &error)
{
    return parseLayout(response, error, image.convertToFormat(QImage::Format_Grayscale8));
}

QList<Region> mathpixViews(const QJsonObject &response, QString &error)
{
    const auto page = response.value(QStringLiteral("lines_json")).toObject();
    const double width = page.value(QStringLiteral("page_width")).toDouble();
    const double height = page.value(QStringLiteral("page_height")).toDouble();
    const auto lines = page.value(QStringLiteral("lines"));
    if (width <= 0 || height <= 0 || !lines.isArray()) {
        error = i18n("Mathpix did not return positioned text lines.");
        return {};
    }
    QList<Block> blocks;
    for (const auto &item : lines.toArray()) {
        const auto line = item.toObject();
        const auto type = line.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("page_info")) continue;
        const auto region = line.value(QStringLiteral("region")).toObject();
        QRectF r(region.value(QStringLiteral("top_left_x")).toDouble() / width, region.value(QStringLiteral("top_left_y")).toDouble() / height,
                 region.value(QStringLiteral("width")).toDouble() / width, region.value(QStringLiteral("height")).toDouble() / height);
        if (!r.isValid()) {
            QPolygonF contour;
            for (const auto &point : line.value(QStringLiteral("cnt")).toArray()) {
                const auto xy = point.toArray();
                if (xy.size() == 2 && xy[0].isDouble() && xy[1].isDouble())
                    contour.append({xy[0].toDouble() / width, xy[1].toDouble() / height});
            }
            r = contour.boundingRect();
        }
        r = r.intersected(QRectF(0, 0, 1, 1));
        if (r.isValid()) blocks.append({r, type == QLatin1String("diagram") || type == QLatin1String("chart") || type == QLatin1String("table"),
                                       type == QLatin1String("figure_label") || type.contains(QLatin1String("caption"))});
    }
    return mergeBlocks(blocks);
}

QList<Region> layoutViews(const QJsonObject &response, QString &error)
{
    return parseLayout(response, error, {});
}

}
