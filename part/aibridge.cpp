// SPDX-License-Identifier: GPL-2.0-or-later
#include "aibridge.h"

#include "aichat.h"
#include "aiservicesettings.h"
#include "core/document.h"
#include "core/page.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

namespace
{
const QColor highlightColor(96, 165, 250);
constexpr int maxRequestBytes = 1024 * 1024;

QByteArray statusText(int status)
{
    switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 404: return "Not Found";
    default: return "Error";
    }
}

QByteArray corsHeaders()
{
    // The chat front end runs inside a WebView with its own origin.
    return "Access-Control-Allow-Origin: *\r\n"
           "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
           "Access-Control-Allow-Headers: X-Auth-Token, Content-Type\r\n"
           "Access-Control-Max-Age: 86400\r\n";
}
}

AiBridge::AiBridge(Okular::Document *document, QObject *parent)
    : QObject(parent)
    , m_document(document)
    , m_server(new QTcpServer(this))
    , m_token(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    connect(m_server, &QTcpServer::newConnection, this, &AiBridge::handleConnection);
}

AiBridge::~AiBridge()
{
    removeDiscoveryFile();
}

int AiBridge::port() const
{
    return m_port;
}

QString AiBridge::token() const
{
    return m_token;
}

QString AiBridge::currentPath() const
{
    return m_pathProvider ? m_pathProvider() : QString();
}

void AiBridge::setPathProvider(std::function<QString()> provider)
{
    m_pathProvider = std::move(provider);
}

void AiBridge::writeDiscoveryFile()
{
    const QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dataRoot.isEmpty() || !QDir().mkpath(dataRoot)) {
        return;
    }
    m_discoveryPath = QDir(dataRoot).filePath(QStringLiteral("ai-bridge.json"));
    QJsonObject object;
    object.insert(QStringLiteral("port"), m_port);
    object.insert(QStringLiteral("token"), m_token);
    object.insert(QStringLiteral("path"), currentPath());
    QFile file(m_discoveryPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    }
}

void AiBridge::removeDiscoveryFile()
{
    if (!m_discoveryPath.isEmpty()) {
        QFile::remove(m_discoveryPath);
    }
}

bool AiBridge::start(QString *error)
{
    if (m_server->isListening()) {
        return true;
    }
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        if (error) {
            *error = m_server->errorString();
        }
        return false;
    }
    m_port = m_server->serverPort();
    writeDiscoveryFile();
    return true;
}

void AiBridge::handleConnection()
{
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readRequest(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }
}

bool AiBridge::authorized(const QString &path, const QByteArray &tokenHeader) const
{
    const QUrl url(path);
    const QString fromQuery = QUrlQuery(url).queryItemValue(QStringLiteral("token"));
    return fromQuery == m_token || QString::fromUtf8(tokenHeader) == m_token;
}

