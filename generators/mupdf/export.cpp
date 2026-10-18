// SPDX-License-Identifier: AGPL-3.0-or-later
#include "generator_mupdf.h"
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>

namespace
{
pdf_obj *normalAppearance(fz_context *ctx, pdf_obj *annot)
{
    auto *ap = pdf_dict_getp(ctx, annot, "AP/N");
    if (!pdf_is_stream(ctx, ap))
        ap = pdf_dict_get(ctx, ap, pdf_dict_get(ctx, annot, PDF_NAME(AS)));
    return pdf_is_stream(ctx, ap) ? ap : nullptr;
}
void appendStream(fz_context *ctx, pdf_document *doc, pdf_obj *array, const QByteArray &bytes)
{
    auto *buffer = fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char *>(bytes.constData()), bytes.size());
    fz_try(ctx)
    {
        pdf_array_push_drop(ctx, array, pdf_add_stream(ctx, doc, buffer, nullptr, 0));
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
void flattenPage(fz_context *ctx, pdf_document *doc, pdf_obj *page, int &flattened, int &preserved)
{
    auto *annots = pdf_dict_get(ctx, page, PDF_NAME(Annots));
    if (!pdf_array_len(ctx, annots))
        return;
    QSet<pdf_obj *> baked;
    QByteArray drawing;
    const auto number = [](double x) { return QByteArray::number(x, 'g', 12); };
    const QSet<QByteArray> supported = {"FreeText", "Line", "Square", "Circle", "Polygon", "PolyLine", "Highlight", "Underline", "Squiggly", "StrikeOut", "Ink", "Stamp", "Text", "Caret"};
    // Copy inherited/shared resources so sibling pages are unaffected.
    auto *resources = pdf_deep_copy_obj(ctx, pdf_dict_get_inheritable(ctx, page, PDF_NAME(Resources)));
    if (!resources)
        resources = pdf_new_dict(ctx, doc, 1);
    pdf_dict_put_drop(ctx, page, PDF_NAME(Resources), resources);
    auto *xobjects = pdf_dict_get(ctx, resources, PDF_NAME(XObject));
    if (!xobjects)
        xobjects = pdf_dict_put_dict(ctx, resources, PDF_NAME(XObject), 8);
    for (int i = 0; i < pdf_array_len(ctx, annots); ++i) {
        auto *annot = pdf_array_get(ctx, annots, i);
        const QByteArray type = pdf_to_name(ctx, pdf_dict_get(ctx, annot, PDF_NAME(Subtype)));
        const int flags = pdf_dict_get_int(ctx, annot, PDF_NAME(F));
        if (type == "Popup")
            continue;
        const auto rect = pdf_dict_get_rect(ctx, annot, PDF_NAME(Rect));
        // Interactive objects remain annotations after flattening. Their normal
        // position above page contents is not a reason to reject overlapping notes.
        if (type == "Link" || type == "Widget" || (flags & (1 | 2 | 32)))
            continue;
        if (!supported.contains(type))
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "Unsupported visible annotation type: %s", type.constData());
        if (flags & (8 | 256))
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "Dynamic annotation visibility cannot be flattened safely");
        if ((flags & 16) && pdf_to_int(ctx, pdf_dict_get_inheritable(ctx, page, PDF_NAME(Rotate))) % 360)
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "NoRotate annotations on rotated pages cannot be flattened safely");
        for (const char *key : {"IRT", "RT", "OC", "A", "AA", "StructParent", "ExData"})
            if (pdf_dict_gets(ctx, annot, key))
                fz_throw(ctx, FZ_ERROR_ARGUMENT, "Annotation feature cannot be flattened: %s", key);
        auto *ap = normalAppearance(ctx, annot);
        if (!ap)
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "A visible annotation has no usable appearance");
        const auto bounds = fz_transform_rect(pdf_dict_get_rect(ctx, ap, PDF_NAME(BBox)), pdf_dict_get_matrix(ctx, ap, PDF_NAME(Matrix)));
        if (!fz_is_valid_rect(bounds) || fz_is_empty_rect(bounds) || fz_is_infinite_rect(bounds) || !fz_is_valid_rect(rect))
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "Invalid annotation appearance geometry");
        const double sx = (rect.x1 - rect.x0) / (bounds.x1 - bounds.x0), sy = (rect.y1 - rect.y0) / (bounds.y1 - bounds.y0);
        QByteArray name = "MengsheeFlatten" + QByteArray::number(i);
        while (pdf_dict_gets(ctx, xobjects, name.constData()))
            name += 'x';
        pdf_dict_puts(ctx, xobjects, name.constData(), ap);
        drawing += "q " + number(sx) + " 0 0 " + number(sy) + ' ' + number(rect.x0 - bounds.x0 * sx) + ' ' + number(rect.y0 - bounds.y0 * sy) + " cm /" + name + " Do Q\n";
        baked.insert(pdf_resolve_indirect(ctx, annot));
        ++flattened;
    }
    auto *retained = pdf_new_array(ctx, doc, pdf_array_len(ctx, annots));
    for (int i = 0; i < pdf_array_len(ctx, annots); ++i) {
        auto *annot = pdf_array_get(ctx, annots, i);
        if (baked.contains(pdf_resolve_indirect(ctx, annot)))
            continue;
        if (pdf_name_eq(ctx, pdf_dict_get(ctx, annot, PDF_NAME(Subtype)), PDF_NAME(Popup)) && baked.contains(pdf_resolve_indirect(ctx, pdf_dict_get(ctx, annot, PDF_NAME(Parent)))))
            continue;
        pdf_array_push(ctx, retained, annot);
        ++preserved;
    }
    pdf_dict_put_drop(ctx, page, PDF_NAME(Annots), retained);
    if (drawing.isEmpty())
        return;
    auto *old = pdf_dict_get(ctx, page, PDF_NAME(Contents));
    int prepend = 0, append = 0;
    pdf_count_q_balance(ctx, doc, resources, old, &prepend, &append);
    auto *contents = pdf_new_array(ctx, doc, 4);
    fz_try(ctx)
    {
        appendStream(ctx, doc, contents, QByteArray("q\n").repeated(prepend + 1));
        if (pdf_is_array(ctx, old)) {
            for (int i = 0; i < pdf_array_len(ctx, old); ++i)
                pdf_array_push(ctx, contents, pdf_array_get(ctx, old, i));
        } else if (old)
            pdf_array_push(ctx, contents, old);
        appendStream(ctx, doc, contents, QByteArray("Q\n").repeated(append + 1) + drawing);
        pdf_dict_put(ctx, page, PDF_NAME(Contents), contents);
    }
    fz_always(ctx)
    {
        pdf_drop_obj(ctx, contents);
    }
    fz_catch(ctx)
    {
        fz_rethrow(ctx);
    }
}
}
bool MuPDFGenerator::canExportFlattenedPdf() const
{
    return m_document && QThread::currentThread() == thread();
}
bool MuPDFGenerator::exportFlattenedPdf(const QString &output, QString *error, int *flattened, int *preserved)
{
    if (flattened)
        *flattened = 0;
    if (preserved)
        *preserved = 0;
    if (!canExportFlattenedPdf())
        return false;
    bool encrypted = false;
    if (!operation([&] { encrypted = m_document->crypt != nullptr; }, error))
        return false;
    if (encrypted) {
        if (error)
            *error = QStringLiteral("Flattened export of encrypted PDFs is not supported.");
        return false;
    }
    const QFileInfo destination(output);
    if (destination.isSymLink() || (destination.exists() && !destination.isFile())) {
        if (error)
            *error = QStringLiteral("The output must be a regular file.");
        return false;
    }
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return false;
    const auto path = temporary.filePath(QStringLiteral("snapshot.pdf"));
    if (!save(path, SaveChanges, error))
        return false;
    MuPDFGenerator snapshot(nullptr, {});
    QList<Okular::Page *> pages;
    if (snapshot.loadDocumentWithPassword(path, pages, {}) != Okular::Document::OpenSuccess)
        return false;
    qDeleteAll(pages);
    int bakedCount = 0, retainedCount = 0;
    if (!snapshot.operation(
            [&] {
                if (pdf_count_signatures(snapshot.m_context, snapshot.m_document))
                    fz_throw(snapshot.m_context, FZ_ERROR_ARGUMENT, "Signed documents cannot be flattened safely");
                for (int i = 0; i < snapshot.m_pages.size(); ++i)
                    flattenPage(snapshot.m_context, snapshot.m_document, pdf_lookup_page_obj(snapshot.m_context, snapshot.m_document, i), bakedCount, retainedCount);
            },
            error))
        return false;
    if (!snapshot.save(output, SaveChanges, error))
        return false;
    if (flattened)
        *flattened = bakedCount;
    if (preserved)
        *preserved = retainedCount;
    return true;
}
