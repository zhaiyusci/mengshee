/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef OKULAR_READINGVIEWEDITINGINTERFACE_H
#define OKULAR_READINGVIEWEDITINGINTERFACE_H

#include "okularcore_export.h"
#include "readingview.h"
#include <QList>
#include <QMap>
#include <functional>

namespace Okular
{
struct ReadingViewGenerationResult {
    bool success = false;
    bool cancelled = false;
    QString errorText;
    // Zero-based snapshot pages; canonical ReadingView coordinates (native
    // rotation, CropBox-relative, top-left origin). Empty means no detections.
    QMap<int, QList<Okular::NormalizedRect>> rectangles;
};

// Optional backend capability. Definitions are non-painting document metadata,
// not annotations, content streams, named destinations or a reading order.
class OKULARCORE_EXPORT ReadingViewEditingInterface
{
public:
    virtual ~ReadingViewEditingInterface() = default;
    // Invoked on the calling worker thread before and after each page, including
    // the last. currentPageNumber is ONE-based; return false to cancel.
    using ProgressCallback = std::function<bool(int completed, int total, int currentPageNumber)>;
    virtual bool canGenerateReadingViews() const { return false; }
    // Analyse a saved snapshot, never the live document. pageNumbers are zero-
    // based. No metadata is written; discard all rectangles on failure/cancel.
    virtual ReadingViewGenerationResult generateReadingViews(const QString &, const QList<int> &, const ProgressCallback &)
    {
        ReadingViewGenerationResult result;
        result.errorText = QStringLiteral("Automatic View generation is not supported by this document backend.");
        return result;
    }
    virtual bool canEditReadingViews() const = 0;
    virtual QList<ReadingView> readingViews(int pageNumber, QString *errorText) const = 0;
    virtual bool setReadingViews(int pageNumber, const QList<ReadingView> &views, QString *errorText) = 0;
    // An identity token exists after the first successful write. It survives
    // page reordering and save/reopen; a removed/ambiguous page resolves to -1.
    virtual QString readingViewPageToken(int pageNumber) const = 0;
    virtual int readingViewPageForToken(const QString &token) const = 0;
};
}

#endif
