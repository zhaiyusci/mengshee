// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QJsonArray>
#include <QJsonObject>
class QNetworkAccessManager;
class QNetworkReply;
namespace AiChat
{
QNetworkReply *post(QNetworkAccessManager &network, const QString &base, const QString &model, const QString &key, const QJsonArray &messages, int timeoutMs, QString &error);
QNetworkReply *postJson(QNetworkAccessManager &network, const QString &base, const QString &path, const QString &key, const QJsonObject &payload, int timeoutMs, QString &error);
QJsonObject response(QNetworkReply *reply, QString &error);
QString answer(QNetworkReply *reply, QString &error);
}
