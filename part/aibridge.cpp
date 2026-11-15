// SPDX-License-Identifier: GPL-2.0-or-later
#include "aibridge.h"

#include "aichat.h"
#include "aiservicesettings.h"
#include "core/document.h"
#include "core/page.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <algorithm>

namespace
{
const QColor highlightColor(96, 165, 250);
}

AiBridge::AiBridge(Okular::Document *document, Sender sender, QObject *parent)
    : QObject(parent)
    , m_document(document)
    , m_sender(std::move(sender))
{
}

void AiBridge::setSender(Sender sender)
{
    m_sender = std::move(sender);
}

void AiBridge::setPathProvider(std::function<QString()> provider)
{
    m_pathProvider = std::move(provider);
}

QString AiBridge::currentPath() const
{
    return m_pathProvider ? m_pathProvider() : QString();
}

void AiBridge::receive(const QByteArray &line)
{
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    const QJsonObject request = QJsonDocument::fromJson(trimmed).object();
    if (!request.isEmpty()) {
        dispatch(request);
    }
}

void AiBridge::reply(int id, const QJsonObject &result)
{
    if (!m_sender) {
        return;
    }
    QJsonObject response;
    response.insert(QStringLiteral("id"), QJsonValue(id));
    response.insert(QStringLiteral("ok"), QJsonValue(true));
    response.insert(QStringLiteral("result"), QJsonValue(result));
    m_sender(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
}

void AiBridge::replyError(int id, const QString &error)
{
    if (!m_sender) {
        return;
    }
    QJsonObject response;
    response.insert(QStringLiteral("id"), QJsonValue(id));
    response.insert(QStringLiteral("ok"), QJsonValue(false));
    response.insert(QStringLiteral("error"), QJsonValue(error));
    m_sender(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
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

void AiBridge::dispatch(const QJsonObject &request)
{
    const int id = request.value(QStringLiteral("id")).toInt();
    const QString method = request.value(QStringLiteral("method")).toString();
    const QJsonObject params = request.value(QStringLiteral("params")).toObject();

    if (method == QLatin1String("ping")) {
        reply(id, {});
        return;
    }

    if (method == QLatin1String("state")) {
        QJsonObject document;
        if (m_document && m_document->pages() > 0) {
            const QString path = currentPath();
            document.insert(QStringLiteral("path"), QJsonValue(path));
            document.insert(QStringLiteral("title"), QJsonValue(QFileInfo(path).fileName()));
            document.insert(QStringLiteral("pages"), QJsonValue(int(m_document->pages())));
            document.insert(QStringLiteral("page"), QJsonValue(int(m_document->currentPage()) + 1));
        }
        reply(id, {{QStringLiteral("document"), QJsonValue(document)}});
        return;
    }

    const int pageNumber = params.value(QStringLiteral("page")).toInt(0) - 1;
    const bool pageValid = m_document && pageNumber >= 0 && pageNumber < int(m_document->pages());

    if (method == QLatin1String("pageText")) {
        if (!pageValid) {
            replyError(id, QStringLiteral("no such page"));
            return;
        }
        const Okular::Page *page = m_document->page(pageNumber);
        reply(id, {{QStringLiteral("text"), QJsonValue(page ? page->text(nullptr) : QString())}});
        return;
    }

    if (method == QLatin1String("pageImage")) {
        if (!pageValid) {
            replyError(id, QStringLiteral("no such page"));
            return;
        }
        const int dpi = std::clamp(params.value(QStringLiteral("dpi")).toInt(140), 36, 600);
        QString error;
        const QImage image = m_document->renderToImage(pageNumber, dpi, false, &error);
        if (image.isNull()) {
            replyError(id, error);
            return;
        }
        // JPEG keeps the request small: a page in PNG is often several times
        // larger, and every image is sent on each turn.
        const QString format = params.value(QStringLiteral("format")).toString().toLower();
        const bool jpeg = format != QLatin1String("png");
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        if (jpeg) {
            image.save(&buffer, "JPEG", 85);
        } else {
            image.save(&buffer, "PNG");
        }
        reply(id,
              {{QStringLiteral("mime"), QJsonValue(jpeg ? QStringLiteral("image/jpeg") : QStringLiteral("image/png"))},
               {QStringLiteral("data"), QJsonValue(QString::fromLatin1(bytes.toBase64()))}});
        return;
    }

    if (method == QLatin1String("jump")) {
        if (!pageValid) {
            replyError(id, QStringLiteral("no such page"));
            return;
        }
        QList<Okular::NormalizedRect> rectangles;
        QStringList anchors;
        for (const QJsonValue &value : params.value(QStringLiteral("anchors")).toArray()) {
            const QString candidate = value.toString().trimmed();
            if (!candidate.isEmpty()) {
                anchors.append(candidate);
            }
        }
        const QString single = params.value(QStringLiteral("anchor")).toString().trimmed();
        if (!single.isEmpty()) {
            anchors.prepend(single);
        }
        const QString label = params.value(QStringLiteral("label")).toString().trimmed();
        if (!label.isEmpty()) {
            anchors.append(label);
        }
        anchors.removeDuplicates();
        if (!anchors.isEmpty()) {
            ensureIndex();
            for (const QString &anchor : std::as_const(anchors)) {
                QString located;
                const QList<Okular::NormalizedRect> found = m_index.locate(pageNumber, anchor, &located);
                if (!found.isEmpty()) {
                    rectangles = found.size() == 1 ? m_index.widenToLine(pageNumber, found) : found;
                    break;
                }
            }
        }
        m_document->clearPageHighlights(AI_SEARCH_ID);
        if (!rectangles.isEmpty()) {
            m_document->setPageHighlights(AI_SEARCH_ID, pageNumber, rectangles, highlightColor);
        }
        // Scroll to the evidence itself, not just to the top of its page.
        Okular::DocumentViewport viewport(pageNumber);
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
        reply(id, {{QStringLiteral("located"), QJsonValue(!rectangles.isEmpty())}});
        return;
    }

    if (method == QLatin1String("ask")) {
        Okular::AiServiceSettings settings;
        settings.load();
        const QString base = settings.baseUrl().trimmed();
        const QString model = settings.modelId().trimmed();
        if (base.isEmpty() || model.isEmpty()) {
            replyError(id, QStringLiteral("ai-not-configured"));
            return;
        }
        const QJsonArray messages = params.value(QStringLiteral("messages")).toArray();
        if (messages.isEmpty()) {
            replyError(id, QStringLiteral("no messages"));
            return;
        }
        if (!m_network) {
            m_network = new QNetworkAccessManager(this);
        }
        // Only what every provider accepts; the thinking depth is a setting.
        QJsonObject payload;
        payload.insert(QStringLiteral("model"), QJsonValue(model));
        payload.insert(QStringLiteral("messages"), QJsonValue(messages));
        const QString effort = settings.reasoningEffort().trimmed();
        if (!effort.isEmpty()) {
            payload.insert(QStringLiteral("reasoning_effort"), QJsonValue(effort));
        }

        QString error;
        auto *networkReply = AiChat::postJson(*m_network, base, QStringLiteral("/chat/completions"), settings.apiKey(), payload, 300000, error);
        if (!networkReply) {
            replyError(id, error);
            return;
        }
        QPointer<AiBridge> guard(this);
        const bool hasEffort = payload.contains(QStringLiteral("reasoning_effort"));
        connect(networkReply, &QNetworkReply::finished, this, [this, networkReply, guard, id, payload, base, hasEffort] {
            QString requestError;
            const QJsonObject response = AiChat::response(networkReply, requestError);
            networkReply->deleteLater();
            if (!guard) {
                return;
            }
            // A service that does not understand the parameter says so with a
            // 400; retry once with the model default.
            if (!requestError.isEmpty() && hasEffort && requestError.contains(QLatin1String("400"))) {
                QJsonObject retry = payload;
                retry.remove(QStringLiteral("reasoning_effort"));
                Okular::AiServiceSettings settings;
                settings.load();
                QString retryError;
                auto *second = AiChat::postJson(*m_network, base, QStringLiteral("/chat/completions"), settings.apiKey(), retry, 300000, retryError);
                if (!second) {
                    replyError(id, retryError);
                    return;
                }
                connect(second, &QNetworkReply::finished, this, [this, second, guard, id] {
                    QString secondError;
                    const QJsonObject secondResponse = AiChat::response(second, secondError);
                    second->deleteLater();
                    if (!guard) {
                        return;
                    }
                    if (!secondError.isEmpty()) {
                        replyError(id, secondError);
                        return;
                    }
                    reply(id, secondResponse);
                });
                return;
            }
            if (!requestError.isEmpty()) {
                replyError(id, requestError);
                return;
            }
            reply(id, response);
        });
        return;
    }

    replyError(id, QStringLiteral("unknown method: %1").arg(method));
}
