// SPDX-License-Identifier: AGPL-3.0-or-later
#include "generator_mupdf.h"
#include <QDomDocument>
#include <QMutex>
#include <QUrl>
#include <QUrlQuery>
#include <cmath>
#include <core/page.h>

namespace
{
Okular::DocumentViewport viewport(fz_context *ctx, pdf_document *doc, const char *uri)
{
    const auto dest = fz_resolve_link_dest(ctx, reinterpret_cast<fz_document *>(doc), uri);
    Okular::DocumentViewport result(dest.loc.page);
    if (dest.loc.page >= 0) {
        fz_rect box;
        fz_matrix matrix;
        pdf_page_obj_transform(ctx, pdf_lookup_page_obj(ctx, doc, dest.loc.page), &box, &matrix);
        box = fz_transform_rect(box, matrix);
        result.rePos.enabled = true;
        result.rePos.pos = Okular::DocumentViewport::TopLeft;
        result.rePos.normalizedX = std::isfinite(dest.x) ? dest.x / (box.x1 - box.x0) : 0;
        result.rePos.normalizedY = std::isfinite(dest.y) ? dest.y / (box.y1 - box.y0) : 0;
    }
    return result;
}
}
void MuPDFGenerator::clearAnnotationLists()
{
    for (auto *list : std::as_const(m_annotationLists))
        fz_drop_display_list(m_context, list);
    m_annotationLists.clear();
}

void MuPDFGenerator::renderAnnotations(pdf_page *page, fz_pixmap *pixmap, fz_matrix transform, fz_cookie *cookie)
{
    // MuPDF excludes Link annotations from its annotation list. Preserve PDF
    // stacking order while explicitly painting their optional appearances.
    QHash<pdf_obj *, pdf_annot *> native;
    for (auto *a = pdf_first_annot(m_context, page); a; a = pdf_next_annot(m_context, a))
        native.insert(pdf_resolve_indirect(m_context, pdf_annot_obj(m_context, a)), a);
    for (auto *a = pdf_first_widget(m_context, page); a; a = pdf_next_widget(m_context, a))
        native.insert(pdf_resolve_indirect(m_context, pdf_annot_obj(m_context, a)), a);
    auto *annots = pdf_dict_get(m_context, page->obj, PDF_NAME(Annots));
    const auto paintOne = [&](pdf_obj *object, fz_device *device) {
        if (auto *a = native.value(pdf_resolve_indirect(m_context, object))) {
            pdf_run_annot(m_context, a, device, fz_identity, cookie);
            return;
        }
        if (!pdf_name_eq(m_context, pdf_dict_get(m_context, object, PDF_NAME(Subtype)), PDF_NAME(Link)))
            return;
        if (pdf_dict_get_int(m_context, object, PDF_NAME(F)) & (PDF_ANNOT_IS_HIDDEN | PDF_ANNOT_IS_INVISIBLE | PDF_ANNOT_IS_NO_VIEW))
            return;
        auto *ap = pdf_dict_getp(m_context, object, "AP/N");
        if (pdf_is_dict(m_context, ap) && !pdf_is_stream(m_context, ap))
            ap = pdf_dict_get(m_context, ap, pdf_dict_get(m_context, object, PDF_NAME(AS)));
        if (!pdf_is_stream(m_context, ap))
            return;
        const auto rect = pdf_dict_get_rect(m_context, object, PDF_NAME(Rect));
        const auto box = fz_transform_rect(pdf_dict_get_rect(m_context, ap, PDF_NAME(BBox)), pdf_dict_get_matrix(m_context, ap, PDF_NAME(Matrix)));
        if (fz_is_empty_rect(box) || !fz_is_valid_rect(box))
            return;
        const float sx = (rect.x1 - rect.x0) / (box.x1 - box.x0), sy = (rect.y1 - rect.y0) / (box.y1 - box.y0);
        const auto placement = fz_make_matrix(sx, 0, 0, sy, rect.x0 - box.x0 * sx, rect.y0 - box.y0 * sy);
        fz_rect media;
        fz_matrix pageMatrix;
        pdf_page_transform(m_context, page, &media, &pageMatrix);
        pdf_obj *resources = nullptr;
        fz_buffer *buffer = nullptr;
        pdf_processor *processor = nullptr;
        fz_var(resources);
        fz_var(buffer);
        fz_var(processor);
        fz_try(m_context)
        {
            resources = pdf_new_dict(m_context, m_document, 1);
            auto *xobjects = pdf_dict_put_dict(m_context, resources, PDF_NAME(XObject), 1);
            pdf_dict_puts(m_context, xobjects, "LinkAppearance", ap);
            const char bytes[] = "q /LinkAppearance Do Q";
            buffer = fz_new_buffer_from_copied_data(m_context, reinterpret_cast<const unsigned char *>(bytes), sizeof(bytes) - 1);
            processor = pdf_new_run_processor(m_context, m_document, device, fz_concat(placement, pageMatrix), -1, "View", nullptr, nullptr, nullptr, nullptr, nullptr);
            pdf_process_glyph(m_context, processor, m_document, resources, buffer);
            pdf_close_processor(m_context, processor);
        }
        fz_always(m_context)
        {
            pdf_drop_processor(m_context, processor);
            pdf_drop_obj(m_context, resources);
            fz_drop_buffer(m_context, buffer);
        }
        fz_catch(m_context)
        {
            fz_rethrow(m_context);
        }
    };
    const auto scissor = fz_expand_rect(fz_rect_from_irect(fz_pixmap_bbox(m_context, pixmap)), 1);
    for (int i = 0; i < pdf_array_len(m_context, annots); ++i) {
        if (cookie && cookie->abort)
            break;
        auto *object = pdf_resolve_indirect(m_context, pdf_array_get(m_context, annots, i));
        if (!m_annotationLists.contains(object)) {
            // Record each appearance independently. A broken one never reaches
            // the draw device and cannot leave graphics state behind.
            fz_display_list *list = nullptr;
            fz_device *recording = nullptr;
            fz_var(list);
            fz_var(recording);
            fz_try(m_context) {
                list = fz_new_display_list(m_context, pdf_bound_page(m_context, page, FZ_CROP_BOX));
                recording = fz_new_list_device(m_context, list);
                paintOne(object, recording);
                fz_close_device(m_context, recording);
                if (cookie && cookie->abort)
                    fz_throw(m_context, FZ_ERROR_ABORT, "Render cancelled");
            }
            fz_always(m_context) { fz_drop_device(m_context, recording); }
            fz_catch(m_context) {
                fz_drop_display_list(m_context, list);
                list = nullptr;
                fz_rethrow_if(m_context, FZ_ERROR_SYSTEM);
                fz_rethrow_if(m_context, FZ_ERROR_ABORT);
                reportAnnotationError(QString::fromUtf8(fz_caught_message(m_context)));
            }
            // Cache even failed appearances until the next edit.
            m_annotationLists.insert(object, list);
        }
        auto *list = m_annotationLists.value(object);
        if (!list)
            continue;
        fz_device *device = nullptr;
        fz_var(device);
        fz_try(m_context) {
            device = fz_new_draw_device(m_context, fz_identity, pixmap);
            fz_run_display_list(m_context, list, device, transform, scissor, cookie);
            fz_close_device(m_context, device);
        }
        fz_always(m_context) { fz_drop_device(m_context, device); }
        fz_catch(m_context) {
            fz_rethrow_if(m_context, FZ_ERROR_SYSTEM);
            fz_rethrow_if(m_context, FZ_ERROR_ABORT);
            reportAnnotationError(QString::fromUtf8(fz_caught_message(m_context)));
        }
    }
}

