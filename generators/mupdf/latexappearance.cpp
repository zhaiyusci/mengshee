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
Okular::NormalizedPoint rotatePoint(Okular::NormalizedPoint p, int angle)
{
    switch ((angle % 360 + 360) % 360) {
    case 90: return {1 - p.y, p.x};
    case 180: return {1 - p.x, 1 - p.y};
    case 270: return {p.y, 1 - p.x};
    default: return p;
    }
}
Okular::NormalizedRect rotateRect(const Okular::NormalizedRect &r, int angle)
{
    const auto a = rotatePoint({r.left, r.top}, angle);
    const auto b = rotatePoint({r.right, r.bottom}, angle);
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}
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
    const int pageRotation = pdf_dict_get_inheritable_int(ctx, pdf_annot_page(ctx, native)->obj, PDF_NAME(Rotate));
    const int delta = pageRotation - root.value(QStringLiteral("rotation")).toInt();
    const auto callout = root.value(QStringLiteral("callout")).toObject();
    const auto points = callout.value(QStringLiteral("pointsNorm")).toArray();
    if (points.size() == 3)
        for (int i = 0; i < 3; ++i) {
            const auto xy = points[i].toArray();
            if (xy.size() == 2)
                annotation->setLatexCalloutPoint(rotatePoint({xy[0].toDouble(), xy[1].toDouble()}, delta), i);
        }
    const auto box = callout.value(QStringLiteral("boxRectPt")).toArray();
    if (annotation->isLatexCallout() && box.size() == 4) {
        double w = bounds.x1 - bounds.x0, h = bounds.y1 - bounds.y0;
        if (delta % 180) std::swap(w, h);
        annotation->setBoundingRectangle(rotateRect({box[0].toDouble() / w, 1 - box[3].toDouble() / h, box[2].toDouble() / w, 1 - box[1].toDouble() / h}, delta));
    }
}

void write(fz_context *ctx, pdf_document *doc, pdf_annot *native, const Okular::StampAnnotation *annotation, pdf_obj *preserved, int viewRotation)
{
    const fz_rect bounds = pdf_bound_page(ctx, pdf_annot_page(ctx, native), FZ_CROP_BOX);
    const int pageRotation = pdf_dict_get_inheritable_int(ctx, pdf_annot_page(ctx, native)->obj, PDF_NAME(Rotate));
    const auto previous = QJsonDocument::fromJson(value(ctx, pdf_annot_obj(ctx, native), "LatexNoteData").toUtf8()).object();
    const int angle = (pageRotation - annotation->appearanceRotation() + 360) % 360;
    const int delta = angle - pageRotation;
    double w = bounds.x1 - bounds.x0, h = bounds.y1 - bounds.y0;
    if (delta % 180) std::swap(w, h);
    const auto r = rotateRect(annotation->boundingRectangle(), delta);
    const QRectF frame(QPointF(r.left * w, (1 - r.bottom) * h), QPointF(r.right * w, (1 - r.top) * h));
    QRectF outer = frame;
    QVector<QPointF> leader;
    if (annotation->isLatexCallout()) {
        for (int i = 0; i < 3; ++i) {
            auto p = rotatePoint(annotation->latexCalloutPoint(i), delta);
            leader.append({p.x * w, (1 - p.y) * h});
            // Include the complete OpenArrow and its stroke at any line width.
            const double margin = std::hypot(8.8, 4.5) * std::max(1.0, annotation->style().width()) + std::max(0.0, annotation->style().width());
            outer = outer.united(QRectF(leader.last() - QPointF(margin, margin), QSizeF(2 * margin, 2 * margin)));
        }
    }
    if (annotation->isNumberedCallout())
        outer = outer.united(orderedCalloutBadgeRect(frame, annotation->orderedCalloutNumber(), annotation->numberedCalloutLabel()));
    Options options;
    options.outerSize = outer.size();
    options.frameRect = frame.translated(-outer.topLeft());
    // Padding is measured from the frame, just as in the reflow width and
    // visual-size calculations. Adding the stroke here shifts content right
    // and down without reducing its width, consuming the opposite padding.
    options.contentFrameInset = std::isfinite(annotation->latexPadding()) && annotation->latexPadding() >= 0
        ? annotation->latexPadding() : Okular::LatexNoteGeometry::defaultPaddingPoints();
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
            if (auto *layout = pdf_dict_gets(ctx, sourcePage, "MengsheeLayoutBox"))
                pdf_dict_puts_drop(ctx, raw, "MengsheeLayoutBox", pdf_graft_object(ctx, doc, layout));
            fz_drop_buffer(ctx, content);
            content = nullptr;
        } else {
            auto *appearance = preserved ? preserved : pdf_dict_getp(ctx, pdf_annot_obj(ctx, native), "AP/N");
            auto *original = pdf_dict_getp(ctx, appearance, "Resources/XObject/Fm0");
            raw = pdf_keep_obj(ctx, pdf_is_stream(ctx, original) ? original : appearance);
            if (!pdf_is_stream(ctx, raw)) {
                // Register new notes while their asynchronous render is pending.
                // An empty source prevents synthesis of a stock Draft stamp.
                pdf_drop_obj(ctx, raw);
                raw = nullptr;
                content = fz_new_buffer(ctx, 0);
                raw = pdf_new_xobject(ctx, doc, fz_make_rect(0, 0, 1, 1), fz_identity, nullptr, content);
                fz_drop_buffer(ctx, content);
                content = nullptr;
            }
        }
        const auto sourceBox = qtRect(pdf_dict_get_rect(ctx, raw, PDF_NAME(BBox)));
        if (auto *layout = pdf_dict_gets(ctx, raw, "MengsheeLayoutBox"))
            options.sourceLayoutBox = qtRect(pdf_to_rect(ctx, layout));
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
        const auto nativeRect = rotateRect({outer.left() / w, 1 - outer.bottom() / h, outer.right() / w, 1 - outer.top() / h}, -delta);
        pdf_set_annot_rect(ctx, native, fz_make_rect(nativeRect.left * (bounds.x1 - bounds.x0), nativeRect.top * (bounds.y1 - bounds.y0),
                                                  nativeRect.right * (bounds.x1 - bounds.x0), nativeRect.bottom * (bounds.y1 - bounds.y0)));
        pdf_set_annot_appearance(ctx, native, "N", nullptr, fz_rotate(angle), fz_make_rect(0, 0, outer.width(), outer.height()), resources, content);
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
                       {QStringLiteral("fillColor"), (annotation->latexFillColor().isValid() ? annotation->latexFillColor() : QColor(Qt::transparent)).name(QColor::HexArgb)},
                       {QStringLiteral("borderColor"), annotation->latexBorderColor().name(QColor::HexArgb)},
                       {QStringLiteral("borderWidthPt"), annotation->style().width()}};
    QJsonObject root {{QStringLiteral("version"), 20260610},
                      {QStringLiteral("rotation"), angle},
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
            auto p = rotatePoint(annotation->latexCalloutPoint(i), delta);
            points.append(QJsonArray {p.x, p.y});
        }
        root.insert(QStringLiteral("callout"), QJsonObject {{QStringLiteral("pointsNorm"), points}, {QStringLiteral("boxRectPt"), QJsonArray {frame.left(), frame.top(), frame.right(), frame.bottom()}}});
    }
    putText(ctx, pdf_annot_obj(ctx, native), "LatexNoteData", QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}
}
