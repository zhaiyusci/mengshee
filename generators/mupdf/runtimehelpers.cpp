// SPDX-License-Identifier: AGPL-3.0-or-later
#include "context.h"
#include "runtimehelpers.h"
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
QSizeF MengsheePdfRuntime::firstPageSize(const QString &pdfFileName)
{
    fz_context *context = MengsheePdfRuntime::createContext();
    if (!context) return {};
    pdf_document *document = nullptr;
    fz_var(document);
    QSizeF size;
    const auto path = pdfFileName.toUtf8();
    fz_try(context) {
        document = pdf_open_document(context, path.constData());
        if (pdf_count_pages(context, document) > 0 && !pdf_needs_password(context, document)) {
            fz_rect box;
            fz_matrix matrix;
            pdf_page_obj_transform(context, pdf_lookup_page_obj(context, document, 0), &box, &matrix);
            box = fz_transform_rect(box, matrix);
            size = QSizeF(box.x1 - box.x0, box.y1 - box.y0);
        }
    }
    fz_always(context) { pdf_drop_document(context, document); }
    fz_catch(context) { size = {}; }
    fz_drop_context(context);
    return size;
}
