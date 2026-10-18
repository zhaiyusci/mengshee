// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../poppler/pdfpagebounds.h"
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cmath>
#include <filesystem>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

namespace PdfPageBounds
{
Result expandAppearance(const std::string &input, const std::string &output, double padding)
{
    Result result;
    if (!std::isfinite(padding) || padding < 0 || QFileInfo::exists(QString::fromUtf8(output.c_str()))) {
        result.error = Error::InvalidArguments;
        result.message = "Invalid padding or output already exists.";
        return result;
    }
    fz_context *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) {
        result.error = Error::LimitExceeded;
        result.message = "Could not create MuPDF context.";
        return result;
    }
    pdf_document *doc = nullptr;
    pdf_page *page = nullptr;
    fz_device *device = nullptr;
    fz_rect ink = fz_empty_rect;
    QTemporaryDir temporary(QFileInfo(QString::fromUtf8(output.c_str())).absolutePath() + QStringLiteral("/.mengshee-bounds-XXXXXX"));
    if (!temporary.isValid()) {
        fz_drop_context(ctx);
        result.error = Error::IoError;
        result.message = "Cannot stage appearance output.";
        return result;
    }
    const auto staged = temporary.filePath(QStringLiteral("appearance.pdf")).toUtf8();
    fz_var(doc);
    fz_var(page);
    fz_var(device);
    fz_var(ink);
    fz_try(ctx)
    {
        doc = pdf_open_document(ctx, input.c_str());
        if (pdf_needs_password(ctx, doc) || pdf_count_pages(ctx, doc) != 1)
            fz_throw(ctx, FZ_ERROR_FORMAT, "Appearance must be an unencrypted single-page PDF.");
        pdf_obj *object = pdf_lookup_page_obj(ctx, doc, 0);
        const float unit = pdf_to_real(ctx, pdf_dict_get(ctx, object, PDF_NAME(UserUnit)));
        if (unit != 0 && unit != 1)
            fz_throw(ctx, FZ_ERROR_FORMAT, "Appearance UserUnit must be 1.");
        for (int i = 1; i < pdf_xref_len(ctx, doc); ++i) {
            auto *entry = pdf_load_object(ctx, doc, i);
            const bool signature = pdf_name_eq(ctx, pdf_dict_get(ctx, entry, PDF_NAME(Type)), PDF_NAME(Sig));
            pdf_drop_obj(ctx, entry);
            if (signature)
                fz_throw(ctx, FZ_ERROR_FORMAT, "Signed appearances cannot be rewritten.");
        }
        if (pdf_to_int(ctx, pdf_dict_get_inheritable(ctx, object, PDF_NAME(Rotate))) != 0)
            fz_throw(ctx, FZ_ERROR_FORMAT, "Rotated appearance PDFs are unsupported.");
        page = pdf_load_page(ctx, doc, 0);
        const fz_rect logical = pdf_to_rect(ctx, pdf_dict_get_inheritable(ctx, object, PDF_NAME(MediaBox)));
        fz_rect pagebox;
        fz_matrix transform;
        pdf_page_transform(ctx, page, &pagebox, &transform);
        device = fz_new_bbox_device(ctx, &ink);
        pdf_run_page_contents(ctx, page, device, fz_invert_matrix(transform), nullptr);
        fz_close_device(ctx, device);
        result.hasInk = !fz_is_empty_rect(ink);
        const fz_rect expanded = fz_union_rect(logical, fz_expand_rect(ink, padding));
        if (!fz_is_valid_rect(expanded) || fz_is_infinite_rect(expanded))
            fz_throw(ctx, FZ_ERROR_LIMIT, "Invalid appearance bounds.");
        if (expanded.x1 - expanded.x0 > 100000 || expanded.y1 - expanded.y0 > 100000)
            fz_throw(ctx, FZ_ERROR_LIMIT, "Appearance is too large.");
        auto *oldContents = pdf_keep_obj(ctx, pdf_dict_get(ctx, object, PDF_NAME(Contents)));
        auto *contents = pdf_dict_put_array(ctx, object, PDF_NAME(Contents), 3);
        const QByteArray prefix = "q\n1 0 0 1 " + QByteArray::number(-expanded.x0, 'g', 9) + ' ' + QByteArray::number(-expanded.y0, 'g', 9) + " cm\n";
        auto *buffer = fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char *>(prefix.constData()), prefix.size());
        pdf_array_push_drop(ctx, contents, pdf_add_stream(ctx, doc, buffer, nullptr, 0));
        fz_drop_buffer(ctx, buffer);
        if (pdf_is_array(ctx, oldContents)) {
            for (int i = 0; i < pdf_array_len(ctx, oldContents); ++i)
                pdf_array_push(ctx, contents, pdf_array_get(ctx, oldContents, i));
        } else if (oldContents)
            pdf_array_push(ctx, contents, oldContents);
        pdf_drop_obj(ctx, oldContents);
        buffer = fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char *>("\nQ\n"), 3);
        pdf_array_push_drop(ctx, contents, pdf_add_stream(ctx, doc, buffer, nullptr, 0));
        fz_drop_buffer(ctx, buffer);
        const auto pdfbox = fz_make_rect(0, 0, expanded.x1 - expanded.x0, expanded.y1 - expanded.y0);
        pdf_dict_put_rect(ctx, object, PDF_NAME(MediaBox), pdfbox);
        pdf_dict_put_rect(ctx, object, PDF_NAME(CropBox), pdfbox);
        pdf_dict_del(ctx, object, PDF_NAME(Annots));
        pdf_dict_del(ctx, pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root)), PDF_NAME(AcroForm));
        pdf_write_options options = pdf_default_write_options;
        options.do_compress = 1;
        pdf_save_document(ctx, doc, staged.constData(), &options);
        result.logicalBounds = {logical.x0, logical.y0, logical.x1, logical.y1};
        result.inkBounds = {ink.x0, ink.y0, ink.x1, ink.y1};
        result.outputBounds = {expanded.x0, expanded.y0, expanded.x1, expanded.y1};
        result.ok = true;
    }
    fz_always(ctx)
    {
        fz_drop_device(ctx, device);
        fz_drop_page(ctx, reinterpret_cast<fz_page *>(page));
        pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx)
    {
        result.error = Error::UnsupportedInput;
        result.message = fz_caught_message(ctx);
    }
    fz_drop_context(ctx);
    if (result.ok) {
        std::error_code error;
        std::filesystem::create_hard_link(std::filesystem::u8path(staged.constData()), std::filesystem::u8path(output), error);
        if (error) {
            result.ok = false;
            result.error = Error::WriteError;
            result.message = error.message();
        }
    }
    return result;
}
}
