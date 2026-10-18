// SPDX-License-Identifier: AGPL-3.0-or-later
#include "annots.h"
#include "generator_mupdf.h"
#include "latexappearance.h"
#include <QDateTime>
#include <QDomDocument>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QUuid>
#include <core/page.h>

namespace
{
struct AppearanceTransfer {
    QByteArray pdf;
};
Okular::NormalizedPoint normalized(fz_point p, fz_rect box)
{
    return {(p.x - box.x0) / (box.x1 - box.x0), (p.y - box.y0) / (box.y1 - box.y0)};
}
fz_point point(Okular::NormalizedPoint p, fz_rect box)
{
    return {float(box.x0 + p.x * (box.x1 - box.x0)), float(box.y0 + p.y * (box.y1 - box.y0))};
}
QString textValue(fz_context *ctx, pdf_obj *dict, const char *key)
{
    return QString::fromUtf8(pdf_to_text_string(ctx, pdf_dict_gets(ctx, dict, key)));
}
void putText(fz_context *ctx, pdf_obj *dict, const char *key, const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    pdf_dict_puts_drop(ctx, dict, key, pdf_new_text_string(ctx, bytes.constData()));
}
enum pdf_annot_type typeFor(const Okular::Annotation *annotation)
{
    switch (annotation->subType()) {
    case Okular::Annotation::AText:
        return static_cast<const Okular::TextAnnotation *>(annotation)->textType() == Okular::TextAnnotation::Linked ? PDF_ANNOT_TEXT : PDF_ANNOT_FREE_TEXT;
    case Okular::Annotation::ALine:
        return static_cast<const Okular::LineAnnotation *>(annotation)->linePoints().size() == 2 ? PDF_ANNOT_LINE : PDF_ANNOT_POLY_LINE;
    case Okular::Annotation::AGeom:
        return static_cast<const Okular::GeomAnnotation *>(annotation)->geometricalType() == Okular::GeomAnnotation::InscribedCircle ? PDF_ANNOT_CIRCLE : PDF_ANNOT_SQUARE;
    case Okular::Annotation::AHighlight: {
        const auto type = static_cast<const Okular::HighlightAnnotation *>(annotation)->highlightType();
        if (type == Okular::HighlightAnnotation::Underline)
            return PDF_ANNOT_UNDERLINE;
        if (type == Okular::HighlightAnnotation::StrikeOut)
            return PDF_ANNOT_STRIKE_OUT;
        if (type == Okular::HighlightAnnotation::Squiggly)
            return PDF_ANNOT_SQUIGGLY;
        return PDF_ANNOT_HIGHLIGHT;
    }
    case Okular::Annotation::AInk:
        return PDF_ANNOT_INK;
    case Okular::Annotation::ACaret:
        return PDF_ANNOT_CARET;
    default:
        return PDF_ANNOT_STAMP;
    }
}
}

MuPDFAnnotationProxy::~MuPDFAnnotationProxy()
{
    clear();
}
void MuPDFAnnotationProxy::clear()
{
    auto *ctx = m_generator->m_context;
    for (auto *annotation : m_annotations)
        pdf_drop_annot(ctx, annotation);
    for (auto *object : m_removed)
        pdf_drop_obj(ctx, object);
    m_annotations.clear();
    m_removed.clear();
}

void MuPDFAnnotationProxy::load(pdf_page *page, Okular::Page *target)
{
    auto *ctx = m_generator->m_context;
    const fz_rect bounds = pdf_bound_page(ctx, page, FZ_CROP_BOX);
    for (auto *native = pdf_first_annot(ctx, page); native; native = pdf_next_annot(ctx, native)) {
        Okular::Annotation *annotation = nullptr;
        fz_var(annotation);
        fz_try(ctx)
        {
            loadOne(native, target, bounds, annotation);
        }
        fz_catch(ctx)
        {
            delete annotation;
            fz_rethrow_if(ctx, FZ_ERROR_SYSTEM);
            m_generator->reportAnnotationError(QString::fromUtf8(fz_caught_message(ctx)));
        }
    }
}

