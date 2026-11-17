// SPDX-License-Identifier: AGPL-3.0-or-later
#include "generator_mupdf.h"
#include "vectorprintdevice.h"
#include <core/printjob.h>
#include <QDebug>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QPainter>
#include <QPen>
#include <QPrinter>
#include <QRegion>
#include <algorithm>
#include <cmath>

namespace
{
QList<int> selectedPages(const QPrinter &printer, int pageCount)
{
    QList<int> pages;
    const int first = printer.fromPage() > 0 ? printer.fromPage() - 1 : 0;
    const int last = printer.toPage() > 0 ? std::min(printer.toPage(), pageCount) - 1 : pageCount - 1;
    for (int i = first; i <= last; ++i)
        pages.append(printer.pageOrder() == QPrinter::LastPageFirst ? last - (i - first) : i);
    return pages;
}
}

Okular::Document::PrintError MuPDFGenerator::printWithLayout(QPrinter &printer, const Okular::PrintLayoutOptions &options)
{
    const QList<int> pages = selectedPages(printer, m_pages.size());
    if (pages.isEmpty())
        return Okular::Document::NoPrintError;

    const int renderDpi = std::min(printer.resolution(), 300);
    QHash<int, QSizeF> pageSizes;
    QHash<quint64, QImage> pageCache;
    const auto pageSize = [&](int page) -> QSizeF {
        if (auto it = pageSizes.constFind(page); it != pageSizes.constEnd())
            return *it;
        QSizeF size;
        if (!readOperation([&] {
                const auto bounds = pdf_bound_page(m_context, m_pages.at(page), FZ_CROP_BOX);
                size = QSizeF(bounds.x1 - bounds.x0, bounds.y1 - bounds.y0);
            }, nullptr))
            return {};
        pageSizes.insert(page, size);
        return size;
    };
    const auto pageImage = [&](int page, const QSize &maximumPixels = {}) -> QImage {
        if (page < 0)
            return {};
        int dpi = renderDpi;
        const QSizeF size = pageSize(page);
        if (maximumPixels.isValid() && size.width() > 0 && size.height() > 0) {
            dpi = std::min(dpi, int(std::ceil(std::min(maximumPixels.width() * 72.0 / size.width(),
                                                      maximumPixels.height() * 72.0 / size.height()))));
            dpi = qMax(36, dpi);
        }
        const quint64 key = (quint64(page) << 32) | quint64(dpi);
        if (auto it = pageCache.constFind(key); it != pageCache.constEnd())
            return *it;
        QString error;
        QImage image = renderToImage(page, dpi, m_printAnnotations, &error);
        if (printer.colorMode() == QPrinter::GrayScale && !image.isNull() && !image.isGrayscale())
            image = image.convertToFormat(QImage::Format_Grayscale8);
        pageCache.insert(key, image);
        return image;
    };

    QList<Okular::PrintSheet> sheets = Okular::buildPrintSheets(pages, options);
    if (options.mode == Okular::PrintLayout::Poster) {
        const qreal overlap = qMax(0.0, options.posterOverlapMm) * printer.resolution() / 25.4;
        const qreal stepWidth = qMax(1.0, printer.width() - overlap);
        const qreal stepHeight = qMax(1.0, printer.height() - overlap);
        QList<Okular::PrintSheet> tiled;
        for (const Okular::PrintSheet &base : std::as_const(sheets)) {
            const QSizeF page = pageSize(base.posterPage);
            if (!page.isValid())
                continue;
            const qreal width = page.width() * printer.resolution() / 72.0 * options.posterScale;
            const qreal height = page.height() * printer.resolution() / 72.0 * options.posterScale;
            const int columns = qMax(1, int(std::ceil(qMax(0.0, width - overlap) / stepWidth)));
            const int rows = qMax(1, int(std::ceil(qMax(0.0, height - overlap) / stepHeight)));
            for (int row = 0; row < rows; ++row) {
                for (int column = 0; column < columns; ++column) {
                    Okular::PrintSheet sheet;
                    sheet.posterPage = base.posterPage;
                    sheet.tileColumn = column;
                    sheet.tileRow = row;
                    sheet.tileColumns = columns;
                    sheet.tileRows = rows;
                    tiled.append(sheet);
                }
            }
        }
        sheets = tiled;
    }

    QPainter painter;
    const QRectF sheetRect(0, 0, printer.width(), printer.height());
    const auto startSheet = [&](bool first) {
        return first ? painter.begin(&printer) : printer.newPage();
    };
    bool firstSheet = true;
    for (const Okular::PrintSheet &sheet : std::as_const(sheets)) {
        if (!startSheet(firstSheet)) {
            if (painter.isActive())
                painter.end();
            return Okular::Document::UnknownPrintError;
        }
        firstSheet = false;
        painter.fillRect(sheetRect, Qt::white);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);

        if (options.mode == Okular::PrintLayout::Poster) {
            const QSizeF page = pageSize(sheet.posterPage);
            if (page.isValid()) {
                const qreal overlap = qMax(0.0, options.posterOverlapMm) * printer.resolution() / 25.4;
                const qreal stepWidth = qMax(1.0, sheetRect.width() - overlap);
                const qreal stepHeight = qMax(1.0, sheetRect.height() - overlap);
                const QSizeF posterSize(page.width() * printer.resolution() / 72.0 * options.posterScale,
                                        page.height() * printer.resolution() / 72.0 * options.posterScale);
                const QSize posterPixels(qCeil(posterSize.width()), qCeil(posterSize.height()));
                const QImage image = pageImage(sheet.posterPage, posterPixels);
                if (image.isNull())
                    continue;
                const QRectF target(sheetRect.left() - sheet.tileColumn * stepWidth,
                                    sheetRect.top() - sheet.tileRow * stepHeight,
                                    posterSize.width(),
                                    posterSize.height());
                painter.save();
                painter.setClipRect(sheetRect);
                painter.drawImage(target, image);
                painter.restore();
                if (options.posterCutMarks) {
                    const qreal mark = 3.0 * printer.resolution() / 25.4;
                    painter.setPen(QPen(Qt::black, 0));
                    painter.drawLine(sheetRect.topLeft(), sheetRect.topLeft() + QPointF(mark, 0));
                    painter.drawLine(sheetRect.topLeft(), sheetRect.topLeft() + QPointF(0, mark));
                    painter.drawLine(sheetRect.topRight(), sheetRect.topRight() - QPointF(mark, 0));
                    painter.drawLine(sheetRect.topRight(), sheetRect.topRight() + QPointF(0, mark));
                    painter.drawLine(sheetRect.bottomLeft(), sheetRect.bottomLeft() + QPointF(mark, 0));
                    painter.drawLine(sheetRect.bottomLeft(), sheetRect.bottomLeft() - QPointF(0, mark));
                    painter.drawLine(sheetRect.bottomRight(), sheetRect.bottomRight() - QPointF(mark, 0));
                    painter.drawLine(sheetRect.bottomRight(), sheetRect.bottomRight() - QPointF(0, mark));
                }
            }
            continue;
        }

        const int columns = options.mode == Okular::PrintLayout::Booklet ? 2 : Okular::printLayoutColumns(options.pagesPerSheet);
        const int rows = options.mode == Okular::PrintLayout::Booklet ? 1 : Okular::printLayoutRows(options.pagesPerSheet);
        const qreal gap = qMin(sheetRect.width(), sheetRect.height()) * 0.015;
        const qreal cellWidth = (sheetRect.width() - gap * (columns + 1)) / columns;
        const qreal cellHeight = (sheetRect.height() - gap * (rows + 1)) / rows;
        for (int index = 0; index < sheet.pages.size(); ++index) {
            const int page = sheet.pages.at(index);
            if (page < 0)
                continue;
            int column = 0;
            int row = 0;
            if (options.pageOrder == Okular::PrintLayout::Vertical) {
                column = index / rows;
                row = index % rows;
            } else if (options.pageOrder == Okular::PrintLayout::VerticalReversed) {
                column = index / rows;
                row = rows - 1 - index % rows;
            } else {
                column = index % columns;
                row = index / columns;
                if (options.pageOrder == Okular::PrintLayout::HorizontalReversed)
                    column = columns - 1 - column;
            }
            const QRectF cell(sheetRect.left() + gap + column * (cellWidth + gap),
                              sheetRect.top() + gap + row * (cellHeight + gap),
                              cellWidth,
                              cellHeight);
            const QImage image = pageImage(page, cell.size().toSize());
            if (image.isNull())
                continue;
            QSizeF target = image.size();
            target.scale(cell.size(), Qt::KeepAspectRatio);
            const QRectF targetRect(cell.center().x() - target.width() / 2,
                                    cell.center().y() - target.height() / 2,
                                    target.width(),
                                    target.height());
            painter.drawImage(targetRect, image);
            if (options.pageBorder)
                painter.drawRect(cell.adjusted(-gap / 2, -gap / 2, gap / 2, gap / 2));
        }
    }
    return painter.end() ? Okular::Document::NoPrintError : Okular::Document::UnknownPrintError;
}

