// SPDX-License-Identifier: AGPL-3.0-or-later
#include "context.h"
#include "systemfonts.h"
#include "generator_mupdf.h"
#include "annots.h"
#include "pageimport.h"
#include <QDebug>
#include <QFile>
#include <QMutex>
#include <QPainter>
#include <QPrinter>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtMath>
#include <cmath>
#include <core/page.h>
#include <core/textpage.h>


MuPDFGenerator::MuPDFGenerator(QObject *parent, const QVariantList &args)
    : Generator(parent, args)
{
    m_context = MengsheePdfRuntime::createContext();
    m_annotations = std::make_unique<MuPDFAnnotationProxy>(this);
    setFeature(TextExtraction);
    setFeature(ReadRawData);
    setFeature(SwapBackingFile);
    setFeature(Threaded);
    setFeature(TiledRendering);
    setFeature(PrintNative);
}

MuPDFGenerator::~MuPDFGenerator()
{
    doCloseDocument();
    fz_drop_context(m_context);
}

bool MuPDFGenerator::doCloseDocument()
{
    clearDisplayList();
    qDeleteAll(m_embeddedFiles);
    m_embeddedFiles.clear();
    m_embeddedLoaded = false;
    if (m_annotations)
        m_annotations->clear();
    for (auto *object : m_detached)
        pdf_drop_obj(m_context, object);
    m_detached.clear();
    for (auto *page : m_pages)
        fz_drop_page(m_context, reinterpret_cast<fz_page *>(page));
    m_pages.clear();
    m_editError.clear();
    m_annotationWarningShown = false;
    pdf_drop_document(m_context, m_document);
    m_document = nullptr;
    m_data.clear();
    return true;
}

Okular::Document::OpenResult MuPDFGenerator::loadDocumentWithPassword(const QString &path, QList<Okular::Page *> &pages, const QString &password)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly))
        return Okular::Document::OpenError;
    return loadDocumentFromDataWithPassword(input.readAll(), pages, password);
}

Okular::Document::OpenResult MuPDFGenerator::loadDocumentFromDataWithPassword(const QByteArray &data, QList<Okular::Page *> &pages, const QString &password)
{
    doCloseDocument();
    if (!m_context)
        return Okular::Document::OpenError;
    m_data = data;
    fz_stream *stream = nullptr;
    fz_var(stream);
    fz_try(m_context)
    {
        stream = fz_open_memory(m_context, reinterpret_cast<const unsigned char *>(m_data.constData()), m_data.size());
        m_document = pdf_open_document_with_stream(m_context, stream);
    }
    fz_always(m_context)
    {
        fz_drop_stream(m_context, stream);
    }
    fz_catch(m_context)
    {
        qWarning() << "MuPDF open:" << fz_caught_message(m_context);
        doCloseDocument();
        return Okular::Document::OpenError;
    }
    return finishOpen(pages, password);
}

Okular::Document::OpenResult MuPDFGenerator::finishOpen(QList<Okular::Page *> &pages, const QString &password)
{
    const QByteArray encodedPassword = password.toUtf8();
    bool needsPassword = false;
    fz_var(needsPassword);
    fz_try(m_context)
    {
        needsPassword = pdf_needs_password(m_context, m_document) && !pdf_authenticate_password(m_context, m_document, encodedPassword.constData());
        if (!needsPassword) {
            const int count = pdf_count_pages(m_context, m_document);
            for (int i = 0; i < count; ++i) {
                fz_rect box;
                fz_matrix matrix;
                pdf_page_obj_transform(m_context, pdf_lookup_page_obj(m_context, m_document, i), &box, &matrix);
                box = fz_transform_rect(box, matrix);
                auto *page = new Okular::Page(i, (box.x1 - box.x0) * dpi().width() / 72.0, (box.y1 - box.y0) * dpi().height() / 72.0, Okular::Rotation0);
                pages.append(page);
                auto *native = pdf_load_page(m_context, m_document, i);
                m_pages.append(native);
                m_annotations->load(native, page);
                loadLinks(native, page);
                loadForms(native, page);
            }
        }
    }
    fz_catch(m_context)
    {
        qWarning() << "MuPDF pages:" << fz_caught_message(m_context);
        qDeleteAll(pages);
        pages.clear();
        doCloseDocument();
        return Okular::Document::OpenError;
    }
    if (needsPassword) {
        doCloseDocument();
        return Okular::Document::OpenNeedsPassword;
    }
    return Okular::Document::OpenSuccess;
}

