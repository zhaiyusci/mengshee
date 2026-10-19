// SPDX-License-Identifier: AGPL-3.0-or-later
#include "generator_mupdf.h"
#include <QJsonDocument>
#include <QMutex>
#include <QSet>
#include <QUuid>
#include <cmath>
#include <core/numberedcalloutgeometry_p.h>

namespace
{
QString string(fz_context *ctx, pdf_obj *object)
{
    return QString::fromUtf8(pdf_to_text_string(ctx, object));
}
void put(fz_context *ctx, pdf_obj *dict, const char *key, const QString &text)
{
    const auto bytes = text.toUtf8();
    pdf_dict_puts_drop(ctx, dict, key, pdf_new_text_string(ctx, bytes.constData()));
}
pdf_obj *views(fz_context *ctx, pdf_document *doc, int page)
{
    auto *metadata = pdf_dict_gets(ctx, pdf_lookup_page_obj(ctx, doc, page), "MengsheeViews");
    if (metadata && (!pdf_is_dict(ctx, metadata) || pdf_to_int(ctx, pdf_dict_gets(ctx, metadata, "Version")) != 1 || !pdf_is_array(ctx, pdf_dict_gets(ctx, metadata, "Items"))))
        fz_throw(ctx, FZ_ERROR_FORMAT, "Unsupported or malformed Reading Views metadata");
    return metadata;
}
}
bool MuPDFGenerator::canEditReadingViews() const
{
    return true;
}
bool MuPDFGenerator::canEditNumberedCalloutNumbering() const
{
    return true;
}
bool MuPDFGenerator::validateNumberedCalloutLabel(const QString &label, QString *error) const
{
    const bool valid = Okular::NumberedCalloutGeometry::badgeLabel(1, label).valid;
    if (!valid && error)
        *error = QStringLiteral("The numbered callout label cannot be rendered.");
    return valid;
}
QString MuPDFGenerator::numberedCalloutNumberingJson() const
{
    QString result;
    const_cast<MuPDFGenerator *>(this)->readOperation([&] { result = string(m_context, pdf_dict_getp(m_context, pdf_trailer(m_context, m_document), "Root/MengsheeNumberedCalloutNumbering")); }, nullptr);
    return result;
}
bool MuPDFGenerator::setNumberedCalloutNumberingJson(const QString &json, QString *error)
{
    if (json.toUtf8().size() > 16384 || (!json.isEmpty() && !QJsonDocument::fromJson(json.toUtf8()).isObject())) {
        if (error)
            *error = QStringLiteral("Invalid numbered callout numbering metadata.");
        return false;
    }
    return operation(
        [&] {
            auto *catalog = pdf_dict_get(m_context, pdf_trailer(m_context, m_document), PDF_NAME(Root));
            if (json.isEmpty())
                pdf_dict_dels(m_context, catalog, "MengsheeNumberedCalloutNumbering");
            else
                put(m_context, catalog, "MengsheeNumberedCalloutNumbering", json);
        },
        error);
}
QList<Okular::ReadingView> MuPDFGenerator::readingViews(int index, QString *error) const
{
    QList<Okular::ReadingView> result;
    const bool ok = const_cast<MuPDFGenerator *>(this)->readOperation(
        [&] {
            auto *metadata = views(m_context, m_document, index);
            if (!metadata)
                return;
            fz_rect box;
            fz_matrix matrix;
            pdf_page_obj_transform(m_context, pdf_lookup_page_obj(m_context, m_document, index), &box, &matrix);
            box = fz_transform_rect(box, matrix);
            auto *items = pdf_dict_gets(m_context, metadata, "Items");
            const int count = pdf_array_len(m_context, items);
            if (count > 10000)
                fz_throw(m_context, FZ_ERROR_LIMIT, "Too many Reading Views");
            QSet<QString> identifiers;
            for (int i = 0; i < count; ++i) {
                auto *item = pdf_array_get(m_context, items, i);
                const QString id = string(m_context, pdf_dict_gets(m_context, item, "Id"));
                const int number = pdf_to_int(m_context, pdf_dict_gets(m_context, item, "Number"));
                if (QUuid(id).isNull() || identifiers.contains(id) || number <= 0)
                    fz_throw(m_context, FZ_ERROR_FORMAT, "Invalid Reading View identity");
                identifiers.insert(id);
                const auto rect = fz_transform_rect(pdf_to_rect(m_context, pdf_dict_get(m_context, item, PDF_NAME(Rect))), matrix);
                result.append({id, number, {(rect.x0 - box.x0) / (box.x1 - box.x0), (rect.y0 - box.y0) / (box.y1 - box.y0), (rect.x1 - box.x0) / (box.x1 - box.x0), (rect.y1 - box.y0) / (box.y1 - box.y0)}});
            }
        },
        error);
    if (!ok)
        result.clear();
    return result;
}
bool MuPDFGenerator::setReadingViews(int index, const QList<Okular::ReadingView> &definitions, QString *error)
{
    if (definitions.size() > 10000)
        return false;
    pdf_obj *metadata = nullptr;
    fz_var(metadata);
    return operation(
        [&] {
            auto *previous = views(m_context, m_document, index);
            auto *page = pdf_lookup_page_obj(m_context, m_document, index);
            fz_rect box;
            fz_matrix matrix;
            pdf_page_obj_transform(m_context, page, &box, &matrix);
            box = fz_transform_rect(box, matrix);
            const auto inverse = fz_invert_matrix(matrix);
            QString token = string(m_context, pdf_dict_gets(m_context, previous, "PageId"));
            if (token.isEmpty())
                token = QUuid::createUuid().toString(QUuid::WithoutBraces);
            fz_try(m_context)
            {
                metadata = pdf_new_dict(m_context, m_document, 3);
                pdf_dict_puts_drop(m_context, metadata, "Version", pdf_new_int(m_context, 1));
                put(m_context, metadata, "PageId", token);
                auto *items = pdf_new_array(m_context, m_document, definitions.size());
                pdf_dict_puts_drop(m_context, metadata, "Items", items);
                QSet<QString> ids;
                for (const auto &view : definitions) {
                    if (QUuid(view.id).isNull() || ids.contains(view.id) || view.number <= 0)
                        fz_throw(m_context, FZ_ERROR_ARGUMENT, "Invalid Reading View identity");
                    ids.insert(view.id);
                    const auto r = view.rectangle;
                    auto rect = fz_make_rect(box.x0 + r.left * (box.x1 - box.x0), box.y0 + r.top * (box.y1 - box.y0), box.x0 + r.right * (box.x1 - box.x0), box.y0 + r.bottom * (box.y1 - box.y0));
                    if (!fz_is_valid_rect(rect) || fz_is_empty_rect(rect) || fz_is_infinite_rect(rect))
                        fz_throw(m_context, FZ_ERROR_ARGUMENT, "Invalid Reading View rectangle");
                    auto *item = pdf_array_push_dict(m_context, items, 3);
                    put(m_context, item, "Id", view.id);
                    pdf_dict_puts_drop(m_context, item, "Number", pdf_new_int(m_context, view.number));
                    pdf_dict_put_rect(m_context, item, PDF_NAME(Rect), fz_transform_rect(rect, inverse));
                }
                pdf_dict_puts(m_context, page, "MengsheeViews", metadata);
            }
            fz_always(m_context)
            {
                pdf_drop_obj(m_context, metadata);
            }
            fz_catch(m_context)
            {
                fz_rethrow(m_context);
            }
        },
        error);
}
QString MuPDFGenerator::readingViewPageToken(int index) const
{
    QString token;
    const_cast<MuPDFGenerator *>(this)->readOperation([&] { token = string(m_context, pdf_dict_gets(m_context, views(m_context, m_document, index), "PageId")); }, nullptr);
    return token;
}
int MuPDFGenerator::readingViewPageForToken(const QString &token) const
{
    int found = -1;
    for (int i = 0; i < m_pages.size(); ++i)
        if (readingViewPageToken(i) == token) {
            if (found >= 0)
                return -1;
            found = i;
        }
    return token.isEmpty() ? -1 : found;
}