Okular::Document::PrintError MuPDFGenerator::print(QPrinter &printer)
{
    if (!isAllowed(Okular::AllowPrint))
        return Okular::Document::UnknownPrintError;
    if (m_printLayoutOptions.mode != Okular::PrintLayout::Size)
        return printWithLayout(printer, m_printLayoutOptions);

    QPainter painter;
    const auto *job = dynamic_cast<const Okular::PrintJobPrinter *>(&printer);
    const bool automaticOrientation = job && job->automaticPageOrientation;
    const bool originalPageSize = job && job->originalPageSize;
    const int first = printer.fromPage() > 0 ? printer.fromPage()-1 : 0;
    const int last = printer.toPage() > 0 ? std::min(printer.toPage(),int(m_pages.size()))-1 : m_pages.size()-1;
    for (int i=first; i<=last; ++i) {
        const int index = printer.pageOrder()==QPrinter::LastPageFirst ? last-(i-first) : i;
        if (automaticOrientation) {
            QSizeF size;
            if (!readOperation([&] {
                const auto bounds = pdf_bound_page(m_context, m_pages.at(index), FZ_CROP_BOX);
                size = QSizeF(bounds.x1-bounds.x0, bounds.y1-bounds.y0);
            }, nullptr)) return Okular::Document::UnknownPrintError;
            // Set the upcoming sheet before begin/newPage. The PDF content
            // keeps its own coordinates and rotation; only the paper turns.
            printer.setPageOrientation(Okular::paperOrientationForPage(size));
        }
        if (i == first) {
            if (!painter.begin(&printer)) return Okular::Document::InvalidPrinterStatePrintError;
        } else if (!printer.newPage()) return Okular::Document::UnknownPrintError;
        QMutexLocker lock(userMutex());
        pdf_page *page = nullptr;
        fz_display_list *list = nullptr;
        fz_device *device = nullptr;
        fz_pixmap *pixmap = nullptr;
        fz_var(page); fz_var(list); fz_var(device); fz_var(pixmap);
        QRegion fallbackRegions;
        // QPrinter's default origin is already the printable area's top left.
        const QRect area(0,0,printer.width(),printer.height());
        painter.save();
        fz_try(m_context) {
            page = pdf_load_page(m_context,m_document,index);
            if (pdf_update_page(m_context,page)) clearAnnotationLists();
            const auto bounds = pdf_bound_page(m_context,page,FZ_CROP_BOX);
            // Mirrors the PDF backend's "None; print original size" path: the
            // page keeps its own physical size (printer resolution / 72) and is
            // centred in the printable area instead of being scaled to it.
            const float scale = originalPageSize
                ? std::max(1,printer.resolution())/72.0f
                : std::min(area.width()/(bounds.x1-bounds.x0),area.height()/(bounds.y1-bounds.y0));
            const auto transform = fz_make_matrix(scale,0,0,scale,
                (area.width()-(bounds.x1-bounds.x0)*scale)/2-bounds.x0*scale,
                (area.height()-(bounds.y1-bounds.y0)*scale)/2-bounds.y0*scale);
            const auto target = fz_transform_rect(bounds,transform);
            painter.setClipRect(QRectF(target.x0,target.y0,target.x1-target.x0,target.y1-target.y0));
            list = fz_new_display_list(m_context,bounds);
            device = fz_new_list_device(m_context,list);
            if (m_printAnnotations)
                pdf_run_page_with_usage(m_context,page,device,fz_identity,"Print",nullptr);
            else
                pdf_run_page_contents_with_usage(m_context,page,device,fz_identity,"Print",nullptr);
            if (m_printAnnotations) {
                auto *annots = pdf_dict_get(m_context,page->obj,PDF_NAME(Annots));
                for (int n=0; n<pdf_array_len(m_context,annots); ++n)
                    renderLinkAppearance(page,pdf_array_get(m_context,annots,n),device,true);
            }
            fz_close_device(m_context,device);
            fz_drop_device(m_context,device); device = nullptr;
            painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
            device = newVectorPrintDevice(m_context,painter,area,fallbackRegions);
            fz_run_display_list(m_context,list,device,transform,fz_infinite_rect,nullptr);
            fz_close_device(m_context,device);
            fz_drop_device(m_context,device); device = nullptr;
            // Replay only unsupported composite regions with their final
            // backdrop/foreground, at printer resolution and bounded memory.
            for (const QRect &region : fallbackRegions) {
                for (int y=region.top(); y<=region.bottom(); y+=512) {
                    for (int x=region.left(); x<=region.right(); x+=512) {
                        const QRect tile = QRect(x,y,512,512).intersected(region);
                        const fz_irect box{tile.left(),tile.top(),tile.right()+1,tile.bottom()+1};
                        pixmap = fz_new_pixmap_with_bbox(m_context,fz_device_rgb(m_context),box,nullptr,0);
                        fz_clear_pixmap_with_value(m_context,pixmap,255);
                        device = fz_new_draw_device(m_context,fz_identity,pixmap);
                        fz_run_display_list(m_context,list,device,transform,fz_rect_from_irect(box),nullptr);
                        fz_close_device(m_context,device);
                        fz_drop_device(m_context,device); device = nullptr;
                        const QImage image(fz_pixmap_samples(m_context,pixmap),tile.width(),tile.height(),fz_pixmap_stride(m_context,pixmap),QImage::Format_RGB888);
                        painter.drawImage(tile.topLeft(),image);
                        fz_drop_pixmap(m_context,pixmap); pixmap = nullptr;
                    }
                }
            }
        }
        fz_always(m_context) {
            fz_drop_device(m_context,device);
            fz_drop_pixmap(m_context,pixmap);
            fz_drop_display_list(m_context,list);
            pdf_drop_page(m_context,page);
            painter.restore();
        }
        fz_catch(m_context) {
            qWarning() << "Vector printing failed:" << fz_caught_message(m_context);
            printer.abort();
            return Okular::Document::UnknownPrintError;
        }
    }
    return painter.end() ? Okular::Document::NoPrintError : Okular::Document::UnknownPrintError;
}

void MuPDFGenerator::setPrintAnnotations(bool enabled)
{
    m_printAnnotations = enabled;
}

bool MuPDFGenerator::printAnnotations() const
{
    return m_printAnnotations;
}

void MuPDFGenerator::setPrintLayoutOptions(const Okular::PrintLayoutOptions &options)
{
    m_printLayoutOptions = options;
}
