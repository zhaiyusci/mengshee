// SPDX-License-Identifier: GPL-2.0-or-later
#include "part/part.h"
#include "part/pageview.h"
#include "core/document.h"
#include "core/page.h"
#include "core/textpage.h"
#include <QApplication>
#include <QClipboard>
#include <QMouseEvent>
#include <QStandardPaths>
#include <QtTest>

namespace Okular
{
class PartTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void gappedGlyphs()
    {
        TextEntity::List glyphs;
        const QString word = QStringLiteral("Page");
        for (int i = 0; i < word.size(); ++i) {
            // Real glyph bounds may have kerning gaps; these are not word separators.
            glyphs.append(TextEntity(word.mid(i, 1), {0.1 + i * .035, .1, .12 + i * .035, .13}));
        }
        Page page(0, 600, 800, Rotation0);
        page.setTextPage(new TextPage(QList<TextEntity::List>{glyphs}));
        QCOMPARE(page.text(), word);
        QCOMPARE(page.words(nullptr, TextPage::CentralPixelTextAreaInclusionBehaviour).size(), 4);
        std::unique_ptr<RegularAreaRect> full(page.findText(1, word, FromTop, Qt::CaseSensitive));
        QVERIFY(full);
        std::unique_ptr<RegularAreaRect> middle(page.findText(2, QStringLiteral("ag"), FromTop, Qt::CaseSensitive));
        QVERIFY(middle);
        QCOMPARE(page.text(middle.get()), QStringLiteral("ag"));
        auto selectedWord = page.wordAt(glyphs[1].area().center());
        QVERIFY(selectedWord);
        QCOMPARE(page.text(selectedWord.get()), word);
        const auto extracted = page.words(nullptr, TextPage::CentralPixelTextAreaInclusionBehaviour);
        for (int i = 0; i < glyphs.size(); ++i) {
            QCOMPARE(extracted[i].area(), glyphs[i].area());
        }
    }

    void groupedUnicode()
    {
        QList<TextEntity::List> words{
            {TextEntity(QStringLiteral("A"), {.1, .1, .12, .13}), TextEntity(QString(QChar(0x030a)), {.1, .09, .12, .11})},
            {TextEntity(QStringLiteral("中"), {.2, .1, .22, .13}), TextEntity(QStringLiteral("文"), {.23, .1, .25, .13})}};
        Page page(0, 600, 800, Rotation0);
        page.setTextPage(new TextPage(words));
        QCOMPARE(page.text(), QStringLiteral("Å 中文"));
        std::unique_ptr<RegularAreaRect> match(page.findText(1, QStringLiteral("文"), FromTop, Qt::CaseSensitive));
        QVERIFY(match);
        QCOMPARE(page.text(match.get()), QStringLiteral("文"));
    }

    void mouseSelection_data()
    {
        QTest::addColumn<bool>("reverse");
        QTest::addColumn<bool>("doubleClick");
        QTest::newRow("drag-forward") << false << false;
        QTest::newRow("drag-backward") << true << false;
        QTest::newRow("double-click-word") << false << true;
    }

    void mouseSelection()
    {
        QFETCH(bool, reverse);
        QFETCH(bool, doubleClick);
        Part part(nullptr, {});
        part.widget()->resize(1100, 1000);
        part.widget()->show();
        QVERIFY(part.openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/simple-multipage.pdf"))));
        auto *document = part.m_document;
        document->requestTextPage(0);
        const auto *page = document->page(0);
        QTRY_VERIFY(page->hasTextPage());
        QVERIFY2(page->text().contains(QStringLiteral("Page")), qPrintable(page->text()));
        QVERIFY(!page->text().contains(QStringLiteral("P age")));
        std::unique_ptr<RegularAreaRect> match(page->findText(1, QStringLiteral("Page"), FromTop, Qt::CaseSensitive));
        QVERIFY(match);
        const auto glyphs = page->words(match.get(), TextPage::CentralPixelTextAreaInclusionBehaviour);
        QCOMPARE(glyphs.size(), 4);
        QCOMPARE(glyphs[1].text(), QStringLiteral("a"));
        QCOMPARE(glyphs[2].text(), QStringLiteral("g"));
        std::unique_ptr<RegularAreaRect> partial(page->findText(2, QStringLiteral("ag"), FromTop, Qt::CaseSensitive));
        QVERIFY(partial);
        QCOMPARE(page->text(partial.get(), TextPage::CentralPixelTextAreaInclusionBehaviour), QStringLiteral("ag"));

        auto *view = part.m_pageView.data();
        auto *viewport = view->viewport();
        QVERIFY(QMetaObject::invokeMethod(view, "slotSetMouseTextSelect"));
        QTRY_VERIFY(page->hasPixmap(view));
        const QSize displayed = view->displayedPageUncroppedSize(0);
        int pageNumber = -1;
        NormalizedPoint center;
        const QPoint middle = viewport->rect().center();
        QVERIFY(view->mapGlobalPosToPagePoint(viewport->mapToGlobal(middle), &pageNumber, &center));
        QCOMPARE(pageNumber, 0);
        const QPointF origin = QPointF(middle) - QPointF(center.x * displayed.width(), center.y * displayed.height());
        auto position = [&](const NormalizedPoint &p) { return (origin + QPointF(p.x * displayed.width(), p.y * displayed.height())).toPoint(); };
        QPoint from = position(glyphs[1].area().center());
        QPoint to = position(glyphs[2].area().center());
        QVERIFY(viewport->rect().contains(from));
        QVERIFY(viewport->rect().contains(to));
        if (doubleClick) {
            QTest::mouseDClick(viewport, Qt::LeftButton, Qt::NoModifier, from);
            QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, from);
        } else {
            if (reverse)
                std::swap(from, to);
            QTest::mouseMove(viewport, from);
            QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, from);
            QMouseEvent move(QEvent::MouseMove, QPointF(to), QPointF(viewport->mapToGlobal(to)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(viewport, &move);
            QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, to);
        }
        QTRY_VERIFY(page->textSelection());
        const QString expected = doubleClick ? QStringLiteral("Page") : QStringLiteral("ag");
        QCOMPARE(page->text(page->textSelection(), TextPage::CentralPixelTextAreaInclusionBehaviour), expected);
        QApplication::clipboard()->clear();
        QVERIFY(QMetaObject::invokeMethod(view, "copyTextSelection", Q_ARG(PageView::TextCopyMode, PageView::TextCopyMode::AsProvided)));
        QCOMPARE(QApplication::clipboard()->text(), expected);
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
#include "textselectiontest.moc"
