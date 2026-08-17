/*
    SPDX-FileCopyrightText: 2006 Pino Toscano <pino@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef BOOKMARKLIST_H
#define BOOKMARKLIST_H

#include <qwidget.h>
#include <QPointer>

#include "core/observer.h"

class QAction;
class QCheckBox;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class KTreeViewSearchLine;
class QUrl;
class BookmarkItem;
class FileItem;
class PageView;

namespace Okular
{
class Document;
}

class BookmarkList : public QWidget, public Okular::DocumentObserver
{
    Q_OBJECT

public:
    explicit BookmarkList(Okular::Document *document, QWidget *parent = nullptr);
    ~BookmarkList() override;

    // inherited from DocumentObserver
    void notifySetup(const QList<Okular::Page *> &pages, int setupFlags) override;

    void setAddBookmarkAction(QAction *addBookmarkAction);
    void setPageView(PageView *pageView);

private Q_SLOTS:
    void slotShowAllBookmarks(bool);
    void slotExecuted(QTreeWidgetItem *item);
    void slotChanged(QTreeWidgetItem *item);
    void slotContextMenu(const QPoint p);
    void slotBookmarksChanged(const QUrl &url);
    void saveSearchOptions();

private:
    void rebuildTree(bool showAll);
    void goTo(BookmarkItem *item);
    void selectiveUrlUpdate(const QUrl &url, QTreeWidgetItem *&item);
    QTreeWidgetItem *itemForUrl(const QUrl &url) const;
    void contextMenuForBookmarkItem(const QPoint p, BookmarkItem *bmItem);
    void contextMenuForFileItem(const QPoint p, FileItem *fItem);

    Okular::Document *m_document;
    QPointer<PageView> m_pageView;
    QTreeWidget *m_tree;
    KTreeViewSearchLine *m_searchLine;
    QCheckBox *m_showForAllDocumentsCheckbox;
    QTreeWidgetItem *m_currentDocumentItem;
    QToolButton *m_showAllToolButton;
};

#endif
