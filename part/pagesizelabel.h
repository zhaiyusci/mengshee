/*
    SPDX-FileCopyrightText: 2006 Albert Astals Cid <aacid@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef _OKULAR_PAGESIZELABEL_H_
#define _OKULAR_PAGESIZELABEL_H_

#include <KSqueezedTextLabel>

#include "core/observer.h"
#include <QMetaObject>
#include <QPointer>

namespace Okular
{
class Document;
}

class PageView;

/**
 * @short A widget to display page size.
 */
class PageSizeLabel : public KSqueezedTextLabel, public Okular::DocumentObserver
{
    Q_OBJECT

public:
    PageSizeLabel(QWidget *parent, Okular::Document *document);
    ~PageSizeLabel() override;

    // [INHERITED] from DocumentObserver
    void notifyCurrentPageChanged(int previous, int current) override;
    void setPageView(PageView *pageView);

private:
    void refreshCurrentPage();

    Okular::Document *m_document;
    QPointer<PageView> m_pageView;
    QMetaObject::Connection m_pageViewViewportConnection;
};

#endif
