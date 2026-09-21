/*
    SPDX-FileCopyrightText: 2026 Mengshee contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>

#include "../core/document.h"
#include "../part/imageexportdialog.h"
#include "../settings_core.h"

namespace
{
enum Scenario { PngAll, JpegRange, CancelSettings, CancelAfterFirst, RefuseOverwrite, ChangeAfterFirst, OverwriteYesAll };

struct DriveResult {
    QString failure;
    bool configured = false;
    bool canceledProgress = false;
    bool changedDocument = false;
    int questions = 0;
    int summaries = 0;
    int warnings = 0;
};

DriveResult driveExport(Okular::Document &document, const QString &directory, Scenario scenario)
{
    DriveResult result;
    QTimer driver;
    QTimer timeout;
    timeout.setSingleShot(true);
    // Abort, rather than leaving CI stuck in a modal dialog after an assertion
    // failure. There are deliberately no QVERIFY/QCOMPARE macros in callbacks:
    // those only return from the callback and would strand exportDocument().
    QObject::connect(&timeout, &QTimer::timeout, &driver, []() { qFatal("Image export UI test exceeded its 15 second modal-dialog timeout"); });
    QObject::connect(&driver, &QTimer::timeout, &driver, [&]() {
        QWidget *modal = QApplication::activeModalWidget();
        if (!modal) {
            return;
        }
        if (auto *message = qobject_cast<QMessageBox *>(modal)) {
            if (message->icon() == QMessageBox::Question) {
                ++result.questions;
                if (scenario == OverwriteYesAll) {
                    if (auto *yesToAll = message->button(QMessageBox::YesToAll)) {
                        yesToAll->click();
                    } else {
                        result.failure = QStringLiteral("Overwrite confirmation has no Yes to All button");
                        message->reject();
                    }
                    return;
                }
                if (scenario != RefuseOverwrite) {
                    result.failure = QStringLiteral("Unexpected overwrite question: ") + message->text();
                }
                // Always refuse unexpected overwrites too; no CI interaction
                // may destroy an existing file just to get past a dialog.
                if (auto *cancel = message->button(QMessageBox::Cancel)) {
                    cancel->click();
                } else {
                    message->reject();
                }
            } else {
                ++result.summaries;
                if (message->icon() == QMessageBox::Warning) {
                    ++result.warnings;
                    if (scenario != ChangeAfterFirst || !result.changedDocument) {
                        result.failure = QStringLiteral("Export reported an error: ") + message->text();
                    }
                } else if (message->icon() == QMessageBox::Critical) {
                    result.failure = QStringLiteral("Export reported a critical error: ") + message->text();
                }
                message->accept();
            }
            return;
        }
        if (auto *progress = qobject_cast<QProgressDialog *>(modal)) {
            // No timer starts rendering or processes events recursively. The
            // exporter services this timer only at its normal page boundary.
            if (scenario == CancelAfterFirst && !result.canceledProgress && QFileInfo::exists(QDir(directory).filePath(QStringLiteral("export_0001.png")))) {
                result.canceledProgress = true;
                progress->cancel();
            }
            if (scenario == ChangeAfterFirst && !result.changedDocument && QFileInfo::exists(QDir(directory).filePath(QStringLiteral("export_0001.png")))) {
                // Set the guard first: observers may synchronously emit signals
                // while moving pages. Never mutate twice during this export.
                result.changedDocument = true;
                QString error;
                if (!document.movePage(0, 1, &error)) {
                    result.failure = QStringLiteral("Could not change the live page order: ") + error;
                    progress->cancel();
                }
            }
            return;
        }
        auto *settings = qobject_cast<QDialog *>(modal);
        if (!settings || settings->objectName() != QLatin1String("imageExportDialog")) {
            result.failure = QStringLiteral("Unexpected modal window in export test");
            if (settings) {
                settings->reject();
            }
            return;
        }
        if (result.configured) {
            result.failure = QStringLiteral("Export unexpectedly reopened its configuration dialog");
            settings->reject();
            return;
        }
        result.configured = true;
        if (scenario == CancelSettings) {
            settings->reject();
            return;
        }
        auto *selection = settings->findChild<QComboBox *>(QStringLiteral("imageExportSelection"));
        auto *range = settings->findChild<QLineEdit *>(QStringLiteral("imageExportRange"));
        auto *format = settings->findChild<QComboBox *>(QStringLiteral("imageExportFormat"));
        auto *dpi = settings->findChild<QSpinBox *>(QStringLiteral("imageExportDpi"));
        auto *output = settings->findChild<QLineEdit *>(QStringLiteral("imageExportDirectory"));
        auto *prefix = settings->findChild<QLineEdit *>(QStringLiteral("imageExportPrefix"));
        auto *annotations = settings->findChild<QCheckBox *>(QStringLiteral("imageExportAnnotations"));
        auto *buttons = settings->findChild<QDialogButtonBox *>();
        if (!selection || !range || !format || !dpi || !output || !prefix || !annotations || !buttons || !buttons->button(QDialogButtonBox::Save)) {
            result.failure = QStringLiteral("Required image-export controls are missing");
            settings->reject();
            return;
        }
        if (dpi->minimum() != 36 || dpi->maximum() != 1200 || dpi->value() != 300 || !annotations->isChecked()) {
            result.failure = QStringLiteral("Unexpected DPI bounds/default or annotation default");
            settings->reject();
            return;
        }
        const bool jpeg = scenario == JpegRange;
        const int formatIndex = format->findData(QByteArray(jpeg ? "jpeg" : "png"));
        if (formatIndex < 0) {
            result.failure = QStringLiteral("Required PNG/JPEG image writer was not available");
            settings->reject();
            return;
        }
        selection->setCurrentIndex(jpeg ? 2 : 1);
        range->setText(QStringLiteral("2"));
        format->setCurrentIndex(formatIndex);
        dpi->setValue(72);
        output->setText(directory);
        prefix->setText(QStringLiteral("export"));
        buttons->button(QDialogButtonBox::Save)->click();
    });
    // A zero-interval timer is serviced even if the small test pages finish
    // rendering in less than a millisecond. It does not fire inside rendering
    // unless the renderer violates the synchronous/no-event-loop contract.
    driver.start(0);
    timeout.start(15000);
    Okular::ImageExportDialog::exportDocument(&document, 0, QStringLiteral("source"), nullptr);
    driver.stop();
    timeout.stop();
    return result;
}
}

class ImageExportUiTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void exportWorkflow_data();
    void exportWorkflow();

private:
    QTemporaryDir m_sourceDirectory;
};

void ImageExportUiTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    Okular::SettingsCore::instance(QStringLiteral("imageexportuitest"));
    QVERIFY(m_sourceDirectory.isValid());
    QPdfWriter writer(m_sourceDirectory.filePath(QStringLiteral("source.pdf")));
    writer.setResolution(72);
    writer.setPageSize(QPageSize(QSizeF(144, 216), QPageSize::Point));
    writer.setPageMargins(QMarginsF(), QPageLayout::Point);
    QPainter painter(&writer);
    QVERIFY(painter.isActive());
    painter.fillRect(QRect(0, 0, 144, 216), Qt::white);
    painter.fillRect(QRect(10, 10, 40, 40), Qt::red);
    QVERIFY(writer.newPage());
    painter.fillRect(QRect(0, 0, 144, 216), Qt::white);
    painter.fillRect(QRect(10, 10, 40, 40), Qt::blue);
    QVERIFY(painter.end());
}

void ImageExportUiTest::exportWorkflow_data()
{
    QTest::addColumn<int>("scenarioValue");
    QTest::newRow("png-all-pages") << int(PngAll);
    QTest::newRow("jpeg-custom-second-page") << int(JpegRange);
    QTest::newRow("cancel-settings") << int(CancelSettings);
    QTest::newRow("cancel-after-first-page-keeps-file") << int(CancelAfterFirst);
    QTest::newRow("existing-file-overwrite-refused") << int(RefuseOverwrite);
    QTest::newRow("live-page-order-change-stops-after-first") << int(ChangeAfterFirst);
    QTest::newRow("existing-files-overwrite-yes-to-all") << int(OverwriteYesAll);
}

void ImageExportUiTest::exportWorkflow()
{
    QFETCH(int, scenarioValue);
    const auto scenario = Scenario(scenarioValue);
    QTemporaryDir output;
    QVERIFY(output.isValid());
    Okular::Document document(nullptr);
    const QString source = m_sourceDirectory.filePath(QStringLiteral("source.pdf"));
    QCOMPARE(document.openDocument(source, QUrl::fromLocalFile(source), QMimeDatabase().mimeTypeForName(QStringLiteral("application/pdf"))), Okular::Document::OpenSuccess);
    // Ignore persisted reader-only rotation from other temporary PDF fixtures.
    document.setRotation(0);
    QCOMPARE(document.pages(), 2u);
    QVERIFY(document.canRenderToImage());

    const QByteArray originalContents("Existing file must remain byte-for-byte unchanged.\n");
    const QString firstPng = output.filePath(QStringLiteral("export_0001.png"));
    if (scenario == RefuseOverwrite || scenario == OverwriteYesAll) {
        const int placeholderCount = scenario == OverwriteYesAll ? 2 : 1;
        for (int page = 1; page <= placeholderCount; ++page) {
            QFile existing(output.filePath(QStringLiteral("export_%1.png").arg(page, 4, 10, QLatin1Char('0'))));
            QVERIFY(existing.open(QIODevice::WriteOnly));
            QCOMPARE(existing.write(originalContents), qint64(originalContents.size()));
            existing.close();
        }
    }

    const DriveResult result = driveExport(document, output.path(), scenario);
    QVERIFY2(result.failure.isEmpty(), qPrintable(result.failure));
    QVERIFY(result.configured);
    QCOMPARE(result.questions, (scenario == RefuseOverwrite || scenario == OverwriteYesAll) ? 1 : 0);
    QCOMPARE(result.summaries, scenario == CancelSettings ? 0 : 1);
    QCOMPARE(result.warnings, scenario == ChangeAfterFirst ? 1 : 0);
    QCOMPARE(result.changedDocument, scenario == ChangeAfterFirst);
    const QStringList files = QDir(output.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
    if (scenario == CancelSettings) {
        QVERIFY(files.isEmpty());
    } else if (scenario == RefuseOverwrite) {
        QCOMPARE(files, QStringList {QStringLiteral("export_0001.png")});
        QFile existing(firstPng);
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), originalContents);
    } else if (scenario == JpegRange) {
        QCOMPARE(files, QStringList {QStringLiteral("export_0002.jpg")});
        QImageReader reader(output.filePath(files.first()));
        QCOMPARE(reader.format(), QByteArray("jpeg"));
        const QImage image = reader.read();
        QVERIFY2(!image.isNull(), qPrintable(reader.errorString()));
        QCOMPARE(image.size(), QSize(144, 216));
        // JPEG is lossy; check the middle of the solid blue square, with slack.
        const QColor color = image.pixelColor(30, 30);
        QVERIFY(color.blue() > 220 && color.red() < 35 && color.green() < 35);
    } else {
        const QStringList expected = (scenario == PngAll || scenario == OverwriteYesAll) ? QStringList {QStringLiteral("export_0001.png"), QStringLiteral("export_0002.png")} : QStringList {QStringLiteral("export_0001.png")};
        QCOMPARE(files, expected);
        QCOMPARE(result.canceledProgress, scenario == CancelAfterFirst);
        for (int i = 0; i < files.size(); ++i) {
            QImageReader reader(output.filePath(files.at(i)));
            QCOMPARE(reader.format(), QByteArray("png"));
            const QImage image = reader.read();
            QVERIFY2(!image.isNull(), qPrintable(reader.errorString()));
            QCOMPARE(image.size(), QSize(144, 216));
            QCOMPARE(image.pixelColor(30, 30), QColor(i == 0 ? Qt::red : Qt::blue));
        }
    }
    document.closeDocument();
}

QTEST_MAIN(ImageExportUiTest)
#include "imageexportuitest.moc"
