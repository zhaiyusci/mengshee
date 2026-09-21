/*
    SPDX-FileCopyrightText: 2026 Mengshee contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef FLATTENEDPDFEXPORTDIALOG_H
#define FLATTENEDPDFEXPORTDIALOG_H

#include "okularpart_export.h"

class QUrl;
class QWidget;

namespace Okular
{
class Document;

/** GUI-thread-only, synchronous export of all pages to a separate final copy.
 * sourceUrl must be Part::realUrl(), not a downloaded/decompressed temporary URL.
 * Takes no ownership and never saves or reloads the source document.
 */
class OKULARPART_EXPORT FlattenedPdfExportDialog
{
public:
    static void exportDocument(Document *document, const QUrl &sourceUrl, QWidget *parent = nullptr);
};
}

#endif
