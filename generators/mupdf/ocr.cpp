// SPDX-License-Identifier: AGPL-3.0-or-later
#include "generator_mupdf.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QScopeGuard>
#include <QSet>
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

namespace
{
// Retain the boundary of each invisible text object, including objects which
// share a baseline. MuPDF can otherwise merge them into one structured line.
struct OcrBoundaryDevice {
    fz_device super;
    fz_stext_page *page;
    QSet<const fz_stext_char *> *ends;
};

void recordOcrText(fz_context *ctx, fz_device *device, const fz_text *text, fz_matrix matrix)
{
    auto *tracker = reinterpret_cast<OcrBoundaryDevice *>(device);
    fz_ignore_text(ctx, device->passthrough, text, matrix);
    auto *block = tracker->page->last_block;
    if (block && block->type == FZ_STEXT_BLOCK_TEXT && block->u.t.last_line)
        tracker->ends->insert(block->u.t.last_line->last_char);
}

// Some scanned PDFs paint the searchable transcript first and then cover it
// with opaque page-wide image strips. Recognize only an isolated text prefix
// followed by a complete scan. Keep every later drawing operation intact.
struct ScanTranscript {
    int stream = -1;
    int64_t end = 0;
};

fz_rect transcriptInk(fz_context *ctx, pdf_page *page, pdf_obj *stream, int64_t length)
{
    fz_rect ink = fz_empty_rect;
    auto *device = fz_new_bbox_device(ctx, &ink);
    pdf_processor *proc = nullptr;
    fz_buffer *prefix = nullptr;
    fz_stream *input = nullptr;
    fz_var(proc);
    fz_var(prefix);
    fz_var(input);
    fz_try(ctx) {
        input = pdf_open_stream(ctx, stream);
        prefix = fz_new_buffer(ctx, length);
        fz_resize_buffer(ctx, prefix, length);
        unsigned char *bytes;
        fz_buffer_storage(ctx, prefix, &bytes);
        if (fz_read(ctx, input, bytes, length) != size_t(length))
            fz_throw(ctx, FZ_ERROR_FORMAT, "Truncated scan transcript");
        prefix->len = length;
        proc = pdf_new_run_processor(ctx, page->doc, device, fz_identity, -1, "View", nullptr, nullptr, nullptr, nullptr, nullptr);
        pdf_process_glyph(ctx, proc, page->doc, pdf_page_resources(ctx, page), prefix);
        pdf_close_processor(ctx, proc);
        fz_close_device(ctx, device);
    }
    fz_always(ctx) {
        fz_drop_stream(ctx, input);
        fz_drop_buffer(ctx, prefix);
        pdf_drop_processor(ctx, proc);
        fz_drop_device(ctx, device);
    }
    fz_catch(ctx) fz_rethrow(ctx);
    return ink;
}

ScanTranscript scanTranscript(fz_context *ctx, pdf_page *page)
{
    auto *contents = pdf_page_contents(ctx, page);
    auto *resources = pdf_page_resources(ctx, page);
    if (!pdf_dict_get(ctx, resources, PDF_NAME(XObject)))
        return {};
    const bool array = pdf_is_array(ctx, contents);
    const auto box = pdf_to_rect(ctx, pdf_dict_get_inheritable(ctx, page->obj, PDF_NAME(CropBox)));
    const auto bounds = fz_is_empty_rect(box) ? pdf_to_rect(ctx, pdf_dict_get_inheritable(ctx, page->obj, PDF_NAME(MediaBox))) : box;
    for (int i = 0; i < (array ? pdf_array_len(ctx, contents) : 1); ++i) {
        auto *stream = array ? pdf_array_get(ctx, contents, i) : contents;
        if (!pdf_is_stream(ctx, stream)) return {};
        auto *input = pdf_open_stream(ctx, stream);
        pdf_lexbuf lex;
        pdf_lexbuf_init(ctx, &lex, PDF_LEXBUF_SMALL);
        ScanTranscript result;
        bool onlySave = true;
        fz_try(ctx) {
            QList<fz_matrix> stack;
            QList<QPair<float, float>> strips;
            fz_matrix matrix = fz_identity;
            float operands[6] = {};
            int count = 0;
            QByteArray name;
            int64_t groupStart = 0;
            bool text = false, image = false;
            for (;;) {
                const auto start = fz_tell(ctx, input);
                const auto token = pdf_lex(ctx, input, &lex);
                if (token == PDF_TOK_EOF) break;
                if (token != PDF_TOK_KEYWORD) onlySave = false;
                if (token == PDF_TOK_INT || token == PDF_TOK_REAL) {
                    if (count >= 6) break;
                    operands[count++] = token == PDF_TOK_INT ? float(lex.i) : lex.f;
                    continue;
                }
                if (token == PDF_TOK_NAME) { name = lex.scratch; continue; }
                if (token == PDF_TOK_STRING) continue;
                if (token != PDF_TOK_KEYWORD) { onlySave = false; break; }
                const QByteArrayView op(lex.scratch);
                if (op != "q") onlySave = false;
                if (op == "q") {
                    if (stack.isEmpty()) groupStart = start;
                    stack.append(matrix);
                } else if (op == "Q") {
                    if (stack.isEmpty()) break;
                    matrix = stack.takeLast();
                } else if (op == "cm" && count == 6 && !stack.isEmpty()) {
                    matrix = fz_concat({operands[0], operands[1], operands[2], operands[3], operands[4], operands[5]}, matrix);
                } else if (op == "Do" && text && !stack.isEmpty()) {
                    auto *obj = pdf_dict_gets(ctx, pdf_dict_get(ctx, resources, PDF_NAME(XObject)), name.constData());
                    if (!pdf_name_eq(ctx, pdf_dict_get(ctx, obj, PDF_NAME(Subtype)), PDF_NAME(Image)) ||
                        pdf_dict_get(ctx, obj, PDF_NAME(Mask)) || pdf_dict_get(ctx, obj, PDF_NAME(SMask)) ||
                        pdf_dict_get_bool(ctx, obj, PDF_NAME(ImageMask)) || pdf_dict_get_int(ctx, obj, PDF_NAME(SMaskInData)) ||
                        matrix.b != 0 || matrix.c != 0) break;
                    const auto r = fz_transform_rect(fz_unit_rect, matrix);
                    if (r.x0 > bounds.x0 + .05f || r.x1 < bounds.x1 - .05f) break;
                    if (!image) result.end = groupStart;
                    image = true;
                    strips.append({r.y0, r.y1});
                    // Accept sub-pixel rounding where image strips meet.
                    std::sort(strips.begin(), strips.end());
                    float y = bounds.y0;
                    for (const auto &strip : strips) {
                        if (strip.first > y + .001f) break;
                        y = std::max(y, strip.second);
                    }
                    if (y >= bounds.y1 - 1.f) {
                        const auto ink = transcriptInk(ctx, page, stream, result.end);
                        if (!fz_is_empty_rect(ink) && ink.x0 >= r.x0 && ink.x1 <= r.x1 &&
                            ink.y0 >= strips.first().first && ink.y1 <= y)
                            result.stream = i;
                        break;
                    }
                } else if (!image && !stack.isEmpty() && (op == "BT" || op == "ET" || op == "Tf" || op == "Td" || op == "Tm" || op == "TL" || op == "Tj")) {
                    if (op == "Tj") text = true;
                } else break;
                count = 0;
                name.clear();
            }
        }
        fz_always(ctx) {
            pdf_lexbuf_fin(ctx, &lex);
            fz_drop_stream(ctx, input);
        }
        fz_catch(ctx) fz_rethrow(ctx);
        if (result.stream >= 0) return result;
        if (!onlySave) break;
    }
    return {};
}

// Insert the standard invisible rendering mode into genuine text objects.
// The prefix has already been validated by scanTranscript; lexing avoids
// mistaking a BT inside an escaped PDF string for an operator.
fz_buffer *normalizedTranscript(fz_context *ctx, pdf_obj *stream, int64_t end)
{
    auto *data = pdf_load_stream(ctx, stream);
    unsigned char *bytes;
    const auto size = fz_buffer_storage(ctx, data, &bytes);
    auto *input = fz_open_memory(ctx, bytes, end);
    auto *output = fz_new_buffer(ctx, size);
    pdf_lexbuf lex;
    pdf_lexbuf_init(ctx, &lex, PDF_LEXBUF_SMALL);
    fz_try(ctx) {
        size_t copied = 0;
        for (;;) {
            const auto token = pdf_lex(ctx, input, &lex);
            if (token == PDF_TOK_EOF) break;
            if (token == PDF_TOK_KEYWORD && !strcmp(lex.scratch, "BT")) {
                const size_t next = fz_tell(ctx, input);
                fz_append_data(ctx, output, bytes + copied, next - copied);
                fz_append_string(ctx, output, " 3 Tr ");
                copied = next;
            }
        }
        fz_append_data(ctx, output, bytes + copied, size - copied);
    }
    fz_always(ctx) {
        pdf_lexbuf_fin(ctx, &lex);
        fz_drop_stream(ctx, input);
        fz_drop_buffer(ctx, data);
    }
    fz_catch(ctx) { fz_drop_buffer(ctx, output); fz_rethrow(ctx); }
    return output;
}
}
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
    QSet<const fz_stext_char *> ends;
    fz_stext_page *text = nullptr;
    fz_var(text);
    fz_device *device = nullptr;
    fz_var(device);
    return readOperation(
        [&] {
            if (index < 0 || index >= m_pages.size())
                fz_throw(m_context, FZ_ERROR_ARGUMENT, "Invalid OCR page index");
            fz_stext_options options = {};
            options.flags = FZ_STEXT_COLLECT_STYLES;
            fz_try(m_context)
            {
                auto *page = m_pages.at(index);
                text = fz_new_stext_page(m_context, pdf_bound_page(m_context, page, FZ_CROP_BOX));
                device = fz_new_stext_device(m_context, text, &options);
                auto *ctx = m_context;
                auto *tracker = fz_new_derived_passthrough_device(ctx, device, OcrBoundaryDevice);
                fz_drop_device(m_context, device);
                device = &tracker->super;
                tracker->page = text;
                tracker->ends = &ends;
                device->ignore_text = recordOcrText;
                const auto transcript = scanTranscript(m_context, page);
                if (transcript.stream < 0) {
                    pdf_run_page_contents(m_context, page, device, fz_identity, nullptr);
                } else {
                    auto *contents = pdf_page_contents(m_context, page);
                    fz_rect bounds;
                    fz_matrix matrix;
                    pdf_page_transform(m_context, page, &bounds, &matrix);
                    auto *proc = pdf_new_run_processor(m_context, m_document, device, matrix, -1, "View", nullptr, nullptr, nullptr, nullptr, nullptr);
                    fz_buffer *joined = nullptr;
                    fz_var(joined);
                    fz_try(m_context) {
                        joined = fz_new_buffer(m_context, 1024);
                        const bool array = pdf_is_array(m_context, contents);
                        for (int i = 0; i < (array ? pdf_array_len(m_context, contents) : 1); ++i) {
                            auto *stream = array ? pdf_array_get(m_context, contents, i) : contents;
                            auto *buffer = i == transcript.stream ? normalizedTranscript(m_context, stream, transcript.end) : pdf_load_stream(m_context, stream);
                            fz_append_buffer(m_context, joined, buffer);
                            fz_drop_buffer(m_context, buffer);
                            fz_append_byte(m_context, joined, 10);
                        }
                        pdf_process_glyph(m_context, proc, m_document, pdf_page_resources(m_context, page), joined);
                        pdf_close_processor(m_context, proc);
                    }
                    fz_always(m_context) { pdf_drop_processor(m_context, proc); fz_drop_buffer(m_context, joined); }
                    fz_catch(m_context) fz_rethrow(m_context);
                }
                fz_close_device(m_context, device);
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
                                {word.trimmed(), QRectF(QPointF((bounds.x0 - box.x0) / (box.x1 - box.x0), (bounds.y0 - box.y0) / (box.y1 - box.y0)), QPointF((bounds.x1 - box.x0) / (box.x1 - box.x0), (bounds.y1 - box.y0) / (box.y1 - box.y0)))});
                            word.clear();
                            bounds = fz_empty_rect;
                        };
                        for (auto *ch = line->first_char; ch; ch = ch->next) {
                            if (ch->flags & (FZ_STEXT_FILLED | FZ_STEXT_STROKED)) {
                                flush();
                                continue;
                            }
                            if (ch->c == ' ') {
                                if (!word.isEmpty())
                                    word += QLatin1Char(' ');
                            } else if (ch->c < 32) {
                                flush();
                            } else {
                                const char32_t code = ch->c;
                                word += QString::fromUcs4(&code, 1);
                                bounds = fz_union_rect(bounds, fz_rect_from_quad(ch->quad));
                            }
                            if (ends.contains(ch))
                                flush();
                        }
                        flush();
                    }
                }
            }
            fz_always(m_context)
            {
                fz_drop_device(m_context, device);
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
    for (qsizetype i = 0; i < words.size(); ++i) {
        const auto &word = words[i];
        if (!word.rectangle.isValid()) {
            if (error)
                *error = QStringLiteral("OCR region %1 has an invalid rectangle (%2, %3, %4 x %5).")
                             .arg(i + 1).arg(word.rectangle.x()).arg(word.rectangle.y()).arg(word.rectangle.width()).arg(word.rectangle.height());
            return false;
        }
        const auto control = std::ranges::find_if(word.text, [](QChar c) { return c.category() == QChar::Other_Control; });
        if (control != word.text.end()) {
            if (error)
                *error = QStringLiteral("OCR region %1 contains control character U+%2.")
                             .arg(i + 1).arg(QString::number(control->unicode(), 16).rightJustified(4, QLatin1Char('0')).toUpper());
            return false;
        }
    }
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
            auto *previousContents = pdf_keep_obj(m_context, pdf_page_contents(m_context, page));
            auto *previousResources = pdf_keep_obj(m_context, pdf_dict_get(m_context, page->obj, PDF_NAME(Resources)));
            fz_try(m_context)
            {
                const auto transcript = scanTranscript(m_context, page);
                if (transcript.stream >= 0) {
                    const bool array = pdf_is_array(m_context, previousContents);
                    auto *old = array ? pdf_array_get(m_context, previousContents, transcript.stream) : previousContents;
                    auto *buffer = normalizedTranscript(m_context, old, transcript.end);
                    auto *stream = pdf_add_stream(m_context, m_document, buffer, nullptr, 0);
                    fz_drop_buffer(m_context, buffer);
                    if (array) {
                        auto *copy = pdf_copy_array(m_context, previousContents);
                        pdf_array_put_drop(m_context, copy, transcript.stream, stream);
                        pdf_dict_put_drop(m_context, page->obj, PDF_NAME(Contents), copy);
                    } else {
                        pdf_dict_put_drop(m_context, page->obj, PDF_NAME(Contents), stream);
                    }
                }
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
                    const fz_matrix trm = {float(width / measured.e), 0, 0, -1, float(box.x0 + word.rectangle.left() * (box.x1 - box.x0)), 0};
                    text = fz_new_text(m_context);
                    // Separate adjacent OCR words explicitly. Readers can join
                    // neighboring runs even when there is a geometric gap.
                    const QByteArray separated = utf8 + ' ';
                    fz_show_string(m_context, text, font, trm, separated.constData(), 0, 0, FZ_BIDI_LTR, FZ_LANG_UNSET);
                    // show_string can select fallback fonts. Keep a common baseline
                    // and fit the union of their typographic extents to the word box.
                    float ascent = 0, descent = 0;
                    for (auto *span = text->head; span; span = span->next) {
                        ascent = std::max(ascent, fz_font_ascender(m_context, span->font));
                        descent = std::min(descent, fz_font_descender(m_context, span->font));
                    }
                    if (ascent <= descent)
                        fz_throw(m_context, FZ_ERROR_FORMAT, "Invalid OCR font metrics");
                    const float fontSize = size / (ascent - descent);
                    const float baseline = box.y0 + word.rectangle.bottom() * (box.y1 - box.y0) + descent * fontSize;
                    for (auto *span = text->head; span; span = span->next) {
                        span->trm.d = -fontSize;
                        for (int i = 0; i < span->len; ++i)
                            span->items[i].y = baseline;
                    }
                    fz_ignore_text(m_context, device, text, inverse);
                    // Start a fresh PDF text object for the next editable region.
                    // The device emits an absolute text matrix for each span.
                    fz_append_string(m_context, buffer, "ET\nBT\n");
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
                    fz_snprintf(name, sizeof(name), "Fm%d", suffix++);
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
                pdf_dict_dels(m_context, object, "MengsheeOCRWords");
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
                for (const auto &[key, previous] : {std::pair{PDF_NAME(Contents), previousContents}, std::pair{PDF_NAME(Resources), previousResources}}) {
                    if (previous)
                        pdf_dict_put(m_context, page->obj, key, previous);
                    else
                        pdf_dict_del(m_context, page->obj, key);
                }
                pdf_drop_obj(m_context, previousContents);
                pdf_drop_obj(m_context, previousResources);
                fz_rethrow(m_context);
            }
            pdf_drop_obj(m_context, previousContents);
            pdf_drop_obj(m_context, previousResources);
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
