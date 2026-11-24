// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QRectF>
#include <core/annotations.h>
#include <mupdf/pdf.h>
namespace MuPDFLatex
{
struct Options {
    QSizeF outerSize;
    QPointF contentOffset;
    bool alignContentToFrameTopLeft = true;
    double contentFrameInset = 0;
    QRectF frameRect;
    double borderWidth = 0;
    QColor fillColor, borderColor;
    QVector<QPointF> leaderLine;
};
QByteArray appearanceStream(const QRectF &, const Options &, int, const QString &);
QRectF orderedCalloutBadgeRect(const QRectF &, int, const QString &);
void read(fz_context *, pdf_annot *, Okular::StampAnnotation *, fz_rect);
void write(fz_context *, pdf_document *, pdf_annot *, const Okular::StampAnnotation *, pdf_obj *preserved);
}
