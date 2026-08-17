/*
    SPDX-FileCopyrightText: 2013 Fabio D 'Urso <fabiodurso@hotmail.it>

    Work sponsored by the LiMux project of the city of Munich:
    SPDX-FileCopyrightText: 2017 Klarälvdalens Datakonsult AB a KDAB Group company <info@kdab.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QMimeDatabase>
#include <QTemporaryFile>
#include <QTest>

#include <threadweaver/queue.h>

#include "../core/action.h"
#include "../core/annotations.h"
#include "../core/document.h"
#include "../core/document_p.h"
#include "../core/generator.h"
#include "../core/observer.h"
#include "../core/page.h"
#include "../core/rotationjob_p.h"
#include "../settings_core.h"

class DocumentTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testCloseDuringRotationJob();
    void testDocdataMigration();
    void testPreOpenViewSessionUsesFinalViewport();
    void testIndependentViewSession();
    void testViewSessionHistorySynchronization();
    void testSessionActionSurvivesDocumentDeletionFromViewportObserver();
    void testSessionNavigationStopsAfterSynchronousClose();
    void testEvaluateKeystrokeEventChange_data();
    void testEvaluateKeystrokeEventChange();
};

class ViewSessionObserver : public Okular::DocumentObserver
{
public:
    void notifyViewportChanged(bool) override
    {
        ++viewportChangeCount;
    }

    void notifyCurrentPageChanged(int previous, int current) override
    {
        ++currentPageChangeCount;
        lastPreviousPage = previous;
        lastCurrentPage = current;
    }

    int viewportChangeCount = 0;
    int currentPageChangeCount = 0;
    int lastPreviousPage = -1;
    int lastCurrentPage = -1;
};

class SelfRemovingViewSessionObserver : public Okular::DocumentObserver
{
public:
    void notifyViewportChanged(bool) override
    {
        ++viewportChangeCount;
        session->removeObserver(this);
    }

    void notifyCurrentPageChanged(int, int) override
    {
        ++currentPageChangeCount;
    }

    Okular::DocumentViewSession *session = nullptr;
    int viewportChangeCount = 0;
    int currentPageChangeCount = 0;
};

class DestroyingViewSessionObserver : public Okular::DocumentObserver
{
public:
    void notifyViewportChanged(bool) override
    {
        ++viewportChangeCount;
        session->reset();
    }

    void notifyCurrentPageChanged(int, int) override
    {
        ++currentPageChangeCount;
    }

    std::unique_ptr<Okular::DocumentViewSession> *session = nullptr;
    int viewportChangeCount = 0;
    int currentPageChangeCount = 0;
};

class DocumentDestroyingViewSessionObserver : public Okular::DocumentObserver
{
public:
    void notifyViewportChanged(bool) override
    {
        ++viewportChangeCount;
        document->reset();
    }

    void notifyCurrentPageChanged(int, int) override
    {
        ++currentPageChangeCount;
    }

    std::unique_ptr<Okular::Document> *document = nullptr;
    int viewportChangeCount = 0;
    int currentPageChangeCount = 0;
};

class DocumentClosingViewSessionObserver : public Okular::DocumentObserver
{
public:
    void notifyViewportChanged(bool) override
    {
        ++viewportChangeCount;
        if (armed) {
            armed = false;
            document->closeDocument();
        }
    }

    void notifyCurrentPageChanged(int, int) override
    {
        ++currentPageChangeCount;
    }

    Okular::Document *document = nullptr;
    bool armed = false;
    int viewportChangeCount = 0;
    int currentPageChangeCount = 0;
};

// Test that we don't crash if the document is closed while a RotationJob
// is enqueued/running
void DocumentTest::testCloseDuringRotationJob()
{
    Okular::SettingsCore::instance(QStringLiteral("documenttest"));
    Okular::Document *m_document = new Okular::Document(nullptr);
    const QString testFile = QStringLiteral(KDESRCDIR "data/file1.pdf");
    QMimeDatabase db;
    const QMimeType mime = db.mimeTypeForFile(testFile);

    Okular::DocumentObserver *dummyDocumentObserver = new Okular::DocumentObserver();
    m_document->addObserver(dummyDocumentObserver);

    m_document->openDocument(testFile, QUrl(), mime);
    m_document->setRotation(1);

    // Tell ThreadWeaver not to start any new job
    ThreadWeaver::Queue::instance()->suspend();

    // Request a pixmap. A RotationJob will be enqueued but not started
    Okular::PixmapRequest *pixmapReq = new Okular::PixmapRequest(dummyDocumentObserver, 0, 100, 100, qApp->devicePixelRatio(), 1, Okular::PixmapRequest::NoFeature);
    m_document->requestPixmaps({pixmapReq});

    // Delete the document
    delete m_document;

    // Resume job processing and wait for the RotationJob to finish
    ThreadWeaver::Queue::instance()->resume();
    ThreadWeaver::Queue::instance()->finish();
    qApp->processEvents();

    delete dummyDocumentObserver;
}

// Test that, if there's a XML file in docdata referring to a document, we
// detect that it must be migrated, that it doesn't get wiped out if you close
// the document without migrating and that it does get wiped out after migrating
void DocumentTest::testDocdataMigration()
{
    Okular::SettingsCore::instance(QStringLiteral("documenttest"));

    const QUrl testFileUrl = QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QString testFilePath = testFileUrl.toLocalFile();
    const qint64 testFileSize = QFileInfo(testFilePath).size();

    // Copy XML file to the docdata/ directory
    const QString docDataPath = Okular::DocumentPrivate::docDataFileName(testFileUrl, testFileSize);
    QFile::remove(docDataPath);
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/file1-docdata.xml"), docDataPath));

    // Open our document
    Okular::Document *m_document = new Okular::Document(nullptr);
    QMimeDatabase db;
    const QMimeType mime = db.mimeTypeForFile(testFilePath);
    QCOMPARE(m_document->openDocument(testFilePath, testFileUrl, mime), Okular::Document::OpenSuccess);

    // Check that the annotation from file1-docdata.xml was loaded
    QCOMPARE(m_document->page(0)->annotations().size(), 1);
    QCOMPARE(m_document->page(0)->annotations().constFirst()->uniqueName(), QStringLiteral("testannot"));

    // Check that we detect that it must be migrated
    QVERIFY(m_document->isDocdataMigrationNeeded());
    m_document->closeDocument();

    // Reopen the document and check that the annotation is still present
    // (because we have not migrated)
    QCOMPARE(m_document->openDocument(testFilePath, testFileUrl, mime), Okular::Document::OpenSuccess);
    QCOMPARE(m_document->page(0)->annotations().size(), 1);
    QCOMPARE(m_document->page(0)->annotations().constFirst()->uniqueName(), QStringLiteral("testannot"));
    QVERIFY(m_document->isDocdataMigrationNeeded());

    // Do the migration
    QTemporaryFile migratedSaveFile(QStringLiteral("%1/okrXXXXXX.pdf").arg(QDir::tempPath()));
    QVERIFY(migratedSaveFile.open());
    migratedSaveFile.close();
    QVERIFY(m_document->saveChanges(migratedSaveFile.fileName()));
    m_document->docdataMigrationDone();
    QVERIFY(!m_document->isDocdataMigrationNeeded());
    m_document->closeDocument();

    // Now the docdata file should have no annotations, let's check
    QCOMPARE(m_document->openDocument(testFilePath, testFileUrl, mime), Okular::Document::OpenSuccess);
    QCOMPARE(m_document->page(0)->annotations().size(), 0);
    QVERIFY(!m_document->isDocdataMigrationNeeded());
    m_document->closeDocument();

    // And the new file should have 1 annotation, let's check
    QCOMPARE(m_document->openDocument(migratedSaveFile.fileName(), QUrl::fromLocalFile(migratedSaveFile.fileName()), mime), Okular::Document::OpenSuccess);
    QCOMPARE(m_document->page(0)->annotations().size(), 1);
    QVERIFY(!m_document->isDocdataMigrationNeeded());
    m_document->closeDocument();

    delete m_document;
}

void DocumentTest::testIndependentViewSession()
{
    Okular::SettingsCore::instance(QStringLiteral("documenttest"));

    const QString testFile = QStringLiteral(KDESRCDIR "data/file2.pdf");
    const QUrl testFileUrl = QUrl::fromLocalFile(testFile);
    QMimeDatabase db;

    auto document = std::make_unique<Okular::Document>(nullptr);
    QCOMPARE(document->openDocument(testFile, testFileUrl, db.mimeTypeForFile(testFile)), Okular::Document::OpenSuccess);
    QVERIFY(document->pages() > 1);
    const int initialPage = static_cast<int>(document->currentPage());
    const int differentPage = initialPage == 0 ? 1 : 0;

    ViewSessionObserver observer;
    document->addObserver(&observer);
    auto session = document->createViewSession(&observer);
    QVERIFY(session->viewport() == document->viewport());
    QVERIFY(session->historyAtBegin());
    QVERIFY(session->historyAtEnd());

    observer.viewportChangeCount = 0;
    observer.currentPageChangeCount = 0;
    session->setViewportPage(differentPage);
    QCOMPARE(document->currentPage(), static_cast<uint>(initialPage));
    QCOMPARE(session->currentPage(), static_cast<uint>(differentPage));
    QCOMPARE(observer.viewportChangeCount, 1);
    QCOMPARE(observer.currentPageChangeCount, 1);
    QCOMPARE(observer.lastPreviousPage, initialPage);
    QCOMPARE(observer.lastCurrentPage, differentPage);
    QVERIFY(!session->historyAtBegin());
    QVERIFY(session->historyAtEnd());

    session->setPrevViewport();
    QCOMPARE(document->currentPage(), static_cast<uint>(initialPage));
    QCOMPARE(session->currentPage(), static_cast<uint>(initialPage));
    QVERIFY(session->historyAtBegin());
    QVERIFY(!session->historyAtEnd());

    session->setNextViewport();
    QCOMPARE(session->currentPage(), static_cast<uint>(differentPage));
    QVERIFY(!session->historyAtBegin());
    QVERIFY(session->historyAtEnd());

    // A session observer still receives shared content notifications through
    // Document, but the default Document navigation channel must not leak
    // into its independent viewport callbacks.
    observer.viewportChangeCount = 0;
    observer.currentPageChangeCount = 0;
    document->setViewportPage(differentPage);
    QCOMPARE(observer.viewportChangeCount, 0);
    QCOMPARE(observer.currentPageChangeCount, 0);
    QCOMPARE(session->currentPage(), static_cast<uint>(differentPage));

    // Removing the observer from its session (as PageView does when promoted
    // to the main frame) restores default Document navigation notifications.
    session->removeObserver(&observer);
    document->setViewportPage(initialPage);
    QCOMPARE(observer.viewportChangeCount, 1);
    QCOMPARE(observer.currentPageChangeCount, 1);
    session->addObserver(&observer);

    SelfRemovingViewSessionObserver selfRemovingObserver;
    auto selfRemovingSession = document->createViewSession(&selfRemovingObserver);
    selfRemovingObserver.session = selfRemovingSession.get();
    selfRemovingSession->setViewportPage(differentPage);
    QCOMPARE(selfRemovingObserver.viewportChangeCount, 1);
    QCOMPARE(selfRemovingObserver.currentPageChangeCount, 0);

    DestroyingViewSessionObserver destroyingObserver;
    auto destroyingSession = document->createViewSession(&destroyingObserver);
    destroyingObserver.session = &destroyingSession;
    destroyingSession->setViewportPage(differentPage);
    QVERIFY(!destroyingSession);
    QCOMPARE(destroyingObserver.viewportChangeCount, 1);
    QCOMPARE(destroyingObserver.currentPageChangeCount, 0);

    document->closeDocument();
    QVERIFY(!session->viewport().isValid());
    QVERIFY(session->isAttached());

    document->removeObserver(&observer);
    document.reset();
    QVERIFY(!session->isAttached());
    session->setViewportPage(0); // A detached session is a safe no-op.
}

void DocumentTest::testPreOpenViewSessionUsesFinalViewport()
{
    Okular::SettingsCore::instance(QStringLiteral("documenttest"));

    const QString testFile = QStringLiteral(KDESRCDIR "data/file2.pdf");
    const QUrl testFileUrl = QUrl::fromLocalFile(testFile);
    QMimeDatabase db;

    auto document = std::make_unique<Okular::Document>(nullptr);
    ViewSessionObserver observer;
    document->addObserver(&observer);
    auto session = document->createViewSession(&observer);
    QVERIFY(!session->viewport().isValid());

    document->setNextDocumentViewport(Okular::DocumentViewport(1));
    QCOMPARE(document->openDocument(testFile, testFileUrl, db.mimeTypeForFile(testFile)), Okular::Document::OpenSuccess);

    QCOMPARE(document->currentPage(), 1u);
    QCOMPARE(session->currentPage(), 1u);
    QVERIFY(session->viewport() == document->viewport());
    QVERIFY(session->historyAtBegin());
    QVERIFY(session->historyAtEnd());
    QCOMPARE(observer.viewportChangeCount, 0);
    QCOMPARE(observer.currentPageChangeCount, 0);

    document->closeDocument();
    QVERIFY(!session->viewport().isValid());

    const QString namedDestinationFile = QStringLiteral(KDESRCDIR "data/kjsfunctionstest.pdf");
    const QUrl namedDestinationUrl = QUrl::fromLocalFile(namedDestinationFile);
    document->setNextDocumentDestination(QStringLiteral("Navigation1"));
    QCOMPARE(document->openDocument(namedDestinationFile, namedDestinationUrl, db.mimeTypeForFile(namedDestinationFile)), Okular::Document::OpenSuccess);

    QVERIFY(document->viewport().rePos.enabled);
    QVERIFY(session->viewport() == document->viewport());
    QVERIFY(session->historyAtBegin());
    QVERIFY(session->historyAtEnd());
    QCOMPARE(observer.viewportChangeCount, 0);
    QCOMPARE(observer.currentPageChangeCount, 0);

    document->removeObserver(&observer);
}

void DocumentTest::testViewSessionHistorySynchronization()
{
    Okular::SettingsCore::instance(QStringLiteral("documenttest"));

    const QString testFile = QStringLiteral(KDESRCDIR "data/file2.pdf");
    const QUrl testFileUrl = QUrl::fromLocalFile(testFile);
    QMimeDatabase db;

    auto document = std::make_unique<Okular::Document>(nullptr);
    QCOMPARE(document->openDocument(testFile, testFileUrl, db.mimeTypeForFile(testFile)), Okular::Document::OpenSuccess);
    QVERIFY(document->pages() > 1);
    const int initialPage = static_cast<int>(document->currentPage());
    const int differentPage = initialPage == 0 ? 1 : 0;

    ViewSessionObserver sessionObserver;
    ViewSessionObserver defaultObserver;
    document->addObserver(&sessionObserver);
    document->addObserver(&defaultObserver);
    auto session = document->createViewSession(&sessionObserver);

    // A restored document may itself have persisted Back/Forward entries.
    // Start this transfer test from the session's known singleton history.
    session->synchronizeToDefault();

    document->setViewportPage(differentPage);
    document->setViewportPage(initialPage);
    QVERIFY(!document->historyAtBegin());
    QVERIFY(document->historyAtEnd());

    session->synchronizeFromDefault();
    QCOMPARE(session->currentPage(), static_cast<uint>(initialPage));
    QVERIFY(!session->historyAtBegin());
    QVERIFY(session->historyAtEnd());
    session->setPrevViewport();
    QCOMPARE(session->currentPage(), static_cast<uint>(differentPage));
    QCOMPARE(document->currentPage(), static_cast<uint>(initialPage));
    QVERIFY(!session->historyAtBegin());
    QVERIFY(!session->historyAtEnd());

    sessionObserver.viewportChangeCount = 0;
    sessionObserver.currentPageChangeCount = 0;
    defaultObserver.viewportChangeCount = 0;
    defaultObserver.currentPageChangeCount = 0;
    session->synchronizeToDefault();

    QCOMPARE(document->currentPage(), static_cast<uint>(differentPage));
    QVERIFY(!document->historyAtBegin());
    QVERIFY(!document->historyAtEnd());
    QCOMPARE(sessionObserver.viewportChangeCount, 0);
    QCOMPARE(sessionObserver.currentPageChangeCount, 0);
    QCOMPARE(defaultObserver.viewportChangeCount, 1);
    QCOMPARE(defaultObserver.currentPageChangeCount, 1);
    QCOMPARE(defaultObserver.lastPreviousPage, initialPage);
    QCOMPARE(defaultObserver.lastCurrentPage, differentPage);

    document->setPrevViewport();
    QCOMPARE(document->currentPage(), static_cast<uint>(initialPage));
    QVERIFY(document->historyAtBegin());
    document->setNextViewport();
    QCOMPARE(document->currentPage(), static_cast<uint>(differentPage));
    document->setNextViewport();
    QCOMPARE(document->currentPage(), static_cast<uint>(initialPage));
    QVERIFY(document->historyAtEnd());

    document->removeObserver(&sessionObserver);
    document->removeObserver(&defaultObserver);
}

void DocumentTest::testSessionActionSurvivesDocumentDeletionFromViewportObserver()
{
    Okular::SettingsCore::instance(QStringLiteral("documenttest"));

    const QString testFile = QStringLiteral(KDESRCDIR "data/simple-multipage.pdf");
    QMimeDatabase db;
    auto document = std::make_unique<Okular::Document>(nullptr);
    QCOMPARE(document->openDocument(testFile, QUrl::fromLocalFile(testFile), db.mimeTypeForFile(testFile)), Okular::Document::OpenSuccess);
    QVERIFY(document->pages() > 1);

    DocumentDestroyingViewSessionObserver observer;
    observer.document = &document;
    auto session = document->createViewSession(&observer);
    const int targetPage = document->currentPage() == 0 ? 1 : 0;
    Okular::GotoAction action{QString(), Okular::DocumentViewport(targetPage)};

    // The Goto viewport notification deletes the Document synchronously. The
    // action helper must not unregister from the deleted object, and the
    // session must stop before sending CurrentPageChanged to an observer whose
    // owning Document has already gone away.
    session->processAction(&action);

    QVERIFY(!document);
    QVERIFY(!session->isAttached());
    QCOMPARE(observer.viewportChangeCount, 1);
    QCOMPARE(observer.currentPageChangeCount, 0);
}

void DocumentTest::testSessionNavigationStopsAfterSynchronousClose()
{
    Okular::SettingsCore::instance(QStringLiteral("documenttest"));

    const QString testFile = QStringLiteral(KDESRCDIR "data/simple-multipage.pdf");
    QMimeDatabase db;
    const QMimeType mime = db.mimeTypeForFile(testFile);

    enum NavigationOperation { SetViewport, PreviousViewport, NextViewport };
    const QList<NavigationOperation> operations = {SetViewport, PreviousViewport, NextViewport};
    for (NavigationOperation operation : operations) {
        auto document = std::make_unique<Okular::Document>(nullptr);
        QCOMPARE(document->openDocument(testFile, QUrl::fromLocalFile(testFile), mime), Okular::Document::OpenSuccess);
        QVERIFY(document->pages() > 1);

        DocumentClosingViewSessionObserver observer;
        observer.document = document.get();
        auto session = document->createViewSession(&observer);
        const int initialPage = static_cast<int>(session->currentPage());
        const int otherPage = initialPage == 0 ? 1 : 0;

        if (operation != SetViewport) {
            session->setViewportPage(otherPage);
            if (operation == NextViewport) {
                session->setPrevViewport();
            }
            observer.viewportChangeCount = 0;
            observer.currentPageChangeCount = 0;
        }

        observer.armed = true;
        switch (operation) {
        case SetViewport:
            session->setViewportPage(otherPage);
            break;
        case PreviousViewport:
            session->setPrevViewport();
            break;
        case NextViewport:
            session->setNextViewport();
            break;
        }

        QVERIFY(!document->isOpened());
        QVERIFY(session->isAttached());
        QVERIFY(!session->viewport().isValid());
        QCOMPARE(observer.viewportChangeCount, 1);
        QCOMPARE(observer.currentPageChangeCount, 0);
    }
}

void DocumentTest::testEvaluateKeystrokeEventChange_data()
{
    QTest::addColumn<QString>("oldVal");
    QTest::addColumn<QString>("newVal");
    QTest::addColumn<int>("selStart");
    QTest::addColumn<int>("selEnd");
    QTest::addColumn<QString>("expectedDiff");

    QTest::addRow("empty") << ""
                           << "" << 0 << 0 << "";
    QTest::addRow("a") << ""
                       << "a" << 0 << 0 << "a";
    QTest::addRow("ab") << "a"
                        << "b" << 0 << 1 << "b";
    QTest::addRow("ab2") << "a"
                         << "ab" << 1 << 1 << "b";
    QTest::addRow("kaesekuchen") << "Käse"
                                 << "Käsekuchen" << 4 << 4 << "kuchen";
    QTest::addRow("replace") << "kuchen"
                             << "wurst" << 0 << 6 << "wurst";
    QTest::addRow("okular") << "Oku"
                            << "Okular" << 3 << 3 << "lar";
    QTest::addRow("okular2") << "Oku"
                             << "Okular" << 0 << 3 << "Okular";
    QTest::addRow("removal1") << "a"
                              << "" << 0 << 1 << "";
    QTest::addRow("removal2") << "ab"
                              << "a" << 1 << 2 << "";
    QTest::addRow("overlapping chang") << "abcd"
                                       << "abclmnopd" << 1 << 3 << "bclmnop";
    QTest::addRow("unicode") << "☮🤌"
                             << "☮🤌❤️" << 2 << 2 << "❤️";
    QTest::addRow("unicode2") << "☮"
                              << "☮🤌❤️" << 1 << 1 << "🤌❤️";
    QTest::addRow("unicode3") << "🤍"
                              << "🤌" << 0 << 1 << "🤌";
}

void DocumentTest::testEvaluateKeystrokeEventChange()
{
    QFETCH(QString, oldVal);
    QFETCH(QString, newVal);
    QFETCH(int, selStart);
    QFETCH(int, selEnd);
    QFETCH(QString, expectedDiff);

    QCOMPARE(Okular::DocumentPrivate::evaluateKeystrokeEventChange(oldVal, newVal, selStart, selEnd), expectedDiff);
}

QTEST_MAIN(DocumentTest)
#include "documenttest.moc"
