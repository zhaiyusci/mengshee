// SPDX-License-Identifier: AGPL-3.0-or-later
#include "generator_mupdf.h"
#include "vectorprintdevice.h"
#include <QDebug>
#include <QMutex>
#include <QPainter>
#include <QPrinter>
#include <QRegion>
#include <algorithm>

Okular::Document::PrintError MuPDFGenerator::print(QPrinter &printer)
{
    if (!isAllowed(Okular::AllowPrint)) return Okular::Document::UnknownPrintError;
    QPainter painter;
    if (!painter.begin(&printer)) return Okular::Document::InvalidPrinterStatePrintError;
    const int first = printer.fromPage() > 0 ? printer.fromPage()-1 : 0;
    const int last = printer.toPage() > 0 ? std::min(printer.toPage(),int(m_pages.size()))-1 : m_pages.size()-1;
    for (int i=first; i<=last; ++i) {
        if (i!=first && !printer.newPage()) return Okular::Document::UnknownPrintError;
        const int index = printer.pageOrder()==QPrinter::LastPageFirst ? last-(i-first) : i;
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
            const float scale = std::min(area.width()/(bounds.x1-bounds.x0),area.height()/(bounds.y1-bounds.y0));
            const auto transform = fz_make_matrix(scale,0,0,scale,
                (area.width()-(bounds.x1-bounds.x0)*scale)/2-bounds.x0*scale,
                (area.height()-(bounds.y1-bounds.y0)*scale)/2-bounds.y0*scale);
            const auto target = fz_transform_rect(bounds,transform);
            painter.setClipRect(QRectF(target.x0,target.y0,target.x1-target.x0,target.y1-target.y0));
            list = fz_new_display_list(m_context,bounds);
            device = fz_new_list_device(m_context,list);
            pdf_run_page_with_usage(m_context,page,device,fz_identity,"Print",nullptr);
            auto *annots = pdf_dict_get(m_context,page->obj,PDF_NAME(Annots));
            for (int n=0; n<pdf_array_len(m_context,annots); ++n)
                renderLinkAppearance(page,pdf_array_get(m_context,annots,n),device,true);
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
