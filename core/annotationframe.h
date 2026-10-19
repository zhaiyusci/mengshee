// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef OKULAR_ANNOTATIONFRAME_H
#define OKULAR_ANNOTATIONFRAME_H
#include "annotations.h"
#include "page.h"
#include <QTransform>
namespace Okular
{
// Work in physical page points, never rotate normalized coordinates on a
// nonsquare page. Reader rotation is applied only by the view afterwards.
class AnnotationFrame
{
public:
    static QSizeF pageSize(const Page *page,QSizeF dpi) {
        if(!page)return {};
        QSizeF size(page->width(),page->height());
        if(int(page->rotation())%2)size.transpose();
        return {size.width()*72.0/dpi.width(),size.height()*72.0/dpi.height()};
    }
    static QSizeF localSize(const NormalizedRect &rect, QSizeF page, int angle)
    {
        QSizeF size(rect.width() * page.width(), rect.height() * page.height());
        QTransform inverse;
        inverse.rotate(-angle);
        return inverse.mapRect(QRectF(QPointF(), size)).size();
    }
    static QTransform rotationAbout(const NormalizedRect &rect, QSizeF page, int angle)
    {
        QTransform transform;
        transform.translate((rect.left + rect.right) * page.width() / 2, (rect.top + rect.bottom) * page.height() / 2);
        transform.rotate(angle);
        transform.translate(-(rect.left + rect.right) * page.width() / 2, -(rect.top + rect.bottom) * page.height() / 2);
        return transform;
    }
    static NormalizedPoint mapPoint(NormalizedPoint point, QSizeF page, const QTransform &transform)
    {
        const auto mapped = transform.map(QPointF(point.x * page.width(), point.y * page.height()));
        return {mapped.x() / page.width(), mapped.y() / page.height()};
    }
    static NormalizedRect mapRect(const NormalizedRect &rect, QSizeF page, const QTransform &transform)
    {
        const auto mapped = transform.mapRect(QRectF(rect.left * page.width(), rect.top * page.height(), rect.width() * page.width(), rect.height() * page.height()));
        return {mapped.left() / page.width(), mapped.top() / page.height(), mapped.right() / page.width(), mapped.bottom() / page.height()};
    }
};
}
#endif
