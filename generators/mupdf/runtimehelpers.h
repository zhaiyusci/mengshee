// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "mengshee_pdf_runtime_export.h"
#include <QSizeF>
#include <QString>
namespace MengsheePdfRuntime {
MENGSHEE_PDF_RUNTIME_EXPORT QSizeF firstPageSize(const QString &pdfFileName);
}
