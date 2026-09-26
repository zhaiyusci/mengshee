/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef MENGSHEE_PDFREADINGVIEWS_H
#define MENGSHEE_PDFREADINGVIEWS_H

#include "core/readingview.h"
#include <QList>

class PDFDoc;

namespace MengsheePdfReadingViews
{
// App-owned PDF page metadata. All page arguments are native, zero-based.
// Caller owns the live document lock. Reading never modifies a PDF.
QList<Okular::ReadingView> read(PDFDoc *document, int page, QString *error = nullptr);
bool write(PDFDoc *document, int page, const QList<Okular::ReadingView> &views, QString *error = nullptr);
QString pageToken(PDFDoc *document, int page);
int pageForToken(PDFDoc *document, const QString &token);
// Called only for a newly duplicated/imported page. Keep View IDs and numbers;
// separate its page identity from the source. Unknown schemas remain untouched.
void renewCopiedPageToken(PDFDoc *document, int page);
}

#endif
