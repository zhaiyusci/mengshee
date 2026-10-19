// SPDX-License-Identifier: AGPL-3.0-or-later
#include "annots.h"
#include "generator_mupdf.h"
#include "pageimport.h"
#include <QFile>
#include <QMutex>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUuid>
#include <cmath>
#include <core/page.h>

bool MuPDFGenerator::operation(const std::function<void()> &body, QString *error)
{
    QMutexLocker lock(userMutex());
    if (error)
        error->clear();
    if (!m_document) {
        if (error)
            *error = QStringLiteral("No PDF document is open.");
        return false;
    }
    fz_try(m_context)
    {
        body();
    }
    fz_catch(m_context)
    {
        if (error)
            *error = QString::fromUtf8(fz_caught_message(m_context));
        return false;
    }
    return true;
}
Okular::Page *MuPDFGenerator::createPage(int index)
{
    auto *native = pdf_load_page(m_context, m_document, index);
    const auto box = pdf_bound_page(m_context, native, FZ_CROP_BOX);
    auto *page = new Okular::Page(index, (box.x1 - box.x0) * dpi().width() / 72.0, (box.y1 - box.y0) * dpi().height() / 72.0, Okular::Rotation0);
    if (index < m_pages.size() && m_pages[index])
        fz_drop_page(m_context, reinterpret_cast<fz_page *>(m_pages[index]));
    if (index >= m_pages.size())
        m_pages.resize(index + 1);
    m_pages[index] = native;
    m_annotations->load(native, page);
    loadLinks(native, page);
    loadForms(native, page);
    return page;
}
bool MuPDFGenerator::canInsertBlankPage() const
{
    return true;
}
bool MuPDFGenerator::canInsertPageFromPdf() const
{
    return true;
}
bool MuPDFGenerator::canCombinePdfFiles() const
{
    return true;
}
bool MuPDFGenerator::canDeletePage() const
{
    return true;
}
bool MuPDFGenerator::canMovePage() const
{
    return true;
}
bool MuPDFGenerator::canMovePageInDocument() const
{
    return true;
}
bool MuPDFGenerator::canRotatePage() const
{
    return true;
}
bool MuPDFGenerator::canSetPageSize() const
{
    return true;
}

bool MuPDFGenerator::insertBlankPageInDocument(int after, double width, double height, Okular::Page **page, quint64 *id, QString *error, const QString &heading)
{
    if (!page || !id || !std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0)
        return false;
    pdf_obj *object = nullptr, *resources = nullptr;
    fz_buffer *contents = nullptr;
    fz_var(object);
    fz_var(resources);
    fz_var(contents);
    const bool ok = operation(
        [&] {
            fz_try(m_context)
            {
                resources = pdf_new_dict(m_context, m_document, 1);
                QByteArray bytes;
                if (!heading.isEmpty()) {
                    auto *fonts = pdf_dict_put_dict(m_context, resources, PDF_NAME(Font), 1);
                    auto *font = pdf_new_dict(m_context, m_document, 3);
                    pdf_dict_puts_drop(m_context, fonts, "F0", font);
                    pdf_dict_put_name(m_context, font, PDF_NAME(Type), "Font");
                    pdf_dict_put_name(m_context, font, PDF_NAME(Subtype), "Type1");
                    pdf_dict_put_name(m_context, font, PDF_NAME(BaseFont), "Helvetica-Bold");
                    QByteArray text = heading.toLatin1();
                    text.replace("\\", "\\\\");
                    text.replace("(", "\\(");
                    text.replace(")", "\\)");
                    bytes = "BT /F0 12 Tf 36 " + QByteArray::number(height - 36) + " Td (" + text + ") Tj ET\n";
                }
                contents = fz_new_buffer_from_copied_data(m_context, reinterpret_cast<const unsigned char *>(bytes.constData()), bytes.size());
                object = pdf_add_page(m_context, m_document, fz_make_rect(0, 0, width, height), 0, resources, contents);
                pdf_insert_page(m_context, m_document, after + 1, object);
                *id = m_nextEdit++;
                m_detached.insert(*id, pdf_keep_obj(m_context, object));
                m_pages.insert(after + 1, nullptr);
                *page = createPage(after + 1);
            }
            fz_always(m_context)
            {
                pdf_drop_obj(m_context, object);
                pdf_drop_obj(m_context, resources);
                fz_drop_buffer(m_context, contents);
            }
            fz_catch(m_context)
            {
                fz_rethrow(m_context);
            }
        },
        error);
    return ok;
}

