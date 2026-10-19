// SPDX-License-Identifier: GPL-2.0-or-later
#include "part/mengsheeprintdialog.h"
#include "core/printoptionswidget.h"
#include <QTest>
#include <QLabel>
#include <QRadioButton>
#include <QLineEdit>
#include <QSpinBox>
#include <QThread>
#include <QElapsedTimer>
#include <QTimer>
#include <atomic>
#include <memory>

class PrintPreviewTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void asynchronousLatestRequestAndCache() {
        QPrinter printer;
        printer.setOutputFormat(QPrinter::PdfFormat);
        Okular::MengsheePrintDialog dialog(&printer, 3, 1, QPageLayout::Portrait, false, new Okular::DefaultPrintOptionsWidget);
        struct State { std::atomic<int> calls{0}; std::atomic<int> last{-1}; std::atomic<bool> worker{false}; };
        auto state = std::make_shared<State>();
        dialog.setPreviewRenderer([state](int page, QSize) {
            ++state->calls;
            state->last = page;
            state->worker = QThread::currentThread() != qApp->thread();
            QThread::msleep(250);
            if (page == 0) return Okular::PrintPreviewImage{{}, QStringLiteral("obsolete error")};
            QImage image(120,160,QImage::Format_RGB32); image.fill(Qt::red);
            return Okular::PrintPreviewImage{image,{}};
        }, {});
        int ticks = 0;
        QTimer heartbeat;
        connect(&heartbeat,&QTimer::timeout,this,[&]{++ticks;});
        heartbeat.start(10);
        QTRY_COMPARE(state->calls.load(),1);
        auto *range = dialog.findChild<QRadioButton *>(QStringLiteral("rangePagesRadio"));
        auto *edit = dialog.findChild<QLineEdit *>(QStringLiteral("pageRangeEdit"));
        range->setChecked(true);
        edit->setText(QStringLiteral("2"));
        QTRY_COMPARE(state->last.load(),1);
        QTRY_VERIFY(!dialog.findChild<QLabel *>(QStringLiteral("previewDescription"))->text().contains(QStringLiteral("Generating")));
        QVERIFY(!dialog.findChild<QLabel *>(QStringLiteral("previewDescription"))->text().contains(QStringLiteral("obsolete")));
        QVERIFY(state->worker);
        QVERIFY(ticks > 10);
        const int count = state->calls;
        dialog.findChild<QSpinBox *>(QStringLiteral("copiesSpin"))->setValue(7);
        dialog.findChild<QRadioButton *>(QStringLiteral("landscapeRadio"))->setChecked(true);
        QTest::qWait(250);
        QCOMPARE(state->calls.load(),count);
    }
    void closingDoesNotWaitForWorker() {
        QPrinter printer;
        printer.setOutputFormat(QPrinter::PdfFormat);
        auto started = std::make_shared<std::atomic<bool>>(false);
        auto finished = std::make_shared<std::atomic<bool>>(false);
        auto dialog = std::make_unique<Okular::MengsheePrintDialog>(&printer, 1, 1, QPageLayout::Portrait, false, new Okular::DefaultPrintOptionsWidget);
        dialog->setPreviewRenderer([started,finished](int,QSize) {
            *started=true;
            QThread::msleep(500);
            *finished=true;
            return Okular::PrintPreviewImage{};
        },{});
        QTRY_VERIFY(started->load());
        QElapsedTimer timer; timer.start();
        dialog.reset();
        QVERIFY(timer.elapsed()<200);
        QTRY_VERIFY(finished->load());
    }
};
QTEST_MAIN(PrintPreviewTest)
#include "printpreviewtest.moc"
