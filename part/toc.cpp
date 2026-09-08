/*
    SPDX-FileCopyrightText: 2004-2006 Albert Astals Cid <aacid@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "toc.h"

// qt/kde includes
#include <algorithm>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLayout>
#include <QListWidget>
#include <QMetaObject>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSet>
#include <QSpinBox>
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
    m_treeView->header()->hide();
    m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeView->setDefaultDropAction(Qt::MoveAction);
    m_treeView->setDropIndicatorShown(true);
    connect(m_treeView, &QTreeView::clicked, this, &TOC::slotExecuted);
    connect(m_treeView, &QTreeView::activated, this, &TOC::slotExecuted);
    configureModel();
    m_searchLine->setTreeView(m_treeView);
}

TOC::~TOC()
{
    m_document->removeObserver(this);
}

void TOC::setPageView(PageView *pageView)
{
    m_pageView = pageView;
}

void TOC::setEditingEnabled(bool enabled)
{
    m_editingEnabled = enabled;
    m_model->setEditingEnabled(enabled);
    m_treeView->setDragEnabled(enabled);
    m_treeView->setAcceptDrops(enabled);
    m_treeView->setDragDropMode(enabled ? QAbstractItemView::InternalMove : QAbstractItemView::NoDragDrop);
}

void TOC::configureModel()
{
    connect(m_model, &TOCModel::structureChanged, this, &TOC::scheduleStructureCommit, Qt::UniqueConnection);
    m_model->setEditingEnabled(m_editingEnabled);
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

void TOC::prepareForReload()
{
    if (m_model->isEmpty()) {
        return;
    }

    const QList<QModelIndex> list = expandedNodes();
    TOCModel *m = m_model;
    m_model = new TOCModel(m_document, m_treeView);
    configureModel();
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
    configureModel();
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
    const QString destinationName = m_model->viewportNameForIndex(index);
    Okular::DocumentViewport viewport = m_model->viewportForIndex(index);
    if (!externalFileName.isEmpty()) {
        if (!destinationName.isEmpty()) {
            Okular::GotoAction action(externalFileName, destinationName);
            m_document->processAction(&action);
        } else {
            Okular::GotoAction action(externalFileName, viewport);
            m_document->processAction(&action);
        }
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

static QModelIndex indexFromPath(const QAbstractItemModel *model, const QList<int> &path)
{
    QModelIndex index;
    for (int row : path) {
        index = model->index(row, 0, index);
        if (!index.isValid()) {
            return QModelIndex();
        }
    }
    return index;
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
    QList<QList<int>> expandedPaths;
    const QList<QModelIndex> expandedIndexes = expandedNodes();
    expandedPaths.reserve(expandedIndexes.size());
    for (const QModelIndex &index : expandedIndexes) {
        expandedPaths.append(indexPath(index));
    }
    const QList<int> currentPath = indexPath(m_treeView->currentIndex());
    const int horizontalScrollPosition = m_treeView->horizontalScrollBar()->value();
    const int verticalScrollPosition = m_treeView->verticalScrollBar()->value();

    QString errorText;
    if (!m_document->setDocumentSynopsis(synopsis, &errorText)) {
        KMessageBox::error(this, errorText.isEmpty() ? i18n("This document's contents cannot be edited.") : errorText, i18n("Edit Contents"));
        return false;
    }

    for (const QList<int> &path : std::as_const(expandedPaths)) {
        const QModelIndex index = indexFromPath(m_model, path);
        if (index.isValid()) {
            m_treeView->expand(index);
        }
    }
    const QModelIndex currentIndex = indexFromPath(m_model, currentPath);
    if (currentIndex.isValid()) {
        for (QModelIndex parent = currentIndex.parent(); parent.isValid(); parent = parent.parent()) {
            m_treeView->expand(parent);
        }
        m_treeView->setCurrentIndex(currentIndex);
    }
    m_treeView->horizontalScrollBar()->setValue(horizontalScrollPosition);
    m_treeView->verticalScrollBar()->setValue(verticalScrollPosition);
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

void TOC::scheduleStructureCommit()
{
    if (!m_editingEnabled || m_structureCommitPending) {
        return;
    }
    m_structureCommitPending = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_structureCommitPending = false;
            const Okular::DocumentSynopsis synopsis = synopsisFromModel();
            if (!applySynopsis(synopsis)) {
                m_model->clear();
                if (const Okular::DocumentSynopsis *currentSynopsis = m_document->documentSynopsis()) {
                    m_model->fill(currentSynopsis);
                }
            }
        },
        Qt::QueuedConnection);
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

bool TOC::setEntryDestination(const QModelIndex &index, const QString &destinationName, const std::optional<Okular::DocumentViewport> &directDestination)
{
    if (!m_editingEnabled || !index.isValid() || (destinationName.isEmpty() && !directDestination)) {
        return false;
    }

    Okular::DocumentSynopsis synopsis = synopsisFromModel();
    QDomElement element = elementForIndexPath(synopsis, index);
    if (element.isNull()) {
        return false;
    }

    element.removeAttribute(QStringLiteral("URL"));
    element.removeAttribute(QStringLiteral("ExternalFileName"));
    element.removeAttribute(QStringLiteral("Viewport"));
    element.removeAttribute(QStringLiteral("ViewportName"));
    element.removeAttribute(QStringLiteral("CreateViewportName"));
    if (!destinationName.isEmpty()) {
        element.setAttribute(QStringLiteral("ViewportName"), destinationName);
    } else {
        const QString generatedName = uniqueContentsDestinationName(m_document, element.tagName());
        element.setAttribute(QStringLiteral("ViewportName"), generatedName);
        element.setAttribute(QStringLiteral("Viewport"), directDestination->toString());
        element.setAttribute(QStringLiteral("CreateViewportName"), QStringLiteral("true"));
    }
    return applySynopsis(synopsis);
}

void TOC::editCurrentEntryDestination()
{
    if (!m_editingEnabled) {
        return;
    }
    const QModelIndex index = m_treeView->currentIndex();
    if (!index.isValid()) {
        return;
    }

    struct DestinationChoice {
        QString name;
        Okular::DocumentViewport viewport;
    };
    QList<DestinationChoice> destinations;
    const QVariantList destinationValues = m_document->metaData(QStringLiteral("NamedViewports")).toList();
    for (const QVariant &value : destinationValues) {
        const QVariantMap destination = value.toMap();
        const QString name = destination.value(QStringLiteral("name")).toString();
        const Okular::DocumentViewport viewport(destination.value(QStringLiteral("viewport")).toString());
        if (!name.isEmpty() && viewport.isValid()) {
            destinations.append({name, viewport});
        }
    }
    std::sort(destinations.begin(), destinations.end(), [](const DestinationChoice &left, const DestinationChoice &right) { return left.name.compare(right.name, Qt::CaseInsensitive) < 0; });

    const QString currentDestinationName = m_model->viewportNameForIndex(index);
    Okular::DocumentViewport currentViewport = m_model->viewportForIndex(index);
    if (!currentViewport.isValid()) {
        currentViewport = documentViewport();
    }

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("EditContentsDestinationDialog"));
    dialog.setWindowTitle(i18n("Edit Contents Destination"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    layout->addLayout(form);

    auto *namedRadio = new QRadioButton(i18n("Named destination (preferred)"), &dialog);
    auto *namedControls = new QWidget(&dialog);
    auto *namedLayout = new QVBoxLayout(namedControls);
    namedLayout->setContentsMargins(0, 0, 0, 0);
    auto *search = new KLineEdit(namedControls);
    search->setPlaceholderText(i18n("Search by name or page..."));
    search->setClearButtonEnabled(true);
    namedLayout->addWidget(search);
    auto *destinationList = new QListWidget(namedControls);
    destinationList->setObjectName(QStringLiteral("ContentsNamedDestinationList"));
    destinationList->setMinimumHeight(180);
    namedLayout->addWidget(destinationList);
    for (int destinationIndex = 0; destinationIndex < destinations.size(); ++destinationIndex) {
        const DestinationChoice &destination = destinations.at(destinationIndex);
        auto *item = new QListWidgetItem(i18n("%1 — page %2", destination.name, destination.viewport.pageNumber + 1), destinationList);
        item->setData(Qt::UserRole, destinationIndex);
        if (destination.name == currentDestinationName) {
            destinationList->setCurrentItem(item);
        }
    }
    if (!destinationList->currentItem() && destinationList->count() > 0) {
        destinationList->setCurrentRow(0);
    }
    namedRadio->setEnabled(!destinations.isEmpty());
    form->addRow(namedRadio, namedControls);

    auto *directRadio = new QRadioButton(i18n("Direct page position (creates a named destination)"), &dialog);
    auto *directControls = new QWidget(&dialog);
    auto *directLayout = new QHBoxLayout(directControls);
    directLayout->setContentsMargins(0, 0, 0, 0);
    auto *pageSpin = new QSpinBox(directControls);
    pageSpin->setRange(1, qMax(1, static_cast<int>(m_document->pages())));
    pageSpin->setValue(qBound(1, currentViewport.pageNumber + 1, qMax(1, static_cast<int>(m_document->pages()))));
    auto *horizontalSpin = new QDoubleSpinBox(directControls);
    horizontalSpin->setRange(0.0, 100.0);
    horizontalSpin->setDecimals(1);
    horizontalSpin->setSuffix(i18n("% x"));
    horizontalSpin->setValue((currentViewport.rePos.enabled ? currentViewport.rePos.normalizedX : 0.0) * 100.0);
    auto *verticalSpin = new QDoubleSpinBox(directControls);
    verticalSpin->setRange(0.0, 100.0);
    verticalSpin->setDecimals(1);
    verticalSpin->setSuffix(i18n("% y"));
    verticalSpin->setValue((currentViewport.rePos.enabled ? currentViewport.rePos.normalizedY : 0.0) * 100.0);
    directLayout->addWidget(new QLabel(i18n("Page:"), directControls));
    directLayout->addWidget(pageSpin);
    directLayout->addWidget(horizontalSpin);
    directLayout->addWidget(verticalSpin);
    form->addRow(directRadio, directControls);

    if (!currentDestinationName.isEmpty() && !destinations.isEmpty()) {
        namedRadio->setChecked(true);
    } else {
        directRadio->setChecked(true);
    }
    const auto updateControls = [=] {
        namedControls->setEnabled(namedRadio->isChecked() && !destinations.isEmpty());
        directControls->setEnabled(directRadio->isChecked());
    };
    connect(namedRadio, &QRadioButton::toggled, &dialog, updateControls);
    connect(directRadio, &QRadioButton::toggled, &dialog, updateControls);
    connect(destinationList, &QListWidget::itemClicked, &dialog, [namedRadio](QListWidgetItem *) { namedRadio->setChecked(true); });
    connect(search, &QLineEdit::textChanged, &dialog, [destinationList](const QString &text) {
        QListWidgetItem *firstVisible = nullptr;
        for (int row = 0; row < destinationList->count(); ++row) {
            QListWidgetItem *item = destinationList->item(row);
            const bool matches = text.trimmed().isEmpty() || item->text().contains(text.trimmed(), Qt::CaseInsensitive);
            item->setHidden(!matches);
            if (matches && !firstVisible) {
                firstVisible = item;
            }
        }
        if (!destinationList->currentItem() || destinationList->currentItem()->isHidden()) {
            destinationList->setCurrentItem(firstVisible);
        }
    });
    updateControls();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    const auto updateAcceptButton = [=] {
        const bool namedChoiceValid = !namedRadio->isChecked() || (destinationList->currentItem() && !destinationList->currentItem()->isHidden());
        buttons->button(QDialogButtonBox::Ok)->setEnabled(namedChoiceValid);
    };
    connect(namedRadio, &QRadioButton::toggled, &dialog, updateAcceptButton);
    connect(destinationList, &QListWidget::currentItemChanged, &dialog, [updateAcceptButton](QListWidgetItem *, QListWidgetItem *) { updateAcceptButton(); });
    connect(search, &QLineEdit::textChanged, &dialog, [updateAcceptButton](const QString &) { updateAcceptButton(); });
    updateAcceptButton();
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    if (namedRadio->isChecked() && destinationList->currentItem()) {
        const int destinationIndex = destinationList->currentItem()->data(Qt::UserRole).toInt();
        setEntryDestination(index, destinations.at(destinationIndex).name, std::nullopt);
        return;
    }

    Okular::DocumentViewport destination(pageSpin->value() - 1);
    destination.rePos.enabled = true;
    destination.rePos.normalizedX = horizontalSpin->value() / 100.0;
    destination.rePos.normalizedY = verticalSpin->value() / 100.0;
    destination.rePos.pos = Okular::DocumentViewport::TopLeft;
    setEntryDestination(index, QString(), destination);
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
    if (e->reason() == QContextMenuEvent::Mouse) {
        const QModelIndex clickedIndex = m_treeView->indexAt(m_treeView->viewport()->mapFromGlobal(e->globalPos()));
        if (clickedIndex.isValid()) {
            index = clickedIndex;
            m_treeView->setCurrentIndex(index);
        }
    }
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
