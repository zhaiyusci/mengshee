// SPDX-License-Identifier: GPL-2.0-or-later
// Appearance drawing policy shared with the original backend; PDF objects are
// supplied by MuPDF in latexappearance.cpp.
#include "core/numberedcalloutgeometry_p.h"
#include "latexappearance.h"
#include <QPainterPath>
#include <algorithm>
#include <cmath>
namespace MuPDFLatex
{
using Okular::NumberedCalloutGeometry::BadgeLabel;
using Okular::NumberedCalloutGeometry::badgeLabel;
QRectF orderedCalloutBadgeRect(const QRectF &frame, int order, const QString &label)
{
    return order > 0 ? QRectF(QPointF(frame.left(), frame.bottom()), Okular::NumberedCalloutGeometry::badgeSize(order, label)) : QRectF();
}
QByteArray number(double value, int precision = 6)
{
    return QByteArray::number(value, 'f', precision);
}

void point(QByteArray &out, const QPointF &p, const char *op)
{
    out += number(p.x(), 2) + ' ' + number(p.y(), 2) + ' ' + op + '\n';
}

void color(QByteArray &out, const QColor &c, bool fill)
{
    out += number(c.redF(), 5) + ' ' + number(c.greenF(), 5) + ' ' + number(c.blueF(), 5) + (fill ? " rg\n" : " RG\n");
}

void boxPath(QByteArray &out, const QRectF &r, int precision)
{
    out += number(r.x(), precision) + ' ' + number(r.y(), precision) + ' ' + number(r.width(), precision) + ' ' + number(r.height(), precision) + " re\n";
}

QByteArray appearanceStream(const QRectF &sourceBox, const Options &options, int orderedNumber, const QString &formattedLabel)
{
    const QSizeF outer = options.outerSize.isValid() ? options.outerSize : sourceBox.size();
    const bool framed = options.frameRect.isValid();
    const QRectF frame = framed ? options.frameRect : QRectF(QPointF(0, 0), sourceBox.size());
    QPointF offset = options.contentOffset;
    if (framed && options.alignContentToFrameTopLeft) {
        offset = QPointF(frame.x() + options.contentFrameInset, frame.y() + frame.height() - options.contentFrameInset - sourceBox.height());
    }
    const QColor border = options.borderColor.isValid() ? options.borderColor : QColor(Qt::black);
    const bool visibleBorder = border.alpha() != 0;
    const bool stroke = visibleBorder && options.borderWidth > 0.0;
    const bool fill = options.fillColor.isValid() && options.fillColor.alpha() != 0;
    QByteArray out("/GS0 gs\n");

    // Same three-point leader and open-arrow geometry used by existing notes.
    // It is outside the source-only clip, including zero-width frame borders.
    if (options.leaderLine.size() == 3 && visibleBorder) {
        const double width = std::max(1.0, options.borderWidth);
        const QPointF p1 = options.leaderLine[0], p2 = options.leaderLine[1], p3 = options.leaderLine[2];
        out += "q\n";
        color(out, border, false);
        out += number(width, 2) + " w\n";
        point(out, p1, "m");
        point(out, p2, "l");
        point(out, p3, "l");
        out += "S\n";
        const double length = std::hypot(p2.x() - p1.x(), p2.y() - p1.y());
        if (length > 0.0) {
            const double size = std::min(std::max(6.0, 6.0 * width), length / 2.0);
            const double ux = (p2.x() - p1.x()) / length, uy = (p2.y() - p1.y()) / length;
            const double wing = size / std::sqrt(3.0);
            point(out, QPointF(p1.x() + size * ux + wing * uy, p1.y() + size * uy - wing * ux), "m");
            point(out, p1, "l");
            point(out, QPointF(p1.x() + size * ux - wing * uy, p1.y() + size * uy + wing * ux), "l");
            out += "S\n";
        }
        out += "Q\n";
    }
    if (framed && (fill || stroke)) {
        out += "q\n";
        if (stroke) {
            color(out, border, false);
            out += number(options.borderWidth, 2) + " w\n";
        }
        const double half = options.borderWidth / 2.0;
        boxPath(out, frame.adjusted(half, half, -half, -half), 2);
        if (fill) {
            color(out, options.fillColor, true);
        }
        out += fill ? (stroke ? "B\n" : "f\n") : "S\n";
        out += "Q\n";
    }

    const double inset = stroke ? options.borderWidth : 0.0;
    const QRectF inner = frame.adjusted(inset, inset, -inset, -inset);
    if (!framed || (inner.width() > 0.0 && inner.height() > 0.0)) {
        out += "q\n";
        if (framed) {
            boxPath(out, inner, 6);
            out += "W n\n";
        }
        out += "1 0 0 1 " + number(offset.x() - sourceBox.x()) + ' ' + number(offset.y() - sourceBox.y()) + " cm\n/Fm0 Do\nQ\n";
    }

    if (orderedNumber > 0) {
        const BadgeLabel shaped = badgeLabel(orderedNumber, formattedLabel);
        const QRectF badge = orderedCalloutBadgeRect(frame, orderedNumber, formattedLabel);
        const double fontSize = 9.0;
        const double textWidth = shaped.width;
        const double textX = badge.left() + (badge.width() - textWidth) / 2.0;
        const double baseline = badge.top() + (badge.height() - fontSize * 0.718) / 2.0;
        // Deliberately outside the source-only clip; never scale the body to
        // make room for the badge, and never bake it into the raw Fm0 source.
        const QColor textColor = options.fillColor.isValid() ? options.fillColor : QColor(Qt::transparent);
        out += "q\n/BadgeBoxGS gs\n";
        color(out, border, true);
        color(out, border, false);
        out += "0.75 w\n[] 0 d\n0 j\n";
        boxPath(out, badge.adjusted(0.375, 0.375, -0.375, -0.375), 6);
        out += "B\n/BadgeTextGS gs\n";
        color(out, textColor, true);
        if (shaped.ascii) {
            QByteArray literal = shaped.text.toLatin1();
            literal.replace("\\", "\\\\");
            literal.replace("(", "\\(");
            literal.replace(")", "\\)");
            out += "BT\n/OrderBadgeFont 9 Tf\n0 Tc 0 Tw 100 Tz 0 Ts 0 Tr\n1 0 0 1 " + number(textX) + ' ' + number(baseline) + " Tm\n(" + literal + ") Tj\nET\n";
        } else {
            const QRectF ink = shaped.outline.boundingRect();
            const QPainterPath path = QTransform::fromTranslate(badge.center().x() - ink.center().x(), badge.center().y() - ink.center().y()).map(shaped.outline);
            for (int i = 0; i < path.elementCount(); ++i) {
                const auto e = path.elementAt(i);
                if (e.isMoveTo() || e.isLineTo()) {
                    out += number(e.x) + ' ' + number(e.y) + (e.isMoveTo() ? " m\n" : " l\n");
                } else if (e.type == QPainterPath::CurveToElement) {
                    const auto c2 = path.elementAt(++i);
                    const auto end = path.elementAt(++i);
                    out += number(e.x) + ' ' + number(e.y) + ' ' + number(c2.x) + ' ' + number(c2.y) + ' ' + number(end.x) + ' ' + number(end.y) + " c\n";
                }
            }
            out += "f\n";
        }
        out += "Q\n";
    }

    return out;
}
}
