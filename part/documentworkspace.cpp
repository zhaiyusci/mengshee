/*
    SPDX-FileCopyrightText: 2026 Mengshee contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "documentworkspace.h"
#include "pageview.h"

#include <KLocalizedString>

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QRubberBand>
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
constexpr auto workspaceTabMime = "application/x-mengshee-document-workspace-tab";

QString normalizedTitle(const QString &title)
{
    const QString simplified = title.simplified();
    return simplified.isEmpty() ? i18nc("@title:tab", "Untitled View") : simplified;
}

enum class DropZone { Center, Left, Right, Top, Bottom };

void configureSplitter(QSplitter *splitter)
{
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(4);
}
}

namespace Okular
{
class WorkspacePane;

class WorkspaceTabBar final : public QTabBar
{
public:
    explicit WorkspaceTabBar(WorkspacePane *pane);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    WorkspacePane *m_pane;
    QPoint m_pressPosition;
    int m_pressedIndex = -1;
};

class WorkspacePane final : public QTabWidget
{
public:
    explicit WorkspacePane(DocumentWorkspace *workspace)
        : m_workspace(workspace)
    {
        setObjectName(QStringLiteral("documentWorkspaceAuxiliaryPane"));
        setDocumentMode(true);
        setMovable(true);
        setTabsClosable(true);
        setElideMode(Qt::ElideRight);
        setAcceptDrops(true);
        auto *bar = new WorkspaceTabBar(this);
        bar->setContextMenuPolicy(Qt::CustomContextMenu);
        setTabBar(bar);

        m_promoteButton = new QToolButton(this);
        m_promoteButton->setObjectName(QStringLiteral("documentWorkspacePromoteButton"));
        m_promoteButton->setAutoRaise(true);
        m_promoteButton->setIcon(QIcon::fromTheme(QStringLiteral("go-home")));
        m_promoteButton->setToolTip(i18nc("@info:tooltip", "Make the current auxiliary tab the main view"));
        m_promoteButton->setAccessibleName(i18nc("@action:button", "Make Main View"));
        setCornerWidget(m_promoteButton, Qt::TopRightCorner);
        m_dropIndicator = new QRubberBand(QRubberBand::Rectangle, this);

        connect(this, &QTabWidget::tabCloseRequested, this, [this](int index) {
            if (PageView *view = viewAt(index)) {
                m_workspace->closeAuxiliaryView(view);
            }
        });
        connect(m_promoteButton, &QToolButton::clicked, this, [this] {
            if (PageView *view = viewAt(currentIndex())) {
                m_workspace->promoteAuxiliaryView(view);
            }
        });
        connect(tabBar(), &QTabBar::tabBarDoubleClicked, this, [this](int index) {
            if (PageView *view = viewAt(index)) {
                m_workspace->promoteAuxiliaryView(view);
            }
        });
        connect(this, &QTabWidget::currentChanged, this, [this](int index) {
            m_promoteButton->setEnabled(index >= 0);
            if (PageView *view = viewAt(index)) {
                m_workspace->setActiveView(view);
            }
        });
        connect(tabBar(), &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
            PageView *view = viewAt(tabBar()->tabAt(position));
            if (!view) {
                return;
            }
            QMenu menu(this);
            QAction *promote = menu.addAction(QIcon::fromTheme(QStringLiteral("go-home")), i18nc("@action:inmenu", "Make Main View"));
            menu.addSeparator();
            QAction *close = menu.addAction(QIcon::fromTheme(QStringLiteral("tab-close")), i18nc("@action:inmenu", "Close Auxiliary View"));
            QAction *selected = menu.exec(tabBar()->mapToGlobal(position));
            if (selected == promote) {
                m_workspace->promoteAuxiliaryView(view);
            } else if (selected == close) {
                m_workspace->closeAuxiliaryView(view);
            }
        });
    }

    DocumentWorkspace *workspace() const { return m_workspace; }
    PageView *viewAt(int index) const { return qobject_cast<PageView *>(widget(index)); }
    QToolButton *promoteButton() const { return m_promoteButton; }
    void clearDropIndicator() { m_dropIndicator->hide(); }

protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->mimeData()->hasFormat(QString::fromLatin1(workspaceTabMime)) && m_workspace->m_draggedView) {
            event->acceptProposedAction();
        } else {
            QTabWidget::dragEnterEvent(event);
        }
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (!event->mimeData()->hasFormat(QString::fromLatin1(workspaceTabMime)) || !m_workspace->m_draggedView) {
            QTabWidget::dragMoveEvent(event);
            return;
        }
        m_dropZone = dropZoneAt(event->position().toPoint());
        m_dropIndicator->setGeometry(indicatorGeometry(m_dropZone));
        m_dropIndicator->show();
        m_dropIndicator->raise();
        event->acceptProposedAction();
    }

    void dragLeaveEvent(QDragLeaveEvent *event) override
    {
        m_dropIndicator->hide();
        QTabWidget::dragLeaveEvent(event);
    }

    void dropEvent(QDropEvent *event) override
    {
        m_dropIndicator->hide();
        PageView *view = m_workspace->m_draggedView;
        PageView *relativeTo = viewAt(currentIndex());
        if (!view || !relativeTo) {
            event->ignore();
            return;
        }
        if (m_dropZone == DropZone::Center) {
            m_workspace->moveAuxiliaryViewToPane(view, this);
        } else {
            const Qt::Orientation orientation = (m_dropZone == DropZone::Left || m_dropZone == DropZone::Right) ? Qt::Horizontal : Qt::Vertical;
            const bool after = m_dropZone == DropZone::Right || m_dropZone == DropZone::Bottom;
            m_workspace->splitAuxiliaryView(view, relativeTo, orientation, after);
        }
        event->acceptProposedAction();
    }

private:
    DropZone dropZoneAt(const QPoint &position) const
    {
        if (tabBar()->geometry().contains(position)) {
            return DropZone::Center;
        }
        const int ew = qMin(96, qMax(32, width() / 4));
        const int eh = qMin(96, qMax(32, height() / 4));
        const qreal distances[] = {qreal(position.x()) / ew, qreal(width() - position.x()) / ew, qreal(position.y()) / eh, qreal(height() - position.y()) / eh};
        int closest = 0;
        for (int i = 1; i < 4; ++i) {
            if (distances[i] < distances[closest]) {
                closest = i;
            }
        }
        if (distances[closest] >= 1.0) {
            return DropZone::Center;
        }
        return static_cast<DropZone>(closest + 1);
    }

    QRect indicatorGeometry(DropZone zone) const
    {
        QRect area = rect().adjusted(3, 3, -3, -3);
        switch (zone) {
        case DropZone::Left: area.setWidth(area.width() / 2); break;
        case DropZone::Right: area.setLeft(area.center().x()); break;
        case DropZone::Top: area.setHeight(area.height() / 2); break;
        case DropZone::Bottom: area.setTop(area.center().y()); break;
        case DropZone::Center: area.adjust(area.width() / 8, area.height() / 8, -area.width() / 8, -area.height() / 8); break;
        }
        return area;
    }

    DocumentWorkspace *m_workspace;
    QToolButton *m_promoteButton;
    QRubberBand *m_dropIndicator;
    DropZone m_dropZone = DropZone::Center;
};

WorkspaceTabBar::WorkspaceTabBar(WorkspacePane *pane)
    : QTabBar(pane)
    , m_pane(pane)
{
}

void WorkspaceTabBar::mousePressEvent(QMouseEvent *event)
{
    m_pressPosition = event->position().toPoint();
    m_pressedIndex = event->button() == Qt::LeftButton ? tabAt(m_pressPosition) : -1;
    QTabBar::mousePressEvent(event);
}

void WorkspaceTabBar::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint position = event->position().toPoint();
    if (m_pressedIndex >= 0 && (event->buttons() & Qt::LeftButton) && (position - m_pressPosition).manhattanLength() >= QApplication::startDragDistance()
        && !rect().adjusted(-12, -12, 12, 12).contains(position)) {
        PageView *view = m_pane->viewAt(m_pressedIndex);
        DocumentWorkspace *workspace = m_pane->workspace();
        if (view && workspace) {
            workspace->m_draggedView = view;
            auto *mime = new QMimeData;
            mime->setData(QString::fromLatin1(workspaceTabMime), QByteArrayLiteral("1"));
            QDrag drag(this);
            drag.setMimeData(mime);
            drag.setPixmap(grab(tabRect(m_pressedIndex)));
            drag.setHotSpot(position - tabRect(m_pressedIndex).topLeft());
            drag.exec(Qt::MoveAction);
            workspace->clearDropIndicators();
            workspace->m_draggedView = nullptr;
            m_pressedIndex = -1;
            return;
        }
    }
    QTabBar::mouseMoveEvent(event);
}

void WorkspaceTabBar::mouseReleaseEvent(QMouseEvent *event)
{
    m_pressedIndex = -1;
    QTabBar::mouseReleaseEvent(event);
}
}

namespace
{
void appendPanes(QWidget *node, QList<WorkspacePane *> *result)
{
    if (auto *pane = dynamic_cast<WorkspacePane *>(node)) {
        result->append(pane);
    } else if (auto *splitter = qobject_cast<QSplitter *>(node)) {
        for (int i = 0; i < splitter->count(); ++i) {
            appendPanes(splitter->widget(i), result);
        }
    }
}

PageView *firstViewInNode(QWidget *node)
{
    if (auto *pane = dynamic_cast<WorkspacePane *>(node)) {
        return pane->viewAt(pane->currentIndex());
    }
    if (auto *splitter = qobject_cast<QSplitter *>(node)) {
        for (int i = 0; i < splitter->count(); ++i) {
            if (PageView *view = firstViewInNode(splitter->widget(i))) {
                return view;
            }
        }
    }
    return nullptr;
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
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("documentWorkspaceSplitter"));
    configureSplitter(m_splitter);
    layout->addWidget(m_splitter);

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

    m_auxiliaryHost = new QWidget(m_splitter);
    m_auxiliaryHost->setObjectName(QStringLiteral("documentWorkspaceAuxiliaryHost"));
    auto *auxLayout = new QVBoxLayout(m_auxiliaryHost);
    auxLayout->setContentsMargins(0, 0, 0, 0);
    auxLayout->setSpacing(0);
    m_primaryAuxiliaryPane = createAuxiliaryPane();
    m_primaryAuxiliaryPane->setObjectName(QStringLiteral("documentWorkspaceAuxiliaryTabs"));
    auxLayout->addWidget(m_primaryAuxiliaryPane);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 2);

    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        if (m_mainView && now && (now == m_mainView || m_mainView->isAncestorOf(now))) {
            setActiveView(m_mainView);
        } else if (now) {
            for (PageView *view : auxiliaryViews()) {
                if (now == view || view->isAncestorOf(now)) {
                    setActiveView(view);
                    break;
                }
            }
        }
    });
    installView(mainView);
    updateMainHeader();
    updateAuxiliaryUi();
}

DocumentWorkspace::~DocumentWorkspace() = default;
PageView *DocumentWorkspace::mainView() const { return m_mainView; }
PageView *DocumentWorkspace::activeView() const { return m_activeView ? m_activeView.data() : m_mainView.data(); }

QList<WorkspacePane *> DocumentWorkspace::auxiliaryPanes() const
{
    QList<WorkspacePane *> result;
    if (QLayoutItem *item = m_auxiliaryHost->layout()->itemAt(0)) {
        appendPanes(item->widget(), &result);
    }
    return result;
}

QList<PageView *> DocumentWorkspace::auxiliaryViews() const
{
    QList<PageView *> result;
    for (WorkspacePane *pane : auxiliaryPanes()) {
        for (int i = 0; i < pane->count(); ++i) {
            if (PageView *view = pane->viewAt(i)) {
                result.append(view);
            }
        }
    }
    return result;
}

int DocumentWorkspace::auxiliaryViewCount() const { return auxiliaryViews().count(); }
int DocumentWorkspace::auxiliaryPaneCount() const
{
    int result = 0;
    for (WorkspacePane *pane : auxiliaryPanes()) {
        result += pane->count() > 0;
    }
    return result;
}
QString DocumentWorkspace::mainViewTitle() const { return m_mainTitle; }
void DocumentWorkspace::setMainViewTitle(const QString &title) { m_mainTitle = normalizedTitle(title); updateMainHeader(); }

WorkspacePane *DocumentWorkspace::paneForView(PageView *view) const
{
    for (WorkspacePane *pane : auxiliaryPanes()) {
        if (view && pane->indexOf(view) >= 0) {
            return pane;
        }
    }
    return nullptr;
}

QString DocumentWorkspace::viewTitle(PageView *view) const
{
    if (view == m_mainView) {
        return m_mainTitle;
    }
    WorkspacePane *pane = paneForView(view);
    return pane ? pane->tabText(pane->indexOf(view)) : QString();
}

void DocumentWorkspace::setViewTitle(PageView *view, const QString &title)
{
    if (view == m_mainView) {
        setMainViewTitle(title);
    } else if (WorkspacePane *pane = paneForView(view)) {
        const int index = pane->indexOf(view);
        const QString display = normalizedTitle(title);
        pane->setTabText(index, display);
        pane->setTabToolTip(index, display);
    }
}

WorkspacePane *DocumentWorkspace::createAuxiliaryPane() { return new WorkspacePane(this); }

void DocumentWorkspace::addViewToPane(PageView *view, const QString &title, WorkspacePane *pane, int index)
{
    const QString display = normalizedTitle(title);
    const int inserted = index < 0 ? pane->addTab(view, display) : pane->insertTab(index, view, display);
    pane->setTabToolTip(inserted, display);
    pane->setCurrentIndex(inserted);
    view->show();
}

int DocumentWorkspace::addAuxiliaryView(PageView *view, const QString &title, PageView *sourceView)
{
    if (!view || view == m_mainView) {
        return -1;
    }
    if (m_mainView && view->document() != m_mainView->document()) {
        qWarning("DocumentWorkspace rejected a PageView for a different document");
        return -1;
    }
    if (WorkspacePane *existing = paneForView(view)) {
        setViewTitle(view, title);
        existing->setCurrentWidget(view);
        return auxiliaryIndex(view);
    }
    const bool wasEmpty = auxiliaryViewCount() == 0;
    WorkspacePane *target = paneForView(sourceView);
    addViewToPane(view, title, target ? target : m_primaryAuxiliaryPane);
    installView(view);
    updateAuxiliaryUi();
    if (wasEmpty) {
        QTimer::singleShot(0, this, [this] { const int w = qMax(width(), 2); m_splitter->setSizes({w * 3 / 5, w * 2 / 5}); });
    }
    const int index = auxiliaryIndex(view);
    Q_EMIT auxiliaryViewAdded(view, index);
    Q_EMIT auxiliaryViewCountChanged(auxiliaryViewCount());
    return index;
}

void DocumentWorkspace::closeAuxiliaryTab(int index)
{
    const QList<PageView *> views = auxiliaryViews();
    if (index >= 0 && index < views.count()) {
        closeAuxiliaryView(views.at(index));
    }
}

void DocumentWorkspace::closeAuxiliaryView(PageView *view)
{
    WorkspacePane *pane = paneForView(view);
    if (!pane) {
        return;
    }
    const QPointer<PageView> activeBefore = m_activeView;
    const bool wasActive = activeBefore == view;
    PageView *nearbyView = nullptr;
    if (wasActive && pane->count() == 1) {
        if (auto *splitter = qobject_cast<QSplitter *>(pane->parentWidget())) {
            const int paneIndex = splitter->indexOf(pane);
            if (paneIndex > 0) {
                nearbyView = firstViewInNode(splitter->widget(paneIndex - 1));
            }
            if (!nearbyView && paneIndex + 1 < splitter->count()) {
                nearbyView = firstViewInNode(splitter->widget(paneIndex + 1));
            }
        }
    }
    Q_EMIT auxiliaryViewAboutToClose(view);
    { const QSignalBlocker blocker(pane); pane->removeTab(pane->indexOf(view)); }
    if (wasActive) {
        PageView *fallback = pane->viewAt(pane->currentIndex());
        const QList<PageView *> remaining = auxiliaryViews();
        fallback = fallback ? fallback : (nearbyView ? nearbyView : (remaining.isEmpty() ? m_mainView.data() : remaining.constFirst()));
        setActiveView(fallback);
        fallback->setFocus(Qt::OtherFocusReason);
        const QPointer<PageView> durableFallback = fallback;
        QTimer::singleShot(0, this, [this, durableFallback] {
            // Deleting the old focused tab can produce one final focusChanged
            // notification after removeTab(). Restore the intended route once
            // that deferred QWidget cleanup has completed.
            if (durableFallback) {
                setActiveView(durableFallback);
                durableFallback->setFocus(Qt::OtherFocusReason);
            }
        });
    } else if (activeBefore) {
        setActiveView(activeBefore);
    }
    view->deleteLater();
    collapseEmptyPane(pane);
    updateAuxiliaryUi();
    Q_EMIT auxiliaryViewCountChanged(auxiliaryViewCount());
}

void DocumentWorkspace::closeAllAuxiliaryViews()
{
    while (auxiliaryViewCount()) {
        closeAuxiliaryView(auxiliaryViews().constLast());
    }
}

void DocumentWorkspace::promoteAuxiliaryTab(int index)
{
    const QList<PageView *> views = auxiliaryViews();
    if (index >= 0 && index < views.count()) {
        promoteAuxiliaryView(views.at(index));
    }
}

void DocumentWorkspace::promoteAuxiliaryView(PageView *view)
{
    WorkspacePane *pane = paneForView(view);
    if (!pane || !m_mainView) {
        return;
    }
    PageView *oldMain = m_mainView;
    const int index = pane->indexOf(view);
    const QString promotedTitle = pane->tabText(index);
    const QString oldTitle = m_mainTitle;
    { const QSignalBlocker blocker(pane); pane->removeTab(index); addViewToPane(oldMain, oldTitle, pane, index); }
    auto *mainLayout = qobject_cast<QVBoxLayout *>(m_mainHost->layout());
    mainLayout->removeWidget(oldMain);
    mainLayout->addWidget(view, 1);
    view->show();
    m_mainView = view;
    m_mainTitle = promotedTitle;
    updateMainHeader();
    setActiveView(view);
    view->setFocus(Qt::OtherFocusReason);
    Q_EMIT mainViewChanged(oldMain, view);
}

void DocumentWorkspace::promoteView(PageView *view) { promoteAuxiliaryView(view); }

bool DocumentWorkspace::splitAuxiliaryView(PageView *view, PageView *relativeTo, Qt::Orientation orientation, bool after)
{
    WorkspacePane *source = paneForView(view);
    WorkspacePane *target = paneForView(relativeTo);
    if (!source || !target || (source == target && source->count() < 2)) {
        return false;
    }
    const QString title = viewTitle(view);
    auto *newPane = createAuxiliaryPane();
    QWidget *parent = target->parentWidget();
    if (auto *splitter = qobject_cast<QSplitter *>(parent); splitter && splitter->orientation() == orientation) {
        splitter->insertWidget(splitter->indexOf(target) + (after ? 1 : 0), newPane);
    } else {
        auto *nested = new QSplitter(orientation);
        configureSplitter(nested);
        if (auto *splitter = qobject_cast<QSplitter *>(parent)) {
            splitter->replaceWidget(splitter->indexOf(target), nested);
        } else {
            Q_ASSERT(parent == m_auxiliaryHost);
            m_auxiliaryHost->layout()->removeWidget(target);
            target->setParent(nullptr);
            m_auxiliaryHost->layout()->addWidget(nested);
        }
        if (after) { nested->addWidget(target); nested->addWidget(newPane); }
        else { nested->addWidget(newPane); nested->addWidget(target); }
        nested->setSizes({1, 1});
    }
    { const QSignalBlocker blocker(source); source->removeTab(source->indexOf(view)); }
    addViewToPane(view, title, newPane);
    collapseEmptyPane(source);
    setActiveView(view);
    view->setFocus(Qt::OtherFocusReason);
    updateAuxiliaryUi();
    QTimer::singleShot(0, newPane, [newPane] {
        if (auto *splitter = qobject_cast<QSplitter *>(newPane->parentWidget())) {
            splitter->setSizes(QList<int>(splitter->count(), 1));
        }
    });
    return true;
}

void DocumentWorkspace::moveAuxiliaryViewToPane(PageView *view, WorkspacePane *target)
{
    WorkspacePane *source = paneForView(view);
    if (!source || !target || source == target) {
        return;
    }
    const QString title = viewTitle(view);
    { const QSignalBlocker blocker(source); source->removeTab(source->indexOf(view)); }
    addViewToPane(view, title, target);
    collapseEmptyPane(source);
    setActiveView(view);
    view->setFocus(Qt::OtherFocusReason);
    updateAuxiliaryUi();
}

void DocumentWorkspace::collapseEmptyPane(WorkspacePane *pane)
{
    if (!pane || pane->count()) {
        return;
    }
    if (auxiliaryPanes().count() <= 1) {
        m_primaryAuxiliaryPane = pane;
        return;
    }
    auto *parent = qobject_cast<QSplitter *>(pane->parentWidget());
    Q_ASSERT(parent);
    pane->setParent(nullptr);
    pane->deleteLater();
    if (parent->count() == 1) {
        QWidget *remaining = parent->widget(0);
        QWidget *grandParent = parent->parentWidget();
        if (grandParent == m_auxiliaryHost) {
            remaining->setParent(nullptr);
            m_auxiliaryHost->layout()->removeWidget(parent);
            m_auxiliaryHost->layout()->addWidget(remaining);
        } else if (auto *grandSplitter = qobject_cast<QSplitter *>(grandParent)) {
            grandSplitter->replaceWidget(grandSplitter->indexOf(parent), remaining);
        }
        parent->setParent(nullptr);
        parent->deleteLater();
    }
    const QList<WorkspacePane *> panes = auxiliaryPanes();
    m_primaryAuxiliaryPane = panes.isEmpty() ? nullptr : panes.constFirst();
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
    if (view && m_activeView != view) {
        m_activeView = view;
        Q_EMIT activeViewChanged(view);
    }
}

void DocumentWorkspace::updateMainHeader()
{
    m_mainLabel->setText(i18nc("@title:frame", "Main — %1", m_mainTitle));
    m_mainLabel->setToolTip(m_mainTitle);
}

void DocumentWorkspace::updateAuxiliaryUi()
{
    const bool visible = auxiliaryViewCount() > 0;
    m_mainLabel->setVisible(visible);
    m_auxiliaryHost->setVisible(visible);
    for (WorkspacePane *pane : auxiliaryPanes()) {
        pane->promoteButton()->setEnabled(pane->currentIndex() >= 0);
    }
}

int DocumentWorkspace::auxiliaryIndex(PageView *view) const { return auxiliaryViews().indexOf(view); }
void DocumentWorkspace::clearDropIndicators() { for (WorkspacePane *pane : auxiliaryPanes()) pane->clearDropIndicator(); }
