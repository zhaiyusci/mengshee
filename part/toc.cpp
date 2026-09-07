/*
    SPDX-FileCopyrightText: 2004-2006 Albert Astals Cid <aacid@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "toc.h"

// qt/kde includes
#include <algorithm>
#include <QContextMenuEvent>
#include <QHeaderView>
#include <QInputDialog>
#include <QLayout>
#include <QSet>
#include <QTreeView>
#include <qdom.h>

#include <KLineEdit>
#include <KLocalizedString>
#include <KMessageBox>
#include <KStandardGuiItem>
#include <KTitleWidget>

#include <kwidgetsaddons_version.h>

// local includes
#include "core/action.h"
#include "gui/tocmodel.h"
#include "ktreeviewsearchline.h"
#include "pageitemdelegate.h"
#include "pageview.h"
#include "settings.h"

TOC::TOC(QWidget *parent, Okular::Document *document)
    : QWidget(parent)
    , m_document(document)
{
    QVBoxLayout *mainlay = new QVBoxLayout(this);
    mainlay->setSpacing(6);

    KTitleWidget *titleWidget = new KTitleWidget(this);
    titleWidget->setLevel(4);
    titleWidget->setText(i18n("Contents"));
    mainlay->addWidget(titleWidget);
    mainlay->setAlignment(titleWidget, Qt::AlignHCenter);
    m_searchLine = new KTreeViewSearchLine(this);
    mainlay->addWidget(m_searchLine);
    m_searchLine->setPlaceholderText(i18n("Search…"));
    m_searchLine->setCaseSensitivity(Okular::Settings::self()->contentsSearchCaseSensitive() ? Qt::CaseSensitive : Qt::CaseInsensitive);
    m_searchLine->setRegularExpression(Okular::Settings::self()->contentsSearchRegularExpression());
    connect(m_searchLine, &KTreeViewSearchLine::searchOptionsChanged, this, &TOC::saveSearchOptions);

    m_treeView = new QTreeView(this);
    mainlay->addWidget(m_treeView);
    m_model = new TOCModel(document, m_treeView);
    m_treeView->setModel(m_model);
    m_treeView->setSortingEnabled(false);
    m_treeView->setRootIsDecorated(true);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setItemDelegate(new PageItemDelegate(m_treeView));
    m_treeView->header()->hide();
    m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    connect(m_treeView, &QTreeView::clicked, this, &TOC::slotExecuted);
    connect(m_treeView, &QTreeView::activated, this, &TOC::slotExecuted);
    m_searchLine->setTreeView(m_treeView);
}

TOC::~TOC()
{
    m_document->removeObserver(this);
}

void TOC::setPageView(PageView *pageView)
{
    if (m_pageView == pageView) {
        refreshCurrentViewport();
        return;
    }

    disconnect(m_pageViewViewportConnection);
    m_pageView = pageView;

    if (pageView) {
        m_pageViewViewportConnection = connect(pageView, &PageView::viewportStateChanged, this, &TOC::refreshCurrentViewport);
    } else {
        m_pageViewViewportConnection = {};
    }

    refreshCurrentViewport();
}

void TOC::setEditingEnabled(bool enabled)
{
    m_editingEnabled = enabled;
}

Okular::DocumentViewport TOC::documentViewport() const
{
    return m_pageView ? m_pageView->documentViewport() : m_document->viewport();
}

void TOC::goToDocumentViewport(const Okular::DocumentViewport &viewport)
{
    if (m_pageView) {
        m_pageView->goToDocumentViewport(viewport, true, true);
    } else {
        m_document->setViewport(viewport);
    }
}

void TOC::refreshCurrentViewport()
{
    m_model->setCurrentViewport(documentViewport());
}

void TOC::notifySetup(const QList<Okular::Page *> & /*pages*/, int setupFlags)
{
    if (!(setupFlags & Okular::DocumentObserver::DocumentChanged)) {
        return;
    }

    // clear contents
    m_model->clear();

    // request synopsis description (is a dom tree)
    const Okular::DocumentSynopsis *syn = m_document->documentSynopsis();
    if (!syn) {
        if (m_document->isOpened()) {
            // Make sure we clear the reload old model data
            m_model->setOldModelData(nullptr, QList<QModelIndex>());
        }
        Q_EMIT hasTOC(false);
        return;
    }

    m_model->fill(syn);
    Q_EMIT hasTOC(!m_model->isEmpty());
}