// Keep C++ conversion temporaries out of the setjmp frame: MSVC longjmp
// unwinds them in this separate frame before returning to the annotation catch.
Q_NEVER_INLINE void MuPDFAnnotationProxy::loadOne(pdf_annot *native, Okular::Page *target, fz_rect bounds, Okular::Annotation *&annotation)
{
    auto *ctx = m_generator->m_context;
    QDomDocument stored;
    QString serialized;
    auto *object = pdf_annot_obj(ctx, native);
    serialized = textValue(ctx, object, "MengsheeAnnotation");
    if (!serialized.isEmpty() && stored.setContent(serialized))
        annotation = Okular::AnnotationUtils::createAnnotation(stored.documentElement());
    const auto type = pdf_annot_type(ctx, native);
    if (!annotation) {
        switch (type) {
        case PDF_ANNOT_TEXT:
        case PDF_ANNOT_FREE_TEXT: {
            auto *text = new Okular::TextAnnotation;
            text->setTextType(type == PDF_ANNOT_TEXT ? Okular::TextAnnotation::Linked : Okular::TextAnnotation::InPlace);
            text->setTemplateNoteData(textValue(ctx, object, "TemplateNoteData"));
            annotation = text;
            break;
        }
        case PDF_ANNOT_HIGHLIGHT:
        case PDF_ANNOT_UNDERLINE:
        case PDF_ANNOT_STRIKE_OUT:
        case PDF_ANNOT_SQUIGGLY: {
            auto *highlight = new Okular::HighlightAnnotation;
            annotation = highlight;
            if (pdf_annot_quad_point_count(ctx, native) == 0)
                fz_throw(ctx, FZ_ERROR_FORMAT, "Text markup annotation has no quadrilaterals");
            highlight->setHighlightType(type == PDF_ANNOT_UNDERLINE        ? Okular::HighlightAnnotation::Underline
                                            : type == PDF_ANNOT_STRIKE_OUT ? Okular::HighlightAnnotation::StrikeOut
                                            : type == PDF_ANNOT_SQUIGGLY   ? Okular::HighlightAnnotation::Squiggly
                                                                           : Okular::HighlightAnnotation::Highlight);
            for (int i = 0; i < pdf_annot_quad_point_count(ctx, native); ++i) {
                const fz_quad quad = pdf_annot_quad_point(ctx, native, i);
                Okular::HighlightAnnotation::Quad q;
                q.setPoint(normalized(quad.ll, bounds), 0);
                q.setPoint(normalized(quad.lr, bounds), 1);
                q.setPoint(normalized(quad.ur, bounds), 2);
                q.setPoint(normalized(quad.ul, bounds), 3);
                highlight->highlightQuads().append(q);
            }
            annotation = highlight;
            break;
        }
        case PDF_ANNOT_SQUARE:
        case PDF_ANNOT_CIRCLE: {
            auto *geometry = new Okular::GeomAnnotation;
            geometry->setGeometricalType(type == PDF_ANNOT_CIRCLE ? Okular::GeomAnnotation::InscribedCircle : Okular::GeomAnnotation::InscribedSquare);
            annotation = geometry;
            break;
        }
        case PDF_ANNOT_LINE: {
            auto *line = new Okular::LineAnnotation;
            annotation = line;
            fz_point a, b;
            pdf_annot_line(ctx, native, &a, &b);
            line->setLinePoints({normalized(a, bounds), normalized(b, bounds)});
            annotation = line;
            break;
        }
        case PDF_ANNOT_INK: {
            auto *ink = new Okular::InkAnnotation;
            annotation = ink;
            QList<QList<Okular::NormalizedPoint>> strokes;
            for (int i = 0; i < pdf_annot_ink_list_count(ctx, native); ++i) {
                QList<Okular::NormalizedPoint> stroke;
                for (int j = 0; j < pdf_annot_ink_list_stroke_count(ctx, native, i); ++j)
                    stroke.append(normalized(pdf_annot_ink_list_stroke_vertex(ctx, native, i, j), bounds));
                strokes.append(stroke);
            }
            ink->setInkPaths(strokes);
            annotation = ink;
            break;
        }
        case PDF_ANNOT_STAMP:
            annotation = new Okular::StampAnnotation;
            break;
        case PDF_ANNOT_CARET:
            annotation = new Okular::CaretAnnotation;
            break;
        default:
            break; // Unknown annotations remain intact in the PDF.
        }
    }
    if (annotation) {
        const auto rect = pdf_annot_has_rect(ctx, native) ? pdf_annot_rect(ctx, native) : pdf_bound_annot(ctx, native);
        const auto a = normalized({rect.x0, rect.y0}, bounds), b = normalized({rect.x1, rect.y1}, bounds);
        annotation->setBoundingRectangle({a.x, a.y, b.x, b.y});
        annotation->setContents(QString::fromUtf8(pdf_annot_contents(ctx, native)));
        annotation->setAuthor(QString::fromUtf8(pdf_annot_author(ctx, native)));
        annotation->setUniqueName(textValue(ctx, object, "NM"));
        annotation->setCreationDate(QDateTime::fromSecsSinceEpoch(pdf_annot_creation_date(ctx, native)));
        annotation->setModificationDate(QDateTime::fromSecsSinceEpoch(pdf_annot_modification_date(ctx, native)));
        int n = 0;
        float color[4] = {};
        pdf_annot_color(ctx, native, &n, color);
        if (n == 1)
            annotation->style().setColor(QColor::fromRgbF(color[0], color[0], color[0]));
        if (n == 3)
            annotation->style().setColor(QColor::fromRgbF(color[0], color[1], color[2]));
        if (n == 4)
            annotation->style().setColor(QColor::fromCmykF(color[0], color[1], color[2], color[3]));
        annotation->style().setOpacity(pdf_annot_opacity(ctx, native));
        if (pdf_annot_has_border(ctx, native))
            annotation->style().setWidth(pdf_annot_border_width(ctx, native));
        const int nativeFlags = pdf_annot_flags(ctx, native);
        int flags = Okular::Annotation::External | Okular::Annotation::ExternallyDrawn;
        if (nativeFlags & (PDF_ANNOT_IS_HIDDEN | PDF_ANNOT_IS_NO_VIEW))
            flags |= Okular::Annotation::Hidden;
        if (!(nativeFlags & PDF_ANNOT_IS_PRINT))
            flags |= Okular::Annotation::DenyPrint;
        if (nativeFlags & PDF_ANNOT_IS_READ_ONLY)
            flags |= Okular::Annotation::DenyWrite;
        if (nativeFlags & PDF_ANNOT_IS_LOCKED)
            flags |= Okular::Annotation::DenyDelete;
        annotation->setFlags(flags);
        if (auto *stamp = dynamic_cast<Okular::StampAnnotation *>(annotation))
            MuPDFLatex::read(ctx, native, stamp, bounds);
        m_annotations.insert(annotation, pdf_keep_annot(ctx, native));
        target->addAnnotation(annotation);
        annotation = nullptr;
    }
}

