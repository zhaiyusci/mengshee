#include "../../../part/latexnoteutils.h"
#include "../../../part/annotationpopup.h"
#include <QMenu>
#include <QClipboard>
#include <QMimeData>
#include <QTimer>
#include <KActionCollection>
#include "../../../part/pageview.h"
// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../../part/part.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QAction>
#include <QPainter>
#include <QPdfWriter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <core/annotations.h>
#include <core/document.h>
#include <core/page.h>
#include <core/annotationframe.h>
#include "../runtimehelpers.h"
#include <core/utils.h>
namespace Okular
{
class PartTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void automaticNoteHeight()
    {
        const QSizeF page(400,700);
        const NormalizedRect original(.2,.3,.5,.6);
        for (int angle : {0,90,180,270}) {
            const auto local=AnnotationFrame::localSize(original,page,angle);
            for (double height : {18.0,300.0}) {
                const auto fitted=AnnotationFrame::withLocalHeight(original,page,angle,height);
                const auto fittedLocal=AnnotationFrame::localSize(fitted,page,angle);
                QVERIFY(qAbs(fittedLocal.width()-local.width())<1e-8);
                QVERIFY(qAbs(fittedLocal.height()-height)<1e-8);
                const auto restored=AnnotationFrame::withLocalHeight(fitted,page,angle,local.height());
                QVERIFY(qAbs(restored.left-original.left)<1e-8);
                QVERIFY(qAbs(restored.top-original.top)<1e-8);
                QVERIFY(qAbs(restored.right-original.right)<1e-8);
                QVERIFY(qAbs(restored.bottom-original.bottom)<1e-8);
            }
        }
    }
    void newLatexNoteHeight()
    {
        if(qEnvironmentVariableIsEmpty("MENGSHEE_TEST_NATIVE_LATEX")) QSKIP("Enable native StemTeX integration test explicitly.");
        QTemporaryDir directory;
        const auto file=directory.filePath(QStringLiteral("page.pdf"));
        { QPdfWriter writer(file); QPainter painter(&writer); painter.fillRect(0,0,200,200,Qt::white); }
        Part part(nullptr,{});
        part.widget()->resize(800,600); part.widget()->show();
        QVERIFY(part.openUrl(QUrl::fromLocalFile(file)));
        QTRY_COMPARE(part.m_document->pages(),1u);
        auto *action=part.actionCollection()->action(QStringLiteral("annotation_add_latex_inline_note"));
        QVERIFY(action); QTRY_VERIFY(action->isEnabled()); action->trigger();
        QTest::qWait(100);
        auto *viewport=part.m_pageView->viewport();
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,viewport->rect().center());
        QTRY_COMPARE(part.m_document->page(0)->annotations().size(),1);
        auto *note=part.m_document->page(0)->annotations().first();
        QCOMPARE(note->contents(),QStringLiteral("\\LaTeX"));
        QVERIFY(note->flags() & Okular::Annotation::ExternallyDrawn);
        const auto pendingImage = part.m_document->renderToImage(0, 72, true);
        QVERIFY(!pendingImage.isNull());
        QVERIFY(!note->latexAppearancePdfFileName().contains(QStringLiteral("latex-default-note")));
        QTRY_VERIFY_WITH_TIMEOUT(!note->latexAppearancePdfFileName().isEmpty(),120000);
        const auto renderedImage = part.m_document->renderToImage(0, 72, true);
        QVERIFY(renderedImage != pendingImage);
        const auto pdfSize=MengsheePdfRuntime::firstPageSize(note->latexAppearancePdfFileName());
        QVERIFY(pdfSize.isValid());
        const auto local=AnnotationFrame::localSize(note->boundingRectangle(),AnnotationFrame::pageSize(part.m_document->page(0),Okular::Utils::realDpi(nullptr)),note->appearanceRotation());
        QVERIFY(qAbs(local.height()-pdfSize.height()-2*note->latexPadding())<.02);
        QVERIFY(qAbs(local.width()-note->latexLayoutWidth()-2*note->latexPadding())<.02);
    }
    void clipboardImageNote_data()
    {
        QTest::addColumn<bool>("fileUrl");
        QTest::newRow("bitmap") << false;
        QTest::newRow("local-image-file") << true;
    }
    void clipboardImageNote()
    {
        QFETCH(bool,fileUrl);
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName(QStringLiteral("mengshee-clipboard-ui-test"));
        QTemporaryDir directory;
        const auto file=directory.filePath(QStringLiteral("page.pdf"));
        {
            QPdfWriter writer(file);
            QPainter painter(&writer);
            painter.fillRect(0,0,200,200,Qt::white);
        }
        Part part(nullptr,{});
        part.widget()->resize(800,600);
        part.widget()->show();
        QVERIFY(part.openUrl(QUrl::fromLocalFile(file)));
        QTRY_COMPARE(part.m_document->pages(),1u);
        auto *action=part.actionCollection()->action(QStringLiteral("annotation_add_image_note"));
        QVERIFY(action);
        QTRY_VERIFY(action->isEnabled());
        QApplication::clipboard()->setText(QStringLiteral("not an image"));
        bool disabled=false;
        QTimer::singleShot(0,[&]() {
            auto *menu=qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if(!menu) return;
            for(auto *item:menu->actions()) if(item->objectName()==QStringLiteral("image_note_from_clipboard"))
                disabled=!item->isEnabled();
            menu->close();
        });
        action->trigger();
        QVERIFY(disabled);
        QCOMPARE(part.m_document->page(0)->annotations().size(),0);
        QImage image(80,40,QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        { QPainter painter(&image); painter.fillRect(0,0,40,40,Qt::red); }
        if(fileUrl) {
            const auto imageFile=directory.filePath(QStringLiteral("clipboard.png"));
            QVERIFY(image.save(imageFile));
            auto *mime=new QMimeData;
            mime->setUrls({QUrl::fromLocalFile(imageFile)});
            QApplication::clipboard()->setMimeData(mime);
        } else {
            QApplication::clipboard()->setImage(image);
        }
        bool selected=false;
        QTimer::singleShot(0,[&]() {
            auto *menu=qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if(!menu) return;
            for(auto *item:menu->actions()) if(item->objectName()==QStringLiteral("image_note_from_clipboard")) {
                selected=item->isEnabled();
                QTest::mouseClick(menu,Qt::LeftButton,Qt::NoModifier,menu->actionGeometry(item).center());
                return;
            }
            menu->close();
        });
        action->trigger();
        QVERIFY(selected);
        QApplication::clipboard()->clear();
        QTest::qWait(100);
        auto *viewport=part.m_pageView->viewport();
        QTest::mouseClick(viewport,Qt::LeftButton,Qt::NoModifier,viewport->rect().center());
        QTRY_COMPARE(part.m_document->page(0)->annotations().size(),1);
        auto *stamp=dynamic_cast<StampAnnotation *>(part.m_document->page(0)->annotations().first());
        QVERIFY(stamp);
        const QImage stored(stamp->stampImagePath());
        QCOMPARE(stored.size(),image.size());
        QCOMPARE(stored.pixelColor(60,20),QColor(Qt::transparent));
        QCOMPARE(stored.pixelColor(20,20),QColor(Qt::red));
        part.m_document->undo();
        QCOMPARE(part.m_document->page(0)->annotations().size(),0);
        part.m_document->redo();
        QCOMPARE(part.m_document->page(0)->annotations().size(),1);
    }
    void rotationMenu()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName(QStringLiteral("mengshee-rotation-ui-test"));
        KConfigGroup config(KSharedConfig::openConfig(), QStringLiteral("Page Editing"));
        config.writeEntry("RotateAnnotationsWithPage", false);
        config.sync();
        QTemporaryDir directory;
        const auto file = directory.filePath(QStringLiteral("page.pdf"));
        {
            QPdfWriter writer(file);
            writer.setResolution(72);writer.setPageSize(QPageSize(QSizeF(400,300),QPageSize::Point));
            QPainter painter(&writer);
            painter.drawText(100, 100, QStringLiteral("Rotate"));
        }
        {
            Part part(nullptr, {});

            QVERIFY(part.openUrl(QUrl::fromLocalFile(file)));

            QTRY_COMPARE(part.m_document->pages(), 1u);
            QImage bitmap(40, 20, QImage::Format_RGB32);
            bitmap.fill(Qt::red);
            {
                QPainter painter(&bitmap);
                painter.fillRect(0, 0, 15, 10, Qt::blue);
            }
            const auto imageFile = directory.filePath(QStringLiteral("image.png"));
            QVERIFY(bitmap.save(imageFile));
            auto *stamp = new StampAnnotation;
            stamp->setBoundingRectangle({.2, .2, .4, .3});
            stamp->setStampImagePath(imageFile);
            part.m_document->addPageAnnotation(0, stamp);
            const auto original = part.m_document->renderToImage(0, 36, true);
            QVERIFY(qAbs(LatexNoteUtils::annotationWidthInPoints(stamp,part.m_document->page(0))-80)<0.01);
            {
                AnnotationPopup popup(part.m_document, AnnotationPopup::SingleAnnotationMode);

                popup.addAnnotation(stamp, 0);
                QMenu menu;
                popup.addActionsToMenu(&menu);
                QAction *right = nullptr, *left = nullptr;
                for (auto *action : menu.actions()) {
                    if (action->objectName() == QStringLiteral("rotate_annotation_right"))
                        right = action;
                    if (action->objectName() == QStringLiteral("rotate_annotation_left"))
                        left = action;
                }
                QVERIFY(right && left);
                QVERIFY(right->isEnabled());

                right->trigger();
                QCOMPARE(stamp->appearanceRotation(), 90);
                QVERIFY(qAbs(LatexNoteUtils::annotationWidthInPoints(stamp,part.m_document->page(0))-80)<0.01);
                part.m_document->setRotation(1);
                QVERIFY(qAbs(LatexNoteUtils::annotationWidthInPoints(stamp,part.m_document->page(0))-80)<0.01);
                part.m_document->setRotation(0);
                const auto selfRotated = part.m_document->renderToImage(0, 36, true);
                QVERIFY(selfRotated != original);
                part.m_document->undo();
                QCOMPARE(stamp->appearanceRotation(), 0);
                QCOMPARE(part.m_document->renderToImage(0, 36, true), original);
                part.m_document->redo();
                QCOMPARE(stamp->appearanceRotation(), 90);
                QCOMPARE(part.m_document->renderToImage(0, 36, true), selfRotated);
                left->trigger();
                QCOMPARE(stamp->appearanceRotation(), 0);
                QCOMPARE(part.m_document->renderToImage(0, 36, true), original);
            }

            part.setPageRotation(0, 90);
            QCOMPARE(int(part.m_document->page(0)->orientation()), 1);
            const auto rotated = part.m_document->renderToImage(0, 36, true);
            part.m_document->undo();
            QCOMPARE(int(part.m_document->page(0)->orientation()), 0);
            QCOMPARE(part.m_document->renderToImage(0, 36, true), original);
            part.m_document->redo();
            QCOMPARE(int(part.m_document->page(0)->orientation()), 1);
            QCOMPARE(part.m_document->renderToImage(0, 36, true), rotated);
            part.setPageRotation(0, 0);
            QCOMPARE(part.m_document->renderToImage(0, 36, true), original);

            part.closeUrl(false);
        }
        config.deleteEntry("RotateAnnotationsWithPage");
        config.sync();
    }
};
}
QTEST_MAIN(Okular::PartTest)
#include "rotationuitest.moc"
