// SPDX-License-Identifier: GPL-2.0-or-later
#include "../part/part.h"
#include "settings.h"
#include <KActionCollection>
#include <KConfigGroup>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QPainter>
#include <QPdfWriter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QToolBar>
#include <core/document.h>
#include <core/page.h>
namespace Okular
{
class PartTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void defaultsAndContextMenu()
    {
        QStandardPaths::setTestModeEnabled(true);
        const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/mengshee/");
        QVERIFY(QDir().mkpath(data));
        for (const auto &name : {"tools.xml", "toolsQuick.xml", "drawingtools.xml"}) {
            const QString destination = data + QLatin1String(name);
            if (!QFile::exists(destination))
                QVERIFY(QFile::copy(QString::fromUtf8(ANNOTATION_TEST_DATA_DIR) + QLatin1String(name), destination));
        }
        Settings::instance(QStringLiteral("mengshee-annotation-defaults-test"));
        Settings::self()->setDefaults();
        Settings::setIdentityAuthor(QStringLiteral("Test"));
        for (const auto &key : {"latex-note", "latex-inline", "latex-callout", "latex-numbered-callout", "template-note", "image-note"}) {
            KConfigGroup(Settings::self()->config(), QStringLiteral("AnnotationToolDefaults/") + QLatin1String(key)).deleteGroup();
        }
        QTemporaryDir directory;
        const QString file = directory.filePath(QStringLiteral("page.pdf"));
        {
            QPdfWriter writer(file);
            QPainter painter(&writer);
            painter.drawText(100, 100, QStringLiteral("Test"));
        }
        {
            Part part(nullptr, {});
            QVERIFY(part.openUrl(QUrl::fromLocalFile(file)));
            // Edit special tools through real toolbar context events. Switching
            // tools must not borrow defaults from the previously selected tool.
            struct Case {
                const char *action;
                double expected;
                double changed;
            };
            const Case cases[] = {{"annotation_add_latex_inline_note", 1, 3.5},
                                  {"annotation_add_latex_callout", 1, 2.5},
                                  {"annotation_add_latex_inline_note", 3.5, 4.5},
                                  {"annotation_add_template_note", -1, -1},
                                  {"annotation_add_latex_callout", 2.5, 2.5},
                                  {"annotation_add_image_note", -1, -1},
                                  {"annotation_rectangle", -1, -1},
                                  {"annotation_highlighter", -1, -1}};
            for (const auto &item : cases) {
                QAction *action = part.actionCollection()->action(QString::fromLatin1(item.action));
                QVERIFY(action);
                QToolBar toolbar;
                toolbar.setContextMenuPolicy(Qt::PreventContextMenu);
                toolbar.addAction(action);
                toolbar.show();
                auto *button = qobject_cast<QToolButton *>(toolbar.widgetForAction(action));
                QVERIFY(button);
                bool seen = false;
                bool valueCorrect = false;
                QTimer timer;
                connect(&timer, &QTimer::timeout, button, [&]() {
                    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                    if (!dialog)
                        return;
                    seen = dialog->windowTitle() == QStringLiteral("Default annotation style") && QByteArray(dialog->metaObject()->className()) == QByteArray("AnnotsPropertiesDialog");
                    valueCorrect = item.expected < 0;
                    if (auto *width = dialog->findChild<QDoubleSpinBox *>(QStringLiteral("annotationBorderWidth"))) {
                        valueCorrect = width->value() == item.expected;
                        width->setValue(item.changed);
                    }
                    dialog->accept();
                });
                timer.start(10);
                QTest::mouseClick(button, Qt::RightButton);
                QTRY_VERIFY_WITH_TIMEOUT(seen, 5000);
                QVERIFY(valueCorrect);
                QCOMPARE(part.m_document->page(0)->annotations().size(), 0);
            }
            part.closeUrl(false);
        }
        {
            Part part(nullptr, {});
            QVERIFY(part.openUrl(QUrl::fromLocalFile(file)));
            QToolButton button;
            button.setDefaultAction(part.actionCollection()->action(QStringLiteral("annotation_add_latex_inline_note")));
            bool restored = false;
            QTimer timer;
            connect(&timer, &QTimer::timeout, &button, [&]() {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog)
                    return;
                auto *width = dialog->findChild<QDoubleSpinBox *>(QStringLiteral("annotationBorderWidth"));
                restored = width && width->value() == 4.5;
                dialog->reject();
            });
            timer.start(10);
            QContextMenuEvent event(QContextMenuEvent::Keyboard, QPoint(2, 2), QPoint(2, 2));
            QApplication::sendEvent(&button, &event);
            QTRY_VERIFY_WITH_TIMEOUT(restored, 5000);
            part.closeUrl(false);
        }
    }
};
}
QTEST_MAIN(Okular::PartTest)
#include "annotationtooldefaultstest.moc"
