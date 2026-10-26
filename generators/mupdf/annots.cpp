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
#include <algorithm>
#include <cmath>
#include <core/document.h>
#include <core/page.h>

namespace
{
// Use an origin-based wrapper. FreeText APs can have a nonzero BBox origin;
// MuPDF's NoRotate transform assumes origin-based appearance coordinates.
// Keep the original form intact, including its resources and matrix.
void rotateStampAppearance(fz_context *ctx, pdf_annot *native, pdf_obj *ap, int angle)
{
    if (pdf_annot_type(ctx, native) != PDF_ANNOT_FREE_TEXT) {
        fz_buffer *content = pdf_load_stream(ctx, ap);
        fz_try(ctx)
        {
            pdf_set_annot_appearance(ctx, native, "N", nullptr, fz_concat(pdf_dict_get_matrix(ctx, ap, PDF_NAME(Matrix)), fz_rotate(angle)), pdf_dict_get_rect(ctx, ap, PDF_NAME(BBox)), pdf_dict_get(ctx, ap, PDF_NAME(Resources)), content);
        }
        fz_always(ctx)
        {
            fz_drop_buffer(ctx, content);
        }
        fz_catch(ctx)
        {
            fz_rethrow(ctx);
        }
        return;
    }
    pdf_obj *resources = nullptr;
    pdf_obj *form = nullptr;
    fz_buffer *content = nullptr;
    fz_var(resources);
    fz_var(form);
    fz_var(content);
    fz_try(ctx)
    {
        auto rotation = fz_rotate(angle);
        const auto bounds = fz_transform_rect(pdf_dict_get_rect(ctx, ap, PDF_NAME(BBox)), fz_concat(pdf_dict_get_matrix(ctx, ap, PDF_NAME(Matrix)), rotation));
        rotation = fz_concat(rotation, fz_translate(-bounds.x0, -bounds.y0));
        resources = pdf_new_dict(ctx, pdf_annot_page(ctx, native)->doc, 1);
        pdf_dict_puts(ctx, pdf_dict_put_dict(ctx, resources, PDF_NAME(XObject), 1), "Original", ap);
        content = fz_new_buffer(ctx, 128);
        fz_append_printf(ctx, content, "q %g %g %g %g %g %g cm /Original Do Q", rotation.a, rotation.b, rotation.c, rotation.d, rotation.e, rotation.f);
        form = pdf_new_xobject(ctx, pdf_annot_page(ctx, native)->doc, fz_make_rect(0, 0, bounds.x1 - bounds.x0, bounds.y1 - bounds.y0), fz_identity, resources, content);
        pdf_dict_put(ctx, pdf_dict_get(ctx, pdf_annot_obj(ctx, native), PDF_NAME(AP)), PDF_NAME(N), form);
        pdf_set_annot_resynthesised(ctx, native);
    }
    fz_always(ctx)
    {
        pdf_drop_obj(ctx, form);
        pdf_drop_obj(ctx, resources);
        fz_drop_buffer(ctx, content);
    }
    fz_catch(ctx)
    {
        fz_rethrow(ctx);
    }
}
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
        if (auto *text = dynamic_cast<Okular::TextAnnotation *>(annotation); text && type == PDF_ANNOT_FREE_TEXT) {
            text->setTextType(Okular::TextAnnotation::InPlace);
            const auto intent = pdf_dict_get(ctx, object, PDF_NAME(IT));
            text->setInplaceIntent(pdf_name_eq(ctx, intent, PDF_NAME(FreeTextTypeWriter)) ? Okular::TextAnnotation::TypeWriter
                : pdf_name_eq(ctx, intent, PDF_NAME(FreeTextCallout)) ? Okular::TextAnnotation::Callout : Okular::TextAnnotation::Unknown);
            text->setInplaceAlignment(pdf_annot_quadding(ctx, native));
            if (pdf_annot_has_default_appearance(ctx, native)) {
                char font[256] = {};
                float size = 0, color[4] = {};
                int components = 0;
                pdf_parse_default_appearance_unmapped(ctx, pdf_dict_get_text_string(ctx, object, PDF_NAME(DA)), font, sizeof(font), &size, &components, color);
                text->setTextFontName(QString::fromUtf8(font));
                text->setTextFontPointSize(size);
                if (components == 1) text->setTextColor(QColor::fromRgbF(color[0], color[0], color[0]));
                else if (components == 3) text->setTextColor(QColor::fromRgbF(color[0], color[1], color[2]));
                else if (components == 4) text->setTextColor(QColor::fromCmykF(color[0], color[1], color[2], color[3]));
            }
        }
        const auto rect = pdf_annot_has_rect(ctx, native) ? pdf_annot_rect(ctx, native) : pdf_bound_annot(ctx, native);
        if (auto *text = dynamic_cast<Okular::TextAnnotation *>(annotation); text && type == PDF_ANNOT_FREE_TEXT && pdf_name_eq(ctx, pdf_dict_get(ctx, object, PDF_NAME(IT)), PDF_NAME(FreeTextCallout))) {
            fz_matrix pageMatrix;
            pdf_page_transform(ctx, pdf_annot_page(ctx, native), nullptr, &pageMatrix);
            auto *cl = pdf_dict_get(ctx, object, PDF_NAME(CL));
            const int count = pdf_array_len(ctx, cl) / 2;
            if (count == 2 || count == 3) {
                text->setInplaceIntent(Okular::TextAnnotation::Callout);
                for (int i = 0; i < 3; ++i) {
                    const int j = std::min(i, count - 1);
                    const fz_point point = {pdf_array_get_real(ctx, cl, 2 * j), pdf_array_get_real(ctx, cl, 2 * j + 1)};
                    text->setInplaceCallout(normalized(fz_transform_point(point, pageMatrix), bounds), i);
                }
            }
        }
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
        if ((type == PDF_ANNOT_STAMP || type == PDF_ANNOT_FREE_TEXT) && (nativeFlags & PDF_ANNOT_IS_NO_ROTATE)) {
            // Hit geometry starts in rotated page coordinates. Apply the same
            // NoRotate compensation as MuPDF's appearance placement, around
            // the PDF Rect's upper-left anchor, without changing the annotation.
            fz_matrix pageMatrix;
            pdf_page_transform(ctx, pdf_annot_page(ctx, native), nullptr, &pageMatrix);
            const auto pdfRect = pdf_dict_get_rect(ctx, object, PDF_NAME(Rect));
            const int angle = pdf_dict_get_inheritable_int(ctx, pdf_annot_page(ctx, native)->obj, PDF_NAME(Rotate));
            auto compensation = fz_translate(-pdfRect.x0, -pdfRect.y1);
            compensation = fz_concat(compensation, fz_rotate(angle));
            compensation = fz_concat(compensation, fz_translate(pdfRect.x0, pdfRect.y1));
            compensation = fz_concat(fz_concat(fz_invert_matrix(pageMatrix), compensation), pageMatrix);
            const auto r = annotation->boundingRectangle();
            const auto top = point({r.left, r.top}, bounds), bottom = point({r.right, r.bottom}, bounds);
            const auto display = fz_transform_rect(fz_make_rect(top.x, top.y, bottom.x, bottom.y), compensation);
            const auto lo = normalized({display.x0, display.y0}, bounds), hi = normalized({display.x1, display.y1}, bounds);
            annotation->setBoundingRectangle({lo.x, lo.y, hi.x, hi.y});
            if (annotation->isLatexCallout())
                for (int i = 0; i < 3; ++i)
                    annotation->setLatexCalloutPoint(normalized(fz_transform_point(point(annotation->latexCalloutPoint(i), bounds), compensation), bounds), i);
        }
        const int pageAngle=pdf_dict_get_inheritable_int(ctx,pdf_annot_page(ctx,native)->obj,PDF_NAME(Rotate));
        int authored=0;
        if(annotation->isOkularLatex())
            authored=QJsonDocument::fromJson(textValue(ctx,object,"LatexNoteData").toUtf8()).object().value(QStringLiteral("rotation")).toInt();
        else if(type==PDF_ANNOT_STAMP)
            authored=pdf_to_int(ctx,pdf_dict_gets(ctx,object,"MengsheeStampRotation"));
        else if(type==PDF_ANNOT_FREE_TEXT)
            authored=pdf_dict_get_int(ctx,object,PDF_NAME(Rotate));
        annotation->setAppearanceRotation((nativeFlags & PDF_ANNOT_IS_NO_ROTATE ? 0 : pageAngle)-authored);
        if(type==PDF_ANNOT_FREE_TEXT && !pdf_dict_get(ctx,object,PDF_NAME(Rotate))) {
            // Standard FreeText has no Rotate key. Inspect the painted text
            // baseline in page display coordinates, including AP/Tm transforms.
            fz_stext_page *textPage=nullptr;
            fz_var(textPage);
            fz_try(ctx) {
                textPage=pdf_new_stext_page_from_annot(ctx,native,nullptr);
                for(auto *block=textPage ? textPage->first_block : nullptr;block;block=block->next) {
                    if(block->type!=FZ_STEXT_BLOCK_TEXT || !block->u.t.first_line)continue;
                    const auto direction=block->u.t.first_line->dir;
                    const double angle=std::atan2(direction.y,direction.x)*180.0/3.141592653589793;
                    const int quarter=qRound(angle/90.0)*90;
                    if(std::abs(angle-quarter)<0.1)annotation->setAppearanceRotation(quarter);
                    break;
                }
            }
            fz_always(ctx) {fz_drop_stext_page(ctx,textPage);}
            fz_catch(ctx) { /* A broken appearance must not prevent opening the page. */ }
        }
        m_annotations.insert(annotation, pdf_keep_annot(ctx, native));
        target->addAnnotation(annotation);
        annotation = nullptr;
    }
}

