// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QList>
#include <QRectF>
#include <QString>
class QImage;
class QNetworkReply;
class QNetworkAccessManager;
class QJsonObject;
class QWidget;
namespace AiPageAnalysis
{

struct Region {
    QRectF rectangle;
    QString text;
};
QNetworkReply *requestLayout(QNetworkAccessManager &network, const QString &base, const QString &key, const QImage &image, QString &error);
QList<Region> layoutViews(const QJsonObject &response, QString &error);
QList<Region> reviewOcr(QWidget *parent, int pageNumber, const QImage &image);
}
