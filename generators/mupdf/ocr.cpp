// SPDX-License-Identifier: AGPL-3.0-or-later
#include "generator_mupdf.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QScopeGuard>
#include <QStandardPaths>
#include <algorithm>
#include <core/page.h>
#include <core/readingviewlayout_p.h>
#if HAVE_TESSERACT
#include <tesseract/baseapi.h>
#include <tesseract/resultiterator.h>
#ifdef Q_OS_WIN
// The bundled MuPDF archive redirects Leptonica allocation through a single
// context. Match its OCR wrapper's lifecycle, including engine destruction.
extern "C" void fz_set_leptonica_mem(fz_context *ctx);
extern "C" void fz_clear_leptonica_mem(fz_context *ctx);
namespace
{
QMutex ocrAllocatorMutex;
}
#endif
#endif

bool MuPDFGenerator::canPerformEnglishOcr() const
{
#if HAVE_TESSERACT
    return true;
#else
    return false;
#endif
}
bool MuPDFGenerator::readOcrTextLayer(int index, QList<Okular::OcrTextWord> *words, QString *error)
{
    if (!words)
        return false;
    words->clear();
    QString stored;
    if (!operation(
            [&] {
                if (index < 0 || index >= m_pages.size())
                    fz_throw(m_context, FZ_ERROR_ARGUMENT, "Invalid OCR page index");
                stored = QString::fromUtf8(pdf_dict_gets(m_context, m_pages[index]->obj, "MengsheeOCRWords") ? pdf_to_text_string(m_context, pdf_dict_gets(m_context, m_pages[index]->obj, "MengsheeOCRWords")) : "");
            },
            error))
        return false;
    if (!stored.isEmpty()) {
        const auto root = QJsonDocument::fromJson(stored.toUtf8()).object();
        if (root.value(QStringLiteral("version")).toInt() == 1 && root.value(QStringLiteral("words")).isArray()) {
            for (const auto &value : root.value(QStringLiteral("words")).toArray()) {
                const auto word = value.toObject();
                const auto rect = word.value(QStringLiteral("rect")).toArray();
                if (rect.size() != 4) {
                    words->clear();
                    if (error)
                        *error = QStringLiteral("Invalid stored OCR word rectangle.");
                    return false;
                }
                words->append({word.value(QStringLiteral("text")).toString(), QRectF(rect[0].toDouble(), rect[1].toDouble(), rect[2].toDouble(), rect[3].toDouble())});
            }
            return true;
        }
    }
    fz_stext_page *text = nullptr;
    fz_var(text);
    return operation(
        [&] {
            fz_stext_options options = {};
            options.flags = FZ_STEXT_COLLECT_STYLES;
            fz_try(m_context)
            {
                text = fz_new_stext_page_from_page_number(m_context, reinterpret_cast<fz_document *>(m_document), index, &options);
                const auto box = text->mediabox;
                for (auto *block = text->first_block; block; block = block->next) {
                    if (block->type != FZ_STEXT_BLOCK_TEXT)
                        continue;
                    for (auto *line = block->u.t.first_line; line; line = line->next) {
                        QString word;
                        fz_rect bounds = fz_empty_rect;
                        const auto flush = [&] {
                            if (word.isEmpty())
                                return;
                            words->append(
                                {word, QRectF(QPointF((bounds.x0 - box.x0) / (box.x1 - box.x0), (bounds.y0 - box.y0) / (box.y1 - box.y0)), QPointF((bounds.x1 - box.x0) / (box.x1 - box.x0), (bounds.y1 - box.y0) / (box.y1 - box.y0)))});
                            word.clear();
                            bounds = fz_empty_rect;
                        };
                        for (auto *ch = line->first_char; ch; ch = ch->next) {
                            if (ch->flags & (FZ_STEXT_FILLED | FZ_STEXT_STROKED)) {
                                flush();
                                continue;
                            }
                            if (ch->c <= 32) {
                                flush();
                                continue;
                            }
                            const char32_t code = ch->c;
                            word += QString::fromUcs4(&code, 1);
                            bounds = fz_union_rect(bounds, fz_rect_from_quad(ch->quad));
                        }
                        flush();
                    }
                }
            }
            fz_always(m_context)
            {
                fz_drop_stext_page(m_context, text);
            }
            fz_catch(m_context)
            {
                fz_rethrow(m_context);
            }
        },
        error);
}
bool MuPDFGenerator::replaceOcrTextLayer(int index, const QList<Okular::OcrTextWord> &words, QString *error)
{
    for (const auto &word : words) {
        if (!word.rectangle.isValid() || std::ranges::any_of(word.text, [](QChar c) { return c.isNull() || c.category() == QChar::Other_Control; })) {
            if (error)
                *error = QStringLiteral("OCR words require printable text and valid rectangles.");
            return false;
        }
    }
    // Glyph bounds include font ascent/descent and cannot recover an editing
    // rectangle exactly. Persist the input geometry independently of glyphs.
    QJsonArray storedWords;
    for (const auto &word : words)
        storedWords.append(QJsonObject {{QStringLiteral("text"), word.text}, {QStringLiteral("rect"), QJsonArray {word.rectangle.x(), word.rectangle.y(), word.rectangle.width(), word.rectangle.height()}}});
    const auto stored = QJsonDocument(QJsonObject {{QStringLiteral("version"), 1}, {QStringLiteral("words"), storedWords}}).toJson(QJsonDocument::Compact);
    fz_buffer *buffer = nullptr;
    fz_device *device = nullptr;
    fz_font *font = nullptr;
    fz_text *text = nullptr;
    pdf_obj *ocrResources = nullptr;
    fz_var(buffer);
    fz_var(device);
    fz_var(font);
    fz_var(text);
    fz_var(ocrResources);
    return operation(
        [&] {
            if (index < 0 || index >= m_pages.size())
                fz_throw(m_context, FZ_ERROR_ARGUMENT, "Invalid OCR page index");
            pdf_sanitize_filter_options sanitize = {};
            sanitize.text_filter = [](fz_context *, void *, int *, int, fz_matrix, fz_matrix, fz_rect, int render, float, float) -> int { return render == 3; };
            pdf_filter_factory filters[] = {{pdf_new_sanitize_filter, &sanitize}, {nullptr, nullptr}};
            pdf_filter_options options = {};
            options.recurse = 1;
            options.instance_forms = 1;
            options.filters = filters;
            auto *page = m_pages.at(index);
            pdf_filter_page_contents(m_context, m_document, page, &options);
            auto *object = pdf_lookup_page_obj(m_context, m_document, index);
            auto *resources = pdf_deep_copy_obj(m_context, pdf_dict_get_inheritable(m_context, object, PDF_NAME(Resources)));
            if (!resources)
                resources = pdf_new_dict(m_context, m_document, 1);
            pdf_dict_put_drop(m_context, object, PDF_NAME(Resources), resources);
            fz_rect box;
            fz_matrix matrix;
            pdf_page_transform(m_context, page, &box, &matrix);
            box = fz_transform_rect(box, matrix);
            const auto inverse = fz_invert_matrix(matrix);
            fz_try(m_context)
            {
                buffer = fz_new_buffer(m_context, 1024);
                fz_append_string(m_context, buffer, "q\n");
                // The PDF device allocates names starting at F0. Keep them in
                // a Form resource dictionary so existing page fonts are untouched.
                ocrResources = pdf_new_dict(m_context, m_document, 1);
                device = pdf_new_pdf_device(m_context, m_document, fz_identity, ocrResources, buffer);
                font = fz_new_base14_font(m_context, "Helvetica");
                for (const auto &word : words) {
                    const auto utf8 = word.text.toUtf8();
                    const double size = word.rectangle.height() * (box.y1 - box.y0);
                    const auto measured = fz_measure_string(m_context, font, fz_identity, utf8.constData(), 0, 0, FZ_BIDI_LTR, FZ_LANG_UNSET);
                    if (measured.e <= 0 || size <= 0)
                        continue;
                    const double width = word.rectangle.width() * (box.x1 - box.x0);
                    const fz_matrix trm = {float(width / measured.e), 0, 0, -float(size), float(box.x0 + word.rectangle.left() * (box.x1 - box.x0)), float(box.y0 + word.rectangle.bottom() * (box.y1 - box.y0))};
                    text = fz_new_text(m_context);
                    fz_show_string(m_context, text, font, trm, utf8.constData(), 0, 0, FZ_BIDI_LTR, FZ_LANG_UNSET);
                    fz_ignore_text(m_context, device, text, inverse);
                    fz_drop_text(m_context, text);
                    text = nullptr;
                }
                fz_close_device(m_context, device);
                fz_append_string(m_context, buffer, "Q\n");
                char name[64];
                int suffix = 0;
                auto *forms = pdf_dict_get(m_context, resources, PDF_NAME(XObject));
                if (!forms)
                    forms = pdf_dict_put_dict(m_context, resources, PDF_NAME(XObject), 1);
                do {
                    fz_snprintf(name, sizeof(name), "MengsheeOCR%d", suffix++);
                } while (pdf_dict_gets(m_context, forms, name));
                pdf_dict_puts_drop(m_context, forms, name, pdf_new_xobject(m_context, m_document, fz_transform_rect(box, inverse), fz_identity, ocrResources, buffer));
                fz_drop_buffer(m_context, buffer);
                buffer = nullptr;
                buffer = fz_new_buffer(m_context, 64);
                fz_append_printf(m_context, buffer, "q /%s Do Q\n", name);
                auto *stream = pdf_add_stream(m_context, m_document, buffer, nullptr, 0);
                auto *contents = pdf_dict_get(m_context, object, PDF_NAME(Contents));
                if (pdf_is_array(m_context, contents))
                    pdf_array_push_drop(m_context, contents, stream);
                else {
                    auto *array = pdf_new_array(m_context, m_document, 2);
                    if (contents)
                        pdf_array_push(m_context, array, contents);
                    pdf_array_push_drop(m_context, array, stream);
                    pdf_dict_put_drop(m_context, object, PDF_NAME(Contents), array);
                }
                pdf_dict_puts_drop(m_context, object, "MengsheeOCRWords", pdf_new_text_string(m_context, stored.constData()));
            }
            fz_always(m_context)
            {
                pdf_drop_obj(m_context, ocrResources);
                fz_drop_text(m_context, text);
                fz_drop_device(m_context, device);
                fz_drop_font(m_context, font);
                fz_drop_buffer(m_context, buffer);
            }
            fz_catch(m_context)
            {
                fz_rethrow(m_context);
            }
        },
        error);
}