void MuPDFGenerator::loadLinks(pdf_page *native, Okular::Page *page)
{
    fz_link *links = nullptr;
    fz_var(links);
    QList<Okular::ObjectRect *> rectangles;
    fz_try(m_context)
    {
        links = pdf_load_links(m_context, native);
        const auto box = pdf_bound_page(m_context, native, FZ_CROP_BOX);
        for (auto *link = links; link; link = link->next) {
            Okular::Action *action = nullptr;
            const QUrl url(QString::fromUtf8(link->uri));
            const QUrlQuery destination(url.fragment());
            if (link->uri && link->uri[0] == '#') {
                const auto name = destination.queryItemValue(QStringLiteral("nameddest"), QUrl::FullyDecoded);
                action = name.isEmpty() ? new Okular::GotoAction(QString(), viewport(m_context, m_document, link->uri)) : new Okular::GotoAction(QString(), name);
            } else if (url.scheme() == QLatin1String("file")) {
                const auto name = destination.queryItemValue(QStringLiteral("nameddest"), QUrl::FullyDecoded);
                const auto file = url.toLocalFile();
                if (!name.isEmpty())
                    action = new Okular::GotoAction(file, name);
                else {
                    bool validPage;
                    const int number = destination.queryItemValue(QStringLiteral("page")).toInt(&validPage);
                    action = new Okular::GotoAction(file, Okular::DocumentViewport(validPage ? qMax(0, number - 1) : 0));
                }
            } else
                action = new Okular::BrowseAction(url);
            rectangles.append(new Okular::ObjectRect((link->rect.x0 - box.x0) / (box.x1 - box.x0),
                                                     (link->rect.y0 - box.y0) / (box.y1 - box.y0),
                                                     (link->rect.x1 - box.x0) / (box.x1 - box.x0),
                                                     (link->rect.y1 - box.y0) / (box.y1 - box.y0),
                                                     false,
                                                     Okular::ObjectRect::Action,
                                                     action));
        }
    }
    fz_always(m_context)
    {
        fz_drop_link(m_context, links);
    }
    fz_catch(m_context)
    {
        qDeleteAll(rectangles);
        fz_rethrow(m_context);
    }
    page->setObjectRects(rectangles);
}
const Okular::DocumentSynopsis *MuPDFGenerator::generateDocumentSynopsis()
{
    fz_outline *outline = nullptr;
    fz_var(outline);
    m_synopsis = Okular::DocumentSynopsis();
    const bool ok = operation(
        [&] {
            fz_try(m_context)
            {
                outline = fz_load_outline(m_context, reinterpret_cast<fz_document *>(m_document));
                std::function<void(fz_outline *, pdf_obj *, QDomNode)> append = [&](fz_outline *entry, pdf_obj *raw, QDomNode parent) {
                    for (; entry; entry = entry->next, raw = pdf_dict_get(m_context, raw, PDF_NAME(Next))) {
                        auto element = m_synopsis.createElement(QString::fromUtf8(entry->title));
                        if (entry->uri && entry->uri[0] == '#')
                            element.setAttribute(QStringLiteral("Viewport"), viewport(m_context, m_document, entry->uri).toString());
                        element.setAttribute(QStringLiteral("Open"), entry->is_open);
                        auto *destination = pdf_dict_get(m_context, raw, PDF_NAME(Dest));
                        if (!destination)
                            destination = pdf_dict_getp(m_context, raw, "A/D");
                        if (pdf_is_name(m_context, destination) || pdf_is_string(m_context, destination))
                            element.setAttribute(QStringLiteral("ViewportName"), QString::fromUtf8(pdf_is_name(m_context, destination) ? pdf_to_name(m_context, destination) : pdf_to_text_string(m_context, destination)));
                        if (entry->uri && entry->uri[0] != '#')
                            element.setAttribute(QStringLiteral("URL"), QString::fromUtf8(entry->uri));
                        parent.appendChild(element);
                        if (entry->down)
                            append(entry->down, pdf_dict_get(m_context, raw, PDF_NAME(First)), element);
                    }
                };
                append(outline, pdf_dict_getp(m_context, pdf_trailer(m_context, m_document), "Root/Outlines/First"), m_synopsis);
            }
            fz_always(m_context)
            {
                fz_drop_outline(m_context, outline);
            }
            fz_catch(m_context)
            {
                fz_rethrow(m_context);
            }
        },
        nullptr);
    return ok ? &m_synopsis : nullptr;
}
bool MuPDFGenerator::setDocumentSynopsis(const Okular::DocumentSynopsis &synopsis, QString *error)
{
    // Validate the entire model before changing catalog entries.
    std::function<bool(const QDomNode &, int)> validate = [&](const QDomNode &parent, int depth) {
        if (depth > 128) {
            if (error)
                *error = QStringLiteral("The contents tree is too deep.");
            return false;
        }
        for (auto item = parent.firstChildElement(); !item.isNull(); item = item.nextSiblingElement()) {
            const auto name = item.attribute(QStringLiteral("ViewportName"));
            const Okular::DocumentViewport target(item.attribute(QStringLiteral("Viewport")));
            if (!item.attribute(QStringLiteral("URL")).isEmpty() || !item.attribute(QStringLiteral("ExternalFileName")).isEmpty() ||
                ((name.isEmpty() || item.attribute(QStringLiteral("CreateViewportName")) == QLatin1String("true")) && (!target.isValid() || target.pageNumber >= m_pages.size()))) {
                if (error)
                    *error = QStringLiteral("Contents entries must point to a valid page or named destination.");
                return false;
            }
            if (!validate(item, depth + 1))
                return false;
        }
        return true;
    };
    if (!validate(synopsis, 0))
        return false;
    pdf_obj *root = nullptr, *pending = nullptr;
    fz_var(root);
    fz_var(pending);
    const bool ok = operation(
        [&] {
            fz_try(m_context)
            {
                root = pdf_add_new_dict(m_context, m_document, 4);
                pending = pdf_new_dict(m_context, m_document, 4);
                pdf_dict_put(m_context, root, PDF_NAME(Type), PDF_NAME(Outlines));
                std::function<int(const QDomNode &, pdf_obj *)> append = [&](const QDomNode &parent, pdf_obj *nativeParent) {
                    pdf_obj *previous = nullptr;
                    int total = 0;
                    for (auto item = parent.firstChildElement(); !item.isNull(); item = item.nextSiblingElement()) {
                        auto *entry = pdf_add_new_dict(m_context, m_document, 8);
                        // The parent's chain owns each reference before filling its fields.
                        if (previous) {
                            pdf_dict_put(m_context, entry, PDF_NAME(Prev), previous);
                            pdf_dict_put(m_context, previous, PDF_NAME(Next), entry);
                        } else
                            pdf_dict_put(m_context, nativeParent, PDF_NAME(First), entry);
                        pdf_dict_put(m_context, nativeParent, PDF_NAME(Last), entry);
                        pdf_drop_obj(m_context, entry);
                        pdf_dict_put(m_context, entry, PDF_NAME(Parent), nativeParent);
                        const auto title = item.tagName().toUtf8();
                        pdf_dict_put_text_string(m_context, entry, PDF_NAME(Title), title.constData());
                        const auto name = item.attribute(QStringLiteral("ViewportName"));
                        const Okular::DocumentViewport target(item.attribute(QStringLiteral("Viewport")));
                        const double x = target.rePos.enabled ? target.rePos.normalizedX : 0;
                        const double y = target.rePos.enabled ? target.rePos.normalizedY : 0;
                        if (!name.isEmpty() && item.attribute(QStringLiteral("CreateViewportName")) == QLatin1String("true")) {
                            const auto key = name.toUtf8();
                            pdf_dict_puts_drop(m_context, pending, key.constData(), linkDestination({}, target.pageNumber, x, y));
                        }
                        pdf_dict_put_drop(m_context, entry, PDF_NAME(Dest), linkDestination(name, target.pageNumber, x, y));
                        const int children = append(item, entry);
                        total += children + 1;
                        if (children)
                            pdf_dict_put_int(m_context, entry, PDF_NAME(Count), children);
                        previous = entry;
                    }
                    return total;
                };
                pdf_dict_put_int(m_context, root, PDF_NAME(Count), append(synopsis, root));
                auto *catalog = pdf_dict_get(m_context, pdf_trailer(m_context, m_document), PDF_NAME(Root));
                auto *names = pdf_dict_get(m_context, catalog, PDF_NAME(Dests));
                if (pdf_dict_len(m_context, pending) && !names)
                    names = pdf_dict_put_dict(m_context, catalog, PDF_NAME(Dests), 4);
                for (int i = 0; i < pdf_dict_len(m_context, pending); ++i)
                    pdf_dict_put(m_context, names, pdf_dict_get_key(m_context, pending, i), pdf_dict_get_val(m_context, pending, i));
                pdf_dict_put(m_context, catalog, PDF_NAME(Outlines), root);
            }
            fz_always(m_context)
            {
                pdf_drop_obj(m_context, root);
                pdf_drop_obj(m_context, pending);
            }
            fz_catch(m_context)
            {
                fz_rethrow(m_context);
            }
        },
        error);
    if (ok)
        m_synopsis = synopsis;
    return ok;
}

