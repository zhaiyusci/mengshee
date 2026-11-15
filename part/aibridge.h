// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <functional>

#include "aisearch.h"

class QTcpServer;
class QTcpSocket;
class QJsonObject;
class QNetworkAccessManager;

namespace Okular
{
class Document;
}

/**
 * A loopback-only HTTP bridge that lets a separate chat front end (Tauri)
 * answer questions about the open document and drive the viewer.
 *
 * It binds 127.0.0.1 on a random port, requires a per-run token, and never
 * exposes model credentials: the front end has its own settings.
 *
 *   GET  /state                 document path, title, page count, current page
 *   GET  /page/{n}/text         text layer of one page
 *   GET  /page/{n}/image?dpi=N  PNG of one page
 *   POST /jump                  {"page": 12, "rects": [[l,t,r,b], ...]}
 *   GET  /ping
 */
class AiBridge : public QObject
{
    Q_OBJECT
public:
    explicit AiBridge(Okular::Document *document, QObject *parent = nullptr);
    ~AiBridge() override;

    bool start(QString *error = nullptr);
    int port() const;
    QString token() const;
    /** Supplies the document path on demand; the part decides what is open. */
    void setPathProvider(std::function<QString()> provider);

private:
    void handleConnection();
    void readRequest(QTcpSocket *socket);
    bool authorized(const QString &path, const QByteArray &tokenHeader) const;
    void respond(QTcpSocket *socket, int status, const QByteArray &contentType, const QByteArray &body);
    void respondJson(QTcpSocket *socket, const QJsonObject &object, int status = 200);
    void handleAsk(QTcpSocket *socket, const QByteArray &body);
    void ensureIndex();
    void writeDiscoveryFile();
    void removeDiscoveryFile();
    QString currentPath() const;

    QPointer<Okular::Document> m_document;
    QTcpServer *m_server = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QString m_token;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    QString m_discoveryPath;
    std::function<QString()> m_pathProvider;
    AiSearch::Index m_index;
    int m_indexPages = -1;
    QString m_indexPath;
    int m_port = 0;
};