Okular::OcrResult MuPDFGenerator::saveWithEnglishOcr(const QString &input, const QString &output, const QList<int> &selected, bool skip, const Okular::PdfOcrInterface::ProgressCallback &progress)
{
    Okular::OcrResult result;
#if HAVE_TESSERACT
    MuPDFGenerator snapshot(nullptr, {});
    QList<Okular::Page *> models;
    const auto cleanup = qScopeGuard([&] { qDeleteAll(models); });
    if (snapshot.loadDocumentWithPassword(input, models, {}) != Okular::Document::OpenSuccess) {
        result.errorText = QStringLiteral("Cannot open the OCR snapshot.");
        return result;
    }
    QString data = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("mengshee/tessdata/eng.traineddata"));
    if (data.isEmpty())
        data = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data/mengshee/tessdata/eng.traineddata"));
    const auto directory = QFileInfo(data).absolutePath().toUtf8();
#ifdef Q_OS_WIN
    QMutexLocker allocatorLock(&ocrAllocatorMutex);
    if (!snapshot.operation([&] { fz_set_leptonica_mem(snapshot.m_context); }, &result.errorText))
        return result;
    const auto releaseAllocator = qScopeGuard([&] { fz_clear_leptonica_mem(snapshot.m_context); });
#endif
    tesseract::TessBaseAPI engine;
    if (engine.Init(directory.constData(), "eng", tesseract::OEM_LSTM_ONLY) != 0) {
        result.errorText = QStringLiteral("Could not load the English OCR model.");
        return result;
    }
    engine.SetPageSegMode(tesseract::PSM_AUTO);
    QList<int> pages = selected;
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    for (int i = 0; i < pages.size(); ++i) {
        const int page = pages[i] - 1;
        if (page < 0 || page >= models.size()) {
            result.errorText = QStringLiteral("Invalid OCR page range.");
            return result;
        }
        if (progress && !progress(i, pages.size(), page + 1)) {
            result.cancelled = true;
            return result;
        }
        if (skip) {
            bool hasText = false;
            snapshot.operation(
                [&] {
                    auto *text = fz_new_stext_page_from_page_number(snapshot.m_context, reinterpret_cast<fz_document *>(snapshot.m_document), page, nullptr);
                    for (auto *block = text->first_block; block; block = block->next)
                        if (block->type == FZ_STEXT_BLOCK_TEXT)
                            hasText = true;
                    fz_drop_stext_page(snapshot.m_context, text);
                },
                &result.errorText);
            if (hasText) {
                ++result.skippedPages;
                continue;
            }
        }
        QImage image = snapshot.renderToImage(page, 300, false, &result.errorText).convertToFormat(QImage::Format_Grayscale8);
        if (image.isNull())
            return result;
        engine.SetImage(image.constBits(), image.width(), image.height(), 1, image.bytesPerLine());
        engine.SetSourceResolution(300);
        if (engine.Recognize(nullptr) != 0) {
            result.errorText = QStringLiteral("Text recognition failed.");
            return result;
        }
        QList<Okular::OcrTextWord> words;
        std::unique_ptr<tesseract::ResultIterator> iterator(engine.GetIterator());
        if (iterator)
            do {
                std::unique_ptr<char[]> utf8(iterator->GetUTF8Text(tesseract::RIL_WORD));
                int l, t, r, b;
                QString word = QString::fromUtf8(utf8.get()).trimmed();
                if (word.isEmpty() || !iterator->BoundingBox(tesseract::RIL_WORD, &l, &t, &r, &b))
                    continue;
                words.append({word, QRectF(QPointF(double(l) / image.width(), double(t) / image.height()), QPointF(double(r) / image.width(), double(b) / image.height()))});
            } while (iterator->Next(tesseract::RIL_WORD));
        if (!snapshot.replaceOcrTextLayer(page, words, &result.errorText))
            return result;
        ++result.recognizedPages;
        result.recognizedWords += words.size();
        engine.Clear();
    }
    if (progress && !progress(pages.size(), pages.size(), pages.isEmpty() ? 0 : pages.last())) {
        result.cancelled = true;
        return result;
    }
    result.success = snapshot.save(output, SaveChanges, &result.errorText);
