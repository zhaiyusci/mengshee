// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef AIBRIDGE_H
#define AIBRIDGE_H

#include <QHash>
#include <QObject>
#include <QPointer>
#include <functional>

#include "aisearch.h"
#include "core/observer.h"

class QJsonObject;
class QNetworkAccessManager;

namespace Okular
{
class Document;
class DocumentViewport;
}

/**
 * The protocol between the reader and its own chat window.
 *
 * The chat window is this application's window, started by the reader as a
 * child process; the two talk over the child's standard input and output, one
 * JSON object per line. That transport is the same on every platform and needs
 * no port, token, discovery file or platform-specific socket.
 *
 * Requests and replies:
 *   {"id": 1, "method": "state", "params": {}} -> {"id": 1, "ok": true, "result": {...}}
 */
class AiBridge : public QObject, public Okular::DocumentObserver
{
    Q_OBJECT

public:
    /** The sender writes one complete line to the chat window. */
    using Sender = std::function<void(const QByteArray &line)>;

    explicit AiBridge(Okular::Document *document, Sender sender, QObject *parent = nullptr);
    ~AiBridge() override;

    void notifySetup(const QList<Okular::Page *> &pages, int flags) override;
    void notifyPageChanged(int page, int flags) override;

    void setSender(Sender sender);
    /** Feeds one line read from the child's standard output. */
    void receive(const QByteArray &line);

    /** Supplies the document path on demand; the part decides what is open. */
    void setPathProvider(std::function<QString()> provider);
    void setNavigator(std::function<void(const Okular::DocumentViewport &)> navigator);

    /** The document identity this bridge belongs to, for the window title. */
    void setDocumentIdentity(const QString &id, const QString &title);

private:
    void dispatch(const QJsonObject &request);
    void reply(int id, const QJsonObject &result);
    void replyError(int id, const QString &error);
    void ensureBlocks(int page);
    void invalidateDocument();
    QString currentPath() const;

    QPointer<Okular::Document> m_document;
    Sender m_sender;
    QNetworkAccessManager *m_network = nullptr;
    std::function<QString()> m_pathProvider;
    std::function<void(const Okular::DocumentViewport &)> m_navigator;
    QString m_documentId;
    QString m_documentTitle;
    struct CitationBlock {
        int page;
        AiSearch::TextBlock block;
    };
    QHash<QString, CitationBlock> m_blocks;
    QHash<int, QStringList> m_pageBlocks;
    QString m_blockScope;
    quint64 m_session = 0;
    quint64 m_revision = 0;
};

#endif
