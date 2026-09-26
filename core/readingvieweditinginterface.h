/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef OKULAR_READINGVIEWEDITINGINTERFACE_H
#define OKULAR_READINGVIEWEDITINGINTERFACE_H

#include "okularcore_export.h"
#include "readingview.h"
#include <QList>

namespace Okular
{
// Optional backend capability. Definitions are non-painting document metadata,
// not annotations, content streams, named destinations or a reading order.
class OKULARCORE_EXPORT ReadingViewEditingInterface
{
public:
    virtual ~ReadingViewEditingInterface() = default;
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