void AiBridge::readRequest(QTcpSocket *socket)
{
    QByteArray &buffer = m_buffers[socket];
    buffer += socket->readAll();
    if (buffer.size() > maxRequestBytes) {
        respond(socket, 400, "text/plain", "request too large");
        m_buffers.remove(socket);
        return;
    }

    const int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return;
    }

    const QByteArray head = buffer.left(headerEnd);
    const QList<QByteArray> lines = head.split('\n');
    if (lines.isEmpty()) {
        return;
    }
    const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
    if (requestLine.size() < 2) {
        return;
    }
    const QByteArray method = requestLine.at(0);
    const QString target = QString::fromUtf8(requestLine.at(1));

    int contentLength = 0;
    QByteArray tokenHeader;
    for (int index = 1; index < lines.size(); ++index) {
        const QByteArray line = lines.at(index).trimmed();
        const int colon = line.indexOf(':');
        if (colon <= 0) {
            continue;
        }
        const QByteArray name = line.left(colon).trimmed().toLower();
        const QByteArray value = line.mid(colon + 1).trimmed();
        if (name == "content-length") {
            contentLength = value.toInt();
        } else if (name == "x-auth-token") {
            tokenHeader = value;
        }
    }
    if (buffer.size() < headerEnd + 4 + contentLength) {
        return;
    }
    const QByteArray body = buffer.mid(headerEnd + 4, contentLength);
    m_buffers.remove(socket);

    if (method == "OPTIONS") {
        respond(socket, 204, "text/plain", QByteArray());
        return;
    }
    if (!authorized(target, tokenHeader)) {
        respondJson(socket, {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("unauthorized")}}, 401);
        return;
    }

    const QUrl url(target);
    const QString path = url.path();

    if (path == QLatin1String("/ping")) {
        respondJson(socket, {{QStringLiteral("ok"), true}});
        return;
    }

    if (path == QLatin1String("/state")) {
        QJsonObject document;
        if (m_document && m_document->pages() > 0) {
            document.insert(QStringLiteral("path"), QJsonValue(currentPath()));
            document.insert(QStringLiteral("title"), QJsonValue(QFileInfo(currentPath()).fileName()));
            document.insert(QStringLiteral("pages"), QJsonValue(int(m_document->pages())));
            document.insert(QStringLiteral("page"), QJsonValue(int(m_document->currentPage()) + 1));
        }
        QJsonObject object{{QStringLiteral("ok"), true}, {QStringLiteral("document"), document}};
        respondJson(socket, object);
        return;
    }

    if (path.startsWith(QLatin1String("/page/"))) {
        const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        // page / {n} / text|image
        if (parts.size() >= 3) {
            bool numberValid = false;
            const int pageNumber = parts.at(1).toInt(&numberValid) - 1;
            const QString kind = parts.at(2);
            if (numberValid && m_document && pageNumber >= 0 && pageNumber < int(m_document->pages())) {
                if (kind == QLatin1String("text")) {
                    const Okular::Page *page = m_document->page(pageNumber);
                    if (page) {
                        QJsonObject object{{QStringLiteral("ok"), true}, {QStringLiteral("page"), pageNumber + 1}, {QStringLiteral("text"), page->text(nullptr)}};
                        respondJson(socket, object);
                        return;
                    }
                } else if (kind == QLatin1String("image")) {
                    bool dpiValid = false;
                    const int dpi = QUrlQuery(url).queryItemValue(QStringLiteral("dpi")).toInt(&dpiValid);
                    QString error;
                    const QImage image = m_document->renderToImage(pageNumber, dpiValid ? qBound(36, dpi, 600) : 140, false, &error);
                    if (!image.isNull()) {
                        QByteArray png;
                        QBuffer buffer(&png);
                        buffer.open(QIODevice::WriteOnly);
                        image.save(&buffer, "PNG");
                        respond(socket, 200, "image/png", png);
                        return;
                    }
                }
            }
        }
        respondJson(socket, {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("no such page")}}, 404);
        return;
    }

    // The chat window never sees the API key: it asks the reader, which owns
    // the "General AI" settings, to run the completion.
    if (path == QLatin1String("/ask") && method == "POST") {
        handleAsk(socket, body);
        return;
    }

    if (path == QLatin1String("/jump") && method == "POST") {
        const QJsonObject request = QJsonDocument::fromJson(body).object();
        const int page = request.value(QStringLiteral("page")).toInt(0) - 1;
        if (!m_document || page < 0 || page >= int(m_document->pages())) {
            respondJson(socket, {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("no such page")}}, 404);
            return;
        }
        QList<Okular::NormalizedRect> rectangles;
        for (const QJsonValue &value : request.value(QStringLiteral("rects")).toArray()) {
            const QJsonArray rect = value.toArray();
            if (rect.size() == 4) {
                const Okular::NormalizedRect rectangle(rect.at(0).toDouble(), rect.at(1).toDouble(), rect.at(2).toDouble(), rect.at(3).toDouble());
                if (rectangle.right > rectangle.left && rectangle.bottom > rectangle.top) {
                    rectangles.append(rectangle);
                }
            }
        }
        // A label or verbatim anchor locates the exact line; explicit
        // rectangles win when the caller already has geometry.
        QString anchor = request.value(QStringLiteral("anchor")).toString().trimmed();
        if (anchor.isEmpty()) {
            anchor = request.value(QStringLiteral("label")).toString().trimmed();
        }
        if (rectangles.isEmpty() && !anchor.isEmpty()) {
            ensureIndex();
            QString located;
            const QList<Okular::NormalizedRect> found = m_index.locate(page, anchor, &located);
            if (!found.isEmpty()) {
                rectangles = found.size() == 1 ? m_index.widenToLine(page, found) : found;
            }
        }
        m_document->clearPageHighlights(AI_SEARCH_ID);
        if (!rectangles.isEmpty()) {
            m_document->setPageHighlights(AI_SEARCH_ID, page, rectangles, highlightColor);
        }
        // Scroll to the evidence itself, not just to the top of its page: the
        // viewport centres on the highlighted area, like following a link.
        Okular::DocumentViewport viewport(page);
        if (!rectangles.isEmpty()) {
            double left = 1.0;
            double top = 1.0;
            double right = 0.0;
            double bottom = 0.0;
            for (const Okular::NormalizedRect &rectangle : std::as_const(rectangles)) {
                left = std::min(left, rectangle.left);
                top = std::min(top, rectangle.top);
                right = std::max(right, rectangle.right);
                bottom = std::max(bottom, rectangle.bottom);
            }
            viewport.rePos.enabled = true;
            viewport.rePos.pos = Okular::DocumentViewport::Center;
            viewport.rePos.normalizedX = (left + right) / 2.0;
            viewport.rePos.normalizedY = (top + bottom) / 2.0;
        }
        m_document->setViewport(viewport);
        respondJson(socket, {{QStringLiteral("ok"), true}, {QStringLiteral("located"), !rectangles.isEmpty()}});
        return;
    }

    respondJson(socket, {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("unknown endpoint")}}, 404);
}

