/*
    SPDX-FileCopyrightText: 2026 Mengshee contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef MENGSHEE_DOCUMENTWORKSPACE_H
#define MENGSHEE_DOCUMENTWORKSPACE_H

#include "okularpart_export.h"

#include <QList>
#include <QPointer>
#include <QWidget>

class QLabel;
class QSplitter;
class PageView;

namespace Okular
{
class DocumentViewport;
class WorkspacePane;
class WorkspaceTabBar;

/**
 * Hosts the views of one document in a main/auxiliary arrangement.
 *
 * DocumentWorkspace deliberately does not create PageViews.  The owner is
 * responsible for constructing each view with the same Okular::Document and
 * for registering the view with that document before adding it here.  This
 * keeps document/session policy outside of this purely presentational class.
 *
 * Closing an auxiliary tab destroys its PageView.  PageView's destructor
 * unregisters it as a DocumentObserver and Okular::View.
 */
class OKULARPART_EXPORT DocumentWorkspace final : public QWidget
{
    Q_OBJECT

public:
    explicit DocumentWorkspace(PageView *mainView, const QString &mainViewTitle = QString(), QWidget *parent = nullptr);
    ~DocumentWorkspace() override;

    PageView *mainView() const;
    PageView *activeView() const;
    QList<PageView *> auxiliaryViews() const;
    int auxiliaryViewCount() const;

    QString mainViewTitle() const;
    void setMainViewTitle(const QString &title);
    QString viewTitle(PageView *view) const;
    void setViewTitle(PageView *view, const QString &title);

    /**
     * Adds an already initialized PageView as an auxiliary tab.
     *
     * Returns the tab index, or -1 when the view is null, is the main view, or
     * belongs to a different document.  Adding an existing auxiliary view only
     * updates/selects its tab.
     */
    int addAuxiliaryView(PageView *view, const QString &title, PageView *sourceView = nullptr);

    /** Number of independently resizable auxiliary tab panes. */
    int auxiliaryPaneCount() const;

    /**
     * Moves an auxiliary view into a new pane beside @p relativeTo.
     *
     * Horizontal creates a left/right split and Vertical creates a top/bottom
     * split.  @p after selects right/bottom instead of left/top.  This is the
     * non-interactive counterpart of dropping a tab at a pane edge.
     */
    bool splitAuxiliaryView(PageView *view, PageView *relativeTo, Qt::Orientation orientation, bool after = true);

public Q_SLOTS:
    void closeAuxiliaryTab(int index);
    void closeAllAuxiliaryViews();
    void promoteAuxiliaryTab(int index);
    void promoteView(PageView *view);

Q_SIGNALS:
    void activeViewChanged(PageView *view);
    void mainViewChanged(PageView *oldMainView, PageView *newMainView);
    void auxiliaryViewAdded(PageView *view, int index);
    void auxiliaryViewAboutToClose(PageView *view);
    void auxiliaryViewCountChanged(int count);

    /** Relays modified internal-link clicks from every hosted PageView. */
    void auxiliaryFrameRequested(PageView *sourceView, const Okular::DocumentViewport &viewport, const QString &title);

private:
    friend class WorkspacePane;
    friend class WorkspaceTabBar;

    bool eventFilter(QObject *watched, QEvent *event) override;
    void installView(PageView *view);
    void setActiveView(PageView *view);
    void updateMainHeader();
    void updateAuxiliaryUi();
    int auxiliaryIndex(PageView *view) const;
    WorkspacePane *createAuxiliaryPane();
    WorkspacePane *paneForView(PageView *view) const;
    QList<WorkspacePane *> auxiliaryPanes() const;
    void addViewToPane(PageView *view, const QString &title, WorkspacePane *pane, int index = -1);
    void closeAuxiliaryView(PageView *view);
    void promoteAuxiliaryView(PageView *view);
    void moveAuxiliaryViewToPane(PageView *view, WorkspacePane *targetPane);
    void collapseEmptyPane(WorkspacePane *pane);
    void clearDropIndicators();

    QSplitter *m_splitter = nullptr;
    QWidget *m_mainHost = nullptr;
    QLabel *m_mainLabel = nullptr;
    QWidget *m_auxiliaryHost = nullptr;
    WorkspacePane *m_primaryAuxiliaryPane = nullptr;
    QPointer<PageView> m_mainView;
    QPointer<PageView> m_activeView;
    QPointer<PageView> m_draggedView;
    QString m_mainTitle;
};

}

#endif
