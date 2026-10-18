// SPDX-License-Identifier: AGPL-3.0-or-later
#include "pageimport.h"
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QUuid>

namespace
{
class Importer
{
public:
    fz_context *ctx;
    pdf_document *dst, *src;
    QHash<int, pdf_obj *> objects;
    QSet<int> pageNumbers;
    QHash<QByteArray, QByteArray> renamed;
    QList<pdf_obj *> output;
    Importer(fz_context *ctx, pdf_document *dst, pdf_document *src)
        : ctx(ctx)
        , dst(dst)
        , src(src)
    {
    }
    void clear()
    {
        for (auto *object : objects)
            pdf_drop_obj(ctx, object);
        objects.clear();
    }
    pdf_obj *clone(pdf_obj *object, int depth = 0)
    {
        if (depth > 256)
            fz_throw(ctx, FZ_ERROR_LIMIT, "PDF object graph is too deep to import");
        if (pdf_is_indirect(ctx, object)) {
            const int number = pdf_to_num(ctx, object);
            if (objects.contains(number))
                return pdf_keep_obj(ctx, objects.value(number));
            if (pageNumbers.contains(number))
                return dst == src ? pdf_keep_obj(ctx, object) : PDF_NULL;
            auto *reference = pdf_new_indirect(ctx, dst, pdf_create_object(ctx, dst), 0);
            objects.insert(number, reference);
            auto *copied = clone(pdf_resolve_indirect(ctx, object), depth + 1);
            pdf_update_object(ctx, dst, pdf_to_num(ctx, reference), copied);
            pdf_drop_obj(ctx, copied);
            if (pdf_is_stream(ctx, object)) {
                auto *buffer = pdf_load_stream(ctx, object);
                fz_try(ctx)
                {
                    pdf_dict_del(ctx, reference, PDF_NAME(Filter));
                    pdf_dict_del(ctx, reference, PDF_NAME(DecodeParms));
                    pdf_update_stream(ctx, dst, reference, buffer, 0);
                }
                fz_always(ctx)
                {
                    fz_drop_buffer(ctx, buffer);
                }
                fz_catch(ctx)
                {
                    fz_rethrow(ctx);
                }
            }
            return pdf_keep_obj(ctx, reference);
        }
        pdf_obj *result = nullptr;
        fz_var(result);
        fz_try(ctx)
        {
            if (pdf_is_dict(ctx, object)) {
                result = pdf_new_dict(ctx, dst, pdf_dict_len(ctx, object));
                for (int i = 0; i < pdf_dict_len(ctx, object); ++i)
                    pdf_dict_put_drop(ctx, result, pdf_dict_get_key(ctx, object, i), clone(pdf_dict_get_val(ctx, object, i), depth + 1));
            } else if (pdf_is_array(ctx, object)) {
                result = pdf_new_array(ctx, dst, pdf_array_len(ctx, object));
                for (int i = 0; i < pdf_array_len(ctx, object); ++i)
                    pdf_array_push_drop(ctx, result, clone(pdf_array_get(ctx, object, i), depth + 1));
            } else
                result = pdf_keep_obj(ctx, object);
        }
        fz_catch(ctx)
        {
            pdf_drop_obj(ctx, result);
            fz_rethrow(ctx);
        }
        return result;
    }
    pdf_obj *allDestinations(pdf_document *doc)
    {
        auto *result = pdf_load_name_tree(ctx, doc, PDF_NAME(Dests));
        if (!result)
            result = pdf_new_dict(ctx, doc, 8);
        auto *legacy = pdf_dict_getp(ctx, pdf_trailer(ctx, doc), "Root/Dests");
        for (int i = 0; i < pdf_dict_len(ctx, legacy); ++i)
            pdf_dict_put(ctx, result, pdf_dict_get_key(ctx, legacy, i), pdf_dict_get_val(ctx, legacy, i));
        return result;
    }
    void names(bool suffix, bool replaceExisting)
    {
        pdf_obj *source = nullptr, *target = nullptr;
        fz_var(source);
        fz_var(target);
        fz_try(ctx)
        {
            source = allDestinations(src);
            target = allDestinations(dst);
            for (int i = 0; i < pdf_dict_len(ctx, source); ++i) {
                auto *dest = pdf_dict_get_val(ctx, source, i);
                if (pdf_is_dict(ctx, dest))
                    dest = pdf_dict_get(ctx, dest, PDF_NAME(D));
                auto *page = pdf_array_get(ctx, dest, 0);
                if (!pdf_is_array(ctx, dest) || !objects.contains(pdf_to_num(ctx, page)))
                    continue;
                const QByteArray old = pdf_to_name(ctx, pdf_dict_get_key(ctx, source, i));
                QByteArray name = old;
                if (suffix) {
                    int n = 2;
                    while (pdf_dict_gets(ctx, target, name.constData()))
                        name = old + '_' + QByteArray::number(n++);
                }
                renamed.insert(old, name);
                if (replaceExisting || !pdf_dict_gets(ctx, target, name.constData()))
                    pdf_dict_puts_drop(ctx, target, name.constData(), clone(dest));
            }
            auto *catalog = pdf_dict_get(ctx, pdf_trailer(ctx, dst), PDF_NAME(Root));
            pdf_dict_put(ctx, catalog, PDF_NAME(Dests), target);
            if (auto *tree = pdf_dict_get(ctx, catalog, PDF_NAME(Names)))
                pdf_dict_del(ctx, tree, PDF_NAME(Dests));
        }
        fz_always(ctx)
        {
            pdf_drop_obj(ctx, source);
            pdf_drop_obj(ctx, target);
        }
        fz_catch(ctx)
        {
            fz_rethrow(ctx);
        }
    }
    void rewriteDestination(pdf_obj *object, pdf_obj *key)
    {
        auto *value = pdf_dict_get(ctx, object, key);
        if (!pdf_is_name(ctx, value) && !pdf_is_string(ctx, value))
            return;
        const QByteArray name = pdf_is_name(ctx, value) ? pdf_to_name(ctx, value) : pdf_to_text_string(ctx, value);
        if (renamed.contains(name))
            pdf_dict_put_text_string(ctx, object, key, renamed.value(name).constData());
    }
    bool pruneField(pdf_obj *field, const QSet<pdf_obj *> &selected, int depth = 0)
    {
        if (depth > 128)
            fz_throw(ctx, FZ_ERROR_LIMIT, "Form hierarchy is too deep");
        auto *kids = pdf_dict_get(ctx, field, PDF_NAME(Kids));
        if (kids) {
            for (int i = pdf_array_len(ctx, kids) - 1; i >= 0; --i)
                if (!pruneField(pdf_array_get(ctx, kids, i), selected, depth + 1))
                    pdf_array_delete(ctx, kids, i);
            return pdf_array_len(ctx, kids) > 0;
        }
        return selected.contains(pdf_resolve_indirect(ctx, field));
    }
    void fields()
    {
        QSet<pdf_obj *> selected;
        QList<pdf_obj *> roots, selectedWidgets;
        auto *catalog = pdf_dict_get(ctx, pdf_trailer(ctx, dst), PDF_NAME(Root));
        auto *sourceForm = pdf_dict_getp(ctx, pdf_trailer(ctx, src), "Root/AcroForm");
        for (auto *page : output) {
            auto *annots = pdf_dict_get(ctx, page, PDF_NAME(Annots));
            for (int i = 0; i < pdf_array_len(ctx, annots); ++i) {
                auto *annot = pdf_array_get(ctx, annots, i);
                if (pdf_name_eq(ctx, pdf_dict_get(ctx, annot, PDF_NAME(Subtype)), PDF_NAME(Widget))) {
                    selected.insert(pdf_resolve_indirect(ctx, annot));
                    selectedWidgets.append(annot);
                }
            }
        }
        if (selected.isEmpty())
            return;
        auto *form = pdf_dict_get(ctx, catalog, PDF_NAME(AcroForm));
        if (!form)
            form = pdf_dict_put_dict(ctx, catalog, PDF_NAME(AcroForm), 4);
        auto *fields = pdf_dict_get(ctx, form, PDF_NAME(Fields));
        if (!fields)
            fields = pdf_dict_put_array(ctx, form, PDF_NAME(Fields), 8);
        auto *dr = pdf_dict_get(ctx, form, PDF_NAME(DR));
        if (!dr)
            dr = pdf_dict_put_dict(ctx, form, PDF_NAME(DR), 4);
        QHash<QString, QString> resourceNames;
        auto *sourceDR = pdf_dict_get(ctx, sourceForm, PDF_NAME(DR));
        const auto prefix = QUuid::createUuid().toString(QUuid::Id128).toLatin1();
        for (int i = 0; i < pdf_dict_len(ctx, sourceDR); ++i) {
            auto *key = pdf_dict_get_key(ctx, sourceDR, i);
            auto *category = pdf_dict_get_val(ctx, sourceDR, i);
            if (!pdf_is_dict(ctx, category))
                continue;
            auto *out = pdf_dict_get(ctx, dr, key);
            if (!out)
                out = pdf_dict_put_dict(ctx, dr, key, 4);
            // Snapshot keys first: source and destination may share AcroForm.
            QList<QByteArray> keys;
            for (int j = 0; j < pdf_dict_len(ctx, category); ++j)
                keys.append(pdf_to_name(ctx, pdf_dict_get_key(ctx, category, j)));
            for (const auto &old : keys) {
                const QByteArray name = prefix + '_' + old;
                resourceNames.insert(QString::fromLatin1(old), QString::fromLatin1(name));
                pdf_dict_puts_drop(ctx, out, name.constData(), clone(pdf_dict_gets(ctx, category, old.constData())));
            }
        }
        QSet<QString> usedNames;
        for (int i = 0; i < pdf_array_len(ctx, fields); ++i)
            usedNames.insert(QString::fromUtf8(pdf_to_text_string(ctx, pdf_dict_get(ctx, pdf_array_get(ctx, fields, i), PDF_NAME(T)))));
        for (auto *widget : selectedWidgets) {
            auto *field = widget;
            QSet<pdf_obj *> seen;
            while (auto *parent = pdf_dict_get(ctx, field, PDF_NAME(Parent))) {
                if (seen.contains(pdf_resolve_indirect(ctx, parent)))
                    fz_throw(ctx, FZ_ERROR_FORMAT, "Cyclic form hierarchy");
                seen.insert(pdf_resolve_indirect(ctx, parent));
                field = parent;
            }
            if (!roots.contains(field))
                roots.append(field);
            auto *da = pdf_dict_get_inheritable(ctx, widget, PDF_NAME(DA));
            if (!da)
                da = pdf_dict_get(ctx, sourceForm, PDF_NAME(DA));
            QString appearance = QString::fromUtf8(pdf_to_text_string(ctx, da));
            const QRegularExpression token(QStringLiteral("/([^\\s]+)"));
            auto matches = token.globalMatch(appearance);
            QList<QRegularExpressionMatch> positions;
            while (matches.hasNext())
                positions.append(matches.next());
            for (auto it = positions.crbegin(); it != positions.crend(); ++it)
                if (resourceNames.contains(it->captured(1)))
                    appearance.replace(it->capturedStart(1), it->capturedLength(1), resourceNames.value(it->captured(1)));
            const auto bytes = appearance.toUtf8();
            pdf_dict_put_text_string(ctx, widget, PDF_NAME(DA), bytes.constData());
        }
        for (auto *root : roots) {
            pruneField(root, selected);
            QString name = QString::fromUtf8(pdf_to_text_string(ctx, pdf_dict_get(ctx, root, PDF_NAME(T))));
            if (name.isEmpty())
                name = QStringLiteral("ImportedField");
            const auto original = name;
            int suffix = 2;
            while (usedNames.contains(name))
                name = original + QLatin1Char('_') + QString::number(suffix++);
            usedNames.insert(name);
            const auto bytes = name.toUtf8();
            pdf_dict_put_text_string(ctx, root, PDF_NAME(T), bytes.constData());
            pdf_array_push(ctx, fields, root);
        }
    }
    void build(const QList<pdf_obj *> &sources, bool suffix, bool renewIds = true)
    {
        for (int i = 0; i < pdf_count_pages(ctx, src); ++i)
            pageNumbers.insert(pdf_to_num(ctx, pdf_lookup_page_obj(ctx, src, i)));
        for (auto *source : sources) {
            auto *target = pdf_add_new_dict(ctx, dst, 12);
            objects.insert(pdf_to_num(ctx, source), target);
            output.append(target);
        }

        for (int i = 0; i < sources.size(); ++i) {
            auto *source = sources[i];
            auto *target = output[i];
            for (int j = 0; j < pdf_dict_len(ctx, source); ++j) {
                auto *key = pdf_dict_get_key(ctx, source, j);
                if (pdf_name_eq(ctx, key, PDF_NAME(Parent)))
                    continue;
                pdf_dict_put_drop(ctx, target, key, clone(pdf_dict_get_val(ctx, source, j)));
            }
            for (auto *key : {PDF_NAME(Resources), PDF_NAME(MediaBox), PDF_NAME(CropBox), PDF_NAME(Rotate)})
                if (auto *value = pdf_dict_get_inheritable(ctx, source, key))
                    pdf_dict_put_drop(ctx, target, key, clone(value));
            if (auto *views = renewIds ? pdf_dict_gets(ctx, target, "MengsheeViews") : nullptr) {
                const auto uuid = QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
                pdf_dict_puts_drop(ctx, views, "PageId", pdf_new_text_string(ctx, uuid.constData()));
            }
        }

        names(suffix, !renewIds);

        for (auto *page : output) {
            auto *annots = pdf_dict_get(ctx, page, PDF_NAME(Annots));
            for (int i = 0; i < pdf_array_len(ctx, annots); ++i) {
                auto *annot = pdf_array_get(ctx, annots, i);
                pdf_dict_put(ctx, annot, PDF_NAME(P), page);
                const auto uuid = QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
                if (renewIds)
                    pdf_dict_put_text_string(ctx, annot, PDF_NAME(NM), uuid.constData());
                rewriteDestination(annot, PDF_NAME(Dest));
                auto *action = pdf_dict_get(ctx, annot, PDF_NAME(A));
                if (pdf_name_eq(ctx, pdf_dict_get(ctx, action, PDF_NAME(S)), PDF_NAME(GoTo)))
                    rewriteDestination(action, PDF_NAME(D));
            }
        }

        fields();
    }
};
}
QList<pdf_obj *> importMuPDFPages(fz_context *ctx, pdf_document *dst, pdf_document *src, const QList<int> &pages, bool suffix, QString *error)
{
    Importer importer(ctx, dst, src);
    QList<pdf_obj *> result;
    QList<pdf_obj *> sources;
    fz_try(ctx)
    {
        for (int index : pages)
            sources.append(pdf_lookup_page_obj(ctx, src, index));
        importer.build(sources, suffix);
        for (auto *page : importer.output)
            result.append(pdf_keep_obj(ctx, page));
    }
    fz_always(ctx)
    {
        importer.clear();
    }
    fz_catch(ctx)
    {
        for (auto *page : result)
            pdf_drop_obj(ctx, page);
        if (error)
            *error = QString::fromUtf8(fz_caught_message(ctx));
        result.clear();
    }
    return result;
}

