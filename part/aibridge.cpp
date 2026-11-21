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
#include <QMap>
#include <QSet>
#include <QUuid>
#include <algorithm>

namespace
{
const QColor highlightColor(96, 165, 250);
}

AiBridge::AiBridge(Okular::Document *document, Sender sender, QObject *parent)
    : QObject(parent)
    , m_document(document)
    , m_sender(std::move(sender))
    , m_blockScope(QUuid::createUuid().toString(QUuid::Id128).left(16))
{
    if (m_document) {
        m_document->addObserver(this);
    }
}

AiBridge::~AiBridge()
{
    if (m_document) {
        m_document->removeObserver(this);
    }
}

void AiBridge::invalidateDocument()
{
    ++m_revision;
    m_blocks.clear();
    m_pageBlocks.clear();
    m_blockScope = QUuid::createUuid().toString(QUuid::Id128).left(16);
}

void AiBridge::notifySetup(const QList<Okular::Page *> &, int flags)
{
    // A backing-file replacement can retain both the path and page count.
    if (flags & (DocumentChanged | NewLayoutForPages | UrlChanged)) {
        invalidateDocument();
    }
}

void AiBridge::notifyPageChanged(int, int flags)
{
    if (flags & Annotations) {
        invalidateDocument();
    }
}

void AiBridge::setSender(Sender sender)
{
    ++m_session;
    m_sender = std::move(sender);
}

void AiBridge::setPathProvider(std::function<QString()> provider)
{
    m_pathProvider = std::move(provider);
}

void AiBridge::setNavigator(std::function<void(const Okular::DocumentViewport &)> navigator)
{
    m_navigator = std::move(navigator);
}

