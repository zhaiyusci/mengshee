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
struct PrintSheet {
    QList<int> pages;
    int posterPage = -1;
    int tileColumn = 0;
    int tileRow = 0;
    int tileColumns = 1;
    int tileRows = 1;
};

int gridColumns(int count)
{
    switch (count) {
    case 2:
        return 2;
    case 4:
        return 2;
    case 6:
        return 3;
    case 9:
        return 3;
    case 16:
        return 4;
    default:
        return 1;
    }
}

int gridRows(int count)
{
    return count <= 0 ? 1 : (count + gridColumns(count) - 1) / gridColumns(count);
}

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

Okular::Document::PrintError MuPDFGenerator::printWithLayout(QPrinter &printer, const PrintLayoutOptions &options)
{
    const QList<int> pages = selectedPages(printer, m_pages.size());
    if (pages.isEmpty())
        return Okular::Document::NoPrintError;

    const int renderDpi = std::min(printer.resolution(), 300);
    QHash<int, QImage> pageCache;
    const auto pageImage = [&](int page) -> QImage {
        if (page < 0)
            return {};
        auto it = pageCache.constFind(page);
        if (it != pageCache.constEnd())
            return *it;
        QString error;
        QImage image = renderToImage(page, renderDpi, m_printAnnotations, &error);
        if (printer.colorMode() == QPrinter::GrayScale && !image.isNull() && !image.isGrayscale())
            image = image.convertToFormat(QImage::Format_Grayscale8);
        pageCache.insert(page, image);
        return image;
    };

    QList<PrintSheet> sheets;
    if (options.mode == Okular::PrintLayout::Multiple) {
        const int pagesPerSheet = qMax(1, options.pagesPerSheet);
        for (int first = 0; first < pages.size(); first += pagesPerSheet) {
            PrintSheet sheet;
            sheet.pages = pages.mid(first, qMin(pagesPerSheet, int(pages.size()) - first));
            sheets.append(sheet);
        }
    } else if (options.mode == Okular::PrintLayout::Booklet) {
        QList<int> booklet = pages;
        while (booklet.size() % 4 != 0)
            booklet.append(-1);
        for (int i = 0; i < booklet.size() / 4; ++i) {
            PrintSheet front;
            front.pages = {booklet.at(booklet.size() - 1 - 2 * i), booklet.at(2 * i)};
            PrintSheet back;
            back.pages = {booklet.at(2 * i + 1), booklet.at(booklet.size() - 2 - 2 * i)};
            if (options.bookletBinding == Okular::PrintLayout::BindRight) {
                std::reverse(front.pages.begin(), front.pages.end());
                std::reverse(back.pages.begin(), back.pages.end());
            }
            if (options.bookletSubset != Okular::PrintLayout::BackSides)
                sheets.append(front);
            if (options.bookletSubset != Okular::PrintLayout::FrontSides)
                sheets.append(back);
        }
    } else if (options.mode == Okular::PrintLayout::Poster) {
        const qreal overlap = qMax(0.0, options.posterOverlapMm) * printer.resolution() / 25.4;
        const qreal stepWidth = qMax(1.0, printer.width() - overlap);
        const qreal stepHeight = qMax(1.0, printer.height() - overlap);
        for (int page : pages) {
            QSizeF pageSize;
            if (!readOperation([&] {
                    const auto bounds = pdf_bound_page(m_context, m_pages.at(page), FZ_CROP_BOX);
                    pageSize = QSizeF(bounds.x1 - bounds.x0, bounds.y1 - bounds.y0);
                }, nullptr))
                return Okular::Document::UnknownPrintError;
            if (!pageSize.isValid())
                continue;
            const qreal width = pageSize.width() * printer.resolution() / 72.0 * options.posterScale;
            const qreal height = pageSize.height() * printer.resolution() / 72.0 * options.posterScale;
            const int columns = qMax(1, int(std::ceil(qMax(0.0, width - overlap) / stepWidth)));
            const int rows = qMax(1, int(std::ceil(qMax(0.0, height - overlap) / stepHeight)));
            for (int row = 0; row < rows; ++row) {
                for (int column = 0; column < columns; ++column) {
                    PrintSheet sheet;
                    sheet.posterPage = page;
                    sheet.tileColumn = column;
                    sheet.tileRow = row;
                    sheet.tileColumns = columns;
                    sheet.tileRows = rows;
                    sheets.append(sheet);
                }
            }
        }
    } else {
        return Okular::Document::NoPrintError;
    }

    QPainter painter;
    const QRectF sheetRect(0, 0, printer.width(), printer.height());
    const auto startSheet = [&](bool first) {
        return first ? painter.begin(&printer) : printer.newPage();
    };
    bool firstSheet = true;
    for (const PrintSheet &sheet : std::as_const(sheets)) {
        if (!startSheet(firstSheet)) {
            if (painter.isActive())
                painter.end();
            return Okular::Document::UnknownPrintError;
        }
        firstSheet = false;
        painter.fillRect(sheetRect, Qt::white);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);

        if (options.mode == Okular::PrintLayout::Poster) {
            const QImage image = pageImage(sheet.posterPage);
            if (!image.isNull()) {
                QSizeF pageSize;
                readOperation([&] {
                    const auto bounds = pdf_bound_page(m_context, m_pages.at(sheet.posterPage), FZ_CROP_BOX);
                    pageSize = QSizeF(bounds.x1 - bounds.x0, bounds.y1 - bounds.y0);
                }, nullptr);
                const qreal overlap = qMax(0.0, options.posterOverlapMm) * printer.resolution() / 25.4;
                const qreal stepWidth = qMax(1.0, sheetRect.width() - overlap);
                const qreal stepHeight = qMax(1.0, sheetRect.height() - overlap);
                const QSizeF posterSize(pageSize.width() * printer.resolution() / 72.0 * options.posterScale,
                                        pageSize.height() * printer.resolution() / 72.0 * options.posterScale);
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

        const int columns = gridColumns(sheet.pages.size());
        const int rows = gridRows(sheet.pages.size());
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
            const QImage image = pageImage(page);
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

void MuPDFGenerator::setPrintLayoutOptions(const PrintLayoutOptions &options)
{
    m_printLayoutOptions = options;
}