void MuPDFGenerator::clearDisplayList()
{
    clearAnnotationLists();
    fz_drop_display_list(m_context, m_displayList);
    m_displayList = nullptr;
    m_displayListPage = -1;
}

fz_display_list *MuPDFGenerator::pageDisplayList(int number, pdf_page *page, fz_cookie *cookie)
{
    if (m_displayList && m_displayListPage == number)
        return m_displayList;
    clearDisplayList();
    fz_display_list *list = nullptr;
    fz_device *device = nullptr;
    fz_var(list);
    fz_var(device);
    fz_try(m_context) {
        list = fz_new_display_list(m_context, pdf_bound_page(m_context, page, FZ_CROP_BOX));
        device = fz_new_list_device(m_context, list);
        pdf_run_page_contents(m_context, page, device, fz_identity, cookie);
        if (cookie && cookie->abort)
            fz_throw(m_context, FZ_ERROR_ABORT, "Render cancelled");
        fz_close_device(m_context, device);
    }
    fz_always(m_context) { fz_drop_device(m_context, device); }
    fz_catch(m_context) {
        fz_drop_display_list(m_context, list);
        fz_rethrow(m_context);
    }
    m_displayList = list;
    m_displayListPage = number;
    return list;
}

QImage MuPDFGenerator::render(int number, int width, int height, bool annotations, QString *error, const QRect &region, fz_cookie *cookie)
{
    // NormalizedRect::geometry includes the final pixel at the right/bottom
    // edge. Preserve that size: TilesManager uses it when placing the tile.
    const QRect target = region.isNull() ? QRect(0, 0, width, height) : region;
    if (!m_document || width <= 0 || height <= 0 || target.isEmpty() || target.x() < 0 || target.y() < 0 || target.right() > width || target.bottom() > height || qint64(target.width()) * target.height() > 100000000) {
        if (error)
            *error = QStringLiteral("Invalid or oversized PDF render request.");
        return {};
    }
    pdf_page *page = nullptr;
    fz_pixmap *pixmap = nullptr;
    fz_device *device = nullptr;
    QImage result;
    fz_var(page);
    fz_var(pixmap);
    fz_var(device);
    fz_try(m_context)
    {
        page = pdf_load_page(m_context, m_document, number);
        const fz_rect bounds = pdf_bound_page(m_context, page, FZ_CROP_BOX);
        const fz_matrix transform = fz_concat(fz_translate(-bounds.x0, -bounds.y0),
                                               fz_scale(width / (bounds.x1 - bounds.x0), height / (bounds.y1 - bounds.y0)));
        const auto bbox = fz_make_irect(target.x(), target.y(), target.x() + target.width(), target.y() + target.height());
        auto *list = pageDisplayList(number, page, cookie);
        pixmap = fz_new_pixmap_with_bbox(m_context, fz_device_rgb(m_context), bbox, nullptr, 0);
        fz_clear_pixmap_with_value(m_context, pixmap, 255);
        device = fz_new_draw_device(m_context, fz_identity, pixmap);
        // Device-space scissor skips offscreen list commands; the pixmap itself
        // only allocates the requested tile. Expand culling slightly for AA edges.
        fz_run_display_list(m_context, list, device, transform, fz_expand_rect(fz_rect_from_irect(bbox), 1), cookie);
        fz_close_device(m_context, device);
        if (cookie && cookie->abort)
            fz_throw(m_context, FZ_ERROR_ABORT, "Render cancelled");
        if (annotations)
            renderAnnotations(page, pixmap, transform, cookie);
        if (cookie && cookie->abort)
            fz_throw(m_context, FZ_ERROR_ABORT, "Render cancelled");
        result = QImage(fz_pixmap_samples(m_context, pixmap), target.width(), target.height(), fz_pixmap_stride(m_context, pixmap), QImage::Format_RGB888).copy();
    }
    fz_always(m_context)
    {
        fz_drop_device(m_context, device);
        fz_drop_pixmap(m_context, pixmap);
        fz_drop_page(m_context, reinterpret_cast<fz_page *>(page));
    }
    fz_catch(m_context)
    {
        if (error)
            *error = QString::fromUtf8(fz_caught_message(m_context));
        return {};
    }
    return result;
}