void MuPDFAnnotationProxy::write(const Okular::Annotation *annotation, pdf_annot *native, bool appearance, pdf_obj *preserved)
{
    auto *ctx = m_generator->m_context;
    auto *object = pdf_annot_obj(ctx, native);
    const fz_rect box = pdf_bound_page(ctx, pdf_annot_page(ctx, native), FZ_CROP_BOX);
    const auto rect = annotation->boundingRectangle();
    const auto a = point({rect.left, rect.top}, box), b = point({rect.right, rect.bottom}, box);
    if (pdf_annot_has_rect(ctx, native))
        pdf_set_annot_rect(ctx, native, fz_make_rect(a.x, a.y, b.x, b.y));
    const QByteArray contents = annotation->contents().toUtf8(), author = annotation->author().toUtf8();
    pdf_set_annot_contents(ctx, native, contents.constData());
    pdf_set_annot_author(ctx, native, author.constData());
    putText(ctx, object, "NM", annotation->uniqueName());
    pdf_set_annot_modification_date(ctx, native, annotation->modificationDate().toSecsSinceEpoch());
    pdf_set_annot_creation_date(ctx, native, annotation->creationDate().toSecsSinceEpoch());
    int flags = annotation->flags() & Okular::Annotation::DenyPrint ? 0 : PDF_ANNOT_IS_PRINT;
    if (annotation->flags() & (Okular::Annotation::Hidden | Okular::Annotation::BeingMoved | Okular::Annotation::BeingResized))
        flags |= PDF_ANNOT_IS_HIDDEN;
    if (annotation->flags() & Okular::Annotation::FixedSize)
        flags |= PDF_ANNOT_IS_NO_ZOOM;
    if (annotation->flags() & Okular::Annotation::FixedRotation)
        flags |= PDF_ANNOT_IS_NO_ROTATE;
    if (annotation->flags() & Okular::Annotation::DenyWrite)
        flags |= PDF_ANNOT_IS_READ_ONLY;
    if (annotation->flags() & Okular::Annotation::DenyDelete)
        flags |= PDF_ANNOT_IS_LOCKED;
    pdf_set_annot_flags(ctx, native, flags);
    // Rect/flag setters request MuPDF resynthesis even for drag-only updates.
    // Reinstall the custom LaTeX AP before the next render can synthesize a stamp.
    if (appearance || annotation->isOkularLatex()) {
        const QColor color = annotation->style().color();
        const float components[] = {float(color.redF()), float(color.greenF()), float(color.blueF())};
        pdf_set_annot_color(ctx, native, color.isValid() ? 3 : 0, components);
        pdf_set_annot_opacity(ctx, native, annotation->style().opacity());
        if (pdf_annot_has_border(ctx, native))
            pdf_set_annot_border_width(ctx, native, annotation->style().width());
        if (const auto *text = dynamic_cast<const Okular::TextAnnotation *>(annotation)) {
            if (text->textType() == Okular::TextAnnotation::InPlace) {
                const QColor color = text->textColor().isValid() ? text->textColor() : QColor(Qt::black);
                const float rgb[] = {float(color.redF()), float(color.greenF()), float(color.blueF())};
                const QByteArray font = text->hasTextFont() ? text->textFontName().toUtf8() : QByteArray("Helv");
                pdf_set_annot_default_appearance(ctx, native, font.constData(), text->hasTextFont() ? text->textFontPointSize() : 12, 3, rgb);
                pdf_set_annot_quadding(ctx, native, text->inplaceAlignment());
            }
            putText(ctx, object, "TemplateNoteData", text->templateNoteData());
        }
        if (const auto *highlight = dynamic_cast<const Okular::HighlightAnnotation *>(annotation)) {
            QVector<fz_quad> quads;
            for (const auto &quad : highlight->highlightQuads())
                quads.append({point(quad.point(3), box), point(quad.point(2), box), point(quad.point(0), box), point(quad.point(1), box)});
            pdf_set_annot_quad_points(ctx, native, quads.size(), quads.constData());
        }
        if (const auto *ink = dynamic_cast<const Okular::InkAnnotation *>(annotation)) {
            QVector<int> counts;
            QVector<fz_point> vertices;
            for (const auto &stroke : ink->inkPaths()) {
                counts.append(stroke.size());
                for (const auto &p : stroke)
                    vertices.append(point(p, box));
            }
            pdf_set_annot_ink_list(ctx, native, counts.size(), counts.constData(), vertices.constData());
        }
        if (const auto *line = dynamic_cast<const Okular::LineAnnotation *>(annotation)) {
            const auto points = line->linePoints();
            if (points.size() == 2)
                pdf_set_annot_line(ctx, native, point(points[0], box), point(points[1], box));
            else {
                QVector<fz_point> vertices;
                for (const auto &p : points)
                    vertices.append(point(p, box));
                pdf_set_annot_vertices(ctx, native, vertices.size(), vertices.constData());
            }
        }
        if (const auto *geometry = dynamic_cast<const Okular::GeomAnnotation *>(annotation)) {
            const QColor fill = geometry->geometricalInnerColor();
            const float rgb[] = {float(fill.redF()), float(fill.greenF()), float(fill.blueF())};
            pdf_set_annot_interior_color(ctx, native, fill.isValid() ? 3 : 0, rgb);
        }
        if (const auto *stamp = dynamic_cast<const Okular::StampAnnotation *>(annotation)) {
            if (stamp->isOkularLatex()) {
                MuPDFLatex::write(ctx, m_generator->m_document, native, stamp, preserved);
            } else if (!stamp->stampImagePath().isEmpty() && QFileInfo::exists(stamp->stampImagePath())) {
                fz_image *image = nullptr;
                fz_var(image);
                const auto path = stamp->stampImagePath().toUtf8();
                fz_try(ctx)
                {
                    image = fz_new_image_from_file(ctx, path.constData());
                    pdf_set_annot_stamp_image(ctx, native, image);
                    pdf_update_annot(ctx, native);
                }
                fz_always(ctx)
                {
                    fz_drop_image(ctx, image);
                }
                fz_catch(ctx)
                {
                    fz_rethrow(ctx);
                }
            } else if (preserved) {
                auto *ap = pdf_dict_put_dict(ctx, object, PDF_NAME(AP), 1);
                pdf_dict_put(ctx, ap, PDF_NAME(N), preserved);
            } else {
                const auto name = stamp->stampIconName().toUtf8();
                pdf_set_annot_icon_name(ctx, native, name.constData());
                pdf_update_annot(ctx, native);
            }
        } else
            pdf_update_annot(ctx, native);
    }
    if (annotation->isOkularLatex())
        return;
    QDomDocument stored;
    QDomElement root = stored.createElement(QStringLiteral("annotation"));
    stored.appendChild(root);
    Okular::AnnotationUtils::storeAnnotation(annotation, root, stored);
    putText(ctx, object, "MengsheeAnnotation", stored.toString(-1));
}

