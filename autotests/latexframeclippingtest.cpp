/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>
#include <poppler-annotation.h>
#include <poppler-qt6.h>
#include <memory>

namespace {
// Tiny vector fixtures deliberately avoid fonts, TeX and platform font metrics.
bool writePdf(const QString &path, int width, int height, const QByteArray &content)
{
    const QList<QByteArray> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ") + QByteArray::number(width) + ' ' + QByteArray::number(height) + "] /Resources << >> /Contents 4 0 R >>",
        QByteArray("<< /Length ") + QByteArray::number(content.size()) + ">>\nstream\n" + content + "\nendstream",
    };
    QByteArray pdf("%PDF-1.4\n");
    QList<qsizetype> offsets;
    for (int i = 0; i < objects.size(); ++i) {
        offsets.append(pdf.size());
        pdf += QByteArray::number(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const qsizetype xref = pdf.size();
    pdf += "xref\n0 5\n0000000000 65535 f \n";
    for (const auto offset : offsets) {
        pdf += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
    }
    pdf += "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(pdf) == pdf.size();
}

QByteArray fileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
}
}

class LatexFrameClippingTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void innerClip_data();
    void innerClip();
};

void LatexFrameClippingTest::innerClip_data()
{
    QTest::addColumn<double>("borderWidth");
    QTest::addColumn<double>("padding");
    QTest::addColumn<bool>("callout");
    QTest::addColumn<double>("opacity");
    QTest::addColumn<int>("frameHeight");
    QTest::addColumn<int>("sourceHeight");
    for (double width : {0., 1., 4.}) {
        for (double padding : {0., 2.}) {
            for (bool callout : {false, true}) {
                for (double opacity : {1., .5}) {
                    const QByteArray name = QStringLiteral("bw%1-pad%2-%3-alpha%4").arg(width).arg(padding).arg(callout ? "callout" : "boxed").arg(opacity).toLatin1();
                    QTest::newRow(name.constData()) << width << padding << callout << opacity << 30 << 70;
                }
            }
        }
    }
    QTest::newRow("zero-content-offset") << 4. << 0. << false << 1. << 30 << 30;
    QTest::newRow("exhausted-inner-frame") << 4. << 0. << true << .5 << 6 << 70;
}

void LatexFrameClippingTest::innerClip()
{
    QFETCH(double, borderWidth);
    QFETCH(double, padding);
    QFETCH(bool, callout);
    QFETCH(double, opacity);
    QFETCH(int, frameHeight);
    QFETCH(int, sourceHeight);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pagePath = dir.filePath(QStringLiteral("page.pdf"));
    const QString sourcePath = dir.filePath(QStringLiteral("ink.pdf"));
    const QString blankPath = dir.filePath(QStringLiteral("empty.pdf"));
    QVERIFY(writePdf(pagePath, 200, 200, {}));
    // Saturated blue distinguishes content from the yellow fill and black stroke.
    QVERIFY(writePdf(sourcePath, 100, sourceHeight, QByteArray("0 0 1 rg 0 0 100 ") + QByteArray::number(sourceHeight) + " re f\n"));
    QVERIFY(writePdf(blankPath, 100, sourceHeight, {}));
    const QByteArray sourceHash = fileHash(sourcePath);
    const QSizeF outer = callout ? QSizeF(160, 120) : QSizeF(100, frameHeight);
    const QRectF frame = callout ? QRectF(40, 60, 100, frameHeight) : QRectF(0, 0, 100, frameHeight);
    // PDF appearance origin is (20,40); public annotation boundary is top-down.
    const QRectF boundary(20. / 200., (200. - 40. - outer.height()) / 200., outer.width() / 200., outer.height() / 200.);
    Poppler::StampAnnotation::CustomPdfAppearanceOptions options;
    options.outerSize = outer;
    options.frameRect = frame;
    options.alignContentToFrameTopLeft = true;
    options.contentFrameInset = padding;
    options.borderWidth = borderWidth;
    options.fillColor = Qt::yellow;
    options.borderColor = Qt::black;
    if (callout) {
        options.leaderLine = {QPointF(8, 8), QPointF(25, 70), QPointF(40, 75)};
    }
    QImage images[2];
    for (int variant = 0; variant < 2; ++variant) {
        auto document = Poppler::Document::load(pagePath);
        QVERIFY(document);
        document->setRenderHint(Poppler::Document::Antialiasing, true);
        document->setRenderHint(Poppler::Document::TextAntialiasing, true);
        auto page = document->page(0);
        QVERIFY(page);
        auto annotation = std::make_unique<Poppler::StampAnnotation>();
        annotation->setBoundary(boundary);
        auto style = annotation->style();
        style.setOpacity(opacity);
        annotation->setStyle(style);
        page->addAnnotation(annotation.get());
        QVERIFY(annotation->setStampCustomPdf(variant == 0 ? blankPath : sourcePath, 1, options));
        QCOMPARE(annotation->boundary(), boundary);
        // 288 DPI: ignore only one device pixel next to the mathematical clip edge.
        images[variant] = page->renderToImage(288, 288);
        QVERIFY(!images[variant].isNull());
        const QString saved = dir.filePath(QStringLiteral("saved-%1.pdf").arg(variant));
        auto converter = document->pdfConverter();
        converter->setOutputFileName(saved);
        converter->setPDFOptions(Poppler::PDFConverter::WithChanges);
        QVERIFY(converter->convert());
        auto reopened = Poppler::Document::load(saved);
        QVERIFY(reopened);
        reopened->setRenderHint(Poppler::Document::Antialiasing, true);
        reopened->setRenderHint(Poppler::Document::TextAntialiasing, true);
        auto reopenedPage = reopened->page(0);
        QVERIFY(reopenedPage);
        QCOMPARE(reopenedPage->renderToImage(288, 288), images[variant]);
        const auto annotations = reopenedPage->annotations();
        QCOMPARE(annotations.size(), 1);
        QCOMPARE(annotations.front()->boundary(), boundary);
    }
    QCOMPARE(fileHash(sourcePath), sourceHash);
    QCOMPARE(images[0].size(), images[1].size());
    const QRectF inner = frame.adjusted(borderWidth, borderWidth, -borderWidth, -borderWidth);
    const bool emptyInner = frame.width() <= 2 * borderWidth || frame.height() <= 2 * borderWidth;
    bool foundContent = false;
    bool foundLeader = false;
    for (int y = 0; y < images[0].height(); ++y) {
        for (int x = 0; x < images[0].width(); ++x) {
            const QPointF local((x + .5) / 4. - 20., 200. - (y + .5) / 4. - 40.);
            const QColor baseline = images[0].pixelColor(x, y);
            const QColor ink = images[1].pixelColor(x, y);
            if (emptyInner || !inner.adjusted(-.25, -.25, .25, .25).contains(local)) {
                QVERIFY2(baseline == ink, qPrintable(QStringLiteral("Content crossed frame-inner clip at pixel (%1,%2), local (%3,%4)").arg(x).arg(y).arg(local.x()).arg(local.y())));
            } else if (baseline != ink) {
                foundContent = true;
            }
            if (callout && local.x() < 30 && local.x() > 0 && local.y() > 0 && local.y() < 80 && baseline != QColor(Qt::white)) {
                foundLeader = true;
                QCOMPARE(ink, baseline);
            }
        }
    }
    QCOMPARE(foundContent, !emptyInner);
    if (callout) {
        QVERIFY(foundLeader);
    }
}

QTEST_MAIN(LatexFrameClippingTest)
#include "latexframeclippingtest.moc"