QHash<quint64, pdf_obj *> migrateMuPDFPageEdits(fz_context *ctx, pdf_document *dst, pdf_document *src, const QHash<quint64, pdf_obj *> &edits, QString *error)
{
    Importer importer(ctx, dst, src);
    QHash<quint64, pdf_obj *> result;
    QList<pdf_obj *> detached;
    fz_try(ctx)
    {
        // A saved copy preserves page order. Bind visible undo entries to its pages.
        const int count = qMin(pdf_count_pages(ctx, dst), pdf_count_pages(ctx, src));
        for (int i = 0; i < count; ++i)
            importer.objects.insert(pdf_to_num(ctx, pdf_lookup_page_obj(ctx, src, i)), pdf_keep_obj(ctx, pdf_lookup_page_obj(ctx, dst, i)));
        for (auto *page : edits)
            if (!importer.objects.contains(pdf_to_num(ctx, page)) && !detached.contains(page))
                detached.append(page);
        importer.build(detached, false, false);
        for (auto it = edits.cbegin(); it != edits.cend(); ++it)
            result.insert(it.key(), pdf_keep_obj(ctx, importer.objects.value(pdf_to_num(ctx, it.value()))));
    }
    fz_always(ctx)
    {
        importer.clear();
    }
    fz_catch(ctx)
    {
        for (auto *page : result)
            pdf_drop_obj(ctx, page);
        if (error)
            *error = QString::fromUtf8(fz_caught_message(ctx));
        result.clear();
    }
    return result;
}