std::shared_ptr<void> MuPDFAnnotationProxy::annotationAppearance(const Okular::Annotation *annotation) const
{
    QMutexLocker lock(m_generator->userMutex());
    auto *ctx = m_generator->m_context;
    auto *native = m_annotations.value(annotation);
    if (!native || !pdf_dict_getp(ctx, pdf_annot_obj(ctx, native), "AP/N"))
        return {};
    auto result = std::make_shared<AppearanceTransfer>();
    pdf_document *copy = nullptr;
    fz_buffer *buffer = nullptr;
    fz_output *output = nullptr;
    fz_var(copy);
    fz_var(buffer);
    fz_var(output);
    fz_try(ctx)
    {
        copy = pdf_create_document(ctx);
        auto *root = pdf_dict_get(ctx, pdf_trailer(ctx, copy), PDF_NAME(Root));
        pdf_dict_puts_drop(ctx, root, "MengsheeAppearance", pdf_graft_object(ctx, copy, pdf_dict_get(ctx, pdf_annot_obj(ctx, native), PDF_NAME(AP))));
        buffer = fz_new_buffer(ctx, 1024);
        output = fz_new_output_with_buffer(ctx, buffer);
        pdf_write_document(ctx, copy, output, &pdf_default_write_options);
        fz_close_output(ctx, output);
        unsigned char *data;
        const size_t length = fz_buffer_storage(ctx, buffer, &data);
        result->pdf = QByteArray(reinterpret_cast<const char *>(data), length);
    }
    fz_always(ctx)
    {
        fz_drop_output(ctx, output);
        fz_drop_buffer(ctx, buffer);
        pdf_drop_document(ctx, copy);
    }
    fz_catch(ctx)
    {
        return {};
    }
    return result;
}