void MuPDFAnnotationProxy::write(const Okular::Annotation *annotation, pdf_annot *native, bool appearance, pdf_obj *preserved)
{
    auto *ctx = m_generator->m_context;
    auto *object = pdf_annot_obj(ctx, native);
    // Local display appearances are not visible in the persistent object dictionary.
    // Preserve the existing-AP contract for imported FreeText and template notes.
    const bool transientFreeText = pdf_annot_type(ctx, native) == PDF_ANNOT_FREE_TEXT
        && !annotation->isTemplateNote() && !pdf_dict_get(ctx, object, PDF_NAME(AP));
    const fz_rect box = pdf_bound_page(ctx, pdf_annot_page(ctx, native), FZ_CROP_BOX);
    const int authored=(pdf_dict_get_inheritable_int(ctx,pdf_annot_page(ctx,native)->obj,PDF_NAME(Rotate))-annotation->appearanceRotation()+360)%360;
    if(pdf_annot_type(ctx,native)==PDF_ANNOT_FREE_TEXT)
        pdf_dict_put_int(ctx,object,PDF_NAME(Rotate),authored);
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
    if (appearance || annotation->isOkularLatex() || annotation->subType() == Okular::Annotation::AStamp) {
        const QColor color = annotation->style().color();
        const float components[] = {float(color.redF()), float(color.greenF()), float(color.blueF())};
        pdf_set_annot_color(ctx, native, color.isValid() ? 3 : 0, components);
        pdf_set_annot_opacity(ctx, native, annotation->style().opacity());
        if (pdf_annot_has_border(ctx, native))
            pdf_set_annot_border_width(ctx, native, annotation->style().width());
    }
    if (const auto *text = dynamic_cast<const Okular::TextAnnotation *>(annotation); text && text->textType() == Okular::TextAnnotation::InPlace) {
        // FreeText callout geometry is separate from its text frame.
        // Store CL in PDF user space and RD relative to the full Rect.
        // The model's rectangle continues to describe only the box.
        if (text->inplaceIntent() == Okular::TextAnnotation::Callout) {
            fz_matrix pageMatrix;
            pdf_page_transform(ctx, pdf_annot_page(ctx, native), nullptr, &pageMatrix);
            const auto toPdf = fz_invert_matrix(pageMatrix);
            const auto textBox = fz_transform_rect(fz_make_rect(a.x, a.y, b.x, b.y), toPdf);
            auto fullBox = textBox;
            auto *cl = pdf_dict_put_array(ctx, object, PDF_NAME(CL), 6);
            for (int i = 0; i < 3; ++i) {
                const auto p = fz_transform_point(point(text->inplaceCallout(i), box), toPdf);
                pdf_array_push_real(ctx, cl, p.x);
                pdf_array_push_real(ctx, cl, p.y);
                fullBox = fz_include_point_in_rect(fullBox, p);
            }
            pdf_dict_put_name(ctx, object, PDF_NAME(IT), "FreeTextCallout");
            pdf_dict_put_name(ctx, object, PDF_NAME(LE), "OpenArrow");
            pdf_dict_put_rect(ctx, object, PDF_NAME(Rect), fullBox);
            auto *rd = pdf_dict_put_array(ctx, object, PDF_NAME(RD), 4);
            for (float inset : {textBox.x0 - fullBox.x0, textBox.y0 - fullBox.y0, fullBox.x1 - textBox.x1, fullBox.y1 - textBox.y1})
                pdf_array_push_real(ctx, rd, inset);
        }
    }
    if (appearance || annotation->isOkularLatex() || annotation->subType() == Okular::Annotation::AStamp) {
        if (const auto *text = dynamic_cast<const Okular::TextAnnotation *>(annotation)) {
            if (text->textType() == Okular::TextAnnotation::InPlace) {
                const QColor color = text->textColor().isValid() ? text->textColor() : QColor(Qt::black);
                const float rgb[] = {float(color.redF()), float(color.greenF()), float(color.blueF())};
                // hasTextFont() describes a legacy QFont, not the PDF font fields.
                const QByteArray font = text->textFontName().isEmpty() ? QByteArray("Helv") : text->textFontName().toUtf8();
                pdf_set_annot_default_appearance(ctx, native, font.constData(), text->textFontPointSize(), 3, rgb);
                pdf_set_annot_quadding(ctx, native, text->inplaceAlignment());
                if (text->inplaceIntent() == Okular::TextAnnotation::TypeWriter)
                    pdf_dict_put_name(ctx, object, PDF_NAME(IT), "FreeTextTypeWriter");
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
                MuPDFLatex::write(ctx, m_generator->m_document, native, stamp, preserved, m_generator->document() ? int(m_generator->document()->rotation()) * 90 : 0);
            } else if (!stamp->stampImagePath().isEmpty() && QFileInfo::exists(stamp->stampImagePath())) {
                fz_image *image = nullptr;
                fz_var(image);
                const auto path = stamp->stampImagePath().toUtf8();
                fz_try(ctx)
                {
                    image = fz_new_image_from_file(ctx, path.constData());
                    pdf_set_annot_stamp_image(ctx, native, image);
                    pdf_update_annot(ctx, native);
                    // Author the image upright in the current view. This matrix
                    // is part of the AP, so later page rotations still apply.
                    auto *ap = pdf_dict_getp(ctx, object, "AP/N");
                    const int angle = authored;
                    pdf_dict_puts_drop(ctx, object, "MengsheeStampRotation", pdf_new_int(ctx, angle));
                    pdf_dict_put_matrix(ctx, ap, PDF_NAME(Matrix), fz_rotate(angle));
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
                pdf_set_annot_resynthesised(ctx, native);
            } else {
                const auto name = stamp->stampIconName().toUtf8();
                pdf_set_annot_icon_name(ctx, native, name.constData());
                pdf_update_annot(ctx, native);
            }
        } else if (!transientFreeText)
            pdf_update_annot(ctx, native);
    }
    if (annotation->isOkularLatex())
        return;
    QDomDocument stored;
    QDomElement root = stored.createElement(QStringLiteral("annotation"));
    stored.appendChild(root);
    Okular::AnnotationUtils::storeAnnotation(annotation, root, stored);
    putText(ctx, object, "MengsheeAnnotation", stored.toString(-1));
    if (transientFreeText) {
        // Property setters request persistent synthesis. Clear that request so
        // MuPDF's missing-AP path builds only a local-xref display appearance.
        pdf_clean_obj(ctx, object);
        pdf_set_annot_resynthesised(ctx, native);
        pdf_update_annot(ctx, native);
    }

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
    m_generator->clearAnnotationLists();
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
        if(!annotation->hasAppearanceRotation())
            annotation->setAppearanceRotation(m_generator->document() ? -int(m_generator->document()->rotation())*90 : 0);
        if (pdf_annot_type(ctx, native) == PDF_ANNOT_FREE_TEXT) {
            const int angle = pdf_dict_get_inheritable_int(ctx, pdf_annot_page(ctx, native)->obj, PDF_NAME(Rotate))
                + (m_generator->document() ? int(m_generator->document()->rotation()) * 90 : 0);
            pdf_dict_put_int(ctx, pdf_annot_obj(ctx, native), PDF_NAME(Rotate), angle % 360);
        }
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
    m_generator->clearAnnotationLists();
    auto *ctx = m_generator->m_context;
    pdf_obj *preserved = nullptr;
    fz_var(preserved);
    fz_try(ctx)
    {
        auto *native = m_annotations.value(annotation);
        if (native) {
            // Normalize the current displayed AP before editing its rectangle;
            // otherwise the native NoRotate anchor would be applied a second time.
            prepareAnnotationsRotation({const_cast<Okular::Annotation *>(annotation)}, true);
            pdf_dict_dels(ctx,pdf_annot_obj(ctx,native),"MengsheeRotationAnchor");
            auto *object=pdf_annot_obj(ctx,native);
            if(annotation->subType()==Okular::Annotation::AStamp && !annotation->isOkularLatex()) {
                const int oldAngle=pdf_to_int(ctx,pdf_dict_gets(ctx,object,"MengsheeStampRotation"));
                const int newAngle=(pdf_dict_get_inheritable_int(ctx,pdf_annot_page(ctx,native)->obj,PDF_NAME(Rotate))-annotation->appearanceRotation()+360)%360;
                auto *ap=pdf_dict_getp(ctx,object,"AP/N");
                if(oldAngle!=newAngle && pdf_is_stream(ctx,ap))rotateStampAppearance(ctx,native,ap,newAngle-oldAngle);
                pdf_dict_puts_drop(ctx,object,"MengsheeStampRotation",pdf_new_int(ctx,newAngle));
            }
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
    m_generator->clearAnnotationLists();
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

void MuPDFAnnotationProxy::preparePageRotation(Okular::Page *page, bool followPage)
{
    prepareAnnotationsRotation(page->annotations(), followPage);
    auto *ctx = m_generator->m_context;
    for (auto *model : page->annotations()) {
        auto *native = m_annotations.value(model);
        const auto *text = dynamic_cast<const Okular::TextAnnotation *>(model);
        if (!native || (model->subType() != Okular::Annotation::AStamp && !(text && text->textType() == Okular::TextAnnotation::InPlace)))
            continue;
        auto *object = pdf_annot_obj(ctx, native);
        if (followPage) {
            pdf_dict_dels(ctx, object, "MengsheeRotationAnchor");
            continue;
        }
        if (pdf_dict_gets(ctx, object, "MengsheeRotationAnchor"))
            continue;
        const auto bounds = pdf_bound_annot(ctx, native);
        fz_matrix matrix;
        pdf_page_transform(ctx, pdf_annot_page(ctx, native), nullptr, &matrix);
        const auto center = fz_transform_point({(bounds.x0 + bounds.x1) / 2, (bounds.y0 + bounds.y1) / 2}, fz_invert_matrix(matrix));
        auto *anchor = pdf_new_array(ctx, m_generator->m_document, 2);
        pdf_dict_puts_drop(ctx, object, "MengsheeRotationAnchor", anchor);
        pdf_array_push_real(ctx, anchor, center.x);
        pdf_array_push_real(ctx, anchor, center.y);
    }
}
void MuPDFAnnotationProxy::prepareAnnotationsRotation(const QList<Okular::Annotation *> &annotations, bool followPage)
{
    auto *ctx = m_generator->m_context;
    for (auto *model : annotations) {
        auto *native = m_annotations.value(model);
        const auto *text = dynamic_cast<const Okular::TextAnnotation *>(model);
        const bool freeText = text && text->textType() == Okular::TextAnnotation::InPlace;
        if (!native || (model->subType() != Okular::Annotation::AStamp && !freeText))
            continue;
        auto *object = pdf_annot_obj(ctx, native);
        const int flags = pdf_annot_flags(ctx, native);
        const bool fixed = flags & PDF_ANNOT_IS_NO_ROTATE;
        if (fixed == !followPage) {
            if (fixed && freeText) {
                auto *ap = pdf_dict_getp(ctx, object, "AP/N");
                const auto bounds = pdf_dict_get_rect(ctx, ap, PDF_NAME(BBox));
                if (pdf_is_stream(ctx, ap) && (bounds.x0 != 0 || bounds.y0 != 0))
                    rotateStampAppearance(ctx, native, ap, 0);
            }
            continue;
        }
        const auto display = pdf_bound_annot(ctx, native);
        fz_matrix pageMatrix;
        pdf_page_transform(ctx, pdf_annot_page(ctx, native), nullptr, &pageMatrix);
        const auto inverse = fz_invert_matrix(pageMatrix);
        const int pageAngle = pdf_dict_get_inheritable_int(ctx, pdf_annot_page(ctx, native)->obj, PDF_NAME(Rotate));
        const int adjustment = followPage ? pageAngle : -pageAngle;
        auto rect = fz_transform_rect(display, inverse);
        if (!followPage) {
            const auto anchor = fz_transform_point({display.x0, display.y0}, inverse);
            const float unit = std::sqrt(std::abs(pageMatrix.a * pageMatrix.d - pageMatrix.b * pageMatrix.c));
            rect = fz_make_rect(anchor.x, anchor.y - (display.y1 - display.y0) / unit, anchor.x + (display.x1 - display.x0) / unit, anchor.y);
        }
        auto *ap = pdf_dict_getp(ctx, object, "AP/N");
        if (pdf_is_stream(ctx, ap))
            rotateStampAppearance(ctx, native, ap, adjustment);
        if (freeText)
            pdf_dict_put_int(ctx, object, PDF_NAME(Rotate), (pdf_dict_get_int(ctx, object, PDF_NAME(Rotate)) + adjustment + 720) % 360);
        pdf_dict_put_rect(ctx, object, PDF_NAME(Rect), rect);
        pdf_dict_put_int(ctx, object, PDF_NAME(F), followPage ? flags & ~PDF_ANNOT_IS_NO_ROTATE : flags | PDF_ANNOT_IS_NO_ROTATE);
        pdf_set_annot_resynthesised(ctx, native);
        auto data = QJsonDocument::fromJson(textValue(ctx, object, "LatexNoteData").toUtf8()).object();
        if (!data.isEmpty()) {
            const int angle = (data.value(QStringLiteral("rotation")).toInt() + adjustment + 720) % 360;
            data.insert(QStringLiteral("rotation"), angle);
            if (model->isLatexCallout()) {
                const auto bounds = pdf_bound_page(ctx, pdf_annot_page(ctx, native), FZ_CROP_BOX);
                double w = bounds.x1 - bounds.x0, h = bounds.y1 - bounds.y0;
                // Map current display geometry back through the new NoRotate
                // compensation, then into the note's authored page axes.
                auto transform = fz_identity;
                if (!followPage) {
                    auto correction = fz_translate(-rect.x0, -rect.y1);
                    correction = fz_concat(correction, fz_rotate(pageAngle));
                    correction = fz_concat(correction, fz_translate(rect.x0, rect.y1));
                    transform = fz_invert_matrix(fz_concat(fz_concat(inverse, correction), pageMatrix));
                }
                const int delta = (angle - pageAngle + 360) % 360;
                const auto mapPoint = [&](Okular::NormalizedPoint p) {
                    p = normalized(fz_transform_point(point(p, bounds), transform), bounds);
                    if (delta == 90)
                        return Okular::NormalizedPoint(1 - p.y, p.x);
                    if (delta == 180)
                        return Okular::NormalizedPoint(1 - p.x, 1 - p.y);
                    if (delta == 270)
                        return Okular::NormalizedPoint(p.y, 1 - p.x);
                    return p;
                };
                if (delta % 180)
                    std::swap(w, h);
                const auto box = model->boundingRectangle();
                const auto a = mapPoint({box.left, box.top}), b = mapPoint({box.right, box.bottom});
                auto callout = data.value(QStringLiteral("callout")).toObject();
                callout.insert(QStringLiteral("boxRectPt"), QJsonArray {std::min(a.x, b.x) * w, (1 - std::max(a.y, b.y)) * h, std::max(a.x, b.x) * w, (1 - std::min(a.y, b.y)) * h});
                QJsonArray points;
                for (int i = 0; i < 3; ++i) {
                    auto p = mapPoint(model->latexCalloutPoint(i));
                    points.append(QJsonArray {p.x, p.y});
                }
                callout.insert(QStringLiteral("pointsNorm"), points);
                data.insert(QStringLiteral("callout"), callout);
            }
            putText(ctx, object, "LatexNoteData", QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)));
        }
        auto *imageAngle = pdf_dict_gets(ctx, object, "MengsheeStampRotation");
        if (imageAngle)
            pdf_dict_puts_drop(ctx, object, "MengsheeStampRotation", pdf_new_int(ctx, (pdf_to_int(ctx, imageAngle) + adjustment + 720) % 360));
    }
}

void MuPDFAnnotationProxy::finishPageRotation(Okular::Page *page, bool followPage)
{
    if (followPage)
        return;
    auto *ctx = m_generator->m_context;
    for (auto *model : page->annotations()) {
        auto *native = m_annotations.value(model);
        if (!native)
            continue;
        auto *object = pdf_annot_obj(ctx, native);
        auto *anchor = pdf_dict_gets(ctx, object, "MengsheeRotationAnchor");
        if (pdf_array_len(ctx, anchor) != 2)
            continue;
        fz_matrix matrix;
        pdf_page_transform(ctx, pdf_annot_page(ctx, native), nullptr, &matrix);
        const auto pageBox = pdf_bound_page(ctx, pdf_annot_page(ctx, native), FZ_CROP_BOX);
        const auto display = pdf_bound_annot(ctx, native);
        auto center = fz_transform_point({pdf_array_get_real(ctx, anchor, 0), pdf_array_get_real(ctx, anchor, 1)}, matrix);
        const float width = display.x1 - display.x0, height = display.y1 - display.y0;
        // Keep the rotated center, moving inward only when the unchanged-size
        // note would cross a page edge. The PDF anchor preserves reversibility.
        center.x = width <= pageBox.x1 - pageBox.x0 ? std::clamp(center.x, pageBox.x0 + width / 2, pageBox.x1 - width / 2) : (pageBox.x0 + pageBox.x1) / 2;
        center.y = height <= pageBox.y1 - pageBox.y0 ? std::clamp(center.y, pageBox.y0 + height / 2, pageBox.y1 - height / 2) : (pageBox.y0 + pageBox.y1) / 2;
        const auto inverse = fz_invert_matrix(matrix);
        const auto oldCenter = fz_transform_point({(display.x0 + display.x1) / 2, (display.y0 + display.y1) / 2}, inverse);
        const auto newCenter = fz_transform_point(center, inverse);
        const float dx = newCenter.x - oldCenter.x, dy = newCenter.y - oldCenter.y;
        auto rect = pdf_dict_get_rect(ctx, object, PDF_NAME(Rect));
        rect.x0 += dx;
        rect.x1 += dx;
        rect.y0 += dy;
        rect.y1 += dy;
        pdf_dict_put_rect(ctx, object, PDF_NAME(Rect), rect);
        auto data = QJsonDocument::fromJson(textValue(ctx, object, "LatexNoteData").toUtf8()).object();
        auto callout = data.value(QStringLiteral("callout")).toObject();
        if (!callout.isEmpty()) {
            const int angle = data.value(QStringLiteral("rotation")).toInt();
            const float unit = std::sqrt(std::abs(matrix.a * matrix.d - matrix.b * matrix.c));
            const auto shift = fz_transform_vector({dx * unit, dy * unit}, fz_rotate(-angle));
            auto box = callout.value(QStringLiteral("boxRectPt")).toArray();
            if (box.size() == 4)
                callout.insert(QStringLiteral("boxRectPt"), QJsonArray {box[0].toDouble() + shift.x, box[1].toDouble() + shift.y, box[2].toDouble() + shift.x, box[3].toDouble() + shift.y});
            double w = pageBox.x1 - pageBox.x0, h = pageBox.y1 - pageBox.y0;
            const int pageAngle = pdf_dict_get_inheritable_int(ctx, pdf_annot_page(ctx, native)->obj, PDF_NAME(Rotate));
            if ((angle - pageAngle) % 180)
                std::swap(w, h);
            QJsonArray points;
            for (auto value : callout.value(QStringLiteral("pointsNorm")).toArray()) {
                auto point = value.toArray();
                points.append(QJsonArray {point[0].toDouble() + shift.x / w, point[1].toDouble() - shift.y / h});
            }
            callout.insert(QStringLiteral("pointsNorm"), points);
            data.insert(QStringLiteral("callout"), callout);
            putText(ctx, object, "LatexNoteData", QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)));
        }
    }
}
