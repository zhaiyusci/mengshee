/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "part/latexnoteutils.h"
#include "core/annotationframe.h"
#include "core/annotations.h"
#include "core/document.h"
#include "core/document_p.h"
#include "core/generator.h"
#include "core/latexnotegeometry.h"
#include "core/page.h"
#include "core/page_p.h"
#include "settings.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QMimeDatabase>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class LatexNoteSizeTest : public QObject
{
    Q_OBJECT
    QTemporaryDir m_directory;
    QString m_pdf;
    int m_documentNumber = 0;

    bool openFixture(Okular::Document &document, const QSizeF &dpi, int rotation)
    {
        // Each case has its own URL. A previous executable can still have
        // saved view state for the same file name and size, so reset rotation
        // before replacing the generator's unrotated internal dimensions.
        const QString path = m_directory.filePath(QStringLiteral("case-%1.pdf").arg(++m_documentNumber));
        if (!QFile::copy(m_pdf, path) || document.openDocument(path, QUrl::fromLocalFile(path), QMimeDatabase().mimeTypeForFile(path)) != Okular::Document::OpenSuccess)
            return false;
        document.setRotation(0);
        // Change only the generator's internal scale, never any real screen.
        auto *page = Okular::PagePrivate::get(const_cast<Okular::Page *>(document.page(0)));
        page->m_doc->m_generator->setDPI(dpi);
        page->m_width = 600.0 * dpi.width() / 72.0;
        page->m_height = 800.0 * dpi.height() / 72.0;
        document.setRotation(rotation);
        return true;
    }

    static LatexNoteUtils::RenderResult render(const QString &source, double width)
    {
        QElapsedTimer timer;
        timer.start();
        LatexNoteUtils::RenderResult result;
        do {
            result = LatexNoteUtils::renderAppearancePdf(source, Qt::black, width, false, 10.0);
            if (result.ok && result.errorMessage.isEmpty())
                break;
            QTest::qWait(100);
        } while (timer.elapsed() < 30000);
        return result;
    }

private Q_SLOTS:
    void initTestCase()
    {
        Okular::Settings::instance(QStringLiteral("latexnotesizetest"));
        QVERIFY(m_directory.isValid());
        qputenv("MENGSHEE_LATEX_TEMP_ROOT", m_directory.filePath(QStringLiteral("appearances")).toUtf8());
        m_pdf = m_directory.filePath(QStringLiteral("blank.pdf"));
        QPdfWriter writer(m_pdf);
        writer.setPageSize(QPageSize(QSizeF(600, 800), QPageSize::Point));
        writer.setResolution(72);
        QPainter painter(&writer);
        QVERIFY(painter.isActive());
        painter.fillRect(QRect(0, 0, 1, 1), Qt::white);
        QVERIFY(painter.end());
    }

    void physicalPageUnits_data()
    {
        QTest::addColumn<QSizeF>("dpi");
        QTest::addColumn<int>("rotation");
        for (const QSizeF &dpi : {QSizeF(72, 72), QSizeF(96, 96), QSizeF(144, 144), QSizeF(96, 120)}) {
            for (int rotation = 0; rotation < 4; ++rotation)
                QTest::newRow(qPrintable(QStringLiteral("dpi-%1-%2-rotation-%3").arg(dpi.width()).arg(dpi.height()).arg(rotation))) << dpi << rotation;
        }
    }

    void physicalPageUnits()
    {
        QFETCH(QSizeF, dpi);
        QFETCH(int, rotation);
        Okular::Document document(nullptr);
        QVERIFY(openFixture(document, dpi, rotation));
        const auto *page = document.page(0);
        QCOMPARE(document.pageSizeInPoints(0), QSizeF(600, 800));
        QCOMPARE(page->sizeInPoints(), QSizeF(600, 800));
        Okular::StampAnnotation note;
        note.setBoundingRectangle(Okular::NormalizedRect(0.2, 0.2, 0.7, 0.7));
        QVERIFY(qAbs(LatexNoteUtils::annotationWidthInPoints(&note, page) - 300.0) < 1e-6);
        note.setAppearanceRotation(90);
        QVERIFY(qAbs(LatexNoteUtils::annotationWidthInPoints(&note, page) - 400.0) < 1e-6);
    }

    void frameFitsRenderedSize()
    {
        const QSizeF visual = Okular::LatexNoteGeometry::visualSizeForContent(QSizeF(60, 26), 60, 3);
        QCOMPARE(visual, QSizeF(66, 32));
        const Okular::NormalizedRect frame(0.2, 0.2, 0.5, 0.5);
        const QSizeF page(600, 800);
        const auto fitted = Okular::AnnotationFrame::withLocalSize(frame, page, 0, visual);
        QCOMPARE(Okular::AnnotationFrame::localSize(fitted, page, 0), visual);
        QCOMPARE(fitted.left, frame.left);
        QCOMPARE(fitted.top, frame.top);
    }

    void trailingBreakDoesNotAddHeight_data()
    {
        QTest::addColumn<double>("width");
        for (double width : {198.3, 360.0, 500.0})
            QTest::newRow(qPrintable(QString::number(width))) << width;
    }

    void trailingBreakDoesNotAddHeight()
    {
        if (qEnvironmentVariableIntValue("OKULAR_TEST_LATEX_ASYNC") != 1)
            QSKIP("Set OKULAR_TEST_LATEX_ASYNC=1 to use the installed StemTeX backend");
        QFETCH(double, width);
        const QString plain = QStringLiteral("about abandon abuse");
        const auto baseline = render(plain, width);
        QVERIFY2(baseline.ok && baseline.errorMessage.isEmpty(), qPrintable(baseline.errorMessage));
        QVERIFY2(!baseline.warning.isValid(), qPrintable(baseline.warningMessage));
        for (const QString &suffix : {QStringLiteral("\\\\"), QStringLiteral("\\\\   \n"), QStringLiteral("\n  \n")}) {
            const auto result = render(plain + suffix, width);
            QVERIFY2(result.ok && result.errorMessage.isEmpty(), qPrintable(result.errorMessage));
            QVERIFY2(!result.warning.isValid(), qPrintable(result.warningMessage));
            QCOMPARE(result.pdfSizePoints, baseline.pdfSizePoints);
        }
    }

    void frameMatchesAppearance_data()
    {
        QTest::addColumn<QSizeF>("dpi");
        QTest::addColumn<int>("rotation");
        for (const QSizeF &dpi : {QSizeF(72, 72), QSizeF(96, 96), QSizeF(144, 144), QSizeF(96, 120)}) {
            for (int rotation : {0, 1})
                QTest::newRow(qPrintable(QStringLiteral("dpi-%1-%2-rotation-%3").arg(dpi.width()).arg(dpi.height()).arg(rotation))) << dpi << rotation;
        }
    }

    void frameMatchesAppearance()
    {
        if (qEnvironmentVariableIntValue("OKULAR_TEST_LATEX_ASYNC") != 1)
            QSKIP("Set OKULAR_TEST_LATEX_ASYNC=1 to use the installed StemTeX backend");
        QFETCH(QSizeF, dpi);
        QFETCH(int, rotation);
        Okular::Document document(nullptr);
        QVERIFY(openFixture(document, dpi, 0));
        const QString source = QStringLiteral("about abandon abuse");
        const double width = 240.0;
        const auto rendered = render(source, width);
        QVERIFY2(rendered.ok && rendered.errorMessage.isEmpty(), qPrintable(rendered.errorMessage));
        QVERIFY2(!rendered.warning.isValid(), qPrintable(rendered.warningMessage));
        const QSizeF expected = Okular::LatexNoteGeometry::visualSizeForContent(rendered.pdfSizePoints, width, 3.0);
        auto *note = new Okular::StampAnnotation;
        note->setBoundingRectangle(Okular::NormalizedRect(0.2, 0.2, 0.7, 0.7));
        note->setContents(source);
        note->setOkularLatex(true);
        note->setLatexNoteType(Okular::Annotation::LatexNoteBoxed);
        note->setLatexPadding(3.0);
        note->setLatexFontSize(10.0);
        note->style().setWidth(1.0);
        document.addPageAnnotation(0, note);
        // Edit the same stored annotation after rotating the view. Inserting
        // into an already rotated view would also rotate the annotation itself.
        document.setRotation(rotation);
        QVERIFY(LatexNoteUtils::updateLatexStampAnnotationAppearance(nullptr, &document, 0, note, Qt::black, Qt::yellow, Qt::red, width, true));
        const auto actual = Okular::AnnotationFrame::localSize(note->boundingRectangle(), document.pageSizeInPoints(0), 0);
        QVERIFY2(qAbs(actual.width() - expected.width()) < 0.02, qPrintable(QStringLiteral("Actual width %1; expected %2").arg(actual.width()).arg(expected.width())));
        QVERIFY2(qAbs(actual.height() - expected.height()) < 0.02, qPrintable(QStringLiteral("Actual height %1; expected %2").arg(actual.height()).arg(expected.height())));
        QCOMPARE(note->latexLayoutWidth(), width);
        QVERIFY(qAbs(note->boundingRectangle().left - 0.2) < 1e-6);
        QVERIFY(qAbs(note->boundingRectangle().top - 0.2) < 1e-6);
        // Check the stored PDF appearance in its native orientation. The
        // legacy and unified image-export paths have different view-rotation
        // behavior; neither should affect the annotation we just updated.
        document.setRotation(0);
        QString error;
        const QImage image = document.renderToImage(0, 72, true, &error);
        QVERIFY2(!image.isNull(), qPrintable(error));
        QRect border;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor color = image.pixelColor(x, y);
                if (color.red() > 180 && color.green() < 80 && color.blue() < 80)
                    border |= QRect(x, y, 1, 1);
            }
        }
        QVERIFY2(!border.isEmpty(), "The rendered annotation has no visible red frame");
        const QSizeF painted(border.size());
        QVERIFY2(qAbs(painted.width() - expected.width()) < 3.0,
                 qPrintable(QStringLiteral("Painted %1x%2; expected %3x%4; image %5x%6")
                     .arg(painted.width()).arg(painted.height()).arg(expected.width()).arg(expected.height()).arg(image.width()).arg(image.height())));
        QVERIFY2(qAbs(painted.height() - expected.height()) < 3.0,
                 qPrintable(QStringLiteral("Painted height %1; expected %2").arg(painted.height()).arg(expected.height())));
    }
};

int main(int argc, char **argv)
{
    // Never create visible windows or use the user's screen.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    LatexNoteSizeTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "latexnotesizetest.moc"
