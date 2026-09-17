/*
    SPDX-FileCopyrightText: 2026 Mengshee contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef IMAGEEXPORTDIALOG_H
#define IMAGEEXPORTDIALOG_H

#include "okularpart_export.h"

#include <QList>
#include <QString>

class QWidget;

namespace Okular
{
class Document;

namespace ImageExportUtils
{
/** Strict ASCII 1-based ranges (e.g. "1-4, 8"); sorted, unique zero-based output.
 * Empty items, descending/open ranges, overflow and out-of-document pages fail.
 * On failure pages is cleared and error, when supplied, is translated.
 */
OKULARPART_EXPORT bool parsePageRange(const QString &text, int pageCount, QList<int> *pages, QString *error = nullptr);

/** A portable single filename component, excluding reserved Windows names. */
OKULARPART_EXPORT bool isSafePrefix(const QString &prefix);
OKULARPART_EXPORT QString suggestedPrefix(const QString &baseName);
/** Returns an empty string for unsafe input. pageZeroBased must be nonnegative.
 * Only "png" and "jpg" extensions are accepted. Uses the original page number.
 */
OKULARPART_EXPORT QString pageFileName(const QString &prefix, int pageZeroBased, const QString &extension);
}

/** GUI-thread-only, application-modal, synchronous image export.
 * Requires Document::canRenderToImage() and Document::renderToImage().
 * Cancellation takes effect between pages; already committed files are retained.
 * No document ownership is taken. Close/replacement/destruction aborts export.
 */
class OKULARPART_EXPORT ImageExportDialog
{
public:
    static void exportDocument(Document *document, int currentPageZeroBased, const QString &suggestedBaseName, QWidget *parent = nullptr);
};
}

#endif
