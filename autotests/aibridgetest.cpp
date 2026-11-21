// SPDX-License-Identifier: GPL-2.0-or-later
// Generated PDFs, loopback services and fake credentials only.
#undef QT_NO_CAST_FROM_ASCII
#undef QT_NO_CAST_FROM_BYTEARRAY
#undef QT_NO_CAST_TO_ASCII
#include "part/aibridge.h"
#include "aiservicesettings.h"
#include "core/document.h"
#include "core/page.h"
#include "settings_core.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMimeDatabase>
#include <QNetworkProxy>
#include <QPainter>
#include <QPdfWriter>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTemporaryDir>
#include <iostream>
#include <functional>
#include <stdexcept>

static void require(bool yes, const char *message) { if (!yes) throw std::runtime_error(message); }
static void waitFor(const std::function<bool()> &condition) {
    QElapsedTimer clock; clock.start();
    while (!condition() && clock.elapsed() < 10000) { QCoreApplication::processEvents(); QThread::msleep(5); }
    require(condition(), "timed out");
}
struct Server : QTcpServer {
    struct Request { QTcpSocket *socket; QByteArray raw; };
    QList<Request> requests;
    Server() {
        require(listen(QHostAddress::LocalHost), "listen failed");
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    if (socket->property("done").toBool()) return;
                    QByteArray buffer = socket->property("buffer").toByteArray() + socket->readAll();
                    socket->setProperty("buffer", buffer);
                    const int headerEnd = buffer.indexOf("\r\n\r\n");
                    if (headerEnd < 0) return;
                    int length = 0;
                    for (const auto &line : buffer.left(headerEnd).split('\n'))
                        if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                    if (buffer.size() < headerEnd + 4 + length) return;
                    socket->setProperty("done", true);
                    requests.append({socket, buffer});
                });
            }
        });
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1/v1").arg(serverPort()); }
    void respond(int index, int status, QJsonObject body) {
        QByteArray json = QJsonDocument(body).toJson(QJsonDocument::Compact);
        auto *socket = requests.at(index).socket;
        socket->write("HTTP/1.1 " + QByteArray::number(status) + " Result\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json);
        socket->disconnectFromHost();
    }
};
static QJsonObject completion(const QString &answer) {
    return {{"choices", QJsonArray{QJsonObject{{"message", QJsonObject{{"role", "assistant"}, {"content", answer}}}}}}};
}
static void send(AiBridge &bridge, int id, const QString &method, QJsonObject params = {}) {
    bridge.receive(QJsonDocument(QJsonObject{{"id", id}, {"method", method}, {"params", params}}).toJson(QJsonDocument::Compact));
}
static QJsonObject askParams(const QString &question) {
    return {{"messages", QJsonArray{QJsonObject{{"role", "user"}, {"content", question}}}}};
}
static void pdf(const QString &path, const QString &phrase) {
    QPdfWriter writer(path); writer.setResolution(72);
    QPainter painter(&writer); require(painter.isActive(), "PDF painter failed");
    for (int i = 0; i < 3; ++i) {
        if (i) require(writer.newPage(), "new page failed");
        painter.drawText(80, 100, phrase + QStringLiteral(" page %1").arg(i + 1));
        painter.drawText(80, 160, QStringLiteral("Second evidence line"));
        painter.drawText(400, 160, QStringLiteral("Other column"));
        painter.drawText(295, 710, QString::number(i + 1));
    }
}
static QString answerOf(const QJsonObject &value) {
    return value["result"].toObject()["choices"].toArray()[0].toObject()["message"].toObject()["content"].toString();
}
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    app.setApplicationName("MengsheeAiRepro20261121");
    QStandardPaths::setTestModeEnabled(true);
    Okular::SettingsCore::instance(QStringLiteral("mengshee-ai-repro-20261121"));
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    QJsonArray reports;
    try {
        QTemporaryDir directory((argc == 3 ? QFileInfo(QString::fromLocal8Bit(argv[1])).absolutePath() : QDir::tempPath()) + QStringLiteral("/ai-bridge-XXXXXX"));
        require(directory.isValid(), "temporary directory failed");
        const QString fixture = directory.filePath("fixture.pdf");
        pdf(fixture, "Original alpha evidence");
        Okular::Document document(nullptr);
        auto open = [&] { return document.openDocument(fixture, QUrl::fromLocalFile(fixture), QMimeDatabase().mimeTypeForFile(fixture)) == Okular::Document::OpenSuccess; };
        require(open(), "cannot open fixture PDF");
        QList<QJsonObject> replies;
        AiBridge bridge(&document, [&](const QByteArray &line) { replies.append(QJsonDocument::fromJson(line).object()); });
        bridge.setPathProvider([&] { return fixture; });
        const bool loadedBefore = document.page(2)->hasTextPage();
        send(bridge, 1, "pageText", {{"page", 3}});
        const QString before = replies.last()["result"].toObject()["text"].toString();
        document.requestTextPage(2);
        send(bridge, 2, "pageText", {{"page", 3}});
        const QString after = replies.last()["result"].toObject()["text"].toString();
        require(after.contains("Original alpha evidence"), "text extraction control failed");
        require(!loadedBefore && before == after, "unloaded page text was lost");
        reports.append(QJsonObject{{"issue", "unloaded-page-text"}, {"reproduced", !loadedBefore && before.isEmpty()}, {"hasTextPageBefore", loadedBefore}, {"before", before}, {"afterExplicitExtraction", after}});
        std::cerr << "page-text case complete\n";

        int blockRequest = 100;
        const auto blockId = [&](int page, const QString &fragment) {
            send(bridge, ++blockRequest, "pageBlocks", {{"page", page}});
            for (const auto &value : replies.last()["result"].toObject()["blocks"].toArray()) {
                const auto block = value.toObject();
                if (block["text"].toString().contains(fragment)) return block["id"].toString();
            }
            return QString();
        };
        const QString originalId = blockId(1, "Original alpha evidence");
        require(!originalId.isEmpty() && originalId == blockId(1, "Original alpha evidence"), "block IDs not cached");
        const QString secondId = blockId(1, "Second evidence line");
        const QString columnId = blockId(1, "Other column");
        require(!secondId.isEmpty() && !columnId.isEmpty() && secondId != columnId, "same-row columns merged");
        const QString nextPageId = blockId(2, "Original alpha evidence");
        int navigations = 0;
        Okular::DocumentViewport lastViewport;
        bridge.setNavigator([&](const Okular::DocumentViewport &viewport) { ++navigations; lastViewport = viewport; });
        send(bridge, 3, "jump", {{"blockIds", QJsonArray{originalId, secondId, nextPageId, originalId}}});
        require(replies.last()["result"].toObject()["located"].toBool(), "original citation control failed");
        require(replies.last()["result"].toObject()["pages"].toInt() == 2 && lastViewport.pageNumber == 0, "multi-page citation navigation failed");
        require(lastViewport.rePos.enabled && lastViewport.rePos.normalizedY < .4, "citation includes footer");
        const int navigationsBeforeInvalid = navigations;
        send(bridge, 31, "jump", {{"blockIds", QJsonArray{originalId, "invented"}}});
        require(replies.last()["error"].toString() == "citation-unavailable" && navigations == navigationsBeforeInvalid, "invalid citation navigated or partially accepted");
        send(bridge, 32, "jump", {{"blockIds", QJsonArray{}}});
        require(replies.last()["error"].toString() == "citation-unavailable", "empty IDs accepted");
        send(bridge, 33, "jump", {{"page", 3}});
        require(!replies.last()["result"].toObject()["located"].toBool() && lastViewport.pageNumber == 2, "page-only citation failed");
        reports.append(QJsonObject{{"issue", "block-id-citations"}, {"cachedIds", true}, {"separateColumns", true}, {"multiPage", true}, {"invalidIdRejected", true}, {"pageFallback", true}});

        // Dense two-column fixture: selecting only endpoints must fill all
        // middle lines without including the parallel lines in the other column.
        {
            const QString columnsPath = directory.filePath("columns.pdf");
            {
                QPdfWriter writer(columnsPath); writer.setResolution(72);
                QPainter painter(&writer); painter.setFont(QFont("Arial", 11));
                for (int col = 0; col < 2; ++col)
                    for (int line = 0; line < 5; ++line)
                        painter.drawText(50 + col * 280, 100 + line * 18,
                            QStringLiteral("%1 line %2 evidence text").arg(col ? "RIGHT" : "LEFT").arg(line + 1));
            }
            Okular::Document columns(nullptr);
            require(columns.openDocument(columnsPath, QUrl::fromLocalFile(columnsPath), QMimeDatabase().mimeTypeForFile(columnsPath)) == Okular::Document::OpenSuccess, "columns PDF open failed");
            QList<QJsonObject> results;
            AiBridge ranges(&columns, [&](const QByteArray &line) { results.append(QJsonDocument::fromJson(line).object()); });
            send(ranges, 1, "pageBlocks", {{"page", 1}});
            const auto blocks = results.last()["result"].toObject()["blocks"].toArray();
            require(blocks.size() == 10, "two-column lines split or merged unexpectedly");
            require(blocks[4].toObject()["text"].toString().contains("LEFT line 5") && blocks[5].toObject()["text"].toString().contains("RIGHT line 1"), "column reading order incorrect");
            const auto range = [&](int first, int last) { return QJsonObject{{"startId", blocks[first].toObject()["id"]}, {"endId", blocks[last].toObject()["id"]}}; };
            int moved = 0;
            Okular::DocumentViewport destination;
            ranges.setNavigator([&](const Okular::DocumentViewport &v) { ++moved; destination = v; });
            send(ranges, 2, "jump", {{"ranges", QJsonArray{range(0, 4)}}});
            require(results.last()["result"].toObject()["blocks"].toInt() == 5 && columns.page(0)->hasHighlights(), "middle lines missing from inclusive range");
            require(destination.rePos.normalizedX < .5, "same-column range included other column");
            send(ranges, 3, "jump", {{"ranges", QJsonArray{range(3, 6)}}});
            require(results.last()["result"].toObject()["blocks"].toInt() == 4, "cross-column continuation failed");
            require(destination.rePos.normalizedX < .5, "cross-column navigation should center first segment");
            send(ranges, 4, "jump", {{"ranges", QJsonArray{range(0, 2), range(1, 4)}}});
            require(results.last()["result"].toObject()["blocks"].toInt() == 5, "overlapping ranges duplicated lines");
            const int beforeInvalid = moved;
            send(ranges, 5, "jump", {{"ranges", QJsonArray{range(0, 2), range(6, 3)}}});
            require(results.last()["error"].toString() == "citation-range-invalid" && moved == beforeInvalid && columns.page(0)->hasHighlights(), "invalid range partially mutated view");
            send(ranges, 6, "jump", {{"ranges", QJsonArray{range(4, 4)}}});
            require(results.last()["result"].toObject()["blocks"].toInt() == 1, "single-block range failed");
            send(ranges, 7, "jump", {{"ranges", QJsonArray{QJsonObject{{"startId", "invented"}, {"endId", blocks[2].toObject()["id"]}}}}});
            require(results.last()["error"].toString() == "citation-unavailable", "invented range endpoint accepted");
            reports.append(QJsonObject{{"issue", "inclusive-ranges"}, {"middleLines", true}, {"sameColumn", true}, {"crossColumn", true}, {"overlapDeduplication", true}, {"invalidAtomic", true}});
        }
        send(bridge, 34, "jump", {{"ranges", QJsonArray{QJsonObject{{"startId", originalId}, {"endId", nextPageId}}}}});
        require(replies.last()["result"].toObject()["pages"].toInt() == 2, "cross-page range failed");
        document.closeDocument();
        pdf(fixture, "Replacement beta evidence");
        require(open(), "cannot reopen modified PDF");
        document.requestTextPage(0);
        send(bridge, 4, "pageText", {{"page", 1}});
        const QString replacement = replies.last()["result"].toObject()["text"].toString();
        const QString replacementId = blockId(1, "Replacement beta evidence");
        require(!replacementId.isEmpty() && replacementId != originalId, "replacement reused citation IDs");
        send(bridge, 5, "jump", {{"blockIds", QJsonArray{replacementId}}});
        const bool newLocated = replies.last()["result"].toObject()["located"].toBool();
        send(bridge, 6, "jump", {{"blockIds", QJsonArray{originalId}}});
        const bool oldLocated = replies.last()["result"].toObject()["located"].toBool();
        QList<QJsonObject> freshReplies;
        AiBridge fresh(&document, [&](const QByteArray &line) { freshReplies.append(QJsonDocument::fromJson(line).object()); });
        fresh.setPathProvider([&] { return fixture; });
        send(fresh, 70, "pageBlocks", {{"page", 1}});
        const auto freshBlocks = freshReplies.last()["result"].toObject()["blocks"].toArray();
        const QString freshId = freshBlocks.first().toObject()["id"].toString();
        require(freshId != replacementId, "bridge scope IDs collided");
        send(fresh, 71, "jump", {{"blockIds", QJsonArray{replacementId}}});
        require(freshReplies.last()["error"].toString() == "citation-unavailable", "foreign bridge citation accepted");
        send(fresh, 7, "jump", {{"blockIds", QJsonArray{freshId}}});
        const bool freshLocated = freshReplies.last()["result"].toObject()["located"].toBool();
        require(replacement.contains("Replacement beta evidence") && freshLocated, "changed document control failed");
        require(newLocated && !oldLocated, "citation index was not invalidated");
        reports.append(QJsonObject{{"issue", "stale-citation-index"}, {"reproduced", !newLocated && oldLocated}, {"currentText", replacement}, {"newQuoteLocated", newLocated}, {"deletedOldQuoteLocated", oldLocated}, {"freshBridgeLocatesNewQuote", freshLocated}});
        std::cerr << "index case complete; starting loopback requests\n";

        Server server;
        Repro::configuration = {server.base(), "test-model", "FAKE-KEY-A", ""};
        QList<QJsonObject> oldWindow, newWindow;
        AiBridge chat(nullptr, [&](const QByteArray &line) { oldWindow.append(QJsonDocument::fromJson(line).object()); });
        send(chat, 1, "ask", askParams("OLD QUESTION"));
        waitFor([&] { return server.requests.size() == 1; });
        chat.setSender({});
        chat.setSender([&](const QByteArray &line) { newWindow.append(QJsonDocument::fromJson(line).object()); });
        send(chat, 1, "ask", askParams("NEW QUESTION"));
        waitFor([&] { return server.requests.size() == 2; });
        server.respond(0, 200, completion("OLD ANSWER"));
        server.respond(1, 200, completion("NEW ANSWER"));
        waitFor([&] { return !newWindow.isEmpty(); });
        require(newWindow.size() == 1 && answerOf(newWindow.first()) == "NEW ANSWER" && oldWindow.isEmpty(), "old response reached new session");
        reports.append(QJsonObject{{"issue", "old-response-new-session"}, {"reproduced", false}, {"newWindowReply", newWindow.first()}});

        Server oldService, newService;
        Repro::configuration = {oldService.base(), "model-a", "FAKE-KEY-A", "high"};
        QList<QJsonObject> retryReplies;
        AiBridge retry(nullptr, [&](const QByteArray &line) { retryReplies.append(QJsonDocument::fromJson(line).object()); });
        send(retry, 8, "ask", askParams("configuration retry test"));
        waitFor([&] { return oldService.requests.size() == 1; });
        Repro::configuration = {newService.base(), "model-b", "FAKE-KEY-B", ""};
        oldService.respond(0, 400, {{"error", QJsonObject{{"message", "unsupported reasoning_effort"}}}});
        waitFor([&] { return oldService.requests.size() == 2; });
        const bool wrongKey = oldService.requests[1].raw.contains("Bearer FAKE-KEY-B");
        oldService.respond(1, 200, completion("RETRY ANSWER"));
        waitFor([&] { return retryReplies.size() == 1; });
        require(!wrongKey && oldService.requests[1].raw.contains("Bearer FAKE-KEY-A") && newService.requests.isEmpty(), "retry mixed settings");
        reports.append(QJsonObject{{"issue", "mixed-service-retry"}, {"reproduced", wrongKey}, {"oldServiceReceivedNewFakeKey", wrongKey}, {"oldServiceRequests", oldService.requests.size()}, {"newServiceRequests", newService.requests.size()}, {"settings", "isolated in-memory settings seam; fake credentials only"}});
        // Already-started retries must also stay in their original session.
        Server retrySessionServer;
        Repro::configuration = {retrySessionServer.base(), "model-a", "FAKE-KEY-A", "high"};
        QList<QJsonObject> retrySessionReplies;
        AiBridge retrySession(nullptr, {});
        send(retrySession, 1, "ask", askParams("old retry"));
        waitFor([&] { return retrySessionServer.requests.size() == 1; });
        retrySessionServer.respond(0, 400, {{"error", QJsonObject{{"message", "unsupported reasoning_effort"}}}});
        waitFor([&] { return retrySessionServer.requests.size() == 2; });
        retrySession.setSender([&](const QByteArray &line) { retrySessionReplies.append(QJsonDocument::fromJson(line).object()); });
        send(retrySession, 1, "ask", askParams("new question"));
        waitFor([&] { return retrySessionServer.requests.size() == 3; });
        retrySessionServer.respond(1, 200, completion("STALE RETRY"));
        retrySessionServer.respond(2, 200, completion("NEW SESSION"));
        waitFor([&] { return !retrySessionReplies.isEmpty(); });
        require(retrySessionReplies.size() == 1 && answerOf(retrySessionReplies.first()) == "NEW SESSION", "retry response reached new session");
        reports.append(QJsonObject{{"issue", "old-retry-new-session"}, {"reproduced", false}});

        send(bridge, 20, "state");
        const QString revision = replies.last()["result"].toObject()["document"].toObject()["revision"].toString();
        require(!revision.isEmpty(), "revision missing from state");
        bridge.notifySetup({}, Okular::DocumentObserver::UrlChanged);
        send(bridge, 21, "pageText", {{"page", 1}, {"revision", revision}});
        require(replies.last()["error"].toString() == "document-changed", "stale revision accepted");

        Server changingDocumentServer;
        Repro::configuration = {changingDocumentServer.base(), "model-a", "FAKE-KEY-A", ""};
        send(bridge, 22, "ask", askParams("question before document edit"));
        waitFor([&] { return changingDocumentServer.requests.size() == 1; });
        bridge.notifyPageChanged(0, Okular::DocumentObserver::Annotations);
        const auto repliesBefore = replies.size();
        changingDocumentServer.respond(0, 200, completion("STALE DOCUMENT"));
        waitFor([&] { return replies.size() > repliesBefore; });
        require(replies.last()["error"].toString() == "document-changed", "stale document answer accepted");
        reports.append(QJsonObject{{"issue", "document-change-during-request"}, {"reproduced", false}});
        if (argc == 3) {
            QFile out(QString::fromLocal8Bit(argv[2])); require(out.open(QIODevice::WriteOnly), "report open failed");
            out.write(QJsonDocument(reports).toJson());
        }
        std::cout << QJsonDocument(reports).toJson().constData();
    } catch (const std::exception &error) {
        if (argc == 3) { QFile out(QString::fromLocal8Bit(argv[2])); if (out.open(QIODevice::WriteOnly)) out.write(QJsonDocument(reports).toJson()); }
        std::cerr << error.what() << '\n'; return 1;
    }
    return 0;
}
