#include "../../../part/latexnoteutils.h"
#include "../../../part/annotationpopup.h"
#include <QMenu>
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
namespace Okular
{
class PartTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
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
