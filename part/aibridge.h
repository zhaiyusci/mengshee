// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef AIBRIDGE_H
#define AIBRIDGE_H

#include <QHash>
#include <QObject>
#include <functional>

#include "aisearch.h"

class QJsonObject;
class QNetworkAccessManager;

namespace Okular
{
class Document;
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
class AiBridge : public QObject
{
    Q_OBJECT

public:
    /** The sender writes one complete line to the chat window. */
    using Sender = std::function<void(const QByteArray &line)>;

    explicit AiBridge(Okular::Document *document, Sender sender, QObject *parent = nullptr);

    void setSender(Sender sender);
    /** Feeds one line read from the child's standard output. */
    void receive(const QByteArray &line);

    /** Supplies the document path on demand; the part decides what is open. */
    void setPathProvider(std::function<QString()> provider);

private:
    void dispatch(const QJsonObject &request);
    void reply(int id, const QJsonObject &result);
    void replyError(int id, const QString &error);
    void ensureIndex();
    QString currentPath() const;

    Okular::Document *m_document = nullptr;
    Sender m_sender;
    QNetworkAccessManager *m_network = nullptr;
    std::function<QString()> m_pathProvider;
    AiSearch::Index m_index;
    int m_indexPages = -1;
    QString m_indexPath;
};

#endif
