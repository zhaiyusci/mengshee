// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QHash>
#include <QList>
#include <QString>
#include <mupdf/pdf.h>

// Returns retained, detached page references; caller inserts and drops them.
QList<pdf_obj *> importMuPDFPages(fz_context *, pdf_document *destination, pdf_document *source, const QList<int> &pages, bool suffixDestinations, QString *error = nullptr);

QHash<quint64, pdf_obj *> migrateMuPDFPageEdits(fz_context *, pdf_document *destination, pdf_document *source, const QHash<quint64, pdf_obj *> &edits, QString *error = nullptr);
