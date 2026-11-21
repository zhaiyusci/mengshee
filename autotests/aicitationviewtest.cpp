// SPDX-License-Identifier: GPL-2.0-or-later
#include "part/part.h"
#include "part/pageview.h"
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
    void citationCenter_data()
    {
        QTest::addColumn<double>("y");
        QTest::addColumn<int>("height");
        for (int height : {900, 1500}) {
            QTest::newRow(qPrintable(QStringLiteral("top-%1").arg(height))) << .10 << height;
            QTest::newRow(qPrintable(QStringLiteral("middle-%1").arg(height))) << .50 << height;
            QTest::newRow(qPrintable(QStringLiteral("bottom-%1").arg(height))) << .90 << height;
        }
    }
    void citationCenter()
    {
        QFETCH(double, y);
        QFETCH(int, height);
        QTemporaryDir directory(QDir::currentPath() + QStringLiteral("/citation-XXXXXX"));
        QVERIFY(directory.isValid());
        const QString file = directory.filePath(QStringLiteral("page.pdf"));
        {
            QPdfWriter writer(file);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QSizeF(600, 800), QPageSize::Point));
            writer.setPageMargins(QMarginsF());
            QPainter painter(&writer);
            painter.drawText(100, 80, QStringLiteral("Top citation"));
            painter.drawText(100, 400, QStringLiteral("Middle citation"));
            painter.drawText(100, 720, QStringLiteral("Bottom citation"));
        }
        Part part(nullptr, {});
        part.widget()->resize(1000, height);
        part.widget()->show();
        QVERIFY(part.openUrl(QUrl::fromLocalFile(file)));
        auto *view = part.m_pageView.data();
        QTRY_VERIFY(view->displayedPageUncroppedSize(0).height() > 100);
        QTest::qWait(100);
        part.m_document->setPageHighlights(12345, 0, {{.2, y - .01, .4, y + .01}}, Qt::yellow);
        DocumentViewport target(0);
        target.rePos.enabled = true;
        target.rePos.pos = DocumentViewport::Center;
        target.rePos.normalizedX = .3;
        target.rePos.normalizedY = y;
        view->goToCenteredDocumentViewport(target);
        QTest::qWait(500);
        int page = -1;
        NormalizedPoint center;
        QVERIFY(view->mapGlobalPosToPagePoint(view->viewport()->mapToGlobal(view->viewport()->rect().center()), &page, &center));
        qInfo() << "target" << .3 << y << "actual" << center.x << center.y
                << "scroll" << view->verticalScrollBar()->minimum() << view->verticalScrollBar()->value() << view->verticalScrollBar()->maximum();
        QCOMPARE(page, 0);
        const QSize pageSize = view->displayedPageUncroppedSize(0);
        QVERIFY(qAbs(center.y - y) * pageSize.height() <= 2);
        QVERIFY(qAbs(center.x - .3) * pageSize.width() <= 2);
        // Relayout must retain the scroll room; otherwise the target snaps
        // back to the old boundary after the initial jump.
        part.widget()->resize(1050, height + 40);
        QTest::qWait(500);
        view->goToCenteredDocumentViewport(target);
        QTest::qWait(500);
        QVERIFY(view->mapGlobalPosToPagePoint(view->viewport()->mapToGlobal(view->viewport()->rect().center()), &page, &center));
        const QSize resizedPage = view->displayedPageUncroppedSize(0);
        QVERIFY(qAbs(center.y - y) * resizedPage.height() <= 2);
        QVERIFY(qAbs(center.x - .3) * resizedPage.width() <= 2);
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