bool MuPDFGenerator::detachPageInDocument(Okular::Page *, int index, quint64 *id, QString *error)
{
    if (!id)
        return false;
    return operation(
        [&] {
            if (index < 0 || index >= m_pages.size() || m_pages.size() <= 1)
                fz_throw(m_context, FZ_ERROR_ARGUMENT, "Cannot remove this page");
            auto *object = pdf_lookup_page_obj(m_context, m_document, index);
            *id = m_nextEdit++;
            m_detached.insert(*id, pdf_keep_obj(m_context, object));
            pdf_delete_page(m_context, m_document, index);
            fz_drop_page(m_context, reinterpret_cast<fz_page *>(m_pages.takeAt(index)));
        },
        error);
}
bool MuPDFGenerator::detachPageInDocument(Okular::Page *, int index, quint64 id, QString *error)
{
    return operation(
        [&] {
            if (!m_detached.contains(id) || index < 0 || index >= m_pages.size())
                fz_throw(m_context, FZ_ERROR_ARGUMENT, "Invalid page edit identity");
            pdf_delete_page(m_context, m_document, index);
            fz_drop_page(m_context, reinterpret_cast<fz_page *>(m_pages.takeAt(index)));
        },
        error);
}
bool MuPDFGenerator::attachPageInDocument(int after, quint64 id, Okular::Page **page, QString *error)
{
    if (!page)
        return false;
    return operation(
        [&] {
            auto *object = m_detached.value(id);
            if (!object)
                fz_throw(m_context, FZ_ERROR_ARGUMENT, "Unknown detached page");
            pdf_insert_page(m_context, m_document, after + 1, object);
            m_pages.insert(after + 1, nullptr);
            *page = createPage(after + 1);
        },
        error);
}
bool MuPDFGenerator::movePageInDocument(int from, int to, QString *error)
{
    pdf_obj *object = nullptr;
    fz_var(object);
    return operation(
        [&] {
            if (from < 0 || to < 0 || from >= m_pages.size() || to >= m_pages.size())
                fz_throw(m_context, FZ_ERROR_ARGUMENT, "Invalid page index");
            fz_try(m_context)
            {
                object = pdf_keep_obj(m_context, pdf_lookup_page_obj(m_context, m_document, from));
                pdf_delete_page(m_context, m_document, from);
                pdf_insert_page(m_context, m_document, to, object);
                m_pages.move(from, to);
            }
            fz_always(m_context)
            {
                pdf_drop_obj(m_context, object);
            }
            fz_catch(m_context)
            {
                fz_rethrow(m_context);
            }
        },
        error);
}
bool MuPDFGenerator::rotatePageInDocument(Okular::Page *, int index, int rotation, Okular::Page **page, QString *error)
{
    if (!page || rotation % 90)
        return false;
    return operation(
        [&] {
            pdf_dict_put_int(m_context, pdf_lookup_page_obj(m_context, m_document, index), PDF_NAME(Rotate), (rotation % 360 + 360) % 360);
            *page = createPage(index);
        },
        error);
}
bool MuPDFGenerator::adjustPageBoxInDocument(Okular::Page *, int index, double x0, double y0, double x1, double y1, Okular::Page **page, QString *error)
{
    if (!page)
        return false;
    return operation(
        [&] {
            auto *object = pdf_lookup_page_obj(m_context, m_document, index);
            // The generator exposes dimensions after native /Rotate and /UserUnit.
            // Deltas arrive in those axes (lower-left origin), whereas PDF boxes
            // use unrotated user space. Convert the edited display box back using
            // the original page transform, including crop origin and unit scale.
            fz_rect media;
            fz_matrix transform;
            pdf_page_transform(m_context, m_pages[index], &media, &transform);
            auto display = pdf_bound_page(m_context, m_pages[index], FZ_CROP_BOX);
            display.x0 += x0;
            display.y0 -= y1;
            display.x1 += x1;
            display.y1 -= y0;
            if (!fz_is_valid_rect(display) || fz_is_empty_rect(display) || fz_is_infinite_rect(display))
                fz_throw(m_context, FZ_ERROR_ARGUMENT, "Invalid page dimensions");
            const auto bounds = fz_transform_rect(display, fz_invert_matrix(transform));
            if (!fz_is_valid_rect(bounds) || fz_is_empty_rect(bounds) || fz_is_infinite_rect(bounds))
                fz_throw(m_context, FZ_ERROR_ARGUMENT, "Invalid page dimensions");
            pdf_dict_put_rect(m_context, object, PDF_NAME(CropBox), bounds);
            pdf_dict_put_rect(m_context, object, PDF_NAME(MediaBox), fz_union_rect(bounds, pdf_to_rect(m_context, pdf_dict_get_inheritable(m_context, object, PDF_NAME(MediaBox)))));
            *page = createPage(index);
        },
        error);
}
int MuPDFGenerator::pdfPageCount(const QString &path, QString *error)
{
    pdf_document *source = nullptr;
    fz_var(source);
    int count = -1;
    const auto name = path.toUtf8();
    operation(
        [&] {
            fz_try(m_context)
            {
                source = pdf_open_document(m_context, name.constData());
                count = pdf_count_pages(m_context, source);
            }
            fz_always(m_context)
            {
                pdf_drop_document(m_context, source);
            }
            fz_catch(m_context)
            {
                fz_rethrow(m_context);
            }
        },
        error);
    return count;
}
bool MuPDFGenerator::duplicatePageInDocument(int index, bool suffix, Okular::Page **page, quint64 *id, QString *error)
{
    if (!page || !id)
        return false;
    const QList<int> indices {index};
    QList<pdf_obj *> imported;
    QString importError;
    pdf_obj *copy = nullptr;
    fz_var(copy);
    return operation(
        [&] {
            fz_try(m_context)
            {
                imported = importMuPDFPages(m_context, m_document, m_document, indices, suffix, &importError);
                if (imported.isEmpty())
                    fz_throw(m_context, FZ_ERROR_ARGUMENT, "%s", qPrintable(importError));
                copy = imported.takeFirst();
                pdf_insert_page(m_context, m_document, index + 1, copy);
                *id = m_nextEdit++;
                m_detached.insert(*id, pdf_keep_obj(m_context, copy));
                m_pages.insert(index + 1, nullptr);
                *page = createPage(index + 1);
            }
            fz_always(m_context)
            {
                pdf_drop_obj(m_context, copy);
            }
            fz_catch(m_context)
            {
                fz_rethrow(m_context);
            }
        },
        error);
}
bool MuPDFGenerator::insertPdfPageInDocument(int after, const QString &path, int index, bool suffix, Okular::Page **page, quint64 *id, QString *error)
{
    if (!page || !id)
        return false;
    const QList<int> indices {index};
    QList<pdf_obj *> imported;
    QString importError;
    pdf_obj *copy = nullptr;
    fz_var(copy);
    pdf_document *source = nullptr;
    fz_var(source);
    const auto name = path.toUtf8();
    return operation(
        [&] {
            fz_try(m_context)
            {
                source = pdf_open_document(m_context, name.constData());
                if (pdf_needs_password(m_context, source))
                    fz_throw(m_context, FZ_ERROR_ARGUMENT, "Cannot import an encrypted PDF without a password");
                if (after < -1 || after >= m_pages.size())
                    fz_throw(m_context, FZ_ERROR_ARGUMENT, "Invalid insertion position");
                imported = importMuPDFPages(m_context, m_document, source, indices, suffix, &importError);
                if (imported.isEmpty())
                    fz_throw(m_context, FZ_ERROR_ARGUMENT, "%s", qPrintable(importError));
                copy = imported.takeFirst();
                pdf_insert_page(m_context, m_document, after + 1, copy);
                *id = m_nextEdit++;
                m_detached.insert(*id, pdf_keep_obj(m_context, pdf_lookup_page_obj(m_context, m_document, after + 1)));
                m_pages.insert(after + 1, nullptr);
                *page = createPage(after + 1);
            }
            fz_always(m_context)
            {
                pdf_drop_obj(m_context, copy);
                pdf_drop_document(m_context, source);
            }
            fz_catch(m_context)
            {
                fz_rethrow(m_context);
            }
        },
        error);
}
bool MuPDFGenerator::combinePdfFiles(const QStringList &paths, const QString &output, bool suffix, QString *error)
{
    pdf_document *combined = nullptr, *source = nullptr;
    fz_var(combined);
    fz_var(source);
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        if (error)
            *error = QStringLiteral("Cannot create temporary output directory.");
        return false;
    }
    const auto path = temporary.filePath(QStringLiteral("combined.pdf"));
    const auto name = path.toUtf8();
    QList<int> indices;
    QList<pdf_obj *> imported;
    QByteArray inputName;
    QString importError;
    const bool ok = operation(
        [&] {
            fz_try(m_context)
            {
                if (paths.isEmpty())
                    fz_throw(m_context, FZ_ERROR_ARGUMENT, "No PDFs selected");
                combined = pdf_create_document(m_context);
                for (const auto &path : paths) {
                    inputName = path.toUtf8();
                    source = pdf_open_document(m_context, inputName.constData());
                    if (pdf_needs_password(m_context, source))
                        fz_throw(m_context, FZ_ERROR_ARGUMENT, "Cannot combine an encrypted PDF without a password");
                    indices.clear();
                    for (int i = 0; i < pdf_count_pages(m_context, source); ++i)
                        indices.append(i);
                    imported = importMuPDFPages(m_context, combined, source, indices, suffix, &importError);
                    if (imported.isEmpty())
                        fz_throw(m_context, FZ_ERROR_ARGUMENT, "%s", qPrintable(importError));
                    fz_try(m_context)
                    {
                        for (auto *page : imported)
                            pdf_insert_page(m_context, combined, -1, page);
                    }
                    fz_always(m_context)
                    {
                        for (auto *page : imported)
                            pdf_drop_obj(m_context, page);
                        imported.clear();
                    }
                    fz_catch(m_context)
                    {
                        fz_rethrow(m_context);
                    }
                    pdf_drop_document(m_context, source);
                    source = nullptr;
                }
                pdf_save_document(m_context, combined, name.constData(), &pdf_default_write_options);
            }
            fz_always(m_context)
            {
                pdf_drop_document(m_context, source);
                pdf_drop_document(m_context, combined);
            }
            fz_catch(m_context)
            {
                fz_rethrow(m_context);
            }
        },
        error);
    if (!ok)
        return false;
    QFile input(path);
    QSaveFile destination(output);
    if (!input.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Cannot open combined PDF output.");
        return false;
    }
    while (!input.atEnd()) {
        const auto chunk = input.read(1024 * 1024);
        if (chunk.isEmpty() && input.error() != QFile::NoError) {
            if (error)
                *error = input.errorString();
            return false;
        }
        if (destination.write(chunk) != chunk.size()) {
            if (error)
                *error = destination.errorString();
            return false;
        }
    }
    if (!destination.commit()) {
        if (error)
            *error = destination.errorString();
        return false;
    }
    return true;
}