void TOC::notifyCurrentPageChanged(int, int)
{
    refreshCurrentViewport();
}

void TOC::prepareForReload()
{
    if (m_model->isEmpty()) {
        return;
    }

    const QList<QModelIndex> list = expandedNodes();
    TOCModel *m = m_model;
    m_model = new TOCModel(m_document, m_treeView);
    m_model->setOldModelData(m, list);
    m->setParent(nullptr);
}

void TOC::rollbackReload()
{
    if (!m_model->hasOldModelData()) {
        return;
    }

    TOCModel *m = m_model;
    m_model = m->clearOldModelData();
    m_model->setParent(m_treeView);
    delete m;
}

void TOC::finishReload()
{
    m_treeView->setModel(m_model);
    m_model->setParent(m_treeView);
}

QList<QModelIndex> TOC::expandedNodes(const QModelIndex &parent) const
{
    QList<QModelIndex> list;
    for (int i = 0; i < m_model->rowCount(parent); i++) {
        const QModelIndex index = m_model->index(i, 0, parent);
        if (m_treeView->isExpanded(index)) {
            list << index;
        }
        if (m_model->hasChildren(index)) {
            list << expandedNodes(index);
        }
    }
    return list;
}

void TOC::reparseConfig()
{
    m_searchLine->setCaseSensitivity(Okular::Settings::contentsSearchCaseSensitive() ? Qt::CaseSensitive : Qt::CaseInsensitive);
    m_searchLine->setRegularExpression(Okular::Settings::contentsSearchRegularExpression());
    m_treeView->update();
}

void TOC::slotExecuted(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }

    QString url = m_model->urlForIndex(index);
    if (!url.isEmpty()) {
        Okular::BrowseAction action(QUrl::fromLocalFile(url));
        m_document->processAction(&action);
        return;
    }

    QString externalFileName = m_model->externalFileNameForIndex(index);
    Okular::DocumentViewport viewport = m_model->viewportForIndex(index);
    if (!externalFileName.isEmpty()) {
        Okular::GotoAction action(externalFileName, viewport);
        m_document->processAction(&action);
    } else if (viewport.isValid()) {
        goToDocumentViewport(viewport);
    }
}

void TOC::saveSearchOptions()
{
    Okular::Settings::setContentsSearchRegularExpression(m_searchLine->regularExpression());
    Okular::Settings::setContentsSearchCaseSensitive(m_searchLine->caseSensitivity() == Qt::CaseSensitive ? true : false);
    Okular::Settings::self()->save();
}

static QList<int> indexPath(const QModelIndex &index)
{
    QList<int> path;
    QModelIndex current = index;
    while (current.isValid()) {
        path.prepend(current.row());
        current = current.parent();
    }
    return path;
}

static QDomElement childElementAt(QDomNode parent, int row)
{
    int currentRow = 0;
    for (QDomNode child = parent.firstChild(); !child.isNull(); child = child.nextSibling()) {
        QDomElement element = child.toElement();
        if (element.isNull()) {
            continue;
        }
        if (currentRow == row) {
            return element;
        }
        ++currentRow;
    }
    return QDomElement();
}

static QString namedDestinationAtViewport(Okular::Document *document, const Okular::DocumentViewport &viewport)
{
    if (!viewport.isValid() || !viewport.rePos.enabled) {
        return QString();
    }

    const QVariantList destinations = document->metaData(QStringLiteral("NamedViewports")).toList();
    for (const QVariant &value : destinations) {
        const QVariantMap destination = value.toMap();
        const Okular::DocumentViewport candidate(destination.value(QStringLiteral("viewport")).toString());
        if (candidate.pageNumber == viewport.pageNumber && candidate.rePos.enabled && qAbs(candidate.rePos.normalizedX - viewport.rePos.normalizedX) < 0.002 && qAbs(candidate.rePos.normalizedY - viewport.rePos.normalizedY) < 0.002) {
            return destination.value(QStringLiteral("name")).toString();
        }
    }
    return QString();
}

