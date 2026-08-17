/*
    SPDX-FileCopyrightText: 2006 Albert Astals Cid <aacid@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "pagesizelabel.h"

#include "core/document.h"
#include "pageview.h"

PageSizeLabel::PageSizeLabel(QWidget *parent, Okular::Document *document)
    : KSqueezedTextLabel(parent)
    , m_document(document)
{
    setAlignment(Qt::AlignRight);
}

PageSizeLabel::~PageSizeLabel()
{
    m_document->removeObserver(this);
}

void PageSizeLabel::notifyCurrentPageChanged(int previousPage, int currentPage)
{
    Q_UNUSED(previousPage)

    Q_UNUSED(currentPage)
    refreshCurrentPage();
}

void PageSizeLabel::setPageView(PageView *pageView)
{
    if (m_pageView == pageView) {
        refreshCurrentPage();
        return;
    }

    disconnect(m_pageViewViewportConnection);
    m_pageView = pageView;
    if (m_pageView) {
        m_pageViewViewportConnection = connect(m_pageView, &PageView::viewportStateChanged, this, &PageSizeLabel::refreshCurrentPage);
    }
    refreshCurrentPage();
}

void PageSizeLabel::refreshCurrentPage()
{
    const int currentPage = m_pageView ? m_pageView->documentViewport().pageNumber : static_cast<int>(m_document->currentPage());

    // if the document is opened
    if (m_document->pages() > 0 && currentPage >= 0 && currentPage < static_cast<int>(m_document->pages()) && !m_document->allPagesSize().isValid()) {
        setText(m_document->pageSizeString(currentPage));
    }
}

#include "moc_pagesizelabel.cpp"