QImage MuPDFGenerator::image(Okular::PixmapRequest *request)
{
    QMutexLocker lock(userMutex());
    if (request->shouldAbortRender())
        return {};
    QRect region;
    if (request->isTile()) {
        region = request->normalizedRect().geometry(request->width(), request->height());
        if (region.isEmpty())
            return {};
    }
    fz_cookie cookie{};
    request->setRenderAbortCallback([&cookie] { cookie.abort = 1; });
    const auto detach = qScopeGuard([request] { request->setRenderAbortCallback({}); });
    auto result = render(request->pageNumber(), request->width(), request->height(), true, nullptr, region, &cookie);
    return request->shouldAbortRender() ? QImage() : result;
}

QImage MuPDFGenerator::renderToImage(int page, int dpi, bool annotations, QString *error)
{
    QMutexLocker lock(userMutex());
    fz_rect box = fz_empty_rect;
    fz_try(m_context)
    {
        fz_matrix matrix;
        pdf_page_obj_transform(m_context, pdf_lookup_page_obj(m_context, m_document, page), &box, &matrix);
        box = fz_transform_rect(box, matrix);
    }
    fz_catch(m_context)
    {
        if (error)
            *error = QString::fromUtf8(fz_caught_message(m_context));
        return {};
    }
    return render(page, qCeil((box.x1 - box.x0) * dpi / 72.0), qCeil((box.y1 - box.y0) * dpi / 72.0), annotations, error);
}

Okular::TextPage *MuPDFGenerator::textPage(Okular::TextRequest *request)
{
    QMutexLocker lock(userMutex());
    auto *result = new Okular::TextPage;
    fz_stext_page *text = nullptr;
    fz_var(text);
    fz_try(m_context)
    {
        text = fz_new_stext_page_from_page_number(m_context, reinterpret_cast<fz_document *>(m_document), request->page()->number(), nullptr);
        const float width = text->mediabox.x1 - text->mediabox.x0;
        const float height = text->mediabox.y1 - text->mediabox.y0;
        for (fz_stext_block *block = text->first_block; block; block = block->next) {
            if (block->type != FZ_STEXT_BLOCK_TEXT)
                continue;
            for (fz_stext_line *line = block->u.t.first_line; line; line = line->next) {
                for (fz_stext_char *ch = line->first_char; ch; ch = ch->next) {
                    const fz_rect rect = fz_rect_from_quad(ch->quad);
                    const char32_t codepoint = ch->c;
                    result->append(QString::fromUcs4(&codepoint, 1),
                                   Okular::NormalizedRect((rect.x0 - text->mediabox.x0) / width, (rect.y0 - text->mediabox.y0) / height, (rect.x1 - text->mediabox.x0) / width, (rect.y1 - text->mediabox.y0) / height));
                }
            }
        }
    }
    fz_always(m_context)
    {
        fz_drop_stext_page(m_context, text);
    }
    fz_catch(m_context)
    {
        qWarning() << "MuPDF text:" << fz_caught_message(m_context);
    }
    return result;
}

Okular::DocumentInfo MuPDFGenerator::generateDocumentInfo(const QSet<Okular::DocumentInfo::Key> &) const
{
    Okular::DocumentInfo info;
    info.set(Okular::DocumentInfo::MimeType, QStringLiteral("application/pdf"));
    const_cast<MuPDFGenerator *>(this)->readOperation(
        [&] {
            const std::pair<Okular::DocumentInfo::Key, const char *> keys[] = {{Okular::DocumentInfo::Title, "Title"},
                                                                               {Okular::DocumentInfo::Author, "Author"},
                                                                               {Okular::DocumentInfo::Subject, "Subject"},
                                                                               {Okular::DocumentInfo::Keywords, "Keywords"},
                                                                               {Okular::DocumentInfo::Creator, "Creator"},
                                                                               {Okular::DocumentInfo::Producer, "Producer"}};
            auto *dictionary = pdf_dict_get(m_context, pdf_trailer(m_context, m_document), PDF_NAME(Info));
            for (const auto &[key, name] : keys)
                info.set(key, QString::fromUtf8(pdf_to_text_string(m_context, pdf_dict_gets(m_context, dictionary, name))));
        },
        nullptr);
    return info;
}

bool MuPDFGenerator::isAllowed(Okular::Permission permission) const
{
    fz_permission native = FZ_PERMISSION_EDIT;
    switch (permission) {
    case Okular::AllowCopy:
        native = FZ_PERMISSION_COPY;
        break;
    case Okular::AllowPrint:
        native = FZ_PERMISSION_PRINT;
        break;
    case Okular::AllowNotes:
        native = FZ_PERMISSION_ANNOTATE;
        break;
    case Okular::AllowFillForms:
        native = FZ_PERMISSION_FORM;
        break;
    default:
        break;
    }
    bool allowed = false;
    const_cast<MuPDFGenerator *>(this)->readOperation([&] { allowed = pdf_has_permission(m_context, m_document, native); }, nullptr);
    return allowed;
}