QVariant MuPDFGenerator::metaData(const QString &key, const QVariant &option) const
{
    if (key == QLatin1String("NamedViewports")) {
        QVariantList result;
        const_cast<MuPDFGenerator *>(this)->readOperation(
            [&] {
                QStringList names;
                const auto collect = [&](pdf_obj *dict) {
                    for (int i = 0; i < pdf_dict_len(m_context, dict); ++i)
                        names.append(QString::fromUtf8(pdf_to_name(m_context, pdf_dict_get_key(m_context, dict, i))));
                };
                collect(pdf_dict_getp(m_context, pdf_trailer(m_context, m_document), "Root/Dests"));
                auto *tree = pdf_load_name_tree(m_context, m_document, PDF_NAME(Dests));
                collect(tree);
                pdf_drop_obj(m_context, tree);
                names.removeDuplicates();
                names.sort();
                for (const auto &name : names) {
                    const auto uri = (QStringLiteral("#nameddest=") + QString::fromLatin1(QUrl::toPercentEncoding(name))).toUtf8();
                    const auto target = viewport(m_context, m_document, uri.constData());
                    if (target.isValid())
                        result.append(QVariantMap {{QStringLiteral("name"), name}, {QStringLiteral("viewport"), target.toString()}});
                }
            },
            nullptr);
        return result;
    }
    if (key == QLatin1String("DocumentTitle")) {
        QString title;
        const_cast<MuPDFGenerator *>(this)->readOperation([&] { title = QString::fromUtf8(pdf_to_text_string(m_context, pdf_dict_getp(m_context, pdf_trailer(m_context, m_document), "Info/Title"))); }, nullptr);
        return title;
    }
    if (key == QLatin1String("NamedViewport")) {
        QString result;
        const auto name = (QStringLiteral("#nameddest=") + QString::fromLatin1(QUrl::toPercentEncoding(option.toString()))).toUtf8();
        const_cast<MuPDFGenerator *>(this)->readOperation([&] { result = viewport(m_context, m_document, name.constData()).toString(); }, nullptr);
        return result;
    }
    return Generator::metaData(key, option);
}
