// SPDX-License-Identifier: AGPL-3.0-or-later
#include "generator_mupdf.h"
#include <cmath>
#include <core/numberedcalloutgeometry_p.h>
#include <core/page.h>

namespace
{
bool validAppearance(const Okular::PdfLinkAppearance &appearance, QString *error)
{
    if (appearance.kind == Okular::PdfLinkAppearance::None)
        return true;
    if (appearance.label.isEmpty() || appearance.label.size() > 128 || std::any_of(appearance.label.begin(), appearance.label.end(), [](QChar c) { return c.unicode() < 32 || c.unicode() > 126; })) {
        if (error)
            *error = QStringLiteral("A note label requires 1–128 printable ASCII characters.");
        return false;
    }
    return true;
}
void applyAppearance(fz_context *ctx, pdf_document *doc, pdf_obj *link, const Okular::PdfLinkAppearance &appearance)
{
    pdf_dict_del(ctx, link, PDF_NAME(AP));
    if (appearance.kind == Okular::PdfLinkAppearance::None)
        return;
    const auto rect = pdf_dict_get_rect(ctx, link, PDF_NAME(Rect));
    const double width = std::max(1.0, double(rect.x1 - rect.x0)), height = std::max(1.0, double(rect.y1 - rect.y0));
    const double size = Okular::NumberedCalloutGeometry::NoteLabel::markFontSizePt, padding = Okular::NumberedCalloutGeometry::NoteLabel::markPaddingPt;
    auto text = appearance.label.toLatin1();
    text.replace("\\", "\\\\");
    text.replace("(", "\\(");
    text.replace(")", "\\)");
    const auto number = [](double n) { return QByteArray::number(n, 'g', 12); };
    const QByteArray bytes = "q\n1 0.95 0.70 rg 0 0 " + number(width) + ' ' + number(height) + " re f\n0.55 0.42 0.10 RG 0.5 w 0.25 0.25 " + number(std::max(0.0, width - .5)) + ' ' + number(std::max(0.0, height - .5)) + " re S\nBT /F1 " +
        number(size) + " Tf 0 g 1 0 0 1 " + number(padding) + ' ' + number(padding + size * .212) + " Tm (" + text + ") Tj ET\nQ\n";
    pdf_obj *resources = nullptr;
    fz_font *font = nullptr;
    fz_buffer *buffer = nullptr;
    fz_var(resources);
    fz_var(font);
    fz_var(buffer);
    fz_try(ctx)
    {
        resources = pdf_new_dict(ctx, doc, 1);
        auto *fonts = pdf_dict_put_dict(ctx, resources, PDF_NAME(Font), 1);
        font = fz_new_base14_font(ctx, "Helvetica-Bold");
        pdf_dict_puts_drop(ctx, fonts, "F1", pdf_add_simple_font(ctx, doc, font, PDF_SIMPLE_ENCODING_LATIN));
        buffer = fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char *>(bytes.constData()), bytes.size());
        auto *ap = pdf_dict_put_dict(ctx, link, PDF_NAME(AP), 1);
        pdf_dict_put_drop(ctx, ap, PDF_NAME(N), pdf_new_xobject(ctx, doc, fz_make_rect(0, 0, width, height), fz_identity, resources, buffer));
        const auto label = appearance.label.toUtf8();
        pdf_dict_put_text_string(ctx, link, PDF_NAME(Contents), label.constData());
    }
    fz_always(ctx)
    {
        pdf_drop_obj(ctx, resources);
        fz_drop_font(ctx, font);
        fz_drop_buffer(ctx, buffer);
    }
    fz_catch(ctx)
    {
        fz_rethrow(ctx);
    }
}
pdf_obj *destinations(fz_context *ctx, pdf_document *doc)
{
    auto *root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
    auto *dict = pdf_dict_get(ctx, root, PDF_NAME(Dests));
    if (!dict)
        dict = pdf_dict_put_dict(ctx, root, PDF_NAME(Dests), 8);
    auto *tree = pdf_load_name_tree(ctx, doc, PDF_NAME(Dests));
    if (tree) {
        const int count = pdf_dict_len(ctx, tree);
        for (int i = 0; i < count; ++i)
            pdf_dict_put(ctx, dict, pdf_dict_get_key(ctx, tree, i), pdf_dict_get_val(ctx, tree, i));
        pdf_drop_obj(ctx, tree);
        pdf_dict_del(ctx, pdf_dict_get(ctx, root, PDF_NAME(Names)), PDF_NAME(Dests));
    }
    return dict;
}
fz_rect pageRect(fz_context *ctx, pdf_obj *page, const QRectF &rect)
{
    fz_rect box;
    fz_matrix matrix;
    pdf_page_obj_transform(ctx, page, &box, &matrix);
    box = fz_transform_rect(box, matrix);
    return fz_transform_rect(fz_make_rect(box.x0 + rect.left() * (box.x1 - box.x0), box.y0 + rect.top() * (box.y1 - box.y0), box.x0 + rect.right() * (box.x1 - box.x0), box.y0 + rect.bottom() * (box.y1 - box.y0)), fz_invert_matrix(matrix));
}
pdf_obj *findLink(fz_context *ctx, pdf_obj *page, const QRectF &rect, int *found = nullptr)
{
    const auto wanted = pageRect(ctx, page, rect);
    auto *annots = pdf_dict_get(ctx, page, PDF_NAME(Annots));
    for (int i = 0; i < pdf_array_len(ctx, annots); ++i) {
        auto *object = pdf_array_get(ctx, annots, i);
        if (!pdf_name_eq(ctx, pdf_dict_get(ctx, object, PDF_NAME(Subtype)), PDF_NAME(Link)))
            continue;
        const auto actual = pdf_dict_get_rect(ctx, object, PDF_NAME(Rect));
        if (std::abs(wanted.x0 - actual.x0) < .05 && std::abs(wanted.y0 - actual.y0) < .05 && std::abs(wanted.x1 - actual.x1) < .05 && std::abs(wanted.y1 - actual.y1) < .05) {
            if (found)
                *found = i;
            return object;
        }
    }
    fz_throw(ctx, FZ_ERROR_ARGUMENT, "The selected PDF link no longer exists");
}
}
bool MuPDFGenerator::canEditPdfLinks() const
{
    return true;
}
pdf_obj *MuPDFGenerator::linkDestination(const QString &name, int index, double x, double y, bool remote)
{
    if (!name.isEmpty()) {
        const auto text = name.toUtf8();
        return pdf_new_text_string(m_context, text.constData());
    }
    auto *dest = pdf_new_array(m_context, m_document, 5);
    if (remote)
        pdf_array_push_int(m_context, dest, index);
    else
        pdf_array_push(m_context, dest, pdf_lookup_page_obj(m_context, m_document, index));
    pdf_array_push(m_context, dest, PDF_NAME(XYZ));
    fz_point target = {float(x), float(y)};
    if (!remote) {
        fz_rect box;
        fz_matrix matrix;
        pdf_page_obj_transform(m_context, pdf_lookup_page_obj(m_context, m_document, index), &box, &matrix);
        box = fz_transform_rect(box, matrix);
        target = fz_transform_point(fz_make_point(box.x0 + x * (box.x1 - box.x0), box.y0 + y * (box.y1 - box.y0)), fz_invert_matrix(matrix));
    }
    pdf_array_push_real(m_context, dest, target.x);
    pdf_array_push_real(m_context, dest, target.y);
    pdf_array_push(m_context, dest, PDF_NULL);
    return dest;
}
bool MuPDFGenerator::setNamedDestination(const QString &name, int index, double x, double y, QString *error)
{
    if (name.isEmpty())
        return false;
    const auto key = name.toUtf8();
    return operation(
        [&] {
            auto *dict = destinations(m_context, m_document);
            pdf_dict_puts_drop(m_context, dict, key.constData(), linkDestination({}, index, x, y));
        },
        error);
}
bool MuPDFGenerator::deleteNamedDestination(const QString &name, QString *error)
{
    const auto key = name.toUtf8();
    return operation([&] { pdf_dict_dels(m_context, destinations(m_context, m_document), key.constData()); }, error);
}
bool MuPDFGenerator::renameNamedDestination(const QString &from, const QString &to, QString *error)
{
    if (to.isEmpty())
        return false;
    const auto oldKey = from.toUtf8(), newKey = to.toUtf8();
    return operation(
        [&] {
            auto *dict = destinations(m_context, m_document);
            auto *target = pdf_dict_gets(m_context, dict, oldKey.constData());
            if (!target || pdf_dict_gets(m_context, dict, newKey.constData()))
                fz_throw(m_context, FZ_ERROR_ARGUMENT, "Unknown or duplicate destination name");
            pdf_dict_puts(m_context, dict, newKey.constData(), target);
            pdf_dict_dels(m_context, dict, oldKey.constData());
            const auto replace = [&](pdf_obj *object, pdf_obj *key) {
                auto *value = pdf_dict_get(m_context, object, key);
                const auto name = pdf_is_name(m_context, value) ? QString::fromUtf8(pdf_to_name(m_context, value)) : QString::fromUtf8(pdf_to_text_string(m_context, value));
                if (name == from)
                    pdf_dict_put_text_string(m_context, object, key, newKey.constData());
            };
            for (int i = 1; i < pdf_xref_len(m_context, m_document); ++i) {
                pdf_obj *object = pdf_load_object(m_context, m_document, i);
                if (pdf_is_dict(m_context, object)) {
                    replace(object, PDF_NAME(Dest));
                    auto *action = pdf_dict_get(m_context, object, PDF_NAME(A));
                    if (pdf_name_eq(m_context, pdf_dict_get(m_context, action, PDF_NAME(S)), PDF_NAME(GoTo)))
                        replace(action, PDF_NAME(D));
                    if (pdf_name_eq(m_context, pdf_dict_get(m_context, object, PDF_NAME(S)), PDF_NAME(GoTo)))
                        replace(object, PDF_NAME(D));
                }
                pdf_drop_obj(m_context, object);
            }
        },
        error);
}
bool MuPDFGenerator::refreshPageLinks(Okular::Page *page, int index, QString *error)
{
    return operation(
        [&] {
            pdf_sync_links(m_context, m_pages.at(index));
            loadLinks(m_pages.at(index), page);
        },
        error);
}
bool MuPDFGenerator::updateLink(Okular::Page *page, const QRectF &rect, bool create, const char *kind, const QString &target, int index, double x, double y, QString *error, const QString &remoteName, Okular::PdfLinkAppearance appearance)
{
    if (!validAppearance(appearance, error))
        return false;
    if (!page)
        return false;
    const auto bytes = target.toUtf8();
    const bool ok = operation(
        [&] {
            auto *pageObject = pdf_lookup_page_obj(m_context, m_document, page->number());
            auto *annots = pdf_dict_get(m_context, pageObject, PDF_NAME(Annots));
            if (!annots)
                annots = pdf_dict_put_array(m_context, pageObject, PDF_NAME(Annots), 4);
            pdf_obj *link;
            if (create) {
                auto *object = pdf_new_dict(m_context, m_document, 6);
                link = pdf_add_object(m_context, m_document, object);
                pdf_drop_obj(m_context, object);
                pdf_array_push_drop(m_context, annots, link);
                pdf_dict_put_name(m_context, link, PDF_NAME(Type), "Annot");
                pdf_dict_put_name(m_context, link, PDF_NAME(Subtype), "Link");
                pdf_dict_put_rect(m_context, link, PDF_NAME(Rect), pageRect(m_context, pageObject, rect));
                auto *border = pdf_dict_put_array(m_context, link, PDF_NAME(Border), 3);
                for (int i = 0; i < 3; ++i)
                    pdf_array_push_int(m_context, border, 0);
            } else
                link = findLink(m_context, pageObject, rect);
            pdf_dict_del(m_context, link, PDF_NAME(Dest));
            auto *action = pdf_dict_put_dict(m_context, link, PDF_NAME(A), 3);
            pdf_dict_put_name(m_context, action, PDF_NAME(S), kind);
            if (qstrcmp(kind, "URI") == 0)
                pdf_dict_put_text_string(m_context, action, PDF_NAME(URI), bytes.constData());
            else {
                const bool remote = qstrcmp(kind, "GoToR") == 0;
                if (remote)
                    pdf_dict_put_text_string(m_context, action, PDF_NAME(F), bytes.constData());
                pdf_dict_put_drop(m_context, action, PDF_NAME(D), linkDestination(remote ? remoteName : target, index, x, y, remote));
            }
            applyAppearance(m_context, m_document, link, appearance);
        },
        error);
    return ok && refreshPageLinks(page, page->number(), error);
}
bool MuPDFGenerator::createInternalLink(Okular::Page *p, double l, double t, double r, double b, const QString &name, int index, double x, double y, QString *e, Okular::PdfLinkAppearance a)
{
    return updateLink(p, QRectF(QPointF(l, t), QPointF(r, b)), true, "GoTo", name, index, x, y, e, {}, a);
}
bool MuPDFGenerator::editInternalLinkDestination(Okular::Page *p, double l, double t, double r, double b, const QString &name, int index, double x, double y, QString *e)
{
    return updateLink(p, QRectF(QPointF(l, t), QPointF(r, b)), false, "GoTo", name, index, x, y, e);
}
bool MuPDFGenerator::createExternalLink(Okular::Page *p, double l, double t, double r, double b, const QString &url, QString *e)
{
    return updateLink(p, QRectF(QPointF(l, t), QPointF(r, b)), true, "URI", url, 0, 0, 0, e);
}
bool MuPDFGenerator::editExternalLinkDestination(Okular::Page *p, double l, double t, double r, double b, const QString &url, QString *e)
{
    return updateLink(p, QRectF(QPointF(l, t), QPointF(r, b)), false, "URI", url, 0, 0, 0, e);
}
bool MuPDFGenerator::createRemoteLink(Okular::Page *p, double l, double t, double r, double b, const QString &file, int index, double x, double y, QString *e, const QString &name)
{
    return updateLink(p, QRectF(QPointF(l, t), QPointF(r, b)), true, "GoToR", file, index, x, y, e, name);
}
bool MuPDFGenerator::editRemoteLinkDestination(Okular::Page *p, double l, double t, double r, double b, const QString &file, int index, double x, double y, QString *e, const QString &name)
{
    return updateLink(p, QRectF(QPointF(l, t), QPointF(r, b)), false, "GoToR", file, index, x, y, e, name);
}
bool MuPDFGenerator::deletePdfLink(Okular::Page *page, double l, double t, double r, double b, QString *error)
{
    if (!page)
        return false;
    const bool ok = operation(
        [&] {
            auto *object = pdf_lookup_page_obj(m_context, m_document, page->number());
            int index;
            findLink(m_context, object, QRectF(QPointF(l, t), QPointF(r, b)), &index);
            pdf_array_delete(m_context, pdf_dict_get(m_context, object, PDF_NAME(Annots)), index);
        },
        error);
    return ok && refreshPageLinks(page, page->number(), error);
}
bool MuPDFGenerator::editPdfLinkRectangle(Okular::Page *page, double l, double t, double r, double b, double nl, double nt, double nr, double nb, QString *error, Okular::PdfLinkAppearance appearance)
{
    if (!validAppearance(appearance, error))
        return false;
    if (!page)
        return false;
    const bool ok = operation(
        [&] {
            auto *object = pdf_lookup_page_obj(m_context, m_document, page->number());
            auto *link = findLink(m_context, object, QRectF(QPointF(l, t), QPointF(r, b)));
            pdf_dict_put_rect(m_context, link, PDF_NAME(Rect), pageRect(m_context, object, QRectF(QPointF(nl, nt), QPointF(nr, nb))));
            applyAppearance(m_context, m_document, link, appearance);
        },
        error);
    return ok && refreshPageLinks(page, page->number(), error);
}