void MuPDFAnnotationProxy::notifyAddition(Okular::Annotation *annotation, int page)
{
    QMutexLocker lock(m_generator->userMutex());
    auto *ctx = m_generator->m_context;
    pdf_annot *native = nullptr;
    pdf_document *source = nullptr;
    pdf_obj *copied = nullptr;
    fz_stream *stream = nullptr;
    fz_var(native);
    fz_var(source);
    fz_var(copied);
    fz_var(stream);
    fz_try(ctx)
    {
        native = pdf_create_annot(ctx, m_generator->m_pages.at(page), typeFor(annotation));
        if (annotation->uniqueName().isEmpty())
            annotation->setUniqueName(QUuid::createUuid().toString(QUuid::WithoutBraces));
        if (const auto *transfer = static_cast<const AppearanceTransfer *>(annotation->nativeData())) {
            stream = fz_open_memory(ctx, reinterpret_cast<const unsigned char *>(transfer->pdf.constData()), transfer->pdf.size());
            source = pdf_open_document_with_stream(ctx, stream);
            copied = pdf_graft_object(ctx, m_generator->m_document, pdf_dict_getp(ctx, pdf_trailer(ctx, source), "Root/MengsheeAppearance"));
        }
        auto *removed = m_removed.value(annotation);
        if (!removed)
            removed = copied;
        write(annotation, native, true, pdf_dict_get(ctx, removed, PDF_NAME(N)));
        if (auto *preserved = m_removed.take(annotation)) {
            pdf_dict_put(ctx, pdf_annot_obj(ctx, native), PDF_NAME(AP), preserved);
            pdf_drop_obj(ctx, preserved);
        }
        m_annotations.insert(annotation, pdf_keep_annot(ctx, native));
        annotation->setFlags(annotation->flags() | Okular::Annotation::ExternallyDrawn);
    }
    fz_always(ctx)
    {
        pdf_drop_annot(ctx, native);
        pdf_drop_obj(ctx, copied);
        pdf_drop_document(ctx, source);
        fz_drop_stream(ctx, stream);
    }
    fz_catch(ctx)
    {
        m_generator->m_editError = QString::fromUtf8(fz_caught_message(ctx));
    }
}