void AiBridge::ensureIndex()
{
    if (!m_document) {
        return;
    }
    const int pages = int(m_document->pages());
    const QString path = currentPath();
    if (!m_index.isEmpty() && m_indexPages == pages && m_indexPath == path) {
        return;
    }
    m_index = AiSearch::Index();
    m_index.build(m_document, nullptr);
    m_indexPages = pages;
    m_indexPath = path;
}

void AiBridge::handleAsk(QTcpSocket *socket, const QByteArray &body)
{
    Okular::AiServiceSettings settings;
    settings.load();
    const QString base = settings.baseUrl().trimmed();
    const QString model = settings.modelId().trimmed();
    if (base.isEmpty() || model.isEmpty()) {
        respondJson(socket, {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("ai-not-configured")}}, 400);
        return;
    }

    const QJsonObject request = QJsonDocument::fromJson(body).object();
    const QJsonArray messages = request.value(QStringLiteral("messages")).toArray();
    if (messages.isEmpty()) {
        respondJson(socket, {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("no messages")}}, 400);
        return;
    }

    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
    }
    QJsonObject payload;
    payload.insert(QStringLiteral("model"), QJsonValue(model));
    payload.insert(QStringLiteral("messages"), QJsonValue(messages));
    payload.insert(QStringLiteral("temperature"), QJsonValue(request.value(QStringLiteral("temperature")).toDouble(0.2)));
    payload.insert(QStringLiteral("reasoning_effort"), QJsonValue(QStringLiteral("low")));

    QString error;
    auto *reply = AiChat::postJson(*m_network, base, QStringLiteral("/chat/completions"), settings.apiKey(), payload, 300000, error);
    if (!reply) {
        respondJson(socket, {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QJsonValue(error)}}, 502);
        return;
    }
    QPointer<QTcpSocket> guard(socket);
    connect(reply, &QNetworkReply::finished, this, [this, reply, guard] {
        QString requestError;
        const QJsonObject response = AiChat::response(reply, requestError);
        reply->deleteLater();
        if (!guard) {
            return;
        }
        if (!requestError.isEmpty()) {
            respondJson(guard, {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QJsonValue(requestError)}}, 502);
            return;
        }
        respondJson(guard, response);
    });
}

void AiBridge::respond(QTcpSocket *socket, int status, const QByteArray &contentType, const QByteArray &body)
{
    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(status) + " " + statusText(status) + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += corsHeaders();
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    socket->write(response);
    socket->disconnectFromHost();
}

void AiBridge::respondJson(QTcpSocket *socket, const QJsonObject &object, int status)
{
    respond(socket, status, "application/json; charset=utf-8", QJsonDocument(object).toJson(QJsonDocument::Compact));
}
