// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../../part/part.h"
#include <KActionCollection>
#include <KConfigGroup>
#include <KCoreConfigSkeleton>
#include <KSharedConfig>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPdfWriter>
#include <QRadioButton>
#include <QComboBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <core/document.h>
#include <core/generator.h>
#include <core/readingview.h>

namespace Okular
{
class PartTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void analyzePage_data()
    {
        QTest::addColumn<bool>("ocr");
        QTest::addColumn<bool>("invalid");
        QTest::addColumn<bool>("layoutKeySet");
        QTest::newRow("reading-order") << false << false << true;
        QTest::newRow("reject-generated-views") << false << true << true;
        QTest::newRow("reading-order-no-key") << false << false << false;
        QTest::newRow("content-mathpix-both") << true << false << true;
        QTest::newRow("content-mathpix-views") << true << false << true;
        QTest::newRow("content-glm-both") << true << false << true;
        QTest::newRow("content-glm-views") << true << false << true;
        QTest::newRow("ocr-word") << true << false << true;
        QTest::newRow("mathpix-ocr") << true << false << true;
        QTest::newRow("dedicated-ocr") << true << false << true;
        QTest::newRow("chat-views") << false << false << true;
        QTest::newRow("invalid-box") << true << true << true;
    }

    void analyzePage()
    {
        const QByteArray tag(QTest::currentDataTag());
        const bool unified = tag.startsWith("content-");
        const bool viewsOnly = unified && tag.endsWith("-views");
        const bool mathpix = tag.contains("mathpix");
        const bool dedicatedOcr = unified || mathpix || QByteArray(QTest::currentDataTag()) == "dedicated-ocr";
        const bool chatViews = QByteArray(QTest::currentDataTag()) == "chat-views";
        QFETCH(bool, ocr);
        QFETCH(bool, invalid);
        QFETCH(bool, layoutKeySet);
        QStandardPaths::setTestModeEnabled(true);
        QTemporaryDir directory;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        auto config = KSharedConfig::openConfig(QStringLiteral("mengshee-aiservicerc"));
        KConfigGroup ai(config, QStringLiteral("Service"));
        ai.writeEntry("BaseUrl", QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()));
        ai.writeEntry("ModelId", QStringLiteral("fixture"));
        ai.writeEntry("UseMathpixForOcr", mathpix);
        ai.writeEntry("MathpixAppId", QStringLiteral("fixture-app"));
        ai.sync();
        QString chatKey, layoutKey, mathpixKey;
        KCoreConfigSkeleton keys(config);
        keys.setCurrentGroup(QStringLiteral("Service"));
        keys.addItemPassword(QStringLiteral("ApiKey"), chatKey);
        keys.addItemPassword(QStringLiteral("LayoutApiKey"), layoutKey);
        keys.addItemPassword(QStringLiteral("MathpixAppKey"), mathpixKey);
        mathpixKey = QStringLiteral("fixture-mathpix-key");
        chatKey = QStringLiteral("test-chat-token");
        layoutKey = layoutKeySet ? QStringLiteral("test-layout-token") : QString();
        QVERIFY(keys.save());