void MuPDFAnnotationProxy::notifyModification(const Okular::Annotation *annotation, int, bool appearance)
{
    QMutexLocker lock(m_generator->userMutex());
    auto *ctx = m_generator->m_context;
    pdf_obj *preserved = nullptr;
    fz_var(preserved);
    fz_try(ctx)
    {
        auto *native = m_annotations.value(annotation);
        if (native) {
            preserved = pdf_keep_obj(ctx, pdf_dict_getp(ctx, pdf_annot_obj(ctx, native), "AP/N"));
            write(annotation, native, appearance, preserved);
        }
    }
    fz_always(ctx)
    {
        pdf_drop_obj(ctx, preserved);
    }
    fz_catch(ctx)
    {
        m_generator->m_editError = QString::fromUtf8(fz_caught_message(ctx));
    }
}

void MuPDFAnnotationProxy::notifyRemoval(Okular::Annotation *annotation, int)
{
    QMutexLocker lock(m_generator->userMutex());
    auto *ctx = m_generator->m_context;
    fz_try(ctx)
    {
        auto *native = m_annotations.value(annotation);
        if (native) {
            pdf_drop_obj(ctx, m_removed.take(annotation));
            m_removed.insert(annotation, pdf_keep_obj(ctx, pdf_dict_get(ctx, pdf_annot_obj(ctx, native), PDF_NAME(AP))));
            pdf_delete_annot(ctx, pdf_annot_page(ctx, native), native);
            pdf_drop_annot(ctx, m_annotations.take(annotation));
        }
    }
    fz_catch(ctx)
    {
        m_generator->m_editError = QString::fromUtf8(fz_caught_message(ctx));
    }
}