Okular::Document::PrintError MuPDFGenerator::print(QPrinter &printer)
{
    if (!isAllowed(Okular::AllowPrint))
        return Okular::Document::UnknownPrintError;
    QPainter painter;
    if (!painter.begin(&printer))
        return Okular::Document::InvalidPrinterStatePrintError;
    const int first = printer.fromPage() > 0 ? printer.fromPage() - 1 : 0;
    const int last = printer.toPage() > 0 ? std::min(printer.toPage(), int(m_pages.size())) - 1 : m_pages.size() - 1;
    for (int i = first; i <= last; ++i) {
        if (i != first && !printer.newPage())
            return Okular::Document::UnknownPrintError;
        const int index = printer.pageOrder() == QPrinter::LastPageFirst ? last - (i - first) : i;
        QString error;
        const auto image = renderToImage(index, std::min(printer.resolution(), 300), true, &error);
        if (image.isNull())
            return Okular::Document::UnknownPrintError;
        const QRect area = printer.pageLayout().paintRectPixels(printer.resolution());
        QSize size = image.size();
        size.scale(area.size(), Qt::KeepAspectRatio);
        painter.drawImage(QRect(area.center() - QPoint(size.width() / 2, size.height() / 2), size), image);
    }
    painter.end();
    return Okular::Document::NoPrintError;
}

Okular::PrintPreviewRenderer MuPDFGenerator::createPrintPreviewRenderer(QString *error)
{
    QMutexLocker lock(userMutex());
    QByteArray snapshot;
    fz_buffer *buffer = nullptr;
    fz_output *output = nullptr;
    fz_var(buffer);
    fz_var(output);
    fz_try(m_context) {
        buffer = fz_new_buffer(m_context, 0);
        output = fz_new_output_with_buffer(m_context, buffer);
        auto options = pdf_default_write_options;
        options.do_encrypt = PDF_ENCRYPT_NONE;
        pdf_write_document(m_context, m_document, output, &options);
        fz_close_output(m_context, output);
        unsigned char *bytes = nullptr;
        const auto length = fz_buffer_storage(m_context, buffer, &bytes);
        snapshot = QByteArray(reinterpret_cast<const char *>(bytes), length);
    }
    fz_always(m_context) {
        fz_drop_output(m_context, output);
        fz_drop_buffer(m_context, buffer);
    }
    fz_catch(m_context) {
        if (error) *error = QString::fromUtf8(fz_caught_message(m_context));
        return {};
    }
    return [snapshot](int page, QSize maximumPixels) -> Okular::PrintPreviewImage {
        // A worker owns its context and document. Open only the requested page;
        // loading every page's annotation model is unnecessary for preview.
        MuPDFGenerator worker(nullptr, {});
        worker.m_data = snapshot;
        auto *ctx = worker.m_context;
        fz_stream *stream = nullptr;
        fz_var(stream);
        fz_rect bounds = fz_empty_rect;
        fz_try(ctx) {
            stream = fz_open_memory(ctx, reinterpret_cast<const unsigned char *>(snapshot.constData()), snapshot.size());
            worker.m_document = pdf_open_document_with_stream(ctx, stream);
            fz_matrix transform;
            pdf_page_obj_transform(ctx, pdf_lookup_page_obj(ctx, worker.m_document, page), &bounds, &transform);
            bounds = fz_transform_rect(bounds, transform);
        }
        fz_always(ctx) { fz_drop_stream(ctx, stream); }
        fz_catch(ctx) { return {{}, QString::fromUtf8(fz_caught_message(ctx))}; }
        maximumPixels = maximumPixels.boundedTo(QSize(2048, 2048)).expandedTo(QSize(1, 1));
        QSize pixels(qCeil(bounds.x1 - bounds.x0), qCeil(bounds.y1 - bounds.y0));
        // Preview at no more than 72 DPI, reducing further for a small pane.
        pixels.scale(maximumPixels.boundedTo(pixels), Qt::KeepAspectRatio);
        Okular::PrintPreviewImage result;
        result.image = worker.render(page, qMax(1, pixels.width()), qMax(1, pixels.height()), true, &result.error);
        return result;
    };
}

bool MuPDFGenerator::supportsOption(SaveOption option) const
{
    return option == SaveChanges;
}
Okular::AnnotationProxy *MuPDFGenerator::annotationProxy() const
{
    return m_annotations.get();
}

