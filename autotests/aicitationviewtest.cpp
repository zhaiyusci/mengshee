// SPDX-License-Identifier: GPL-2.0-or-later
#include "part/part.h"
#include "part/pageview.h"
#include "part/documentworkspace.h"
#include "core/action.h"
#include <QApplication>
#include <QPainter>
#include <QPdfWriter>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace Okular
{
class PartTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void viewportPositionRoundTrip()
    {
        for (auto position : {DocumentViewport::Center, DocumentViewport::TopLeft, DocumentViewport::TopInset}) {
            DocumentViewport original(2);
            original.rePos.enabled = true;
            original.rePos.pos = position;
            original.rePos.normalizedX = .25;
            original.rePos.normalizedY = .75;
            QVERIFY(DocumentViewport(original.toString()) == original);
        }
        QCOMPARE(DocumentViewport(QStringLiteral("2;C1:0.25:0.75")).rePos.pos, DocumentViewport::Center);
    }
    void navigationTopInset_data()
    {
        QTest::addColumn<double>("y");
        QTest::addColumn<int>("height");
        QTest::addColumn<QString>("route");
        for (const QString &route : {QStringLiteral("citation"), QStringLiteral("panel"), QStringLiteral("pdf-link"), QStringLiteral("auxiliary")}) {
            for (int height : {900, 1500}) {
                for (double y : {.01, .50, .99}) {
                    QTest::newRow(qPrintable(QStringLiteral("%1-%2-%3").arg(route).arg(y).arg(height))) << y << height << route;
                }
            }
        }
    }
    void navigationTopInset()
    {
        QFETCH(double, y);
        QFETCH(int, height);
        QFETCH(QString, route);
        QTemporaryDir directory(QDir::currentPath() + QStringLiteral("/citation-XXXXXX"));
        QVERIFY(directory.isValid());
        const QString file = directory.filePath(QStringLiteral("page.pdf"));
        {
            QPdfWriter writer(file);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QSizeF(600, 800), QPageSize::Point));
            writer.setPageMargins(QMarginsF());
            QPainter painter(&writer);
            for (int page = 0; page < 3; ++page) {
                if (page) {
                    QVERIFY(writer.newPage());
                }
                painter.drawText(100, 80, QStringLiteral("Top target"));
                painter.drawText(100, 400, QStringLiteral("Middle target"));
                painter.drawText(100, 720, QStringLiteral("Bottom target"));
            }
        }
        Part part(nullptr, {});
        part.widget()->resize(1000, height);
        part.widget()->show();
        QVERIFY(part.openUrl(QUrl::fromLocalFile(file)));
        auto *view = part.m_pageView.data();
        QTRY_VERIFY(view->displayedPageUncroppedSize(0).height() > 100);
        QTest::qWait(100);
        QCOMPARE(static_cast<View *>(view)->capability(View::ZoomModality).toInt(), int(PageView::ZoomFitWidth));
        const int targetPage = y < .5 ? 0 : y > .5 ? 2 : 1;
        view->goToDocumentViewport(DocumentViewport(targetPage == 0 ? 1 : 0), false);
        QTest::qWait(100);
        part.m_document->setPageHighlights(12345, targetPage, {{.2, y, .4, qMin(1.0, y + .01)}}, Qt::yellow);
        const auto source = view->documentViewport();
        DocumentViewport target(targetPage);
        target.rePos.enabled = true;
        target.rePos.pos = route == QLatin1String("citation") ? DocumentViewport::TopInset : DocumentViewport::TopLeft;
        target.rePos.normalizedX = .3;
        target.rePos.normalizedY = y;
        if (route == QLatin1String("auxiliary")) {
            part.openAuxiliaryView(view, target, QStringLiteral("Evidence"));
            QCOMPARE(part.m_documentWorkspace->auxiliaryViewCount(), 1);
            view = part.m_documentWorkspace->auxiliaryViews().first();
        }
        const auto jump = [&] {
            if (route == QLatin1String("pdf-link")) {
                GotoAction link(QString(), target);
                part.m_document->processAction(&link);
            } else {
                view->goToDocumentTarget(target);
            }
        };
        // Check auxiliary initialization itself, before using the common helper.
        if (route != QLatin1String("auxiliary")) {
            jump();
        }
        QTest::qWait(500);
        int page = -1;
        NormalizedPoint center;
        const auto targetScreenPoint = [&] {
            return QPoint(view->viewport()->width() / 2, qRound(40.0 * view->logicalDpiY() / 72.0));
        };
        QVERIFY(view->mapGlobalPosToPagePoint(view->viewport()->mapToGlobal(targetScreenPoint()), &page, &center));
        qInfo() << "target" << .5 << y << "actual" << center.x << center.y
                << "scroll" << view->verticalScrollBar()->minimum() << view->verticalScrollBar()->value() << view->verticalScrollBar()->maximum();
        QCOMPARE(page, targetPage);
        QCOMPARE(view->documentViewport().rePos.pos, DocumentViewport::TopInset);
        const QSize pageSize = view->displayedPageUncroppedSize(targetPage);
        QCOMPARE(static_cast<View *>(view)->capability(View::ZoomModality).toInt(), int(PageView::ZoomFitWidth));
        QVERIFY(pageSize.width() <= view->viewport()->width());
        QCOMPARE(view->horizontalScrollBar()->maximum(), 0);
        QVERIFY(qAbs(center.y - y) * pageSize.height() <= 2);
        QVERIFY(qAbs(center.x - .5) * pageSize.width() <= 2);
        // Relayout must retain the scroll room; otherwise the target snaps
        // back to the old boundary after the initial jump.
        part.widget()->resize(1050, height + 40);
        QTest::qWait(500);
        QVERIFY(view->mapGlobalPosToPagePoint(view->viewport()->mapToGlobal(targetScreenPoint()), &page, &center));
        QCOMPARE(page, targetPage);
        QVERIFY(qAbs(center.y - y) * view->displayedPageUncroppedSize(targetPage).height() <= 2);
        // Resizing a narrow pane can put several pages in view and change its
        // reported current page. Capture it after layout, before auxiliary navigation.
        const auto mainBeforeNavigation = part.m_pageView->documentViewport();
        const int mainScrollBeforeNavigation = part.m_pageView->verticalScrollBar()->value();
        jump();
        QTest::qWait(500);
        QVERIFY(view->mapGlobalPosToPagePoint(view->viewport()->mapToGlobal(targetScreenPoint()), &page, &center));
        const QSize resizedPage = view->displayedPageUncroppedSize(targetPage);
        QVERIFY(qAbs(center.y - y) * resizedPage.height() <= 2);
        QVERIFY(qAbs(center.x - .5) * resizedPage.width() <= 2);
        // Back/Forward restores the source and the inset destination. A page-only
        // jump retains normal top alignment rather than centering an invented point.
        view->goToPreviousViewport();
        QTRY_COMPARE(view->documentViewport().pageNumber, source.pageNumber);
        view->goToNextViewport();
        QTRY_COMPARE(view->documentViewport().pageNumber, targetPage);
        QCOMPARE(view->documentViewport().rePos.pos, DocumentViewport::TopInset);
        QTest::qWait(500);
        QVERIFY(view->mapGlobalPosToPagePoint(view->viewport()->mapToGlobal(targetScreenPoint()), &page, &center));
        QCOMPARE(page, targetPage);
        QVERIFY(qAbs(center.y - y) * resizedPage.height() <= 2);
        const int otherPage = (targetPage + 1) % 3;
        view->goToDocumentTarget(DocumentViewport(otherPage));
        QTRY_COMPARE(view->documentViewport().pageNumber, otherPage);
        QVERIFY(!view->documentViewport().rePos.enabled);
        if (route == QLatin1String("auxiliary")) {
            QCOMPARE(part.m_pageView->documentViewport().toString(), mainBeforeNavigation.toString());
            QCOMPARE(part.m_pageView->verticalScrollBar()->value(), mainScrollBeforeNavigation);
            QCOMPARE(part.m_document->currentPage(), uint(mainBeforeNavigation.pageNumber));
        }
    }
};
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    Okular::PartTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "aicitationviewtest.moc"
