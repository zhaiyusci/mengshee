// SPDX-License-Identifier: AGPL-3.0-or-later
#include "latexappearance.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <core/latexnotegeometry.h>

namespace MuPDFLatex
{
namespace
{
QRectF qtRect(fz_rect r)
{
    return QRectF(QPointF(r.x0, r.y0), QPointF(r.x1, r.y1));
}
QString value(fz_context *ctx, pdf_obj *obj, const char *key)
{
    return QString::fromUtf8(pdf_to_text_string(ctx, pdf_dict_gets(ctx, obj, key)));
}
void putText(fz_context *ctx, pdf_obj *obj, const char *key, const QString &text)
{
    const auto bytes = text.toUtf8();
    pdf_dict_puts_drop(ctx, obj, key, pdf_new_text_string(ctx, bytes.constData()));
}
void state(fz_context *ctx, pdf_document *doc, pdf_obj *states, const char *key, double alpha)
{
    pdf_obj *gs = pdf_new_dict(ctx, doc, 2);
    pdf_dict_puts_drop(ctx, states, key, gs);
    pdf_dict_put_real(ctx, gs, PDF_NAME(CA), alpha);
    pdf_dict_put_real(ctx, gs, PDF_NAME(ca), alpha);
}
}

void read(fz_context *ctx, pdf_annot *native, Okular::StampAnnotation *annotation, fz_rect bounds)
{
    const auto root = QJsonDocument::fromJson(value(ctx, pdf_annot_obj(ctx, native), "LatexNoteData").toUtf8()).object();
    if (root.value(QStringLiteral("version")).toInt() != 20260610)
        return;
    const auto type = root.value(QStringLiteral("type")).toString();
    annotation->setOkularLatex(true);
    annotation->setStampIconName(QStringLiteral("latex-notes"));
    annotation->setLatexNoteType(type == QLatin1String("boxed")                                                                ? Okular::Annotation::LatexNoteBoxed
                                     : type == QLatin1String("callout")                                                        ? Okular::Annotation::LatexNoteCallout
                                     : (type == QLatin1String("numbered-callout") || type == QLatin1String("ordered-callout")) ? Okular::Annotation::LatexNoteNumberedCallout
                                                                                                                               : Okular::Annotation::LatexNotePlain);
    annotation->setOrderedCalloutNumber(root.value(QStringLiteral("order")).toInt());
    annotation->setNumberedCalloutId(root.value(QStringLiteral("id")).toInt(annotation->orderedCalloutNumber()));
    annotation->setNumberedCalloutLabel(root.value(QStringLiteral("label")).toString());
    const auto layout = root.value(QStringLiteral("layout")).toObject();
    annotation->setLatexLayoutWidth(layout.value(QStringLiteral("widthPt")).toDouble());
    annotation->setLatexPadding(layout.value(QStringLiteral("paddingPt")).toDouble(Okular::LatexNoteGeometry::defaultPaddingPoints()));
    const auto style = root.value(QStringLiteral("style")).toObject();
    annotation->setLatexFontSize(style.value(QStringLiteral("fontSizePt")).toDouble());
    annotation->setLatexTextColor(QColor(style.value(QStringLiteral("textColor")).toString()));
    annotation->setLatexFillColor(QColor(style.value(QStringLiteral("fillColor")).toString()));
    annotation->setLatexBorderColor(QColor(style.value(QStringLiteral("borderColor")).toString()));
    annotation->style().setWidth(style.value(QStringLiteral("borderWidthPt")).toDouble());
    const auto callout = root.value(QStringLiteral("callout")).toObject();
    const auto points = callout.value(QStringLiteral("pointsNorm")).toArray();
    if (points.size() == 3)
        for (int i = 0; i < 3; ++i) {
            const auto xy = points[i].toArray();
            if (xy.size() == 2)
                annotation->setLatexCalloutPoint({xy[0].toDouble(), xy[1].toDouble()}, i);
        }
    const auto box = callout.value(QStringLiteral("boxRectPt")).toArray();
    if (annotation->isLatexCallout() && box.size() == 4) {
        const double w = bounds.x1 - bounds.x0, h = bounds.y1 - bounds.y0;
        annotation->setBoundingRectangle({box[0].toDouble() / w, 1 - box[3].toDouble() / h, box[2].toDouble() / w, 1 - box[1].toDouble() / h});
    }
}

void write(fz_context *ctx, pdf_document *doc, pdf_annot *native, const Okular::StampAnnotation *annotation, pdf_obj *preserved)
{
    const fz_rect bounds = pdf_bound_page(ctx, pdf_annot_page(ctx, native), FZ_CROP_BOX);
    const double w = bounds.x1 - bounds.x0, h = bounds.y1 - bounds.y0;
    const auto r = annotation->boundingRectangle();
    const QRectF frame(QPointF(r.left * w, (1 - r.bottom) * h), QPointF(r.right * w, (1 - r.top) * h));
    QRectF outer = frame;
    QVector<QPointF> leader;
    if (annotation->isLatexCallout()) {
        for (int i = 0; i < 3; ++i) {
            auto p = annotation->latexCalloutPoint(i);
            leader.append({p.x * w, (1 - p.y) * h});
            outer = outer.united(QRectF(leader.last() - QPointF(8, 8), QSizeF(16, 16)));
        }
    }
    if (annotation->isNumberedCallout())
        outer = outer.united(orderedCalloutBadgeRect(frame, annotation->orderedCalloutNumber(), annotation->numberedCalloutLabel()));
    Options options;
    options.outerSize = outer.size();
    options.frameRect = frame.translated(-outer.topLeft());
    options.contentFrameInset = std::max(0.0, annotation->latexPadding()) + std::max(0.0, annotation->style().width());
    options.fillColor = annotation->latexFillColor();
    options.borderColor = annotation->latexBorderColor().isValid() ? annotation->latexBorderColor() : QColor(Qt::black);
    options.borderWidth = annotation->style().width();
    if (annotation->latexNoteType() == Okular::Annotation::LatexNotePlain) {
        options.fillColor = Qt::transparent;
        options.borderColor = Qt::transparent;
        options.borderWidth = 0;
    }
    for (auto p : leader)
        options.leaderLine.append(p - outer.topLeft());
    pdf_document *source = nullptr;
    pdf_obj *raw = nullptr, *resources = nullptr, *sourceResources = nullptr;
    fz_buffer *content = nullptr, *part = nullptr;
    fz_var(source);
    fz_var(raw);
    fz_var(resources);
    fz_var(sourceResources);
    fz_var(content);
    fz_var(part);
    const QByteArray sourceName = annotation->latexAppearancePdfFileName().toUtf8();
    fz_try(ctx)
    {
        if (!sourceName.isEmpty() && QFileInfo::exists(annotation->latexAppearancePdfFileName())) {
            source = pdf_open_document(ctx, sourceName.constData());
            if (pdf_count_pages(ctx, source) != 1 || pdf_needs_password(ctx, source))
                fz_throw(ctx, FZ_ERROR_FORMAT, "Invalid LaTeX source PDF");
            auto *sourcePage = pdf_lookup_page_obj(ctx, source, 0);
            const auto sourceBox = pdf_dict_get_rect(ctx, sourcePage, PDF_NAME(MediaBox));
            sourceResources = pdf_graft_object(ctx, doc, pdf_dict_get_inheritable(ctx, sourcePage, PDF_NAME(Resources)));
            content = fz_new_buffer(ctx, 0);
            auto *streams = pdf_dict_get(ctx, sourcePage, PDF_NAME(Contents));
            const int count = pdf_is_array(ctx, streams) ? pdf_array_len(ctx, streams) : 1;
            for (int i = 0; i < count; ++i) {
                auto *stream = pdf_is_array(ctx, streams) ? pdf_array_get(ctx, streams, i) : streams;
                if (!pdf_is_stream(ctx, stream))
                    continue;
                part = pdf_load_stream(ctx, stream);
                fz_append_buffer(ctx, content, part);
                fz_append_byte(ctx, content, '\n');
                fz_drop_buffer(ctx, part);
                part = nullptr;
            }
            raw = pdf_new_xobject(ctx, doc, sourceBox, fz_identity, sourceResources, content);
            fz_drop_buffer(ctx, content);
            content = nullptr;
        } else {
            auto *appearance = preserved ? preserved : pdf_dict_getp(ctx, pdf_annot_obj(ctx, native), "AP/N");
            auto *original = pdf_dict_getp(ctx, appearance, "Resources/XObject/Fm0");
            raw = pdf_keep_obj(ctx, pdf_is_stream(ctx, original) ? original : appearance);
            if (!pdf_is_stream(ctx, raw))
                fz_throw(ctx, FZ_ERROR_FORMAT, "LaTeX annotation has no source appearance");
        }
        const auto sourceBox = qtRect(pdf_dict_get_rect(ctx, raw, PDF_NAME(BBox)));
        const auto bytes = appearanceStream(sourceBox, options, annotation->orderedCalloutNumber(), annotation->numberedCalloutLabel());
        resources = pdf_new_dict(ctx, doc, 3);
        auto *forms = pdf_dict_put_dict(ctx, resources, PDF_NAME(XObject), 1);
        pdf_dict_puts(ctx, forms, "Fm0", raw);
        auto *states = pdf_dict_put_dict(ctx, resources, PDF_NAME(ExtGState), 3);
        const double opacity = annotation->style().opacity();
        state(ctx, doc, states, "GS0", opacity);
        state(ctx, doc, states, "BadgeBoxGS", opacity * options.borderColor.alphaF());
        state(ctx, doc, states, "BadgeTextGS", opacity * (options.fillColor.isValid() ? options.fillColor.alphaF() : 0));
        auto *fonts = pdf_dict_put_dict(ctx, resources, PDF_NAME(Font), 1);
        auto *font = pdf_new_dict(ctx, doc, 4);
        pdf_dict_puts_drop(ctx, fonts, "OrderBadgeFont", font);
        pdf_dict_put_name(ctx, font, PDF_NAME(Type), "Font");
        pdf_dict_put_name(ctx, font, PDF_NAME(Subtype), "Type1");
        pdf_dict_put_name(ctx, font, PDF_NAME(BaseFont), "Helvetica-Bold");
        pdf_dict_put_name(ctx, font, PDF_NAME(Encoding), "WinAnsiEncoding");
        content = fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char *>(bytes.constData()), bytes.size());
        pdf_set_annot_rect(ctx, native, fz_make_rect(outer.left(), h - outer.bottom(), outer.right(), h - outer.top()));
        pdf_set_annot_appearance(ctx, native, "N", nullptr, fz_identity, fz_make_rect(0, 0, outer.width(), outer.height()), resources, content);
    }
    fz_always(ctx)
    {
        fz_drop_buffer(ctx, part);
        fz_drop_buffer(ctx, content);
        pdf_drop_obj(ctx, sourceResources);
        pdf_drop_obj(ctx, resources);
        pdf_drop_obj(ctx, raw);
        pdf_drop_document(ctx, source);
    }
    fz_catch(ctx)
    {
        fz_rethrow(ctx);
    }
    QJsonObject layout {{QStringLiteral("widthPt"), annotation->latexLayoutWidth()}, {QStringLiteral("paddingPt"), annotation->latexPadding()}};
    QJsonObject style {{QStringLiteral("fontSizePt"), annotation->latexFontSize()},
                       {QStringLiteral("textColor"), annotation->latexTextColor().name(QColor::HexArgb)},
                       {QStringLiteral("fillColor"), annotation->latexFillColor().name(QColor::HexArgb)},
                       {QStringLiteral("borderColor"), annotation->latexBorderColor().name(QColor::HexArgb)},
                       {QStringLiteral("borderWidthPt"), annotation->style().width()}};
    QJsonObject root {{QStringLiteral("version"), 20260610},
                      {QStringLiteral("type"),
                       annotation->isNumberedCallout()                                         ? QStringLiteral("numbered-callout")
                           : annotation->isLatexCallout()                                      ? QStringLiteral("callout")
                           : annotation->latexNoteType() == Okular::Annotation::LatexNoteBoxed ? QStringLiteral("boxed")
                                                                                               : QStringLiteral("plain")},
                      {QStringLiteral("layout"), layout},
                      {QStringLiteral("style"), style}};
    if (annotation->isNumberedCallout()) {
        root.insert(QStringLiteral("id"), annotation->numberedCalloutId());
        root.insert(QStringLiteral("order"), annotation->orderedCalloutNumber());
        root.insert(QStringLiteral("label"), annotation->numberedCalloutLabel());
    }
    if (annotation->isLatexCallout()) {
        QJsonArray points;
        for (int i = 0; i < 3; ++i) {
            auto p = annotation->latexCalloutPoint(i);
            points.append(QJsonArray {p.x, p.y});
        }
        root.insert(QStringLiteral("callout"), QJsonObject {{QStringLiteral("pointsNorm"), points}, {QStringLiteral("boxRectPt"), QJsonArray {frame.left(), frame.top(), frame.right(), frame.bottom()}}});
    }
    putText(ctx, pdf_annot_obj(ctx, native), "LatexNoteData", QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}
}