bool MuPDFGenerator::save(const QString &path, SaveOptions, QString *error)
{
    QMutexLocker lock(userMutex());
    QTemporaryDir temporary;
    if (!m_editError.isEmpty()) {
        if (error)
            *error = m_editError;
        return false;
    }
    if (!temporary.isValid()) {
        if (error)
            *error = QStringLiteral("Could not create the PDF save staging directory.");
        return false;
    }
    const QString stagedPath = temporary.filePath(QStringLiteral("document.pdf"));
    const QByteArray name = stagedPath.toUtf8();
    pdf_write_options options = pdf_default_write_options;
    options.do_compress = 1;
    fz_try(m_context)
    {
        pdf_save_document(m_context, m_document, name.constData(), &options);
    }
    fz_catch(m_context)
    {
        if (error)
            *error = QString::fromUtf8(fz_caught_message(m_context));
        return false;
    }
    QFile source(stagedPath);
    QSaveFile destination(path);
    if (!source.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Could not open PDF save destination.");
        return false;
    }
    while (!source.atEnd()) {
        const QByteArray block = source.read(1024 * 1024);
        if (block.isEmpty() && source.error() != QFileDevice::NoError)
            return false;
        if (destination.write(block) != block.size())
            return false;
    }
    if (!destination.commit()) {
        if (error)
            *error = destination.errorString();
        return false;
    }
    return true;
}


MuPDFGenerator::SwapBackingFileResult MuPDFGenerator::swapBackingFile(const QString &path, QList<Okular::Page *> &pages)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly) || !pages.isEmpty())
        return SwapBackingFileError;
    QMutexLocker lock(userMutex());
    auto oldData = m_data;
    auto *oldDocument = m_document;
    auto oldPages = std::move(m_pages);
    auto oldAnnotations = std::move(m_annotations);
    auto oldDetached = std::move(m_detached);
    auto oldEmbedded = std::move(m_embeddedFiles);
    const bool oldEmbeddedLoaded = m_embeddedLoaded;
    const auto oldError = m_editError;
    m_document = nullptr;
    m_annotations = std::make_unique<MuPDFAnnotationProxy>(this);
    m_embeddedLoaded = false;
    bool valid = loadDocumentFromDataWithPassword(input.readAll(), pages, {}) == Okular::Document::OpenSuccess;
    QString migrationError;
    if (valid) {
        fz_try(m_context)
        {
            if (pages.isEmpty())
                fz_throw(m_context, FZ_ERROR_FORMAT, "Replacement PDF has no pages");
            if (!oldDetached.isEmpty()) {
                if (pdf_count_pages(m_context, oldDocument) != pages.size())
                    fz_throw(m_context, FZ_ERROR_ARGUMENT, "Replacement changes page count while page edits are pending");
                m_detached = migrateMuPDFPageEdits(m_context, m_document, oldDocument, oldDetached, &migrationError);
                if (!migrationError.isEmpty())
                    fz_throw(m_context, FZ_ERROR_ARGUMENT, "%s", qPrintable(migrationError));
            }
        }
        fz_catch(m_context)
        {
            valid = false;
        }
    }
    if (!valid) {
        qDeleteAll(pages);
        pages.clear();
        doCloseDocument();
        m_data = oldData;
        m_document = oldDocument;
        m_pages = std::move(oldPages);
        m_annotations = std::move(oldAnnotations);
        m_detached = std::move(oldDetached);
        m_embeddedFiles = std::move(oldEmbedded);
        m_embeddedLoaded = oldEmbeddedLoaded;
        m_editError = oldError;
        return SwapBackingFileError;
    }
    oldAnnotations.reset();
    qDeleteAll(oldEmbedded);
    for (auto *page : oldPages)
        fz_drop_page(m_context, reinterpret_cast<fz_page *>(page));
    for (auto *object : oldDetached)
        pdf_drop_obj(m_context, object);
    pdf_drop_document(m_context, oldDocument);
    m_synopsis = Okular::DocumentSynopsis();
    return SwapBackingFileReloadInternalData;
}

void MuPDFGenerator::reportAnnotationError(const QString &detail)
{
    qWarning() << "MuPDF annotation:" << detail;
    if (!m_annotationWarningShown) {
        m_annotationWarningShown = true;
        Q_EMIT warning(tr("Some annotations could not be loaded or displayed. Their original PDF data has been retained."), -1);
    }
}