static QString uniqueContentsDestinationName(Okular::Document *document, const QString &title)
{
    QSet<QString> existingNames;
    const QVariantList destinations = document->metaData(QStringLiteral("NamedViewports")).toList();
    for (const QVariant &value : destinations) {
        existingNames.insert(value.toMap().value(QStringLiteral("name")).toString());
    }

    QString baseName;
    bool lastWasSeparator = false;
    for (const QChar character : title.trimmed()) {
        const ushort value = character.unicode();
        const bool isAsciiLetterOrNumber = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9');
        if (isAsciiLetterOrNumber || character == QLatin1Char('.') || character == QLatin1Char('_') || character == QLatin1Char('-')) {
            baseName.append(character);
            lastWasSeparator = false;
        } else if (!baseName.isEmpty() && !lastWasSeparator) {
            baseName.append(QLatin1Char('-'));
            lastWasSeparator = true;
        }
        if (baseName.size() >= 32) {
            break;
        }
    }
    while (baseName.endsWith(QLatin1Char('-'))) {
        baseName.chop(1);
    }
    if (baseName.isEmpty()) {
        baseName = QStringLiteral("toc");
    }
    if (!existingNames.contains(baseName)) {
        return baseName;
    }
    for (int suffix = 2;; ++suffix) {
        const QString candidate = baseName + QLatin1Char('.') + QString::number(suffix);
        if (!existingNames.contains(candidate)) {
            return candidate;
        }
    }
}

QDomElement TOC::synopsisElementForIndex(QDomDocument &document, const QModelIndex &index) const
{
    QDomElement element = document.createElement(m_model->data(index, Qt::DisplayRole).toString());
    const QString viewportName = m_model->viewportNameForIndex(index);
    if (!viewportName.isEmpty()) {
        element.setAttribute(QStringLiteral("ViewportName"), viewportName);
    } else {
        const Okular::DocumentViewport viewport = m_model->viewportForIndex(index);
        if (viewport.isValid()) {
            element.setAttribute(QStringLiteral("Viewport"), viewport.toString());
        }
    }
    const QString externalFileName = m_model->externalFileNameForIndex(index);
    if (!externalFileName.isEmpty()) {
        element.setAttribute(QStringLiteral("ExternalFileName"), externalFileName);
    }
    const QString url = m_model->urlForIndex(index);
    if (!url.isEmpty()) {
        element.setAttribute(QStringLiteral("URL"), url);
    }

    for (int row = 0; row < m_model->rowCount(index); ++row) {
        element.appendChild(synopsisElementForIndex(document, m_model->index(row, 0, index)));
    }
    return element;
}

Okular::DocumentSynopsis TOC::synopsisFromModel() const
{
    Okular::DocumentSynopsis synopsis;
    for (int row = 0; row < m_model->rowCount(); ++row) {
        synopsis.appendChild(synopsisElementForIndex(synopsis, m_model->index(row, 0)));
    }
    return synopsis;
}

QDomElement TOC::elementForIndexPath(QDomDocument &document, const QModelIndex &index) const
{
    QDomNode parent = document;
    for (int row : indexPath(index)) {
        QDomElement element = childElementAt(parent, row);
        if (element.isNull()) {
            return QDomElement();
        }
        parent = element;
    }
    return parent.toElement();
}

bool TOC::applySynopsis(const Okular::DocumentSynopsis &synopsis)
{
    QString errorText;
    if (!m_document->setDocumentSynopsis(synopsis, &errorText)) {
        KMessageBox::error(this, errorText.isEmpty() ? i18n("This document's contents cannot be edited.") : errorText, i18n("Edit Contents"));
        return false;
    }

    m_model->clear();
    const Okular::DocumentSynopsis *updatedSynopsis = m_document->documentSynopsis();
    if (updatedSynopsis) {
        m_model->fill(updatedSynopsis);
    }
    Q_EMIT hasTOC(!m_model->isEmpty());
    Q_EMIT contentsModified();
    return true;
}

void TOC::addCurrentPageEntry()
{
    if (!m_editingEnabled || !m_document->isOpened()) {
        return;
    }

    const Okular::DocumentViewport viewport = documentViewport();
    const int pageNumber = viewport.pageNumber + 1;
    const QString title = QInputDialog::getText(this, i18n("Add Contents Entry"), i18n("Entry title:"), QLineEdit::Normal, i18n("Page %1", pageNumber));
    if (title.trimmed().isEmpty()) {
        return;
    }

    Okular::DocumentSynopsis synopsis = synopsisFromModel();
    QDomElement element = synopsis.createElement(title.trimmed());
    QString destinationName = namedDestinationAtViewport(m_document, viewport);
    if (destinationName.isEmpty()) {
        destinationName = uniqueContentsDestinationName(m_document, title);
        element.setAttribute(QStringLiteral("Viewport"), viewport.toString());
        element.setAttribute(QStringLiteral("CreateViewportName"), QStringLiteral("true"));
    }
    element.setAttribute(QStringLiteral("ViewportName"), destinationName);
    synopsis.appendChild(element);
    applySynopsis(synopsis);
}

