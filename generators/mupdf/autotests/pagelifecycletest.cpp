// SPDX-License-Identifier: GPL-2.0-or-later
#include <core/document.h>
#include <core/annotations.h>
#include <core/action.h>
#include "settings_core.h"
#include <core/generator.h>
#include <core/observer.h>
#include <core/page.h>
#include <QMimeDatabase>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QTest>
#include <QScopeGuard>

class PageLifecycleTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void tiledZoomRequestsNewResolution() {
        Okular::SettingsCore::instance(QStringLiteral("mupdf-page-lifecycle-test"));
        QTemporaryDir directory;
        const auto file = directory.filePath(QStringLiteral("zoom.pdf"));
        { QPdfWriter writer(file); QPainter painter(&writer); painter.drawText(100, 100, QStringLiteral("Sharp text")); }
        Okular::Document document(nullptr);
        QCOMPARE(document.openDocument(file, QUrl::fromLocalFile(file), QMimeDatabase().mimeTypeForName(QStringLiteral("application/pdf"))), Okular::Document::OpenSuccess);
        Okular::DocumentObserver observer;
        document.addObserver(&observer);
        const auto cleanup = qScopeGuard([&] { document.removeObserver(&observer); });
        const Okular::NormalizedRect visible(.01, .01, .02, .02);
        // A small visible region of a large raster forces the tiled path even
        // on high-DPI monitors, without allocating a whole-page bitmap.
        for (int size : {20000, 30000, 15000}) {
            auto *request = new Okular::PixmapRequest(&observer, 0, size, size, 1.0, 1, Okular::PixmapRequest::Asynchronous);
            request->setNormalizedRect(visible);
            request->setTile(document.page(0)->hasTilesManager(&observer));
            document.requestPixmaps({request});
            QTRY_VERIFY_WITH_TIMEOUT(document.page(0)->hasPixmap(&observer, size, size, visible), 10000);
            QVERIFY(document.page(0)->hasTilesManager(&observer));
        }
    }
    void pageNumberBoundaries() {
        Okular::SettingsCore::instance(QStringLiteral("mupdf-page-lifecycle-test"));
        QTemporaryDir directory;
        const QString file = directory.filePath(QStringLiteral("three-pages.pdf"));
        {
            QPdfWriter writer(file);
            QPainter painter(&writer);
            for (const auto color : {Qt::red, Qt::green, Qt::blue}) {
                if (color != Qt::red) QVERIFY(writer.newPage());
                painter.fillRect(0, 0, writer.width(), writer.height(), QColor(color));
            }
        }
        Okular::Document document(nullptr);
        const auto mime = QMimeDatabase().mimeTypeForName(QStringLiteral("application/pdf"));
        QCOMPARE(document.openDocument(file, QUrl::fromLocalFile(file), mime), Okular::Document::OpenSuccess);
        QString error;
        const auto target = [](const Okular::Document &doc) -> const Okular::GotoAction * {
            for (auto *rect : doc.page(0)->objectRects())
                if (rect->objectType() == Okular::ObjectRect::Action)
                    if (auto *action = dynamic_cast<const Okular::GotoAction *>(static_cast<const Okular::Action *>(rect->object()))) return action;
            return nullptr;
        };
        for (int page : {1, 2, 3}) {
            QVERIFY2(document.setNamedDestination(QStringLiteral("target"), page, 0, 0, &error), qPrintable(error));
            QCOMPARE(Okular::DocumentViewport(document.metaData(QStringLiteral("NamedViewport"), QStringLiteral("target")).toString()).pageNumber, page - 1);
            QVERIFY2(document.createInternalLink(1, .1, .1, .2, .2, {}, page, 0, 0, &error), qPrintable(error));
            QVERIFY(target(document));
            QCOMPARE(target(document)->destViewport().pageNumber, page - 1);
            QVERIFY2(document.editInternalLinkDestination(1, .1, .1, .2, .2, {}, 4 - page, 0, 0, &error), qPrintable(error));
            QCOMPARE(target(document)->destViewport().pageNumber, 3 - page);
            const auto saved = directory.filePath(QStringLiteral("internal.pdf"));
            QVERIFY2(document.saveChanges(saved, &error), qPrintable(error));
            Okular::Document reopened(nullptr);
            QCOMPARE(reopened.openDocument(saved, QUrl::fromLocalFile(saved), mime), Okular::Document::OpenSuccess);
            QVERIFY(target(reopened));
            QCOMPARE(target(reopened)->destViewport().pageNumber, 3 - page);
            QCOMPARE(Okular::DocumentViewport(reopened.metaData(QStringLiteral("NamedViewport"), QStringLiteral("target")).toString()).pageNumber, page - 1);
            reopened.closeDocument();
            for (int bad : {0, -1, 4}) {
                QVERIFY(!document.editInternalLinkDestination(1, .1, .1, .2, .2, {}, bad, 0, 0, &error));
                QCOMPARE(target(document)->destViewport().pageNumber, 3 - page);
                QVERIFY(!document.setNamedDestination(QStringLiteral("target"), bad, 0, 0, &error));
            }
            QVERIFY(document.deletePdfLink(1, .1, .1, .2, .2, &error));
        }
        for (int page : {1, 3, 100}) {
            QVERIFY2(document.createRemoteLink(1, .1, .1, .2, .2, file, page, 0, 0, &error), qPrintable(error));
            QVERIFY(target(document));
            QCOMPARE(target(document)->destViewport().pageNumber, page - 1);
            QVERIFY(document.editRemoteLinkDestination(1, .1, .1, .2, .2, file, 1, 0, 0, &error));
            QCOMPARE(target(document)->destViewport().pageNumber, 0);
            QVERIFY(document.editRemoteLinkDestination(1, .1, .1, .2, .2, file, page, 0, 0, &error));
            const auto saved = directory.filePath(QStringLiteral("saved.pdf"));
            QVERIFY2(document.saveChanges(saved, &error), qPrintable(error));
            Okular::Document reopened(nullptr);
            QCOMPARE(reopened.openDocument(saved, QUrl::fromLocalFile(saved), mime), Okular::Document::OpenSuccess);
            QVERIFY(target(reopened));
            QCOMPARE(target(reopened)->destViewport().pageNumber, page - 1);
            reopened.closeDocument();
            QVERIFY(document.deletePdfLink(1, .1, .1, .2, .2, &error));
        }
        // A name overrides the numeric sentinel used by note-page links.
        QVERIFY(document.createInternalLink(1, .1, .1, .2, .2, QStringLiteral("target"), 0, 0, 0, &error));
        QCOMPARE(target(document)->destinationName(), QStringLiteral("target"));
        QVERIFY(document.deletePdfLink(1, .1, .1, .2, .2, &error));
        QVERIFY(document.createRemoteLink(1, .1, .1, .2, .2, file, 0, 0, 0, &error, QStringLiteral("remote")));
        QCOMPARE(target(document)->destinationName(), QStringLiteral("remote"));
        QVERIFY(document.deletePdfLink(1, .1, .1, .2, .2, &error));
        for (int bad : {0, -1, 4}) {
            QVERIFY(!document.createInternalLink(1, .1, .1, .2, .2, {}, bad, 0, 0, &error));
            QVERIFY(!target(document));
            quint64 id = 0;
            QVERIFY(!document.insertPdfPage(2, file, bad, true, &id, &error));
            QCOMPARE(document.pages(), 3u);
        }
        for (int page : {1, 2, 3}) {
            const auto expected = document.renderToImage(page - 1, 36, false, &error);
            QVERIFY(!expected.isNull());
            quint64 id = 0;
            QVERIFY2(document.insertPdfPage(2, file, page, true, &id, &error), qPrintable(error));
            QCOMPARE(document.renderToImage(3, 36, false, &error), expected);
            QVERIFY(document.detachPage(3, id, &error));
        }
        document.closeDocument();
    }
    void observersReleaseAnnotationsBeforePageDestruction() {
        Okular::SettingsCore::instance(QStringLiteral("mupdf-page-lifecycle-test"));
        QTemporaryDir directory;
        const auto file = directory.filePath(QStringLiteral("rotation.pdf"));
        { QPdfWriter writer(file); QPainter painter(&writer); painter.drawText(100, 100, QStringLiteral("Rotate")); }
        Okular::Document document(nullptr);
        QCOMPARE(document.openDocument(file, QUrl::fromLocalFile(file), QMimeDatabase().mimeTypeForName(QStringLiteral("application/pdf"))), Okular::Document::OpenSuccess);
        struct TrackedStamp : Okular::StampAnnotation {
            bool *destroyed;
            explicit TrackedStamp(bool *flag) : destroyed(flag) {}
            ~TrackedStamp() override { *destroyed = true; }
        };
        struct Observer : Okular::DocumentObserver {
            bool *destroyed = nullptr;
            bool notified = false;
            bool aliveDuringSetup = false;
            void notifySetup(const QList<Okular::Page *> &, int flags) override {
                if (destroyed && (flags & DocumentChanged)) {
                    notified = true;
                    aliveDuringSetup = !*destroyed;
                }
            }
        } observer;
        document.addObserver(&observer);
        const auto removeObserver = qScopeGuard([&] { document.removeObserver(&observer); });
        QString error;
        for (int rotation : {90, 180, 270, 0}) {
            bool destroyed = false;
            auto *stamp = new TrackedStamp(&destroyed);
            stamp->setBoundingRectangle({.1, .1, .3, .2});
            stamp->setStampIconName(QStringLiteral("Approved"));
            document.addPageAnnotation(0, stamp);
            observer.destroyed = &destroyed;
            observer.notified = false;
            QVERIFY2(document.rotatePage(0, rotation, &error), qPrintable(error));
            observer.destroyed = nullptr;
            QVERIFY(observer.notified);
            QVERIFY2(observer.aliveDuringSetup, "Selected annotation was freed before observers could release it");
            QVERIFY(destroyed);
        }
        document.closeDocument();
    }
    void notePageAndRollback() {
        Okular::SettingsCore::instance(QStringLiteral("mupdf-page-lifecycle-test"));
        QTemporaryDir directory;
        const QString file = directory.filePath(QStringLiteral("source.pdf"));
        {
            QPdfWriter writer(file);
            QPainter painter(&writer);
            painter.drawText(100, 100, QStringLiteral("Source page"));
        }
        Okular::Document document(nullptr);
        QCOMPARE(document.openDocument(file, QUrl::fromLocalFile(file), QMimeDatabase().mimeTypeForName(QStringLiteral("application/pdf"))), Okular::Document::OpenSuccess);
        Okular::DocumentObserver observer;
        document.addObserver(&observer);
        const auto removeObserver = qScopeGuard([&] { document.removeObserver(&observer); });
        QString error;
        for (int i = 0; i < 30; ++i) {
            quint64 id = 0;
            // Many characters with a tiny raster make text extraction outlive the image.
            // The old core frees the page before the queued text completion in this case.
            QVERIFY2(document.insertBlankPage(0, 600, 800, &id, &error, QStringLiteral("note 1 ") + QString(8192, QLatin1Char('x'))), qPrintable(error));
            QCOMPARE(document.pages(), 2u);
            // Same one-based destination API used by Part::slotAddNotePage.
            QVERIFY2(document.setNamedDestination(QStringLiteral("note"), 2, 0, 0, &error), qPrintable(error));
            Okular::DocumentViewport target(document.metaData(QStringLiteral("NamedViewport"), QStringLiteral("note")).toString());
            QCOMPARE(target.pageNumber, 1);
            auto *request = new Okular::PixmapRequest(&observer, 1, 20, 26, 1.0, 1, Okular::PixmapRequest::Asynchronous);
            document.requestPixmaps({request});
            // Vary whether extraction is queued, running, or awaiting its GUI callback.
            if (i % 3) QTest::qWait(i % 3);
            if (i % 2) {
                QVERIFY(!document.setNamedDestination(QStringLiteral("invalid"), 3, 0, 0, &error));
                QVERIFY(!error.isEmpty());
            }
            QVERIFY2(document.deleteNamedDestination(QStringLiteral("note"), &error), qPrintable(error));
            QVERIFY2(document.detachPage(1, id, &error), qPrintable(error));
            QCOMPARE(document.pages(), 1u);
            QTest::qWait(5);
            QVERIFY2(document.attachPage(0, id, &error), qPrintable(error));
            QVERIFY2(document.detachPage(1, id, &error), qPrintable(error));
        }
        document.closeDocument();
    }
};
QTEST_MAIN(PageLifecycleTest)
#include "pagelifecycletest.moc"
