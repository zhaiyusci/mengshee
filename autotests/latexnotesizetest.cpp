/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "part/latexnoteutils.h"
#include "core/annotationframe.h"
#include "core/latexnotegeometry.h"
#include <QColor>
#include <QtTest>

class LatexNoteSizeTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void trailingBreakDoesNotAddHeight()
    {
        if (qEnvironmentVariableIntValue("OKULAR_TEST_LATEX_ASYNC") != 1)
            QSKIP("Set OKULAR_TEST_LATEX_ASYNC=1 to use the installed StemTeX backend");
        const QString plain = QStringLiteral("Test Name");
        const QString trailingBreak = QStringLiteral("Test Name\\\\");
        auto render = [](const QString &source) {
            LatexNoteUtils::RenderResult result;
            for (int attempt = 0; attempt < 240; ++attempt) {
                result = LatexNoteUtils::renderAppearancePdf(source, Qt::black, 120.0, false, 10.0);
                if (result.ok && result.errorMessage.isEmpty())
                    break;
                QTest::qWait(500);
            }
            return result;
        };
        const auto plainResult = render(plain);
        const auto breakResult = render(trailingBreak);
        QVERIFY2(plainResult.ok, qPrintable(plainResult.errorMessage));
        QVERIFY2(breakResult.ok, qPrintable(breakResult.errorMessage));
        QCOMPARE(breakResult.pdfSizePoints, plainResult.pdfSizePoints);
        QCOMPARE(breakResult.pdfSizePoints.height(), plainResult.pdfSizePoints.height());
        QCOMPARE(breakResult.pdfSizePoints.width(), plainResult.pdfSizePoints.width());
    }

    void frameFitsRenderedSize()
    {
        const QSizeF content(60, 26);
        const QSizeF visual = Okular::LatexNoteGeometry::visualSizeForContent(content, 60, 3);
        QCOMPARE(visual, QSizeF(66, 32));
        const Okular::NormalizedRect frame(0.2, 0.2, 0.5, 0.5);
        const QSizeF page(600, 800);
        const auto fitted = Okular::AnnotationFrame::withLocalSize(frame, page, 0, visual);
        const auto fittedSize = Okular::AnnotationFrame::localSize(fitted, page, 0);
        QCOMPARE(fittedSize, visual);
        QCOMPARE(fitted.left, frame.left);
        QCOMPARE(fitted.top, frame.top);
    }
};

QTEST_MAIN(LatexNoteSizeTest)

#include "latexnotesizetest.moc"