void AiBridge::setDocumentIdentity(const QString &id, const QString &title)
{
    if (m_documentId == id && m_documentTitle == title)
        return;
    m_documentId = id;
    m_documentTitle = title;
    if (m_sender) {
        QJsonObject params;
        params.insert(QStringLiteral("id"), QJsonValue(id));
        params.insert(QStringLiteral("title"), QJsonValue(title));
        QJsonObject request;
        request.insert(QStringLiteral("method"), QJsonValue(QStringLiteral("documentIdentity")));
        request.insert(QStringLiteral("params"), QJsonValue(params));
        m_sender(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    }
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

void AiBridge::ensureBlocks(int page)
{
    if (!m_document || m_pageBlocks.contains(page)) {
        return;
    }
    m_document->requestTextPage(page);
    QStringList ids;
    const auto blocks = AiSearch::textBlocks(m_document->page(page));
    for (const auto &block : blocks) {
        const QString id = QStringLiteral("b%1p%2l%3").arg(m_blockScope).arg(page + 1).arg(ids.size() + 1);
        ids.append(id);
        m_blocks.insert(id, {page, block});
    }
    m_pageBlocks.insert(page, ids);
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
            document.insert(QStringLiteral("title"), QJsonValue(m_documentTitle.isEmpty() ? QFileInfo(path).fileName() : m_documentTitle));
            document.insert(QStringLiteral("id"), QJsonValue(m_documentId));
            document.insert(QStringLiteral("pages"), QJsonValue(int(m_document->pages())));
            document.insert(QStringLiteral("page"), QJsonValue(int(m_document->currentPage()) + 1));
            document.insert(QStringLiteral("revision"), QString::number(m_revision));
        }
        reply(id, {{QStringLiteral("document"), QJsonValue(document)}});
        return;
    }

    if (params.contains(QStringLiteral("revision")) && params.value(QStringLiteral("revision")).toString() != QString::number(m_revision)) {
        replyError(id, QStringLiteral("document-changed"));
        return;
    }

    const int pageNumber = params.value(QStringLiteral("page")).toInt(0) - 1;
    const bool pageValid = m_document && pageNumber >= 0 && pageNumber < int(m_document->pages());

    if (method == QLatin1String("pageBlocks")) {
        if (!pageValid) {
            replyError(id, QStringLiteral("no such page"));
            return;
        }
        ensureBlocks(pageNumber);
        QJsonArray blocks;
        for (const auto &blockId : m_pageBlocks.value(pageNumber)) {
            blocks.append(QJsonObject{{QStringLiteral("id"), blockId}, {QStringLiteral("text"), m_blocks.value(blockId).block.text}});
        }
        reply(id, {{QStringLiteral("blocks"), blocks}, {QStringLiteral("page"), pageNumber + 1}, {QStringLiteral("revision"), QString::number(m_revision)}});
        return;
    }

    if (method == QLatin1String("pageText")) {
        if (!pageValid) {
            replyError(id, QStringLiteral("no such page"));
            return;
        }
        m_document->requestTextPage(pageNumber);
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
        int targetPage = pageNumber;
        QMap<int, QList<Okular::NormalizedRect>> highlights;
        QSet<QString> seen;
        if (params.contains(QStringLiteral("ranges"))) {
            const auto ranges = params.value(QStringLiteral("ranges")).toArray();
            if (!m_document || ranges.isEmpty() || ranges.size() > 64 || params.contains(QStringLiteral("blockIds"))) {
                replyError(id, QStringLiteral("citation-range-invalid"));
                return;
            }
            for (const auto &value : ranges) {
                const auto range = value.toObject();
                const QString startId = range.value(QStringLiteral("startId")).toString();
                const QString endId = range.value(QStringLiteral("endId")).toString();
                if (!m_blocks.contains(startId) || !m_blocks.contains(endId)) {
                    replyError(id, QStringLiteral("citation-unavailable"));
                    return;
                }
                // Copy page numbers before filling intermediate pages can rehash m_blocks.
                const int startPage = m_blocks.value(startId).page;
                const int endPage = m_blocks.value(endId).page;
                const int first = m_pageBlocks.value(startPage).indexOf(startId);
                const int last = m_pageBlocks.value(endPage).indexOf(endId);
                if (startPage > endPage || (startPage == endPage && first > last)) {
                    replyError(id, QStringLiteral("citation-range-invalid"));
                    return;
                }
                // Expand in the text layer's reading order, never by ID string sorting
                // or by a bounding rectangle (which would paint unrelated columns).
                for (int page = startPage; page <= endPage; ++page) {
                    ensureBlocks(page);
                    const auto ids = m_pageBlocks.value(page);
                    const int begin = page == startPage ? first : 0;
                    const int end = page == endPage ? last : ids.size() - 1;
                    for (int index = begin; index <= end; ++index) {
                        const auto &blockId = ids[index];
                        if (seen.contains(blockId)) continue;
                        if (seen.size() >= 4096) {
                            replyError(id, QStringLiteral("citation-range-invalid"));
                            return;
                        }
                        if (seen.isEmpty()) targetPage = page;
                        seen.insert(blockId);
                        highlights[page].append(m_blocks.value(blockId).block.rectangle);
                    }
                }
            }
        } else if (params.contains(QStringLiteral("blockIds"))) {
            const auto ids = params.value(QStringLiteral("blockIds")).toArray();
            if (ids.isEmpty() || ids.size() > 256 || !m_document) {
                replyError(id, QStringLiteral("citation-unavailable"));
                return;
            }
            for (const auto &value : ids) {
                const QString blockId = value.toString();
                const auto found = m_blocks.constFind(blockId);
                // Validate the whole citation before changing highlights or viewport.
                if (found == m_blocks.cend()) {
                    replyError(id, QStringLiteral("citation-unavailable"));
                    return;
                }
                if (seen.contains(blockId)) continue;
                if (seen.isEmpty()) targetPage = found->page;
                seen.insert(blockId);
                highlights[found->page].append(found->block.rectangle);
            }
        } else if (!pageValid) {
            replyError(id, QStringLiteral("no such page"));
            return;
        }
        m_document->clearPageHighlights(AI_SEARCH_ID);
        for (auto it = highlights.cbegin(); it != highlights.cend(); ++it) {
            m_document->setPageHighlights(AI_SEARCH_ID, it.key(), it.value(), highlightColor);
        }
        const auto rectangles = highlights.value(targetPage);
        // Scroll to the evidence itself, not just to the top of its page.
        Okular::DocumentViewport viewport(targetPage);
        if (!rectangles.isEmpty()) {
            double left = 1.0;
            double top = 1.0;
            double right = 0.0;
            double bottom = 0.0;
            Okular::NormalizedRect previous;
            for (const Okular::NormalizedRect &rectangle : std::as_const(rectangles)) {
                // A continued column starts back at the top. Center the first
                // evidence segment, while still highlighting every selected line.
                if (bottom > 0.0 && (rectangle.bottom < previous.top
                    || rectangle.left >= right || rectangle.right <= left)) {
                    break;
                }
                left = std::min(left, rectangle.left);
                top = std::min(top, rectangle.top);
                right = std::max(right, rectangle.right);
                bottom = std::max(bottom, rectangle.bottom);
                previous = rectangle;
            }
            viewport.rePos.enabled = true;
            viewport.rePos.pos = Okular::DocumentViewport::Center;
            viewport.rePos.normalizedX = (left + right) / 2.0;
            viewport.rePos.normalizedY = (top + bottom) / 2.0;
        }
        if (m_navigator) {
            m_navigator(viewport);
        } else {
            m_document->setViewport(viewport);
        }
        reply(id, {{QStringLiteral("located"), !rectangles.isEmpty()}, {QStringLiteral("page"), targetPage + 1}, {QStringLiteral("pages"), highlights.size()}, {QStringLiteral("blocks"), seen.size()}});
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
        const quint64 session = m_session;
        const quint64 revision = m_revision;
        // Capture the resolved credential, including environment variables, so
        // a retry cannot combine a new credential with the old endpoint.
        const QString apiKey = QString::fromUtf8(networkReply->request().rawHeader("Authorization").mid(7));
        const bool hasEffort = payload.contains(QStringLiteral("reasoning_effort"));
        connect(networkReply, &QNetworkReply::finished, this, [this, networkReply, guard, id, payload, base, apiKey, hasEffort, session, revision] {
            QString requestError;
            const QJsonObject response = AiChat::response(networkReply, requestError);
            networkReply->deleteLater();
            if (!guard || session != m_session) {
                return;
            }
            if (revision != m_revision) {
                replyError(id, QStringLiteral("document-changed"));
                return;
            }
            // A service that does not understand the parameter says so with a
            // 400; retry once with the model default.
            if (!requestError.isEmpty() && hasEffort && requestError.contains(QLatin1String("400"))) {
                QJsonObject retry = payload;
                retry.remove(QStringLiteral("reasoning_effort"));
                QString retryError;
                auto *second = AiChat::postJson(*m_network, base, QStringLiteral("/chat/completions"), apiKey, retry, 300000, retryError);
                if (!second) {
                    replyError(id, retryError);
                    return;
                }
                connect(second, &QNetworkReply::finished, this, [this, second, guard, id, session, revision] {
                    QString secondError;
                    const QJsonObject secondResponse = AiChat::response(second, secondError);
                    second->deleteLater();
                    if (!guard || session != m_session) {
                        return;
                    }
                    if (revision != m_revision) {
                        replyError(id, QStringLiteral("document-changed"));
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
