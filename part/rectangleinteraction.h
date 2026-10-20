// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QLineF>
#include <QPainter>
#include <QRectF>
#include <array>
#include <limits>
#include <utility>

// Geometry lives in the caller's coordinate system. Handles and painting use
// viewport logical pixels; document conversion stays with each editing tool.
class RectangleInteraction
{
public:
    enum class Handle { None = 0, Top = 1, Right = 2, Bottom = 4, Left = 8, TopLeft = 9, TopRight = 3, BottomLeft = 12, BottomRight = 6, Move = 16 };
    static constexpr qreal handleSize = 8;
    static constexpr qreal handlePenWidth = 1;
    static constexpr std::array<Handle, 8> handles {Handle::TopLeft, Handle::Top, Handle::TopRight, Handle::Right, Handle::BottomRight, Handle::Bottom, Handle::BottomLeft, Handle::Left};

    static bool hasEdge(Handle handle, Handle edge)
    {
        return (int(handle) & int(edge)) != 0;
    }
    static QPointF center(const QRectF &rect, Handle handle)
    {
        return {hasEdge(handle, Handle::Left)        ? rect.left()
                    : hasEdge(handle, Handle::Right) ? rect.right()
                                                     : rect.center().x(),
                hasEdge(handle, Handle::Top)          ? rect.top()
                    : hasEdge(handle, Handle::Bottom) ? rect.bottom()
                                                      : rect.center().y()};
    }
    static QRectF handleRect(const QPointF &center)
    {
        return {center.x() - handleSize / 2, center.y() - handleSize / 2, handleSize, handleSize};
    }
    static QRectF grabRect(const QPointF &center)
    {
        const qreal halfPen = handlePenWidth / 2;
        return handleRect(center).adjusted(-halfPen, -halfPen, halfPen, halfPen);
    }
    static Handle hitTest(const QRectF &rect, const QPointF &point, bool resize = true, bool move = true)
    {
        if (!rect.isValid())
            return Handle::None;
        Handle best = Handle::None;
        qreal nearest = std::numeric_limits<qreal>::max();
        if (resize)
            for (Handle handle : handles) {
                const auto position = center(rect, handle);
                const auto distance = QLineF(position, point).length();
                if (grabRect(position).contains(point) && distance < nearest) {
                    best = handle;
                    nearest = distance;
                }
            }
        if (best != Handle::None)
            return best;
        return move && rect.contains(point) ? Handle::Move : Handle::None;
    }
    static Qt::CursorShape cursor(Handle handle)
    {
        switch (handle) {
        case Handle::TopLeft:
        case Handle::BottomRight:
            return Qt::SizeFDiagCursor;
        case Handle::TopRight:
        case Handle::BottomLeft:
            return Qt::SizeBDiagCursor;
        case Handle::Top:
        case Handle::Bottom:
            return Qt::SizeVerCursor;
        case Handle::Left:
        case Handle::Right:
            return Qt::SizeHorCursor;
        case Handle::Move:
            return Qt::SizeAllCursor;
        default:
            return Qt::ArrowCursor;
        }
    }
    static std::pair<QPointF, QPointF> adjustment(Handle handle, QPointF delta)
    {
        if (handle == Handle::Move)
            return {delta, delta};
        return {{hasEdge(handle, Handle::Left) ? delta.x() : 0, hasEdge(handle, Handle::Top) ? delta.y() : 0}, {hasEdge(handle, Handle::Right) ? delta.x() : 0, hasEdge(handle, Handle::Bottom) ? delta.y() : 0}};
    }
    static QRectF drag(QRectF initial, Handle handle, QPointF start, QPointF current, QSizeF minimum, const QRectF &bounds = {})
    {
        QPointF delta = current - start;
        if (handle == Handle::Move) {
            if (bounds.isValid()) {
                delta.setX(qBound(bounds.left() - initial.left(), delta.x(), bounds.right() - initial.right()));
                delta.setY(qBound(bounds.top() - initial.top(), delta.y(), bounds.bottom() - initial.bottom()));
            }
            return initial.translated(delta);
        }
        const auto [first, second] = adjustment(handle, delta);
        const qreal minWidth = qMin(minimum.width(), initial.width());
        const qreal minHeight = qMin(minimum.height(), initial.height());
        const qreal left = bounds.isValid() ? bounds.left() : -std::numeric_limits<qreal>::max();
        const qreal top = bounds.isValid() ? bounds.top() : -std::numeric_limits<qreal>::max();
        const qreal right = bounds.isValid() ? bounds.right() : std::numeric_limits<qreal>::max();
        const qreal bottom = bounds.isValid() ? bounds.bottom() : std::numeric_limits<qreal>::max();
        if (hasEdge(handle, Handle::Left))
            initial.setLeft(qBound(left, initial.left() + first.x(), initial.right() - minWidth));
        if (hasEdge(handle, Handle::Top))
            initial.setTop(qBound(top, initial.top() + first.y(), initial.bottom() - minHeight));
        if (hasEdge(handle, Handle::Right))
            initial.setRight(qBound(initial.left() + minWidth, initial.right() + second.x(), right));
        if (hasEdge(handle, Handle::Bottom))
            initial.setBottom(qBound(initial.top() + minHeight, initial.bottom() + second.y(), bottom));
        return initial;
    }
    // Scale about the opposite corner, or the opposite edge's midpoint.
    // A single scale factor also preserves physical aspect ratio when callers
    // use normalized page coordinates or rotate the page by quarter turns.
    static QRectF dragProportional(const QRectF &initial, Handle handle, QPointF delta, QSizeF minimum, const QRectF &bounds = {})
    {
        if (!initial.isValid() || handle == Handle::None || handle == Handle::Move)
            return drag(initial, handle, {}, delta, minimum, bounds);
        const bool horizontal = hasEdge(handle, Handle::Left) || hasEdge(handle, Handle::Right);
        const bool vertical = hasEdge(handle, Handle::Top) || hasEdge(handle, Handle::Bottom);
        const QPointF anchor(hasEdge(handle, Handle::Left) ? initial.right() : hasEdge(handle, Handle::Right) ? initial.left() : initial.center().x(),
                             hasEdge(handle, Handle::Top) ? initial.bottom() : hasEdge(handle, Handle::Bottom) ? initial.top() : initial.center().y());
        const qreal dx = (hasEdge(handle, Handle::Left) ? -delta.x() : delta.x()) / initial.width();
        const qreal dy = (hasEdge(handle, Handle::Top) ? -delta.y() : delta.y()) / initial.height();
        qreal scale = 1 + (horizontal && (!vertical || qAbs(dx) >= qAbs(dy)) ? dx : dy);
        const qreal minScale = qMin(qreal(1), qMax(minimum.width() / initial.width(), minimum.height() / initial.height()));
        qreal maxScale = std::numeric_limits<qreal>::max();
        if (bounds.isValid()) {
            if (anchor.x() > initial.left()) maxScale = qMin(maxScale, (anchor.x() - bounds.left()) / (anchor.x() - initial.left()));
            if (anchor.x() < initial.right()) maxScale = qMin(maxScale, (bounds.right() - anchor.x()) / (initial.right() - anchor.x()));
            if (anchor.y() > initial.top()) maxScale = qMin(maxScale, (anchor.y() - bounds.top()) / (anchor.y() - initial.top()));
            if (anchor.y() < initial.bottom()) maxScale = qMin(maxScale, (bounds.bottom() - anchor.y()) / (initial.bottom() - anchor.y()));
        }
        scale = qBound(qMin(minScale, maxScale), scale, maxScale);
        return QRectF(anchor + (initial.topLeft() - anchor) * scale, initial.size() * scale);
    }
    static QColor color()
    {
        return QColor(0, 120, 212);
    }
    struct Colors {
        QColor outline;
        QColor handleBorder;
        QColor handleFill;
    };
    static void drawHandle(QPainter *painter, const QPointF &position, const Colors &colors = {color(), color(), Qt::white})
    {
        painter->save();
        QPen pen(colors.handleBorder, handlePenWidth);
        pen.setCosmetic(true);
        painter->setPen(pen);
        painter->setBrush(colors.handleFill);
        painter->drawRect(handleRect(position));
        painter->restore();
    }
    static void draw(QPainter *painter, const QRectF &rect, bool resize = true, bool preview = false, const Colors &colors = {color(), color(), Qt::white})
    {
        painter->save();
        QPen pen(colors.outline, 2, preview ? Qt::DashLine : Qt::SolidLine);
        pen.setCosmetic(true);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(rect);
        if (resize)
            for (auto handle : handles)
                drawHandle(painter, center(rect, handle), colors);
        painter->restore();
    }
};