        const QString replay = mathpix && !unified ? qEnvironmentVariable("MENGSHEE_TEST_MATHPIX_FIXTURE") : QString();
        int requests = 0;
        bool validRequest = false;
        const auto content = invalid ? R"({"items":[{"box":[200,0,100,500],"text":"bad"}]})" : ocr ? R"({"items":[{"box":[50,100,450,150],"text":"示例 test"}]})" : R"({"items":[{"box":[500,100,950,900]},{"box":[50,100,450,900]}]})";
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket, body = QByteArray()]() mutable {
                body += socket->readAll();
                const auto headerEnd = body.indexOf("\r\n\r\n");
                if (headerEnd < 0)
                    return;
                const auto root = QJsonDocument::fromJson(body.mid(headerEnd + 4)).object();
                if (root.isEmpty())
                    return;
                ++requests;
                QString dataUrl;
                QJsonObject response;
                if (mathpix) {
                    dataUrl = root.value(QStringLiteral("src")).toString();
                    validRequest = body.startsWith("POST /v3/text ") && root.value(QStringLiteral("enable_document_layout")).toBool();
                    if (!replay.isEmpty()) {
                        QFile fixture(replay + QStringLiteral(".json"));
                        QVERIFY(fixture.open(QIODevice::ReadOnly));
                        response = QJsonDocument::fromJson(fixture.readAll()).object();
                    } else {
                        response = QJsonDocument::fromJson(R"json({"lines_json":{"page_width":1000,"page_height":1000,"lines":[{"text":"","type":"abstract"},{"text":"Re\u0000sult\r\n\\(\tK \\)","text_display":"WRONG","cnt":[[50,100],[450,100],[450,150],[50,150]]}]}})json").object();
                    }
                } else if ((ocr && !dedicatedOcr) || chatViews) {
                    const auto parts = root[QStringLiteral("messages")].toArray().first().toObject()[QStringLiteral("content")].toArray();
                    dataUrl = parts.last().toObject()[QStringLiteral("image_url")].toObject()[QStringLiteral("url")].toString();
                    validRequest = body.startsWith("POST /v1/chat/completions ") && root[QStringLiteral("model")].toString() == QStringLiteral("fixture");
                    const QJsonObject message {{QStringLiteral("content"), chatViews ? QStringLiteral(R"({"items":[{"box":[50,100,450,300]},{"box":[530,100,950,300]},{"box":[50,340,950,620]},{"box":[50,660,450,900]},{"box":[530,660,950,900]}]})") : QString::fromUtf8(content)}};
                    response.insert(QStringLiteral("choices"), QJsonArray {QJsonObject {{QStringLiteral("message"), message}}});
                } else {
                    dataUrl = root.value(QStringLiteral("file")).toString();
                    validRequest = body.startsWith("POST /v1/layout_parsing ") && root.value(QStringLiteral("model")).toString() == QStringLiteral("glm-ocr");
                    QJsonArray blocks;
                    for (const auto &box : {QJsonArray {50, 100, 470, 300}, QJsonArray {530, 100, 950, 300}, QJsonArray {50, 340, 950, 620}, QJsonArray {50, 660, 470, 900}, QJsonArray {530, 660, 950, 900}}) {
                        blocks.append(QJsonObject {{QStringLiteral("width"), 1000}, {QStringLiteral("height"), 1000}, {QStringLiteral("bbox_2d"), box}, {QStringLiteral("label"), QStringLiteral("text")}});
                    }
                    QJsonArray pages;
                    if (dedicatedOcr) {
                        blocks = QJsonArray{QJsonObject{{QStringLiteral("width"), 1000}, {QStringLiteral("height"), 1000}, {QStringLiteral("label"), QStringLiteral("text")},
                            {QStringLiteral("content"), QStringLiteral("<p>示<b>例</b></p><p>第二行</p>")}, {QStringLiteral("bbox_2d"), QJsonArray{50,100,450,150}}}};
                    }
                    pages.append(blocks);
                    response.insert(QStringLiteral("layout_details"), pages);
                }
                const auto headers = body.first(headerEnd).toLower();
                const QByteArray expectedKey = ((ocr && !dedicatedOcr) || chatViews) ? "test-chat-token" : "test-layout-token";
                if (mathpix)
                    validRequest &= headers.contains("app_id: fixture-app") && headers.contains("app_key: fixture-mathpix-key") && !headers.contains("authorization:");
                else if (ocr || layoutKeySet)
                    validRequest &= headers.contains(QByteArray("authorization: bearer ").append(expectedKey));
                else
                    validRequest &= !headers.contains("authorization:");
                validRequest &= !QImage::fromData(QByteArray::fromBase64(dataUrl.section(u',', 1).toLatin1()), "JPEG").isNull();
                const QByteArray data = QJsonDocument(response).toJson();
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " + QByteArray::number(data.size()) + "\r\n\r\n" + data);
                socket->disconnectFromHost();
            });
        });

        const QString file = directory.filePath(QStringLiteral("test.pdf"));
        {
            QPdfWriter writer(file);
            QPainter painter(&writer);
            painter.drawText(100, 500, QStringLiteral("AI test"));
            if (!ocr || mathpix) {
                QVERIFY(writer.newPage());
                painter.drawText(100, 500, QStringLiteral("Second page"));
            }
        }
        Part part(nullptr, {});
        part.m_glmOcrBaseUrl = ai.readEntry("BaseUrl", QString());
        part.m_mathpixBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        QVERIFY(part.openUrl(QUrl::fromLocalFile(replay.isEmpty() ? file : replay + QStringLiteral(".pdf"))));
        auto *doc = part.m_document;
        const QImage before = doc->renderToImage(0, 72, false);
        QVERIFY(!before.isNull());
        doc->setRotation(Rotation90);
        part.setEditingMode(EditingMode::ContentRecognition);
        auto *action = part.actionCollection()->action(unified ? QStringLiteral("recognize_content") : dedicatedOcr ? QStringLiteral("advanced_glm_ocr") : chatViews ? QStringLiteral("view_chat_reading_views") : ocr ? QStringLiteral("advanced_ai_ocr") : QStringLiteral("view_ai_reading_views"));
        QVERIFY(action && action->isEnabled() && action->isVisible());
        bool sent = false, applied = false, missingKeyBlocked = false;
        QTimer drive;
        QElapsedTimer clock;
        clock.start();
        connect(&drive, &QTimer::timeout, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            if (clock.elapsed() > 6000) {
                dialog->reject();
                return;
            }
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            if (!buttons)
                return;
            if (unified && dialog->objectName() == QStringLiteral("confirmReadingViewsDialog")) {
                QVERIFY(doc->readingViews(0).isEmpty());
                applied = true;
                dialog->accept();
                return;
            }
            if (!ocr) {
                if (dialog->objectName() == QStringLiteral("generateReadingViewsDialog")) {
                    auto *all = dialog->findChild<QRadioButton *>(QStringLiteral("generateViewsAllPages"));
                    QVERIFY(all);
                    all->setChecked(true);
                    for (auto *button : buttons->buttons()) {
                        if (buttons->buttonRole(button) != QDialogButtonBox::AcceptRole) continue;
                        if (!layoutKeySet) {
                            missingKeyBlocked = !button->isEnabled();
                            dialog->reject();
                        } else {
                            sent = true;
                            button->click();
                        }
                        return;
                    }
                } else if (dialog->objectName() == QStringLiteral("confirmReadingViewsDialog")) {
                    QVERIFY(doc->readingViews(0).isEmpty() && doc->readingViews(1).isEmpty());
                    auto *pages = dialog->findChild<QComboBox *>();
                    QVERIFY(pages);
                    QCOMPARE(pages->count(), 2);
                    pages->setCurrentIndex(1);
                    if (invalid) dialog->reject();
                    else {
                        applied = true;
                        dialog->accept();
                    }
                }
                return;
            }
            if (dialog->objectName() == QStringLiteral("recognizeTextDialog")) {
                if (unified) {
                    auto *engines = dialog->findChild<QComboBox *>(QStringLiteral("recognitionEngine"));
                    engines->setCurrentText(mathpix ? QStringLiteral("Mathpix") : QStringLiteral("GLM-OCR"));
                    auto *text = dialog->findChild<QCheckBox *>(QStringLiteral("recognitionTextOutput"));
                    QVERIFY(!text->isChecked()); // Native text on the fixture page.
                    QVERIFY(dialog->findChild<QCheckBox *>(QStringLiteral("recognitionViewOutput"))->isChecked());
                    text->setChecked(!viewsOnly);
                    dialog->findChild<QCheckBox *>(QStringLiteral("skipRecognizedText"))->setChecked(false);
                    sent = true;
                    dialog->accept();
                    return;
                }
                const auto options = dialog->findChildren<QCheckBox *>();
                auto skip = std::find_if(options.begin(), options.end(), [](QCheckBox *box) { return box->isVisible() && box->isChecked(); });
                QVERIFY(skip != options.end());
                (*skip)->setChecked(false);
                options.last()->setChecked(!mathpix);
                if (mathpix && replay.isEmpty()) dialog->findChild<QRadioButton *>()->setChecked(true);
                sent = true;
                applied = mathpix;
                dialog->accept();
                return;
            }
            if (invalid && validRequest) {
                dialog->accept();
                return;
            }
            for (auto *button : buttons->buttons()) {
                if (!sent && buttons->buttonRole(button) == QDialogButtonBox::ActionRole) {
                    if (!ocr && !layoutKeySet && !button->isEnabled()) {
                        missingKeyBlocked = true;
                        dialog->reject();
                        return;
                    }
                    sent = true;
                    button->click();
                    return;
                }
                if (sent && buttons->buttonRole(button) == QDialogButtonBox::AcceptRole && button->isEnabled()) {
                    applied = true;
                    button->click();
                    return;
                }
                if (sent && invalid && validRequest && buttons->buttonRole(button) == QDialogButtonBox::ActionRole && button->isEnabled()) {
                    dialog->reject();
                    return;
                }
            }
        });
        drive.start(20);
        action->trigger();
        drive.stop();
        if (!ocr && !layoutKeySet) {
            QVERIFY(missingKeyBlocked);
            QVERIFY(!server.hasPendingConnections());
            QVERIFY(!sent && !applied && !validRequest);
            return;
        }
        QVERIFY(validRequest);
        QCOMPARE(requests, unified ? 1 : (ocr && (!mathpix || !replay.isEmpty())) ? 1 : 2);
        QCOMPARE(applied, !invalid);
        QList<OcrTextWord> words;
        QString error;
        QVERIFY(doc->readOcrTextLayer(0, &words, &error));
        if (!replay.isEmpty()) {
            QVERIFY(!words.isEmpty());
            QVERIFY(doc->saveChanges(replay + QStringLiteral("-searchable.pdf"), &error));
            QCOMPARE(doc->renderToImage(0, 72, false), before);
            return;
        }
        if (invalid) {
            QVERIFY(words.isEmpty());
            if (!ocr) QVERIFY(doc->readingViews(1).isEmpty());
            QVERIFY(!doc->canUndo());
            return;
        }
        if (unified) {
            QVERIFY(!doc->readingViews(0).isEmpty());
            QCOMPARE(words.isEmpty(), viewsOnly);
            QCOMPARE(doc->renderToImage(0, 72, false), before);
            QVERIFY(doc->canUndo());
            doc->undo();
            QVERIFY(doc->readingViews(0).isEmpty());
            QVERIFY(doc->readOcrTextLayer(0, &words, &error));
            QVERIFY(words.isEmpty());
            return;
        }
        if (ocr) {
            QCOMPARE(words.size(), dedicatedOcr && !mathpix ? 2 : 1);
            const QRectF expected(QPointF(.05, .1), QPointF(.45, .15));
            const auto actual = words.first().rectangle;
            // The embedded fallback font rounds its metrics. Allow 1/20 of a unit on the OCR service's 1000-unit grid.
            constexpr double tolerance = 0.05 / 1000;
            if (mathpix) {
                QCOMPARE(words.first().text, QStringLiteral("Result \\( K \\)"));
                QVERIFY(actual.adjusted(-tolerance,-tolerance,tolerance,tolerance).contains(expected));
            } else if (dedicatedOcr) {
                QCOMPARE(words.first().text, QStringLiteral("示例"));
                QCOMPARE(words.last().text, QStringLiteral("第二行"));
                QVERIFY(actual.height() < expected.height() * .7);
                QVERIFY(words.last().rectangle.top() > actual.top());
            } else {
                QCOMPARE(words.first().text, QStringLiteral("示例 test"));
                QVERIFY2(actual.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(expected), qPrintable(QDebug::toString(actual)));
                QVERIFY(expected.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(actual));
            }
            QCOMPARE(doc->renderToImage(0, 72, false), before);
        } else {
            QCOMPARE(doc->readingViews(1).size(), 5);
            const auto views = doc->readingViews(0);
            QCOMPARE(views.size(), 5);
            QVERIFY(views[0].rectangle.right < views[1].rectangle.left);
            QVERIFY(views[1].rectangle.bottom < views[2].rectangle.top);
            QVERIFY(views[2].rectangle.right > .94);
            QVERIFY(views[2].rectangle.bottom < views[3].rectangle.top);
            QVERIFY(views[3].rectangle.right < views[4].rectangle.left);
            QCOMPARE(views[1].number, 2);
        }
        if (mathpix) {
            QList<OcrTextWord> second;
            QVERIFY(doc->readOcrTextLayer(1, &second, &error));
            QCOMPARE(second.size(), 1);
        }
        QVERIFY(doc->canUndo());
        doc->undo();
        if (mathpix) {
            QList<OcrTextWord> second;
            QVERIFY(doc->readOcrTextLayer(1, &second, &error));
            QVERIFY(second.isEmpty());
        }
        if (!ocr) QVERIFY(doc->readingViews(1).isEmpty());
        QVERIFY(doc->readingViews(0).isEmpty());
        QVERIFY(doc->readOcrTextLayer(0, &words, &error));
        QVERIFY(words.isEmpty());
    }
};
}
QTEST_MAIN(Okular::PartTest)
#include "aipageanalysistest.moc"
