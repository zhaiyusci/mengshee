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
class QTabWidget;
class QToolButton;
class PageView;

namespace Okular
{
class DocumentViewport;

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
    int addAuxiliaryView(PageView *view, const QString &title);

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
    bool eventFilter(QObject *watched, QEvent *event) override;
    void installView(PageView *view);
    void setActiveView(PageView *view);
    void updateMainHeader();
    void updateAuxiliaryUi();
    int auxiliaryIndex(PageView *view) const;

    QSplitter *m_splitter = nullptr;
    QWidget *m_mainHost = nullptr;
    QLabel *m_mainLabel = nullptr;
    QTabWidget *m_auxiliaryTabs = nullptr;
    QToolButton *m_promoteButton = nullptr;
    QPointer<PageView> m_mainView;
    QPointer<PageView> m_activeView;
    QString m_mainTitle;
};

}

#endif