#else
    Q_UNUSED(input);
    Q_UNUSED(output);
    Q_UNUSED(selected);
    Q_UNUSED(skip);
    Q_UNUSED(progress);
    result.errorText = QStringLiteral("OCR support is not built.");
#endif
    return result;
}
bool MuPDFGenerator::canGenerateReadingViews() const
{
    return canPerformEnglishOcr();
}
Okular::ReadingViewGenerationResult MuPDFGenerator::generateReadingViews(const QString &input, const QList<int> &selected, const Okular::ReadingViewEditingInterface::ProgressCallback &progress)
{
    Okular::ReadingViewGenerationResult result;
#if HAVE_TESSERACT
    MuPDFGenerator snapshot(nullptr, {});
    QList<Okular::Page *> models;
    const auto cleanup = qScopeGuard([&] { qDeleteAll(models); });
    if (snapshot.loadDocumentWithPassword(input, models, {}) != Okular::Document::OpenSuccess) {
        result.errorText = QStringLiteral("Cannot open the layout snapshot.");
        return result;
    }
#ifdef Q_OS_WIN
    QMutexLocker allocatorLock(&ocrAllocatorMutex);
    if (!snapshot.operation([&] { fz_set_leptonica_mem(snapshot.m_context); }, &result.errorText))
        return result;
    const auto releaseAllocator = qScopeGuard([&] { fz_clear_leptonica_mem(snapshot.m_context); });
#endif
    tesseract::TessBaseAPI engine;
    engine.InitForAnalysePage();
    engine.SetPageSegMode(tesseract::PSM_AUTO);
    for (int i = 0; i < selected.size(); ++i) {
        const int index = selected[i];
        if (index < 0 || index >= models.size()) {
            result.errorText = QStringLiteral("Invalid layout page range.");
            result.rectangles.clear();
            return result;
        }
        if (progress && !progress(i, selected.size(), index + 1)) {
            result.cancelled = true;
            result.rectangles.clear();
            return result;
        }
        const double area = models[index]->width() * models[index]->height();
        const int dpi = std::min(300, int(72 * std::sqrt(16.0 * 1024 * 1024 / area)));
        if (dpi < 36) {
            result.errorText = QStringLiteral("Page is too large for layout analysis.");
            result.rectangles.clear();
            return result;
        }
        QImage image = snapshot.renderToImage(index, dpi, false, &result.errorText).convertToFormat(QImage::Format_Grayscale8);
        if (image.isNull()) {
            result.rectangles.clear();
            return result;
        }
        engine.SetImage(image.constBits(), image.width(), image.height(), 1, image.bytesPerLine());
        engine.SetSourceResolution(dpi);
        QList<Okular::ReadingViewLayout::Block> blocks;
        std::unique_ptr<tesseract::PageIterator> iterator(engine.AnalyseLayout());
        if (iterator)
            do {
                int l, t, r, b;
                if (iterator->BoundingBox(tesseract::RIL_BLOCK, &l, &t, &r, &b) && r > l && b > t)
                    blocks.append({QRectF(QPointF(double(l) / image.width(), double(t) / image.height()), QPointF(double(r) / image.width(), double(b) / image.height())), int(iterator->BlockType())});
            } while (iterator->Next(tesseract::RIL_BLOCK));
        for (const auto &rect : Okular::ReadingViewLayout::merge(blocks))
            result.rectangles[index].append({rect.left(), rect.top(), rect.right(), rect.bottom()});
        engine.Clear();
    }
    if (progress && !progress(selected.size(), selected.size(), selected.isEmpty() ? 0 : selected.last() + 1)) {
        result.cancelled = true;
        result.rectangles.clear();
        return result;
    }
    result.success = true;
#else
    Q_UNUSED(input);
    Q_UNUSED(selected);
    Q_UNUSED(progress);
    result.errorText = QStringLiteral("Layout analysis is not built.");
#endif
    return result;
}
