/*
    SPDX-FileCopyrightText: 2007 Pino Toscano <pino@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "tocmodel.h"

#include <algorithm>
#include <utility>
#include <QDataStream>
#include <QIODevice>
#include <QList>
#include <QMimeData>
#include <qdom.h>

#include "core/document.h"
#include "core/page.h"

Q_DECLARE_METATYPE(QModelIndex)

struct TOCItem {
    TOCItem();
    TOCItem(TOCItem *parent, const QDomElement &e);
    ~TOCItem();

    TOCItem(const TOCItem &) = delete;
    TOCItem &operator=(const TOCItem &) = delete;

    QString text;
    Okular::DocumentViewport viewport;
    QString viewportName;
    QString extFileName;
    QString url;
    TOCItem *parent;
    QList<TOCItem *> children;
    TOCModelPrivate *model;
};

class TOCModelPrivate
{
public:
    explicit TOCModelPrivate(TOCModel *qq);
    ~TOCModelPrivate();

    void addChildren(const QDomNode &parentNode, TOCItem *parentItem);
    QModelIndex indexForItem(TOCItem *item) const;
    Okular::DocumentViewport viewportForItem(const TOCItem *item) const;

    TOCModel *q;
    TOCItem *root;
    bool dirty : 1;
    Okular::Document *document;
    QList<TOCItem *> itemsToOpen;
    TOCModel *m_oldModel;
    QList<QModelIndex> m_oldTocExpandedIndexes;
    bool editingEnabled = false;
    Q_DISABLE_COPY(TOCModelPrivate)
};

TOCItem::TOCItem()
    : parent(nullptr)
    , model(nullptr)
{
}

TOCItem::TOCItem(TOCItem *_parent, const QDomElement &e)
    : parent(_parent)
{
    parent->children.append(this);
    model = parent->model;
    text = e.tagName();

    // viewport loading
    viewportName = e.attribute(QStringLiteral("ViewportName"));
    if (e.hasAttribute(QStringLiteral("Viewport"))) {
        // Direct destinations belong to the outline entry itself. Named
        // destinations remain references and are resolved when used.
        viewport = Okular::DocumentViewport(e.attribute(QStringLiteral("Viewport")));
    }

    extFileName = e.attribute(QStringLiteral("ExternalFileName"));
    url = e.attribute(QStringLiteral("URL"));
}

TOCItem::~TOCItem()
{
    qDeleteAll(children);
}

TOCModelPrivate::TOCModelPrivate(TOCModel *qq)
    : q(qq)
    , root(new TOCItem)
    , dirty(false)
    , document(nullptr)
    , m_oldModel(nullptr)
{
    root->model = this;
}

TOCModelPrivate::~TOCModelPrivate()
{
    delete root;
    delete m_oldModel;
}

void TOCModelPrivate::addChildren(const QDomNode &parentNode, TOCItem *parentItem)
{
    TOCItem *currentItem = nullptr;
    QDomNode n = parentNode.firstChild();
    while (!n.isNull()) {
        // convert the node to an element (sure it is)
        QDomElement e = n.toElement();

        // insert the entry as top level (listview parented) or 2nd+ level
        currentItem = new TOCItem(parentItem, e);

        // descend recursively and advance to the next node
        if (e.hasChildNodes()) {
            addChildren(n, currentItem);
        }

        // open/keep close the item
        bool isOpen = false;
        if (e.hasAttribute(QStringLiteral("Open"))) {
            isOpen = QVariant(e.attribute(QStringLiteral("Open"))).toBool();
        }
        if (isOpen) {
            itemsToOpen.append(currentItem);
        }

        n = n.nextSibling();
        Q_EMIT q->countChanged();
    }
}

QModelIndex TOCModelPrivate::indexForItem(TOCItem *item) const
{
    if (item->parent) {
        int id = item->parent->children.indexOf(item);
        if (id >= 0 && id < item->parent->children.count()) {
            return q->createIndex(id, 0, item);
        }
    }
    return QModelIndex();
}

Okular::DocumentViewport TOCModelPrivate::viewportForItem(const TOCItem *item) const
{
    if (!item->viewportName.isEmpty()) {
        if (!item->extFileName.isEmpty()) {
            return Okular::DocumentViewport();
        }
        return Okular::DocumentViewport(document->metaData(QStringLiteral("NamedViewport"), item->viewportName).toString());
    }
    return item->viewport;
}

TOCModel::TOCModel(Okular::Document *document, QObject *parent)
    : QAbstractItemModel(parent)
    , d(new TOCModelPrivate(this))
{
    d->document = document;

    qRegisterMetaType<QModelIndex>();
}

TOCModel::~TOCModel()
{
    delete d;
}

QHash<int, QByteArray> TOCModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractItemModel::roleNames();
    roles[PageRole] = "page";
    roles[PageLabelRole] = "pageLabel";
    return roles;
}

int TOCModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return 1;
}

QVariant TOCModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    TOCItem *item = static_cast<TOCItem *>(index.internalPointer());
    switch (role) {
    case Qt::DisplayRole:
    case Qt::ToolTipRole:
        return item->text;
        break;
    case PageRole: {
        const Okular::DocumentViewport viewport = d->viewportForItem(item);
        if (viewport.isValid()) {
            return viewport.pageNumber + 1;
        }
        break;
    }
    case PageLabelRole: {
        const Okular::DocumentViewport viewport = d->viewportForItem(item);
        if (viewport.isValid() && viewport.pageNumber < int(d->document->pages())) {
            return d->document->page(viewport.pageNumber)->label();
        }
        break;
    }
    }
    return QVariant();
}

bool TOCModel::hasChildren(const QModelIndex &parent) const
{
    if (!parent.isValid()) {
        return true;
    }

    TOCItem *item = static_cast<TOCItem *>(parent.internalPointer());
    return !item->children.isEmpty();
}

QVariant TOCModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal) {
        return QVariant();
    }

    if (section == 0 && role == Qt::DisplayRole) {
        return QStringLiteral("Topics");
    }

    return QVariant();
}

QModelIndex TOCModel::index(int row, int column, const QModelIndex &parent) const
{
    if (row < 0 || column != 0) {
        return QModelIndex();
    }

    TOCItem *item = parent.isValid() ? static_cast<TOCItem *>(parent.internalPointer()) : d->root;
    if (row < item->children.count()) {
        return createIndex(row, column, item->children.at(row));
    }

    return QModelIndex();
}

QModelIndex TOCModel::parent(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return QModelIndex();
    }

    TOCItem *item = static_cast<TOCItem *>(index.internalPointer());
    return d->indexForItem(item->parent);
}

int TOCModel::rowCount(const QModelIndex &parent) const
{
    TOCItem *item = parent.isValid() ? static_cast<TOCItem *>(parent.internalPointer()) : d->root;
    return item->children.count();
}

static QModelIndex indexForIndex(const QModelIndex &oldModelIndex, QAbstractItemModel *newModel)
{
    QModelIndex newModelIndex;
    if (oldModelIndex.parent().isValid()) {
        newModelIndex = newModel->index(oldModelIndex.row(), oldModelIndex.column(), indexForIndex(oldModelIndex.parent(), newModel));
    } else {
        newModelIndex = newModel->index(oldModelIndex.row(), oldModelIndex.column());
    }
    return newModelIndex;
}

void TOCModel::fill(const Okular::DocumentSynopsis *toc)
{
    if (!toc) {
        return;
    }

    clear();
    Q_EMIT layoutAboutToBeChanged();
    d->addChildren(*toc, d->root);
    d->dirty = true;
    Q_EMIT layoutChanged();
    if (equals(d->m_oldModel)) {
        for (const QModelIndex &oldIndex : std::as_const(d->m_oldTocExpandedIndexes)) {
            const QModelIndex idx = indexForIndex(oldIndex, this);
            if (!idx.isValid()) {
                continue;
            }

            // TODO misusing parent() here, fix
            QMetaObject::invokeMethod(QObject::parent(), "expand", Qt::QueuedConnection, Q_ARG(QModelIndex, idx));
        }
    } else {
        for (TOCItem *item : std::as_const(d->itemsToOpen)) {
            const QModelIndex idx = d->indexForItem(item);
            if (!idx.isValid()) {
                continue;
            }

            // TODO misusing parent() here, fix
            QMetaObject::invokeMethod(QObject::parent(), "expand", Qt::QueuedConnection, Q_ARG(QModelIndex, idx));
        }
    }
    d->itemsToOpen.clear();
    delete d->m_oldModel;
    d->m_oldModel = nullptr;
    d->m_oldTocExpandedIndexes.clear();
}

void TOCModel::clear()
{
    if (!d->dirty) {
        return;
    }

    beginResetModel();
    qDeleteAll(d->root->children);
    d->root->children.clear();
    endResetModel();
    d->dirty = false;
}

bool TOCModel::isEmpty() const
{
    return d->root->children.isEmpty();
}

bool TOCModel::equals(const TOCModel *model) const
{
    if (model) {
        return checkequality(model);
    } else {
        return false;
    }
}

void TOCModel::setOldModelData(TOCModel *model, const QList<QModelIndex> &list)
{
    delete d->m_oldModel;
    d->m_oldModel = model;
    d->m_oldTocExpandedIndexes = list;
}

bool TOCModel::hasOldModelData() const
{
    return (d->m_oldModel != nullptr);
}

TOCModel *TOCModel::clearOldModelData() const
{
    TOCModel *oldModel = d->m_oldModel;
    d->m_oldModel = nullptr;
    d->m_oldTocExpandedIndexes.clear();
    return oldModel;
}

QString TOCModel::externalFileNameForIndex(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return QString();
    }

    const TOCItem *item = static_cast<TOCItem *>(index.internalPointer());
    return item->extFileName;
}

Qt::ItemFlags TOCModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags result = QAbstractItemModel::flags(index);
    if (!d->editingEnabled) {
        return result;
    }
    if (index.isValid()) {
        result |= Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    } else {
        result |= Qt::ItemIsDropEnabled;
    }
    return result;
}

Qt::DropActions TOCModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

QStringList TOCModel::mimeTypes() const
{
    return {QStringLiteral("application/x-mengshee-toc-index")};
}

static QList<int> tocIndexPath(const QModelIndex &index)
{
    QList<int> path;
    for (QModelIndex current = index; current.isValid(); current = current.parent()) {
        path.prepend(current.row());
    }
    return path;
}

QMimeData *TOCModel::mimeData(const QModelIndexList &indexes) const
{
    const auto indexIt = std::find_if(indexes.cbegin(), indexes.cend(), [](const QModelIndex &index) { return index.isValid() && index.column() == 0; });
    if (indexIt == indexes.cend()) {
        return nullptr;
    }

    QByteArray encodedPath;
    QDataStream stream(&encodedPath, QIODevice::WriteOnly);
    stream << tocIndexPath(*indexIt);
    auto *data = new QMimeData;
    data->setData(QStringLiteral("application/x-mengshee-toc-index"), encodedPath);
    return data;
}

bool TOCModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &destinationParent)
{
    static const QString mimeType = QStringLiteral("application/x-mengshee-toc-index");
    if (!d->editingEnabled || !data || action != Qt::MoveAction || (column > 0) || !data->hasFormat(mimeType)) {
        return false;
    }

    QList<int> sourcePath;
    QByteArray encodedPath = data->data(mimeType);
    QDataStream stream(&encodedPath, QIODevice::ReadOnly);
    stream >> sourcePath;
    if (stream.status() != QDataStream::Ok || sourcePath.isEmpty()) {
        return false;
    }

    QModelIndex sourceIndex;
    for (int sourceRow : std::as_const(sourcePath)) {
        sourceIndex = index(sourceRow, 0, sourceIndex);
        if (!sourceIndex.isValid()) {
            return false;
        }
    }

    auto *sourceItem = static_cast<TOCItem *>(sourceIndex.internalPointer());
    auto *destinationParentItem = destinationParent.isValid() ? static_cast<TOCItem *>(destinationParent.internalPointer()) : d->root;
    for (TOCItem *ancestor = destinationParentItem; ancestor; ancestor = ancestor->parent) {
        if (ancestor == sourceItem) {
            return false;
        }
    }

    TOCItem *sourceParentItem = sourceItem->parent;
    const QModelIndex sourceParentIndex = sourceIndex.parent();
    const int sourceRow = sourceIndex.row();
    int destinationRow = row < 0 ? destinationParentItem->children.size() : qBound(0, row, destinationParentItem->children.size());
    if (sourceParentItem == destinationParentItem && (destinationRow == sourceRow || destinationRow == sourceRow + 1)) {
        return false;
    }
    if (!beginMoveRows(sourceParentIndex, sourceRow, sourceRow, destinationParent, destinationRow)) {
        return false;
    }

    sourceParentItem->children.removeAt(sourceRow);
    if (sourceParentItem == destinationParentItem && sourceRow < destinationRow) {
        --destinationRow;
    }
    destinationParentItem->children.insert(destinationRow, sourceItem);
    sourceItem->parent = destinationParentItem;
    endMoveRows();
    Q_EMIT structureChanged();
    return true;
}

void TOCModel::setEditingEnabled(bool enabled)
{
    if (d->editingEnabled == enabled) {
        return;
    }
    d->editingEnabled = enabled;
    if (rowCount() > 0) {
        Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, 0));
    }
}

QString TOCModel::viewportNameForIndex(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return QString();
    }

    const TOCItem *item = static_cast<TOCItem *>(index.internalPointer());
    return item->viewportName;
}

Okular::DocumentViewport TOCModel::viewportForIndex(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Okular::DocumentViewport();
    }

    const TOCItem *item = static_cast<TOCItem *>(index.internalPointer());
    return d->viewportForItem(item);
}

QString TOCModel::urlForIndex(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return QString();
    }

    const TOCItem *item = static_cast<TOCItem *>(index.internalPointer());
    return item->url;
}

bool TOCModel::checkequality(const TOCModel *model, const QModelIndex &parentA, const QModelIndex &parentB) const
{
    if (rowCount(parentA) != model->rowCount(parentB)) {
        return false;
    }
    for (int i = 0; i < rowCount(parentA); i++) {
        QModelIndex indxA = index(i, 0, parentA);
        QModelIndex indxB = model->index(i, 0, parentB);
        if (indxA.data() != indxB.data()) {
            return false;
        }
        if (hasChildren(indxA) != model->hasChildren(indxB)) {
            return false;
        }
        if (!checkequality(model, indxA, indxB)) {
            return false;
        }
    }
    return true;
}
#include "moc_tocmodel.cpp"