void TOC::addNamedDestinationEntry(const QString &name)
{
    if (!m_editingEnabled || !m_document->isOpened() || name.isEmpty()) {
        return;
    }

    const QVariantList destinations = m_document->metaData(QStringLiteral("NamedViewports")).toList();
    const bool destinationExists = std::ranges::any_of(destinations, [&name](const QVariant &value) { return value.toMap().value(QStringLiteral("name")).toString() == name; });
    if (!destinationExists) {
        return;
    }

    Okular::DocumentSynopsis synopsis = synopsisFromModel();
    QDomElement element = synopsis.createElement(name);
    element.setAttribute(QStringLiteral("ViewportName"), name);
    synopsis.appendChild(element);
    applySynopsis(synopsis);
}

void TOC::renameCurrentEntry()
{
    if (!m_editingEnabled) {
        return;
    }

    const QModelIndex index = m_treeView->currentIndex();
    if (!index.isValid()) {
        return;
    }

    const QString oldTitle = m_model->data(index, Qt::DisplayRole).toString();
    const QString newTitle = QInputDialog::getText(this, i18n("Rename Contents Entry"), i18n("Entry title:"), QLineEdit::Normal, oldTitle);
    if (newTitle.trimmed().isEmpty() || newTitle == oldTitle) {
        return;
    }

    Okular::DocumentSynopsis synopsis = synopsisFromModel();
    QDomElement element = elementForIndexPath(synopsis, index);
    if (element.isNull()) {
        return;
    }
    element.setTagName(newTitle.trimmed());
    applySynopsis(synopsis);
}

void TOC::deleteCurrentEntry()
{
    if (!m_editingEnabled) {
        return;
    }

    const QModelIndex index = m_treeView->currentIndex();
    if (!index.isValid()) {
        return;
    }

    const QString title = m_model->data(index, Qt::DisplayRole).toString();
    const int result = KMessageBox::warningContinueCancel(this, i18n("Delete the contents entry '%1' and its child entries?", title), i18n("Delete Contents Entry"), KStandardGuiItem::del(), KStandardGuiItem::cancel());
    if (result != KMessageBox::Continue) {
        return;
    }

    Okular::DocumentSynopsis synopsis = synopsisFromModel();
    QDomElement element = elementForIndexPath(synopsis, index);
    if (element.isNull()) {
        return;
    }
    element.parentNode().removeChild(element);
    applySynopsis(synopsis);
}

void TOC::contextMenuEvent(QContextMenuEvent *e)
{
    QModelIndex index = m_treeView->currentIndex();
    if (!index.isValid()) {
        return;
    }

    Okular::DocumentViewport viewport = m_model->viewportForIndex(index);

    Q_EMIT rightClick(viewport, e->globalPos(), m_model->data(index).toString());
}

void TOC::expandRecursively()
{
    QList<QModelIndex> worklist = {m_treeView->currentIndex()};
    if (!worklist[0].isValid()) {
        return;
    }
    while (!worklist.isEmpty()) {
        QModelIndex index = worklist.takeLast();
        m_treeView->expand(index);
        for (int i = 0; i < m_model->rowCount(index); i++) {
            worklist += m_model->index(i, 0, index);
        }
    }
}

void TOC::collapseRecursively()
{
    QList<QModelIndex> worklist = {m_treeView->currentIndex()};
    if (!worklist[0].isValid()) {
        return;
    }
    while (!worklist.isEmpty()) {
        QModelIndex index = worklist.takeLast();
        m_treeView->collapse(index);
        for (int i = 0; i < m_model->rowCount(index); i++) {
            worklist += m_model->index(i, 0, index);
        }
    }
}

void TOC::expandAll()
{
    m_treeView->expandAll();
}

void TOC::collapseAll()
{
    m_treeView->collapseAll();
}
