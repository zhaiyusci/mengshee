// SPDX-License-Identifier: GPL-2.0-or-later
#include "part/part.h"
#include "part/pageview.h"
#include "part/latexnoteutils.h"
#include "part/annotationpopup.h"
#include "core/annotationframe.h"
#include "core/document.h"
#include "core/page.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QStandardPaths>
#include <QScopeGuard>
#include <QTimer>
#include <QTemporaryDir>
#include <QtTest>

namespace Okular {
class PartTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void draggedPadding_data()
    {
        QTest::addColumn<double>("padding");
        QTest::addColumn<bool>("leftHandle");
        for (double padding : {3.0, 12.0}) {
            for (bool left : {false, true})
                QTest::newRow(qPrintable(QStringLiteral("padding-%1-%2").arg(padding).arg(left ? "left" : "right"))) << padding << left;
        }
    }

    void draggedPadding()
    {
        if (qEnvironmentVariableIntValue("OKULAR_TEST_LATEX_ASYNC") != 1)
            QSKIP("Set OKULAR_TEST_LATEX_ASYNC=1 to run native drag/reflow integration tests");
        QFETCH(double, padding);
        QFETCH(bool, leftHandle);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        qputenv("MENGSHEE_LATEX_TEMP_ROOT", directory.filePath(QStringLiteral("appearances")).toUtf8());
        const QString file = directory.filePath(QStringLiteral("page.pdf"));
        {
            QPdfWriter writer(file);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QSizeF(600, 800), QPageSize::Point));
            writer.setPageMargins(QMarginsF());
            QPainter painter(&writer);
            painter.fillRect(QRect(0, 0, 600, 800), Qt::white);
        }
        Part part(nullptr, {});
        part.widget()->resize(1000, 1000);
        part.widget()->show(); // The executable forces Qt's offscreen platform.
        QVERIFY(part.openUrl(QUrl::fromLocalFile(file)));
        QTRY_COMPARE(part.m_document->pages(), 1u);
        auto *document = part.m_document;
        auto *note = new StampAnnotation;
        note->setBoundingRectangle({.2, .2, .75, .4});
        note->setContents(QStringLiteral("\\LaTeX{} national scientific foundation of China"));
        note->setOkularLatex(true);
        note->setLatexNoteType(Annotation::LatexNoteBoxed);
        note->setLatexPadding(padding);
        note->setLatexFontSize(12);
        note->style().setWidth(1);
        document->addPageAnnotation(0, note);
        LatexNoteUtils::RenderResult rendered;
        QElapsedTimer timer;
        timer.start();
        do {
            rendered = LatexNoteUtils::renderAppearancePdf(note->contents(), Qt::black, 300, false, 12);
            if (rendered.ok && rendered.errorMessage.isEmpty()) break;
            QTest::qWait(100);
        } while (timer.elapsed() < 60000);
        QVERIFY2(rendered.ok && rendered.errorMessage.isEmpty(), qPrintable(rendered.errorMessage));
        QVERIFY(LatexNoteUtils::updateLatexStampAnnotationAppearance(nullptr, document, 0, note, Qt::black, Qt::yellow, Qt::red, 300, true));
        auto *view = part.m_pageView.data();
        auto *viewport = view->viewport();
        QTRY_VERIFY(view->displayedPageUncroppedSize(0).width() > 100);
        QTest::qWait(100);
        const QSize displayed = view->displayedPageUncroppedSize(0);
        int pageNumber = -1;
        NormalizedPoint center;
        const QPoint middle = viewport->rect().center();
        QVERIFY(view->mapGlobalPosToPagePoint(viewport->mapToGlobal(middle), &pageNumber, &center));
        QCOMPARE(pageNumber, 0);
        const QPointF origin = QPointF(middle) - QPointF(center.x * displayed.width(), center.y * displayed.height());
        auto position = [&](double x, double y) { return (origin + QPointF(x * displayed.width(), y * displayed.height())).toPoint(); };
        auto drag = [&](QPoint from, QPoint to) {
            QTest::mouseMove(viewport, from);
            QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, from);
            QMouseEvent move(QEvent::MouseMove, QPointF(to), QPointF(viewport->mapToGlobal(to)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(viewport, &move);
            QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, to);
        };
        // Exercise the real mouse interaction and its asynchronous completion,
        // then compare it with an explicit edit at the same paragraph width.
        for (int delta : {-80, 45, -35}) {
            const auto before = note->boundingRectangle();
            const QString oldAppearance = note->latexAppearancePdfFileName();
            QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, position((before.left + before.right) / 2, (before.top + before.bottom) / 2));
            const QPoint handle = position(leftHandle ? before.left : before.right, (before.top + before.bottom) / 2);
            drag(handle, handle + QPoint(leftHandle ? -delta : delta, 0));
            QTRY_VERIFY_WITH_TIMEOUT(note->latexAppearancePdfFileName() != oldAppearance, 30000);
            const double layoutWidth = note->latexLayoutWidth();
            const auto content = LatexNoteUtils::renderAppearancePdf(note->contents(), Qt::black, layoutWidth, false, 12);
            QVERIFY2(content.ok && content.errorMessage.isEmpty(), qPrintable(content.errorMessage));
            const QSizeF actual = AnnotationFrame::localSize(note->boundingRectangle(), document->pageSizeInPoints(0), note->appearanceRotation());
            const double rightPadding = actual.width() - padding - content.pdfSizePoints.width();
            qInfo() << "Dragged width:" << actual.width() << "layout:" << layoutWidth << "content PDF:" << content.pdfSizePoints << "padding left/right:" << padding << rightPadding;
            QVERIFY2(rightPadding >= padding - .02, qPrintable(QStringLiteral("Right padding %1 pt, expected at least %2 pt").arg(rightPadding).arg(padding)));
            const auto draggedRect = note->boundingRectangle();
            const QImage draggedImage = document->renderToImage(0, 144, true);
            QVERIFY(!draggedImage.isNull());
            QVERIFY(LatexNoteUtils::updateLatexStampAnnotationAppearance(nullptr, document, 0, note, Qt::black, Qt::yellow, Qt::red, layoutWidth, true));
            const auto editedRect = note->boundingRectangle();
            QVERIFY(qAbs(draggedRect.left - editedRect.left) < 1e-6);
            QVERIFY(qAbs(draggedRect.top - editedRect.top) < 1e-6);
            QVERIFY(qAbs(draggedRect.right - editedRect.right) < 1e-6);
            QVERIFY(qAbs(draggedRect.bottom - editedRect.bottom) < 1e-6);
            QCOMPARE(document->renderToImage(0, 144, true), draggedImage);
        }
        // Editing after a resize must keep the stored paragraph width. The
        // rendered PDF's preview border must not become new paragraph space.
        const double paragraphWidth = note->latexLayoutWidth();
        const double frameWidth = LatexNoteUtils::annotationWidthInPoints(note, document->page(0));
        for (int edit = 0; edit < 3; ++edit) {
            const auto rect = note->boundingRectangle();
            QTest::mouseDClick(viewport, Qt::LeftButton, Qt::NoModifier, position((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2));
            QWidget *popup = nullptr;
            QTRY_VERIFY((popup = part.widget()->findChild<QWidget *>(QStringLiteral("AnnotWindow"))) && popup->isVisible());
            QWidget *editor = popup->focusProxy();
            QVERIFY(editor);
            const QString source = note->contents() + QLatin1Char('.');
            const QString previousAppearance = note->latexAppearancePdfFileName();
            const char *setter = editor->inherits("QsciScintilla") ? "setText" : "setPlainText";
            QVERIFY(QMetaObject::invokeMethod(editor, setter, Qt::DirectConnection, Q_ARG(QString, source)));
            popup->close();
            QTRY_COMPARE(note->contents(), source);
            QTRY_VERIFY_WITH_TIMEOUT(note->latexAppearancePdfFileName() != previousAppearance, 30000);
            QCOMPARE(note->latexLayoutWidth(), paragraphWidth);
            QVERIFY(qAbs(LatexNoteUtils::annotationWidthInPoints(note, document->page(0)) - frameWidth) < .02);
        }

        // Reach the real properties dialog through its context-menu action.
        // Font edits preserve paragraph width; padding edits redistribute it
        // inside the same outer frame, including the PDF's preview border.
        AnnotationPopup popup(document, AnnotationPopup::SingleAnnotationMode, part.widget());
        popup.addAnnotation(note, 0);
        QMenu menu;
        popup.addActionsToMenu(&menu);
        QAction *properties = nullptr;
        for (QAction *action : menu.actions()) {
            if (action->text().remove(QLatin1Char('&')) == QLatin1String("Properties"))
                properties = action;
        }
        QVERIFY(properties);
        for (const auto &change : {qMakePair(QStringLiteral("Font size:"), 13.0),
                                   qMakePair(QStringLiteral("Font size:"), 12.0),
                                   qMakePair(QStringLiteral("Padding:"), padding + 5.0),
                                   qMakePair(QStringLiteral("Padding:"), padding)}) {
            bool applied = false;
            QTimer::singleShot(0, &menu, [&] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                const auto closeDialog = qScopeGuard([dialog] { dialog->reject(); });
                QDoubleSpinBox *spin = nullptr;
                for (QLabel *label : dialog->findChildren<QLabel *>()) {
                    if (label->text().remove(QLatin1Char('&')) == change.first)
                        spin = qobject_cast<QDoubleSpinBox *>(label->buddy());
                }
                QVERIFY(spin);
                spin->setValue(change.second);
                applied = QMetaObject::invokeMethod(dialog, "slotapply", Qt::DirectConnection);
            });
            properties->trigger();
            QVERIFY(applied);
            const double expectedWidth = paragraphWidth + 2.0 * (padding - note->latexPadding());
            QVERIFY(qAbs(note->latexLayoutWidth() - expectedWidth) < 1e-6);
            QVERIFY(qAbs(LatexNoteUtils::annotationWidthInPoints(note, document->page(0)) - frameWidth) < .02);
        }
    }
};
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("LANGUAGE", "en_US"); // Context-menu and form-label lookup below.
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    Okular::PartTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "latexnotedragtest.moc"
