/*
    SPDX-FileCopyrightText: 2013 Albert Astals Cid <aacid@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// clazy:excludeall=qstring-allocations

#include <QMimeDatabase>
#include <QSignalSpy>
#include <QTest>

#include "../core/document.h"
#include "../core/observer.h"
#include "../core/page.h"
#include "../core/textpage.h"
#include "../settings_core.h"

Q_DECLARE_METATYPE(Okular::Document::SearchStatus)

class SearchFinishedReceiver : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    void searchFinished(int id, Okular::Document::SearchStatus status)
    {
        m_id = id;
        m_status = status;
    }

public:
    int m_id;
    Okular::Document::SearchStatus m_status;
};

class SearchTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void testNextAndPrevious();
    void test311232();
    void testViewSessionSearchNavigation();
    void testDestroyedViewSessionSearchDoesNotMoveDefault();
    void testReusedSearchIdKeepsLatestViewSessionGeneration();
    void testViewportCallbackCanReplaceSearchGeneration();
    void testCompletionCallbackCanReplaceSearchGeneration();
    void testResetSearchIsReentrant();
    void testStaleWorkersDoNotNotifyReplacementDocument();
    void testCleanupNotificationStopsAfterDocumentReplacement();
    void test323262();
    void test323263();
    void test430243();
    void testDottedI();
    void testHyphenAtEndOfLineWithoutYOverlap();
    void testHyphenWithYOverlap();
    void testHyphenAtEndOfPage();
    void testOneColumn();
    void testTwoColumns();
};

class ViewportSearchReplacementObserver : public Okular::DocumentObserver
{
public:
    void notifyViewportChanged(bool) override
    {
        if (!armed) {
            return;
        }

        armed = false;
        triggered = true;
        document->resetSearch(searchID);
        document->searchText(searchID, QStringLiteral("Page 20"), true, Qt::CaseSensitive, Okular::Document::NextMatch, false, QColor(), session);
    }

    Okular::Document *document = nullptr;
    Okular::DocumentViewSession *session = nullptr;
    int searchID = -1;
    bool armed = false;
    bool triggered = false;
};

class SetupSearchReplacementObserver : public Okular::DocumentObserver
{
public:
    void notifySetup(const QList<Okular::Page *> &, int) override
    {
        if (!armed) {
            return;
        }

        armed = false;
        triggered = true;
        document->resetSearch(searchID);
        document->searchText(searchID, QStringLiteral("Page"), true, Qt::CaseSensitive, Okular::Document::NextMatch, false, QColor());
    }

    Okular::Document *document = nullptr;
    int searchID = -1;
    bool armed = false;
    bool triggered = false;
};

class ReentrantResetSearchObserver : public Okular::DocumentObserver
{
public:
    void notifyPageChanged(int, int flags) override
    {
        if (!armed || !(flags & Okular::DocumentObserver::Highlights)) {
            return;
        }

        armed = false;
        ++triggerCount;
        document->resetSearch(searchID);
    }

    Okular::Document *document = nullptr;
    int searchID = -1;
    bool armed = false;
    int triggerCount = 0;
};

class HighlightChangeObserver : public Okular::DocumentObserver
{
public:
    void notifyPageChanged(int page, int flags) override
    {
        if (flags & Okular::DocumentObserver::Highlights) {
            notifiedPages.append(page);
        }
    }

    QList<int> notifiedPages;
};

class ReopeningHighlightObserver : public Okular::DocumentObserver
{
public:
    void notifyPageChanged(int page, int flags) override
    {
        if (!(flags & Okular::DocumentObserver::Highlights)) {
            return;
        }

        notifiedPages.append(page);
        notificationOrder->append(this);
        if (!replaceDocument) {
            return;
        }

        replaceDocument = false;
        ++replacementCount;
        document->closeDocument();
        reopenSucceeded = document->openDocument(testFile, QUrl::fromLocalFile(testFile), mime) == Okular::Document::OpenSuccess;
        *replacementComplete = true;
    }

    void notifySetup(const QList<Okular::Page *> &, int) override
    {
        if (*replacementComplete) {
            ++setupNotificationsAfterReplacement;
        }
    }

    Okular::Document *document = nullptr;
    QList<ReopeningHighlightObserver *> *notificationOrder = nullptr;
    bool *replacementComplete = nullptr;
    QString testFile;
    QMimeType mime;
    QList<int> notifiedPages;
    bool replaceDocument = false;
    bool reopenSucceeded = false;
    int replacementCount = 0;
    int setupNotificationsAfterReplacement = 0;
};

void SearchTest::initTestCase()
{
    qRegisterMetaType<Okular::Document::SearchStatus>();
    Okular::SettingsCore::instance(QStringLiteral("searchtest"));
}

static void createTextPage(const QList<QString> &text, const QList<Okular::NormalizedRect> &rect, Okular::TextPage *&tp, Okular::Page *&page)
{
    tp = new Okular::TextPage();
    for (int i = 0; i < text.size(); i++) {
        tp->append(text[i], rect[i]);
    }

    // The Page::setTextPage method invokes the layout analysis algorithms tested by some tests here
    // and also sets the tp->d->m_page field (the latter was used in older versions of Okular by
    // TextPage::stringLengthAdaptedWithHyphen).
    // Note that calling "delete page;" will delete the TextPage as well.
    page = new Okular::Page(1, 100, 100, Okular::Rotation0);
    page->setTextPage(tp);
}

#define CREATE_PAGE                                                                                                                                                                                                                            \
    QCOMPARE(text.size(), rect.size());                                                                                                                                                                                                        \
    Okular::Page *page;                                                                                                                                                                                                                        \
    Okular::TextPage *tp;                                                                                                                                                                                                                      \
    createTextPage(text, rect, tp, page);

#define TEST_NEXT_PREV(searchType, expectedStatus)                                                                                                                                                                                             \
    {                                                                                                                                                                                                                                          \
        Okular::RegularAreaRect *result = tp->findText(0, searchString, searchType, Qt::CaseSensitive, NULL);                                                                                                                                  \
        QCOMPARE(!!result, expectedStatus);                                                                                                                                                                                                    \
        delete result;                                                                                                                                                                                                                         \
    }

// The test testNextAndPrevious checks that
// a) if one starts a new search, then the first or last match is found, depending on the search direction
//   (2 cases: FromTop/FromBottom)
// b) if the last search has found a match,
//   then clicking the "Next" button moves to the next occurrence an "Previous" to the previous one
//   (if there is any). Altogether there are four combinations of the last search and new search
//   direction: Next-Next, Previous-Previous, Next-Previous, Previous-Next; the first two combination
//   have two subcases (the new search may give a match or not, so altogether 6 cases to test).
// This gives 8 cases altogether. By taking into account the cases where the last search has given no match,
// we would have 4 more cases (Next (no match)-Next, Next (no match)-Previous, Previous (no match)-Previous,
// Previous (no match)-Next), but those are more the business of Okular::Document::searchText rather than
// Okular::TextPage (at least in the multi-page case).

//   We have four test situations: four documents and four corresponding search strings.
//   The first situation (document="ababa", search string="b") is a generic one where the
// two matches are not side-by-side and neither the first character nor the last character of
// the document match. The only special thing is that the search string has only length 1.
//   The second situation (document="abab", search string="ab") is notable for that the two occurrences
// of the search string are side-by-side with no characters in between, so some off-by-one errors
// would be detected by this test. As the first match starts at the beginning at the document the
// last match ends at the end of the document, it also detects off-by-one errors for finding the first/last match.
//   The third situation (document="abababa", search string="aba") is notable for it shows whether
// the next match is allowed to contain letters from the previous one: currently it is not
//(as in the majority of browsers, viewers and editors), and therefore "abababa" is considered to
// contain not three but two occurrences of "aba" (if one starts search from the beginning of the document).
//   The fourth situation (document="a ba b", search string="a b") demonstrates the case when one TinyTextEntity
// contains multiple characters that are contained in different matches (namely, the middle "ba" is one TinyTextEntity);
// in particular, since these matches are side-by-side, this test would detect some off-by-one
// offset errors.

void SearchTest::testNextAndPrevious()
{
#define TEST_NEXT_PREV_SITUATION_COUNT 4

    QList<QString> texts[TEST_NEXT_PREV_SITUATION_COUNT] = {QList<QString>() << QStringLiteral("a") << QStringLiteral("b") << QStringLiteral("a") << QStringLiteral("b") << QStringLiteral("a"),
                                                            QList<QString>() << QStringLiteral("a") << QStringLiteral("b") << QStringLiteral("a") << QStringLiteral("b"),
                                                            QList<QString>() << QStringLiteral("a") << QStringLiteral("b") << QStringLiteral("a") << QStringLiteral("b") << QStringLiteral("a") << QStringLiteral("b") << QStringLiteral("a"),
                                                            QList<QString>() << QStringLiteral("a") << QStringLiteral(" ") << QStringLiteral("ba") << QStringLiteral(" ") << QStringLiteral("b")};

    QString searchStrings[TEST_NEXT_PREV_SITUATION_COUNT] = {QStringLiteral("b"), QStringLiteral("ab"), QStringLiteral("aba"), QStringLiteral("a b")};

    for (int i = 0; i < TEST_NEXT_PREV_SITUATION_COUNT; i++) {
        const QList<QString> &text = texts[i];
        const QString &searchString = searchStrings[i];

        QList<Okular::NormalizedRect> rect;

        for (int i = 0; i < text.size(); i++) {
            rect << Okular::NormalizedRect(0.1 * i, 0.0, 0.1 * (i + 1), 0.1);
        }

        CREATE_PAGE;

        // Test 3 of the 8 cases listed above:
        // FromTop, Next-Next (match) and Next-Next (no match)
        TEST_NEXT_PREV(Okular::FromTop, true);
        TEST_NEXT_PREV(Okular::NextResult, true);
        TEST_NEXT_PREV(Okular::NextResult, false);

        // Test 5 cases: FromBottom, Previous-Previous (match), Previous-Next,
        // Next-Previous, Previous-Previous (no match)
        TEST_NEXT_PREV(Okular::FromBottom, true);
        TEST_NEXT_PREV(Okular::PreviousResult, true);
        TEST_NEXT_PREV(Okular::NextResult, true);
        TEST_NEXT_PREV(Okular::PreviousResult, true);
        TEST_NEXT_PREV(Okular::PreviousResult, false);

        delete page;
    }
}

void SearchTest::test311232()
{
    Okular::Document d(nullptr);
    SearchFinishedReceiver receiver;
    QSignalSpy spy(&d, &Okular::Document::searchFinished);

    QObject::connect(&d, &Okular::Document::searchFinished, &receiver, &SearchFinishedReceiver::searchFinished);

    const QString testFile = QStringLiteral(KDESRCDIR "data/file1.pdf");
    QMimeDatabase db;
    const QMimeType mime = db.mimeTypeForFile(testFile);
    d.openDocument(testFile, QUrl(), mime);

    const int searchId = 0;
    d.searchText(searchId, QStringLiteral(" i "), true, Qt::CaseSensitive, Okular::Document::NextMatch, false, QColor());
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(receiver.m_id, searchId);
    QCOMPARE(receiver.m_status, Okular::Document::MatchFound);

    d.continueSearch(searchId, Okular::Document::PreviousMatch);
    QTRY_COMPARE(spy.count(), 2);
    QCOMPARE(receiver.m_id, searchId);
    QCOMPARE(receiver.m_status, Okular::Document::NoMatchFound);
}

void SearchTest::testViewSessionSearchNavigation()
{
    Okular::Document document(nullptr);
    SearchFinishedReceiver receiver;
    QSignalSpy spy(&document, &Okular::Document::searchFinished);
    QObject::connect(&document, &Okular::Document::searchFinished, &receiver, &SearchFinishedReceiver::searchFinished);

    const QString testFile = QStringLiteral(KDESRCDIR "data/simple-multipage.pdf");
    QMimeDatabase db;
    QCOMPARE(document.openDocument(testFile, QUrl(), db.mimeTypeForFile(testFile)), Okular::Document::OpenSuccess);
    QVERIFY(document.pages() > 15);

    document.setViewportPage(0);
    auto session = document.createViewSession();
    session->setViewportPage(15);
    QCOMPARE(document.currentPage(), 0u);
    QCOMPARE(session->currentPage(), 15u);

    // "Page" occurs on every page. Starting from the session therefore
    // distinguishes page 15 from the default Document viewport on page 0.
    const int searchId = 100;
    document.searchText(searchId, QStringLiteral("Page"), false, Qt::CaseSensitive, Okular::Document::NextMatch, true, QColor(), session.get());
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(receiver.m_id, searchId);
    QCOMPARE(receiver.m_status, Okular::Document::MatchFound);

    QCOMPARE(document.currentPage(), 0u);
    QCOMPARE(session->currentPage(), 15u);
    QVERIFY(session->viewport().rePos.enabled);
}

void SearchTest::testDestroyedViewSessionSearchDoesNotMoveDefault()
{
    Okular::Document document(nullptr);
    SearchFinishedReceiver receiver;
    QSignalSpy spy(&document, &Okular::Document::searchFinished);
    QObject::connect(&document, &Okular::Document::searchFinished, &receiver, &SearchFinishedReceiver::searchFinished);

    const QString testFile = QStringLiteral(KDESRCDIR "data/simple-multipage.pdf");
    QMimeDatabase db;
    QCOMPARE(document.openDocument(testFile, QUrl(), db.mimeTypeForFile(testFile)), Okular::Document::OpenSuccess);
    QVERIFY(document.pages() > 30);

    document.setViewportPage(0);
    auto survivingSession = document.createViewSession();
    survivingSession->setViewportPage(5);
    const Okular::DocumentViewport defaultViewport = document.viewport();
    const Okular::DocumentViewport survivingViewport = survivingSession->viewport();

    auto destroyedSession = document.createViewSession();
    destroyedSession->setViewportPage(30);

    // Directional search advances one page per queued event. Destroying the
    // session before those events run must not make a page-40 match fall back
    // to moving Document's default viewport (or another surviving session).
    const int searchId = 101;
    document.searchText(searchId, QStringLiteral("Page 40"), false, Qt::CaseSensitive, Okular::Document::NextMatch, true, QColor(), destroyedSession.get());
    destroyedSession.reset();

    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(receiver.m_id, searchId);
    QCOMPARE(receiver.m_status, Okular::Document::MatchFound);
    QVERIFY(document.viewport() == defaultViewport);
    QVERIFY(survivingSession->viewport() == survivingViewport);
}

void SearchTest::testReusedSearchIdKeepsLatestViewSessionGeneration()
{
    Okular::Document document(nullptr);
    SearchFinishedReceiver receiver;
    QSignalSpy spy(&document, &Okular::Document::searchFinished);
    QObject::connect(&document, &Okular::Document::searchFinished, &receiver, &SearchFinishedReceiver::searchFinished);

    const QString testFile = QStringLiteral(KDESRCDIR "data/simple-multipage.pdf");
    QMimeDatabase db;
    QCOMPARE(document.openDocument(testFile, QUrl(), db.mimeTypeForFile(testFile)), Okular::Document::OpenSuccess);
    QVERIFY(document.pages() > 30);

    document.setViewportPage(0);
    auto sessionA = document.createViewSession();
    sessionA->setViewportPage(10);
    auto sessionB = document.createViewSession();
    sessionB->setViewportPage(20);

    const Okular::DocumentViewport defaultViewport = document.viewport();
    const Okular::DocumentViewport sessionAViewport = sessionA->viewport();

    const int searchId = 102;
    // The old generation would need to advance from page 10 to page 39.
    document.searchText(searchId, QStringLiteral("Page 40"), false, Qt::CaseSensitive, Okular::Document::NextMatch, true, QColor(), sessionA.get());
    document.resetSearch(searchId);

    // Reuse the same ID before the old queued continuation runs. "Page" is
    // present on session B's current page, so the latest generation must
    // terminate at page 20 without an obsolete page-10 continuation winning.
    document.searchText(searchId, QStringLiteral("Page"), false, Qt::CaseSensitive, Okular::Document::NextMatch, true, QColor(), sessionB.get());

    QTRY_COMPARE(spy.count(), 1);
    QTest::qWait(50);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(receiver.m_id, searchId);
    QCOMPARE(receiver.m_status, Okular::Document::MatchFound);

    QVERIFY(document.viewport() == defaultViewport);
    QVERIFY(sessionA->viewport() == sessionAViewport);
    QCOMPARE(sessionB->currentPage(), 20u);
    QVERIFY(sessionB->viewport().rePos.enabled);
}

void SearchTest::testViewportCallbackCanReplaceSearchGeneration()
{
    Okular::Document document(nullptr);
    QSignalSpy spy(&document, &Okular::Document::searchFinished);

    const QString testFile = QStringLiteral(KDESRCDIR "data/simple-multipage.pdf");
    QMimeDatabase db;
    QCOMPARE(document.openDocument(testFile, QUrl(), db.mimeTypeForFile(testFile)), Okular::Document::OpenSuccess);
    QVERIFY(document.pages() > 20);

    ViewportSearchReplacementObserver observer;
    observer.document = &document;
    observer.searchID = 103;
    auto session = document.createViewSession(&observer);
    observer.session = session.get();
    session->setViewportPage(15);
    observer.armed = true;

    // The first match moves the session and synchronously starts a replacement
    // search from its viewport callback. Only that replacement generation may
    // report completion after the callback returns.
    document.searchText(observer.searchID, QStringLiteral("Page"), false, Qt::CaseSensitive, Okular::Document::NextMatch, true, QColor(), session.get());

    QTRY_VERIFY(observer.triggered);
    QTRY_COMPARE(spy.count(), 1);
    QTest::qWait(50);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.constFirst().at(0).toInt(), observer.searchID);
    QCOMPARE(qvariant_cast<Okular::Document::SearchStatus>(spy.constFirst().at(1)), Okular::Document::MatchFound);
}

void SearchTest::testCompletionCallbackCanReplaceSearchGeneration()
{
    Okular::Document document(nullptr);
    QSignalSpy spy(&document, &Okular::Document::searchFinished);

    const QString testFile = QStringLiteral(KDESRCDIR "data/simple-multipage.pdf");
    QMimeDatabase db;
    QCOMPARE(document.openDocument(testFile, QUrl(), db.mimeTypeForFile(testFile)), Okular::Document::OpenSuccess);

    SetupSearchReplacementObserver observer;
    observer.document = &document;
    document.addObserver(&observer);

    const QList<Okular::Document::SearchType> completionTypes = {Okular::Document::AllDocument, Okular::Document::GoogleAny};
    int searchID = 104;
    for (Okular::Document::SearchType completionType : completionTypes) {
        observer.searchID = searchID++;
        observer.triggered = false;
        observer.armed = true;
        spy.clear();

        // All-document searches synchronously notifySetup() when committing
        // their results. Replacing the search there must prevent the old
        // completion branch from notifying or emitting for the new ID owner.
        document.searchText(observer.searchID, QStringLiteral("Page"), true, Qt::CaseSensitive, completionType, false, QColor());

        QTRY_VERIFY(observer.triggered);
        QTRY_COMPARE(spy.count(), 1);
        QTest::qWait(50);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.constFirst().at(0).toInt(), observer.searchID);
        QCOMPARE(qvariant_cast<Okular::Document::SearchStatus>(spy.constFirst().at(1)), Okular::Document::MatchFound);
        document.resetSearch(observer.searchID);
    }

    document.removeObserver(&observer);
}

void SearchTest::testResetSearchIsReentrant()
{
    Okular::Document document(nullptr);
    QSignalSpy spy(&document, &Okular::Document::searchFinished);

    const QString testFile = QStringLiteral(KDESRCDIR "data/simple-multipage.pdf");
    QMimeDatabase db;
    QCOMPARE(document.openDocument(testFile, QUrl(), db.mimeTypeForFile(testFile)), Okular::Document::OpenSuccess);

    const int searchID = 106;
    document.searchText(searchID, QStringLiteral("Page"), true, Qt::CaseSensitive, Okular::Document::NextMatch, false, QColor());
    QTRY_COMPARE(spy.count(), 1);

    ReentrantResetSearchObserver observer;
    observer.document = &document;
    observer.searchID = searchID;
    document.addObserver(&observer);
    observer.armed = true;

    // The nested reset must see the descriptor as already removed. In
    // particular it must not delete the RunningSearch that the outer reset is
    // still iterating or attempt to erase it twice.
    document.resetSearch(searchID);
    QCOMPARE(observer.triggerCount, 1);

    spy.clear();
    document.searchText(searchID, QStringLiteral("Page"), true, Qt::CaseSensitive, Okular::Document::NextMatch, false, QColor());
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(qvariant_cast<Okular::Document::SearchStatus>(spy.constFirst().at(1)), Okular::Document::MatchFound);

    document.removeObserver(&observer);
}

void SearchTest::testStaleWorkersDoNotNotifyReplacementDocument()
{
    Okular::Document document(nullptr);
    QSignalSpy spy(&document, &Okular::Document::searchFinished);
    HighlightChangeObserver observer;
    document.addObserver(&observer);

    const QString testFile = QStringLiteral(KDESRCDIR "data/simple-multipage.pdf");
    QMimeDatabase db;
    const QMimeType mime = db.mimeTypeForFile(testFile);
    QCOMPARE(document.openDocument(testFile, QUrl::fromLocalFile(testFile), mime), Okular::Document::OpenSuccess);

    const QList<Okular::Document::SearchType> pendingTypes = {Okular::Document::NextMatch, Okular::Document::AllDocument, Okular::Document::GoogleAny};
    int searchID = 107;
    for (Okular::Document::SearchType pendingType : pendingTypes) {
        // Seed a highlight so the next worker owns a non-empty changed-pages
        // set. It is precisely this deferred cleanup notification that must
        // not escape into a subsequently opened page topology.
        document.searchText(searchID, QStringLiteral("Page"), true, Qt::CaseSensitive, Okular::Document::NextMatch, false, QColor(Qt::yellow));
        QTRY_COMPARE(spy.count(), 1);

        spy.clear();
        observer.notifiedPages.clear();
        document.searchText(searchID, QStringLiteral("text that is not present"), true, Qt::CaseSensitive, pendingType, false, QColor(Qt::yellow));

        document.closeDocument();
        QCOMPARE(document.openDocument(testFile, QUrl::fromLocalFile(testFile), mime), Okular::Document::OpenSuccess);
        QTest::qWait(50);

        QVERIFY(observer.notifiedPages.isEmpty());
        QVERIFY(spy.isEmpty());
        ++searchID;
    }

    document.removeObserver(&observer);
}

void SearchTest::testCleanupNotificationStopsAfterDocumentReplacement()
{
    Okular::Document document(nullptr);

    const QString testFile = QStringLiteral(KDESRCDIR "data/simple-multipage.pdf");
    QMimeDatabase db;
    const QMimeType mime = db.mimeTypeForFile(testFile);
    QCOMPARE(document.openDocument(testFile, QUrl::fromLocalFile(testFile), mime), Okular::Document::OpenSuccess);

    QList<ReopeningHighlightObserver *> notificationOrder;
    bool replacementComplete = false;
    ReopeningHighlightObserver firstObserver;
    ReopeningHighlightObserver secondObserver;
    for (ReopeningHighlightObserver *observer : {&firstObserver, &secondObserver}) {
        observer->document = &document;
        observer->notificationOrder = &notificationOrder;
        observer->replacementComplete = &replacementComplete;
        observer->testFile = testFile;
        observer->mime = mime;
        document.addObserver(observer);
    }

    const int searchID = 110;
    document.searchText(searchID, QStringLiteral("Page"), true, Qt::CaseSensitive, Okular::Document::NextMatch, false, QColor(Qt::yellow));
    QTRY_COMPARE(notificationOrder.size(), 2);

    // Learn the QSet iteration order first, then make its first observer
    // replace the page topology during the search cleanup notification.
    ReopeningHighlightObserver *reopeningObserver = notificationOrder.constFirst();
    ReopeningHighlightObserver *laterObserver = reopeningObserver == &firstObserver ? &secondObserver : &firstObserver;
    notificationOrder.clear();
    firstObserver.notifiedPages.clear();
    secondObserver.notifiedPages.clear();
    reopeningObserver->replaceDocument = true;
    document.resetSearch(searchID);

    QCOMPARE(reopeningObserver->replacementCount, 1);
    QVERIFY(reopeningObserver->reopenSucceeded);
    QCOMPARE(reopeningObserver->notifiedPages.size(), 1);
    QVERIFY(laterObserver->notifiedPages.isEmpty());
    QCOMPARE(reopeningObserver->setupNotificationsAfterReplacement, 0);
    QCOMPARE(laterObserver->setupNotificationsAfterReplacement, 0);

    document.removeObserver(&firstObserver);
    document.removeObserver(&secondObserver);
}

void SearchTest::test323262()
{
    QList<QString> text;
    text << QStringLiteral("a\n");

    QList<Okular::NormalizedRect> rect;
    rect << Okular::NormalizedRect(1, 2, 3, 4);

    CREATE_PAGE;

    Okular::RegularAreaRect *result = tp->findText(0, QStringLiteral("a"), Okular::FromBottom, Qt::CaseSensitive, nullptr);
    QVERIFY(result);
    delete result;

    delete page;
}

void SearchTest::test323263()
{
    QList<QString> text;
    text << QStringLiteral("a") << QStringLiteral("a") << QStringLiteral("b");

    QList<Okular::NormalizedRect> rect;
    rect << Okular::NormalizedRect(0, 0, 1, 1) << Okular::NormalizedRect(1, 0, 2, 1) << Okular::NormalizedRect(2, 0, 3, 1);

    CREATE_PAGE;

    Okular::RegularAreaRect *result = tp->findText(0, QStringLiteral("ab"), Okular::FromTop, Qt::CaseSensitive, nullptr);
    QVERIFY(result);
    Okular::RegularAreaRect expected;
    expected.append(rect[1]);
    expected.append(rect[2]);
    expected.simplify();
    QCOMPARE(*result, expected);
    delete result;

    delete page;
}

void SearchTest::test430243()
{
    // 778 is COMBINING RING ABOVE
    // 197 is LATIN CAPITAL LETTER A WITH RING ABOVE
    QList<QString> text;
    text << QStringLiteral("A") << QString(QChar(778));

    QList<Okular::NormalizedRect> rect;
    rect << Okular::NormalizedRect(0, 0, 1, 1) << Okular::NormalizedRect(1, 0, 2, 1);

    CREATE_PAGE;

    Okular::RegularAreaRect *result = tp->findText(0, QString(QChar(197)), Okular::FromTop, Qt::CaseSensitive, nullptr);
    QVERIFY(result);
    Okular::RegularAreaRect expected;
    expected.append(rect[0] | rect[1]);
    QCOMPARE(*result, expected);
    delete result;

    delete page;
}

void SearchTest::testDottedI()
{
    // Earlier versions of okular had the bug that the letter "İ" (capital dotter i) did not match itself
    // in case-insensitive mode (this was caused by an unnecessary call of toLower() and the fact that
    // QString::fromUtf8("İ").compare(QString::fromUtf8("İ").toLower(), Qt::CaseInsensitive) == FALSE,
    // at least in Qt 4.8).

    // In the future it would be nice to add support for matching "İ"<->"i" and "I"<->"ı" in case-insensitive
    // mode as well (QString::compare does not match them, at least in non-Turkish locales, since it follows
    // the Unicode case-folding rules https://www.unicode.org/Public/6.2.0/ucd/CaseFolding.txt).

    QList<QString> text;
    text << QStringLiteral("İ");

    QList<Okular::NormalizedRect> rect;
    rect << Okular::NormalizedRect(1, 2, 3, 4);

    CREATE_PAGE;

    Okular::RegularAreaRect *result = tp->findText(0, QStringLiteral("İ"), Okular::FromTop, Qt::CaseInsensitive, nullptr);
    QVERIFY(result);
    delete result;

    delete page;
}

void SearchTest::testHyphenAtEndOfLineWithoutYOverlap()
{
    QList<QString> text;
    text << QStringLiteral("super-") << QStringLiteral("cali-\n") << QStringLiteral("fragilistic") << QStringLiteral("-") << QStringLiteral("expiali") << QStringLiteral("-\n") << QStringLiteral("docious");

    QList<Okular::NormalizedRect> rect;
    rect << Okular::NormalizedRect(0.4, 0.0, 0.9, 0.1) << Okular::NormalizedRect(0.0, 0.1, 0.6, 0.2) << Okular::NormalizedRect(0.0, 0.2, 0.8, 0.3) << Okular::NormalizedRect(0.8, 0.2, 0.9, 0.3) << Okular::NormalizedRect(0.0, 0.3, 0.8, 0.4)
         << Okular::NormalizedRect(0.8, 0.3, 0.9, 0.4) << Okular::NormalizedRect(0.0, 0.4, 0.7, 0.5);

    CREATE_PAGE;

    Okular::RegularAreaRect *result = tp->findText(0, QStringLiteral("supercalifragilisticexpialidocious"), Okular::FromTop, Qt::CaseSensitive, nullptr);
    QVERIFY(result);
    Okular::RegularAreaRect expected;
    for (int i = 0; i < text.size(); i++) {
        expected.append(rect[i]);
    }
    expected.simplify();
    QCOMPARE(*result, expected);
    delete result;

    result = tp->findText(0, QStringLiteral("supercalifragilisticexpialidocious"), Okular::FromBottom, Qt::CaseSensitive, nullptr);
    QVERIFY(result);
    QCOMPARE(*result, expected);
    delete result;

    // If the user is looking for the text explicitly with the hyphen also find it
    result = tp->findText(0, QStringLiteral("super-cali-fragilistic"), Okular::FromTop, Qt::CaseSensitive, nullptr);
    QVERIFY(result);
    delete result;

    // If the user is looking for the text explicitly with the hyphen also find it
    result = tp->findText(0, QStringLiteral("super-cali-fragilistic"), Okular::FromBottom, Qt::CaseSensitive, nullptr);
    QVERIFY(result);
    delete result;

    delete page;
}

#define CREATE_PAGE_AND_TEST_SEARCH(searchString, matchExpected)                                                                                                                                                                               \
    {                                                                                                                                                                                                                                          \
        CREATE_PAGE;                                                                                                                                                                                                                           \
                                                                                                                                                                                                                                               \
        Okular::RegularAreaRect *result = tp->findText(0, QStringLiteral(searchString), Okular::FromTop, Qt::CaseSensitive, NULL);                                                                                                             \
                                                                                                                                                                                                                                               \
        QCOMPARE(!!result, matchExpected);                                                                                                                                                                                                     \
                                                                                                                                                                                                                                               \
        delete result;                                                                                                                                                                                                                         \
        delete page;                                                                                                                                                                                                                           \
    }

void SearchTest::testHyphenWithYOverlap()
{
    QList<QString> text;
    text << QStringLiteral("a-") << QStringLiteral("b");

    QList<Okular::NormalizedRect> rect(2);

    // different lines (50% y-coordinate overlap), first rectangle has larger height
    rect[0] = Okular::NormalizedRect(0.0, 0.0, 0.9, 0.35);
    rect[1] = Okular::NormalizedRect(0.0, 0.3, 0.2, 0.4);
    CREATE_PAGE_AND_TEST_SEARCH("ab", true);

    // different lines (50% y-coordinate overlap), second rectangle has larger height
    rect[0] = Okular::NormalizedRect(0.0, 0.0, 0.9, 0.1);
    rect[1] = Okular::NormalizedRect(0.0, 0.05, 0.2, 0.4);
    CREATE_PAGE_AND_TEST_SEARCH("ab", true);

    // same line (90% y-coordinate overlap), first rectangle has larger height
    rect[0] = Okular::NormalizedRect(0.0, 0.0, 0.4, 0.2);
    rect[1] = Okular::NormalizedRect(0.4, 0.11, 0.6, 0.21);
    CREATE_PAGE_AND_TEST_SEARCH("ab", false);
    CREATE_PAGE_AND_TEST_SEARCH("a-b", true);

    // same line (90% y-coordinate overlap), second rectangle has larger height
    rect[0] = Okular::NormalizedRect(0.0, 0.0, 0.4, 0.1);
    rect[1] = Okular::NormalizedRect(0.4, 0.01, 0.6, 0.2);
    CREATE_PAGE_AND_TEST_SEARCH("ab", false);
    CREATE_PAGE_AND_TEST_SEARCH("a-b", true);
}

void SearchTest::testHyphenAtEndOfPage()
{
    // Tests for segmentation fault that would occur if
    // we tried look ahead (for determining whether the
    // next character is at the same line) at the end of the page.

    QList<QString> text;
    text << QStringLiteral("a-");

    QList<Okular::NormalizedRect> rect;
    rect << Okular::NormalizedRect(0, 0, 1, 1);

    CREATE_PAGE;

    {
        Okular::RegularAreaRect *result = tp->findText(0, QStringLiteral("a"), Okular::FromTop, Qt::CaseSensitive, nullptr);
        QVERIFY(result);
        delete result;
    }

    {
        Okular::RegularAreaRect *result = tp->findText(0, QStringLiteral("a"), Okular::FromBottom, Qt::CaseSensitive, nullptr);
        QVERIFY(result);
        delete result;
    }

    delete page;
}

void SearchTest::testOneColumn()
{
    // Tests that the layout analysis algorithm does not create too many columns.
    // Bug 326207 was caused by the fact that if all the horizontal breaks in a line
    // had the same length and were smaller than vertical breaks between lines then
    // the horizontal breaks were treated as column separators.
    //(Note that "same length" means "same length after rounding rectangles to integer pixels".
    // The resolution used by the XY Cut algorithm with a square page is 1000 x 1000,
    // and the horizontal spaces in the example are 0.1, so they are indeed both exactly 100 pixels.)

    QList<QString> text;
    text << QStringLiteral("Only") << QStringLiteral("one") << QStringLiteral("column") << QStringLiteral("here");

    // characters and line breaks have length 0.05, word breaks 0.1
    QList<Okular::NormalizedRect> rect;
    rect << Okular::NormalizedRect(0.0, 0.0, 0.2, 0.1) << Okular::NormalizedRect(0.3, 0.0, 0.5, 0.1) << Okular::NormalizedRect(0.6, 0.0, 0.9, 0.1) << Okular::NormalizedRect(0.0, 0.15, 0.2, 0.25);

    CREATE_PAGE;

    Okular::RegularAreaRect *result = tp->findText(0, QStringLiteral("Only one column"), Okular::FromTop, Qt::CaseSensitive, nullptr);
    QVERIFY(result);
    delete result;

    delete page;
}

void SearchTest::testTwoColumns()
{
    // Tests that the layout analysis algorithm can detect two columns.

    QList<QString> text;
    text << QStringLiteral("This") << QStringLiteral("text") << QStringLiteral("in") << QStringLiteral("two") << QStringLiteral("is") << QStringLiteral("set") << QStringLiteral("columns.");

    // characters, word breaks and line breaks have length 0.05
    QList<Okular::NormalizedRect> rect;
    rect << Okular::NormalizedRect(0.0, 0.0, 0.20, 0.1) << Okular::NormalizedRect(0.25, 0.0, 0.45, 0.1) << Okular::NormalizedRect(0.6, 0.0, 0.7, 0.1) << Okular::NormalizedRect(0.75, 0.0, 0.9, 0.1)
         << Okular::NormalizedRect(0.0, 0.15, 0.1, 0.25) << Okular::NormalizedRect(0.15, 0.15, 0.3, 0.25) << Okular::NormalizedRect(0.6, 0.15, 1.0, 0.25);

    CREATE_PAGE;

    Okular::RegularAreaRect *result = tp->findText(0, QStringLiteral("This text in"), Okular::FromTop, Qt::CaseSensitive, nullptr);
    QVERIFY(!result);
    delete result;

    delete page;
}

QTEST_MAIN(SearchTest)
#include "searchtest.moc"
