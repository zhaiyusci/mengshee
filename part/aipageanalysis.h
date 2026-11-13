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

inline const QString siliconFlowBase = QStringLiteral("https://api.siliconflow.cn/v1");
inline const QString siliconFlowModel = QStringLiteral("deepseek-ai/DeepSeek-OCR");
struct Region;
enum class SiliconTask { Probe, Words, Views };
QList<Region> siliconRegions(const QString &answer, bool views, QString &error);
QNetworkReply *requestSiliconFlowOcr(QNetworkAccessManager &network, const QString &key, const QImage &image, SiliconTask task, QString &error);

struct Region {
    QRectF rectangle;
    QString text;
};
QNetworkReply *requestLayout(QNetworkAccessManager &network, const QString &base, const QString &key, const QImage &image, QString &error);
QNetworkReply *requestChatViews(QNetworkAccessManager &network, const QString &base, const QString &model, const QString &key, const QImage &image, QString &error);
QList<Region> chatRegions(QString answer, QString &error, bool requireText);
QList<Region> layoutTextLines(const QJsonObject &response, const QImage &image, QString &error);
QList<Region> mathpixViews(const QJsonObject &response, QString &error);
QList<Region> layoutViews(const QJsonObject &response, QString &error);
struct OcrService {
    QString base, model, key, appId, error;
    bool silicon = false, mathpix = false, dedicated = false;
};
OcrService ocrService(bool dedicated, const QString &glmBase, const QString &mathpixBase, const QString &engine = {});
bool reviewOcr(QWidget *parent, int pageNumber, const QImage &image, const OcrService &service, bool review, QList<Region> &regions, QString &error, bool needText = true, QList<Region> *views = nullptr);
}
