/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef OKULAR_READINGVIEWLAYOUT_P_H
#define OKULAR_READINGVIEWLAYOUT_P_H

#include <QList>
#include <QRectF>

#include <algorithm>
#include <cmath>

namespace Okular::ReadingViewLayout
{
// Internal geometry-only input. Coordinates are normalized to the analysis page,
// with a top-left origin. Values match Tesseract PolyBlockType without linking it.
struct Block {
    enum Type {
        Unknown = 0,
        FlowingText = 1,
        HeadingText = 2,
        PulloutText = 3,
        Equation = 4,
        InlineEquation = 5,
        Table = 6,
        VerticalText = 7,
        CaptionText = 8,
        FlowingImage = 9,
        HeadingImage = 10,
        PulloutImage = 11,
        HorizontalLine = 12,
        VerticalLine = 13,
        Noise = 14
    };

    QRectF rectangle;
    int type = Unknown;
};

namespace Detail
{
inline bool isText(int type)
{
    return type == Block::FlowingText || type == Block::HeadingText || type == Block::PulloutText || type == Block::CaptionText;
}

inline bool leavesSpanningRegion(const QList<Block> &blocks, qsizetype index, qsizetype first, const QRectF &group, qreal contentWidth)
{
    const Block &block = blocks[index];
    const QRectF &box = block.rectangle;
    const qreal width = box.width();
    if (!isText(block.type) || width < .28 * contentWidth || width > .65 * contentWidth) {
        return false;
    }
    // Do not split on a short table note or inside a tall existing block.
    if (box.height() < .01 || group.bottom() > box.top() + .003) {
        return false;
    }
    // A peer can occur after the entire first lane in iterator order.
    for (qsizetype peerIndex = index + 1; peerIndex < blocks.size(); ++peerIndex) {
        const Block &peer = blocks[peerIndex];
        const QRectF &p = peer.rectangle;
        const qreal peerWidth = p.width();
        if (!isText(peer.type) || peerWidth < .28 * contentWidth || peerWidth > .65 * contentWidth) {
            continue;
        }
        if (p.height() < .01 || width / peerWidth < .65 || width / peerWidth > 1 / .65) {
            continue;
        }
        const qreal overlap = std::min(box.bottom(), p.bottom()) - std::max(box.top(), p.top());
        if (overlap < .25 * std::min(box.height(), p.height())) {
            continue;
        }
        if (std::min(box.right(), p.right()) - std::max(box.left(), p.left()) > 0) {
            continue;
        }
        const qreal leftCenter = std::min(box.center().x(), p.center().x());
        const qreal rightCenter = std::max(box.center().x(), p.center().x());
        for (qsizetype priorIndex = index; priorIndex > first;) {
            const QRectF &prior = blocks[--priorIndex].rectangle;
            // Critically, classify ORIGINAL widths, never the accumulated union.
            if (prior.width() >= 1.25 * std::max(width, peerWidth) && prior.left() <= leftCenter && prior.right() >= rightCenter && prior.bottom() <= box.top() + .003) {
                return true;
            }
        }
    }
    return false;
}
}

/**
 * Merge contiguous iterator intervals, without sorting or assigning columns.
 * Port of order_deskew.py merge_in_order, including original-width spanning
 * region detection. All block types participate in unions and lane transitions;
 * only the four text types above provide evidence for leaving a spanning region.
 *
 * Invalid/nonfinite, non-positive, and wholly off-page rectangles are ignored.
 * Otherwise geometry is kept unchanged for classification. Each output union
 * gets .006 normalized page padding on all sides, then is clipped to [0,1]^2.
 * Outputs follow the surviving input sequence. No deskew or mapping is done here.
 */
inline QList<QRectF> merge(const QList<Block> &input)
{
    const QRectF page(0, 0, 1, 1);
    QList<Block> blocks;
    blocks.reserve(input.size());
    QRectF content;
    for (const Block &block : input) {
        const QRectF &box = block.rectangle;
        if (!std::isfinite(box.left()) || !std::isfinite(box.top()) || !std::isfinite(box.right()) || !std::isfinite(box.bottom()) || !std::isfinite(box.width()) || !std::isfinite(box.height()) || !box.isValid()
            || !box.intersects(page)) {
            continue;
        }
        content = blocks.isEmpty() ? box : content.united(box);
        blocks.append(block);
    }
    if (blocks.isEmpty()) {
        return {};
    }

    QList<QRectF> result;
    qsizetype first = 0;
    QRectF group;
    bool allHeadings = true;
    const auto appendGroup = [&]() {
        result.append(group.adjusted(-.006, -.006, .006, .006).intersected(page));
    };
    for (qsizetype index = 0; index < blocks.size(); ++index) {
        const Block &block = blocks[index];
        const QRectF &box = block.rectangle;
        if (index > first) {
            const qreal overlap = std::max(qreal(0), std::min(group.right(), box.right()) - std::max(group.left(), box.left()));
            const bool leavesSpan = Detail::leavesSpanningRegion(blocks, index, first, group, content.width());
            const bool changesLane = overlap / std::max(qreal(1e-9), std::min(box.width(), group.width())) < .2 && box.top() < group.bottom() - .03;
            const bool leavesHeading = allHeadings && block.type == Block::FlowingText && group.width() > .6 && box.width() > .2 && box.width() < .72 * group.width();
            if (leavesSpan || changesLane || leavesHeading) {
                appendGroup();
                first = index;
            }
        }
        if (index == first) {
            group = box;
            allHeadings = block.type == Block::HeadingText;
        } else {
            group = group.united(box);
            allHeadings = allHeadings && block.type == Block::HeadingText;
        }
    }
    appendGroup();
    return result;
}
}

#endif
