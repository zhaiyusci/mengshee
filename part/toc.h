/*
    SPDX-FileCopyrightText: 2004-2006 Albert Astals Cid <aacid@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef _OKULAR_TOC_H_
#define _OKULAR_TOC_H_

#include "core/document.h"
#include "core/observer.h"
#include <QMetaObject>
#include <QModelIndex>
#include <QPointer>
#include <qwidget.h>

#include "okularpart_export.h"

class QModelIndex;
class QTreeView;
class QDomDocument;
class QDomElement;
class KTreeViewSearchLine;
class PageView;
class TOCModel;

namespace Okular
{
class Document;
class PartTest;
}

class OKULARPART_EXPORT TOC : public QWidget, public Okular::DocumentObserver
{
    Q_OBJECT
    friend class Okular::PartTest;

public:
    TOC(QWidget *parent, Okular::Document *document);
    ~TOC() override;

    void setPageView(PageView *pageView);

    // inherited from DocumentObserver
    void notifySetup(const QList<Okular::Page *> &pages, int setupFlags) override;
    void notifyCurrentPageChanged(int previous, int current) override;

    void reparseConfig();

    void prepareForReload();
    void rollbackReload();
    void finishReload();

public Q_SLOTS:
    void addCurrentPageEntry();
    void renameCurrentEntry();
    void deleteCurrentEntry();
    void expandRecursively();
    void collapseRecursively();
    void expandAll();
    void collapseAll();

Q_SIGNALS:
    void hasTOC(bool has);
    void rightClick(const Okular::DocumentViewport &, const QPoint, const QString &);
    void contentsModified();

private Q_SLOTS:
    void slotExecuted(const QModelIndex &);
    void saveSearchOptions();

protected:
    void contextMenuEvent(QContextMenuEvent *e) override;

private:
    QList<QModelIndex> expandedNodes(const QModelIndex &parent = QModelIndex()) const;
    bool applySynopsis(const Okular::DocumentSynopsis &synopsis);
    Okular::DocumentSynopsis synopsisFromModel() const;
    QDomElement synopsisElementForIndex(QDomDocument &document, const QModelIndex &index) const;
    QDomElement elementForIndexPath(QDomDocument &document, const QModelIndex &index) const;
    Okular::DocumentViewport documentViewport() const;
    void goToDocumentViewport(const Okular::DocumentViewport &viewport);
    void refreshCurrentViewport();

    Okular::Document *m_document;
    QPointer<PageView> m_pageView;
    QMetaObject::Connection m_pageViewViewportConnection;
    QTreeView *m_treeView;
    KTreeViewSearchLine *m_searchLine;
    TOCModel *m_model;
};

#endif
