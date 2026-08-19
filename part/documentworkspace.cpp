/*
    SPDX-FileCopyrightText: 2026 Mengshee contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "documentworkspace.h"

#include "pageview.h"

#include <KLocalizedString>

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Okular;

namespace
{
QString normalizedTitle(const QString &title)
{
    const QString simplified = title.simplified();
    return simplified.isEmpty() ? i18nc("@title:tab", "Untitled View") : simplified;
}
}

DocumentWorkspace::DocumentWorkspace(PageView *mainView, const QString &mainViewTitle, QWidget *parent)
    : QWidget(parent)
    , m_mainView(mainView)
    , m_activeView(mainView)
    , m_mainTitle(normalizedTitle(mainViewTitle))
{
    Q_ASSERT(mainView);

    setObjectName(QStringLiteral("documentWorkspace"));

    auto *workspaceLayout = new QHBoxLayout(this);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("documentWorkspaceSplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(4);
    workspaceLayout->addWidget(m_splitter);

    m_mainHost = new QWidget(m_splitter);
    m_mainHost->setObjectName(QStringLiteral("documentWorkspaceMainHost"));
    auto *mainLayout = new QVBoxLayout(m_mainHost);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_mainLabel = new QLabel(m_mainHost);
    m_mainLabel->setObjectName(QStringLiteral("documentWorkspaceMainLabel"));
    m_mainLabel->setMargin(4);
    m_mainLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mainLayout->addWidget(m_mainLabel);
    mainLayout->addWidget(mainView, 1);

    m_auxiliaryTabs = new QTabWidget(m_splitter);
    m_auxiliaryTabs->setObjectName(QStringLiteral("documentWorkspaceAuxiliaryTabs"));
    m_auxiliaryTabs->setDocumentMode(true);
    m_auxiliaryTabs->setMovable(true);
    m_auxiliaryTabs->setTabsClosable(true);
    m_auxiliaryTabs->setElideMode(Qt::ElideRight);
    m_auxiliaryTabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);

    m_promoteButton = new QToolButton(m_auxiliaryTabs);
    m_promoteButton->setObjectName(QStringLiteral("documentWorkspacePromoteButton"));
    m_promoteButton->setAutoRaise(true);
    m_promoteButton->setIcon(QIcon::fromTheme(QStringLiteral("go-home")));
    m_promoteButton->setToolTip(i18nc("@info:tooltip", "Make the current auxiliary tab the main view"));
    m_promoteButton->setAccessibleName(i18nc("@action:button", "Make Main View"));
    m_auxiliaryTabs->setCornerWidget(m_promoteButton, Qt::TopRightCorner);

    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 2);

    connect(m_auxiliaryTabs, &QTabWidget::tabCloseRequested, this, &DocumentWorkspace::closeAuxiliaryTab);
    connect(m_promoteButton, &QToolButton::clicked, this, [this]() { promoteAuxiliaryTab(m_auxiliaryTabs->currentIndex()); });
    connect(m_auxiliaryTabs->tabBar(), &QTabBar::tabBarDoubleClicked, this, &DocumentWorkspace::promoteAuxiliaryTab);
    connect(m_auxiliaryTabs, &QTabWidget::currentChanged, this, [this](int index) {
        m_promoteButton->setEnabled(index >= 0);
        if (auto *view = qobject_cast<PageView *>(m_auxiliaryTabs->widget(index))) {
            setActiveView(view);
        }
    });
    connect(m_auxiliaryTabs->tabBar(), &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        const int index = m_auxiliaryTabs->tabBar()->tabAt(position);
        if (index < 0) {
            return;
        }

        QMenu menu(this);
        QAction *promoteAction = menu.addAction(QIcon::fromTheme(QStringLiteral("go-home")), i18nc("@action:inmenu", "Make Main View"));
        menu.addSeparator();
        QAction *closeAction = menu.addAction(QIcon::fromTheme(QStringLiteral("tab-close")), i18nc("@action:inmenu", "Close Auxiliary View"));
        QAction *selected = menu.exec(m_auxiliaryTabs->tabBar()->mapToGlobal(position));
        if (selected == promoteAction) {
            promoteAuxiliaryTab(index);
        } else if (selected == closeAction) {
            closeAuxiliaryTab(index);
        }
    });
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        if (!now) {
            return;
        }
        if (m_mainView && (now == m_mainView || m_mainView->isAncestorOf(now))) {
            setActiveView(m_mainView);
            return;
        }
        for (PageView *view : auxiliaryViews()) {
            if (now == view || view->isAncestorOf(now)) {
                setActiveView(view);
                return;
            }
        }
    });

    installView(mainView);
    updateMainHeader();
    updateAuxiliaryUi();
}

DocumentWorkspace::~DocumentWorkspace() = default;

PageView *DocumentWorkspace::mainView() const
{
    return m_mainView;
}

PageView *DocumentWorkspace::activeView() const
{
    return m_activeView ? m_activeView.data() : m_mainView.data();
}

QList<PageView *> DocumentWorkspace::auxiliaryViews() const
{
    QList<PageView *> result;
    result.reserve(m_auxiliaryTabs->count());
    for (int index = 0; index < m_auxiliaryTabs->count(); ++index) {
        if (auto *view = qobject_cast<PageView *>(m_auxiliaryTabs->widget(index))) {
            result.append(view);
        }
    }
    return result;
}

int DocumentWorkspace::auxiliaryViewCount() const
{
    return m_auxiliaryTabs->count();
}

QString DocumentWorkspace::mainViewTitle() const
{
    return m_mainTitle;
}

void DocumentWorkspace::setMainViewTitle(const QString &title)
{
    m_mainTitle = normalizedTitle(title);
    updateMainHeader();
}

QString DocumentWorkspace::viewTitle(PageView *view) const
{
    if (view == m_mainView) {
        return m_mainTitle;
    }

    const int index = auxiliaryIndex(view);
    return index >= 0 ? m_auxiliaryTabs->tabText(index) : QString();
}

void DocumentWorkspace::setViewTitle(PageView *view, const QString &title)
{
    if (view == m_mainView) {
        setMainViewTitle(title);
        return;
    }

    const int index = auxiliaryIndex(view);
    if (index >= 0) {
        const QString displayTitle = normalizedTitle(title);
        m_auxiliaryTabs->setTabText(index, displayTitle);
        m_auxiliaryTabs->setTabToolTip(index, displayTitle);
    }
}

int DocumentWorkspace::addAuxiliaryView(PageView *view, const QString &title)
{
    if (!view || view == m_mainView) {
        return -1;
    }
    if (m_mainView && view->document() != m_mainView->document()) {
        qWarning("DocumentWorkspace rejected a PageView for a different document");
        return -1;
    }

    const int existingIndex = auxiliaryIndex(view);
    if (existingIndex >= 0) {
        setViewTitle(view, title);
        m_auxiliaryTabs->setCurrentIndex(existingIndex);
        return existingIndex;
    }

    const bool wasEmpty = m_auxiliaryTabs->count() == 0;
    const QString displayTitle = normalizedTitle(title);
    const int index = m_auxiliaryTabs->addTab(view, displayTitle);
    m_auxiliaryTabs->setTabToolTip(index, displayTitle);
    m_auxiliaryTabs->setCurrentIndex(index);
    installView(view);
    updateAuxiliaryUi();

    if (wasEmpty) {
        QTimer::singleShot(0, this, [this]() {
            const int availableWidth = qMax(width(), 2);
            m_splitter->setSizes({availableWidth * 3 / 5, availableWidth * 2 / 5});
        });
    }

    Q_EMIT auxiliaryViewAdded(view, index);
    Q_EMIT auxiliaryViewCountChanged(m_auxiliaryTabs->count());
    return index;
}

void DocumentWorkspace::closeAuxiliaryTab(int index)
{
    auto *view = qobject_cast<PageView *>(m_auxiliaryTabs->widget(index));
    if (!view) {
        return;
    }
    const QPointer<PageView> activeBeforeClose = m_activeView;
    const bool closingWasActive = activeBeforeClose == view;
    Q_EMIT auxiliaryViewAboutToClose(view);
    {
        // Removing an unrelated tab can emit currentChanged solely because
        // the current index was renumbered. That must not steal action routing
        // from the main frame (or another focused auxiliary frame).
        const QSignalBlocker blocker(m_auxiliaryTabs);
        m_auxiliaryTabs->removeTab(index);
    }
    if (closingWasActive) {
        auto *currentAuxiliaryView = qobject_cast<PageView *>(m_auxiliaryTabs->currentWidget());
        setActiveView(currentAuxiliaryView ? currentAuxiliaryView : m_mainView.data());
    } else if (activeBeforeClose) {
        setActiveView(activeBeforeClose);
    }
    view->deleteLater();

    updateAuxiliaryUi();
    Q_EMIT auxiliaryViewCountChanged(m_auxiliaryTabs->count());
}

void DocumentWorkspace::closeAllAuxiliaryViews()
{
    while (m_auxiliaryTabs->count() > 0) {
        closeAuxiliaryTab(m_auxiliaryTabs->count() - 1);
    }
}

void DocumentWorkspace::promoteAuxiliaryTab(int index)
{
    auto *promotedView = qobject_cast<PageView *>(m_auxiliaryTabs->widget(index));
    if (!promotedView || !m_mainView) {
        return;
    }

    PageView *oldMainView = m_mainView;
    const QString promotedTitle = m_auxiliaryTabs->tabText(index);
    const QString oldMainTitle = m_mainTitle;

    m_auxiliaryTabs->removeTab(index);
    auto *mainLayout = qobject_cast<QVBoxLayout *>(m_mainHost->layout());
    Q_ASSERT(mainLayout);
    mainLayout->removeWidget(oldMainView);
    mainLayout->addWidget(promotedView, 1);
    // QTabWidget explicitly hides a page when removeTab() detaches it. Merely
    // reparenting that page into the main layout does not clear the explicit
    // hidden state, leaving the promoted frame blank until something else
    // happens to show it.
    promotedView->show();

    const int oldMainIndex = m_auxiliaryTabs->insertTab(index, oldMainView, oldMainTitle);
    m_auxiliaryTabs->setTabToolTip(oldMainIndex, oldMainTitle);
    m_auxiliaryTabs->setCurrentIndex(oldMainIndex);

    m_mainView = promotedView;
    m_mainTitle = promotedTitle;
    updateMainHeader();
    setActiveView(promotedView);
    promotedView->setFocus(Qt::OtherFocusReason);

    Q_EMIT mainViewChanged(oldMainView, promotedView);
}

void DocumentWorkspace::promoteView(PageView *view)
{
    promoteAuxiliaryTab(auxiliaryIndex(view));
}

bool DocumentWorkspace::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FocusIn) {
        if (auto *view = qobject_cast<PageView *>(watched)) {
            setActiveView(view);
        }
    }
    return QWidget::eventFilter(watched, event);
}

void DocumentWorkspace::installView(PageView *view)
{
    view->installEventFilter(this);
    connect(view, &PageView::openInternalLinkInAuxiliaryFrame, this, [this, view](const DocumentViewport &viewport, const QString &title) { Q_EMIT auxiliaryFrameRequested(view, viewport, title); });
}

void DocumentWorkspace::setActiveView(PageView *view)
{
    if (!view || m_activeView == view) {
        return;
    }
    m_activeView = view;
    Q_EMIT activeViewChanged(view);
}

void DocumentWorkspace::updateMainHeader()
{
    m_mainLabel->setText(i18nc("@title:frame", "Main — %1", m_mainTitle));
    m_mainLabel->setToolTip(m_mainTitle);
}

void DocumentWorkspace::updateAuxiliaryUi()
{
    const bool hasAuxiliaryViews = m_auxiliaryTabs->count() > 0;
    m_mainLabel->setVisible(hasAuxiliaryViews);
    m_auxiliaryTabs->setVisible(hasAuxiliaryViews);
    m_promoteButton->setEnabled(hasAuxiliaryViews && m_auxiliaryTabs->currentIndex() >= 0);
}

int DocumentWorkspace::auxiliaryIndex(PageView *view) const
{
    return view ? m_auxiliaryTabs->indexOf(view) : -1;
}
