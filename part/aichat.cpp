// SPDX-License-Identifier: GPL-2.0-or-later
#include "aichat.h"
#include <KLocalizedString>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

QNetworkReply *AiChat::post(QNetworkAccessManager &network, const QString &base, const QString &model, const QString &key, const QJsonArray &messages, int timeoutMs, QString &error)
{
    if (model.trimmed().isEmpty()) {
        error = i18n("Enter the model name supplied by the service.");
        return nullptr;
    }
    return postJson(network, base, QStringLiteral("/chat/completions"), key, {{QStringLiteral("model"), model.trimmed()}, {QStringLiteral("messages"), messages}}, timeoutMs, error);
}

QNetworkReply *AiChat::postJson(QNetworkAccessManager &network, const QString &base, const QString &path, const QString &key, const QJsonObject &payload, int timeoutMs, QString &error)
{
    QUrl url(base.trimmed(), QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty() || (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http")) || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()) {
        error = i18n("Enter an HTTP or HTTPS API base URL.");
        return nullptr;
    }
    QByteArray token = key.trimmed().toUtf8();
    if (token.startsWith('$')) {
        const auto variable = token.mid(1);
        token = qgetenv(variable.constData()).trimmed();
        if (token.isEmpty()) {
            error = i18n("The API key environment variable is empty or unset.");
            return nullptr;
        }
    }
    if (token.contains('\r') || token.contains('\n') || token.contains('\0')) {
        error = i18n("The API key contains an invalid character.");
        return nullptr;
    }
    url = url.adjusted(QUrl::StripTrailingSlash);
    url.setPath(url.path(QUrl::FullyEncoded) + path, QUrl::StrictMode);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!token.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + token);
    }
    auto *reply = network.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    auto *idle = new QTimer(reply);
    idle->setSingleShot(true);
    idle->setInterval(timeoutMs);
    QObject::connect(reply, &QNetworkReply::readyRead, idle, [idle] { idle->start(); });
    QObject::connect(reply, &QNetworkReply::uploadProgress, idle, [idle] { idle->start(); });
    QObject::connect(reply, &QNetworkReply::finished, idle, &QTimer::stop);
    QObject::connect(idle, &QTimer::timeout, reply, [reply, timeoutMs] {
        reply->setProperty("idleTimeoutMs", timeoutMs);
        reply->abort();
    });
    idle->start();
    return reply;
}

QJsonObject AiChat::response(QNetworkReply *reply, QString &error)
{
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->property("idleTimeoutMs").isValid()) {
        error = i18n("No data was transferred for %1 seconds.", reply->property("idleTimeoutMs").toInt() / 1000);
    } else if (code && (code < 200 || code >= 300)) {
        const QByteArray body = reply->read(16384);
        const auto object = QJsonDocument::fromJson(body).object();
        QString detail = object.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
        if (detail.isEmpty()) {
            detail = object.value(QStringLiteral("message")).toString();
        }
        if (detail.isEmpty() && reply->header(QNetworkRequest::ContentTypeHeader).toString().startsWith(QLatin1String("text/plain"), Qt::CaseInsensitive)) {
            detail = QString::fromUtf8(body);
        }
        const auto token = reply->request().rawHeader("Authorization").mid(7);
        if (!token.isEmpty()) {
            detail.replace(QString::fromUtf8(token), QStringLiteral("[redacted]"));
        }
        detail = detail.simplified().left(500);
        error = detail.isEmpty() ? i18n("The service returned HTTP %1.", code) : i18n("The service returned HTTP %1. %2", code, detail);
    } else if (reply->error() != QNetworkReply::NoError) {
        error = reply->errorString();
    } else {
        return QJsonDocument::fromJson(reply->readAll()).object();
    }
    return {};
}

QString AiChat::answer(QNetworkReply *reply, QString &error)
{
    const auto object = response(reply, error);
    if (error.isEmpty()) {
        const auto choices = object.value(QStringLiteral("choices")).toArray();
        const auto choice = choices.isEmpty() ? QJsonObject() : choices.first().toObject();
        const auto content = choice.value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
        if (choice.value(QStringLiteral("finish_reason")).toString() == QLatin1String("length")) {
            error = i18n("The model reached its output limit.");
        } else if (!content.trimmed().isEmpty()) {
            return content;
        } else {
            error = i18n("The service did not return a chat completion.");
        }
    }
    return {};
}
