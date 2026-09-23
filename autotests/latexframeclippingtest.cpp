/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "latexappearance.h"

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
    void genericBackendKeepsItsPolicy();
    void reopenAndGrowFromRawSource();
    void rejectForeignOrUnboundSource();
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
        annotation->setUniqueName(QStringLiteral("mengshee-frame-test"));
        auto style = annotation->style();
        style.setOpacity(opacity);
        annotation->setStyle(style);
        page->addAnnotation(annotation.get());
        QString error;
        QVERIFY2(MengsheeLatexAppearance::rebuild(document.get(), 0, annotation.get(), variant == 0 ? blankPath : sourcePath, options, {}, &error), qPrintable(error));
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

void LatexFrameClippingTest::genericBackendKeepsItsPolicy()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pagePath = dir.filePath(QStringLiteral("page.pdf"));
    const QString sourcePath = dir.filePath(QStringLiteral("source.pdf"));
    QVERIFY(writePdf(pagePath, 200, 200, {}));
    QVERIFY(writePdf(sourcePath, 100, 70, "0 0 1 rg 0 0 100 70 re f\n"));
    auto document = Poppler::Document::load(pagePath);
    QVERIFY(document);
    auto page = document->page(0);
    auto stamp = std::make_unique<Poppler::StampAnnotation>();
    stamp->setUniqueName(QStringLiteral("legacy-generic-stamp"));
    stamp->setBoundary(QRectF(.1, .65, .5, .15));
    page->addAnnotation(stamp.get());
    Poppler::StampAnnotation::CustomPdfAppearanceOptions options;
    options.outerSize = QSizeF(100, 30);
    options.frameRect = QRectF(0, 0, 100, 30);
    options.alignContentToFrameTopLeft = true;
    options.borderWidth = 4;
    options.fillColor = Qt::yellow;
    options.borderColor = Qt::black;
    QVERIFY(stamp->setStampCustomPdf(sourcePath, 1, options));
    // The pinned, unmodified backend does not choose Mengshee's clipping policy.
    const QColor genericPixel = page->renderToImage(288, 288).pixelColor(280, 636);
    QVERIFY(genericPixel.blue() > 200 && genericPixel.red() < 30);
    QString error;
    // Also migrate a legacy Fm0 appearance without a runtime source file.
    QVERIFY(QFile::remove(sourcePath));
    QVERIFY2(MengsheeLatexAppearance::rebuild(document.get(), 0, stamp.get(), {}, options, {}, &error), qPrintable(error));
    QCOMPARE(page->renderToImage(288, 288).pixelColor(280, 636), QColor(Qt::black));
}

void LatexFrameClippingTest::reopenAndGrowFromRawSource()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pagePath = dir.filePath(QStringLiteral("page.pdf"));
    const QString sourcePath = dir.filePath(QStringLiteral("source.pdf"));
    const QString savedPath = dir.filePath(QStringLiteral("small.pdf"));
    QVERIFY(writePdf(pagePath, 200, 200, {}));
    QVERIFY(writePdf(sourcePath, 100, 70, "0 0 1 rg 0 0 100 70 re f\n1 0 0 rg 0 5 100 10 re f\n"));
    auto document = Poppler::Document::load(pagePath);
    QVERIFY(document);
    document->setRenderHint(Poppler::Document::Antialiasing, true);
    auto page = document->page(0);
    auto stamp = std::make_unique<Poppler::StampAnnotation>();
    stamp->setUniqueName(QStringLiteral("reversible-frame"));
    const QRectF largeBoundary(.1, .45, .5, .35);
    stamp->setBoundary(largeBoundary);
    auto style = stamp->style();
    style.setOpacity(.5);
    stamp->setStyle(style);
    page->addAnnotation(stamp.get());
    Poppler::StampAnnotation::CustomPdfAppearanceOptions options;
    options.outerSize = QSizeF(100, 70);
    options.frameRect = QRectF(0, 0, 100, 70);
    options.alignContentToFrameTopLeft = true;
    options.borderWidth = 2;
    options.fillColor = Qt::yellow;
    options.borderColor = Qt::black;
    QString error;
    QVERIFY2(MengsheeLatexAppearance::rebuild(document.get(), 0, stamp.get(), sourcePath, options, {}, &error), qPrintable(error));
    const QImage full = page->renderToImage(288, 288);
    auto raw = MengsheeLatexAppearance::captureRawSource(document.get(), 0, stamp.get(), &error);
    QVERIFY2(raw.isValid(), qPrintable(error));
    stamp->setBoundary(QRectF(.1, .65, .5, .15));
    options.outerSize.setHeight(30);
    options.frameRect.setHeight(30);
    QVERIFY2(MengsheeLatexAppearance::rebuild(document.get(), 0, stamp.get(), {}, options, raw, &error), qPrintable(error));
    const QImage small = page->renderToImage(288, 288);
    QVERIFY(small != full);
    auto converter = document->pdfConverter();
    converter->setOutputFileName(savedPath);
    converter->setPDFOptions(Poppler::PDFConverter::WithChanges);
    QVERIFY(converter->convert());
    converter.reset();
    raw = {};
    stamp.reset();
    page.reset();
    document.reset();
    QVERIFY(QFile::remove(sourcePath));
    document = Poppler::Document::load(savedPath);
    QVERIFY(document);
    document->setRenderHint(Poppler::Document::Antialiasing, true);
    page = document->page(0);
    auto annotations = page->annotations();
    QCOMPARE(annotations.size(), 1);
    auto *reopened = static_cast<Poppler::StampAnnotation *>(annotations.front().get());
    QCOMPARE(page->renderToImage(288, 288), small);
    // Exercise both a small frame and an exhausted inner frame. Growing must
    // recover the red bottom stripe, with no accumulated clipping or opacity.
    for (const int height : {30, 2, 70, 30, 70}) {
        raw = MengsheeLatexAppearance::captureRawSource(document.get(), 0, reopened, &error);
        QVERIFY2(raw.isValid(), qPrintable(error));
        reopened->setBoundary(QRectF(.1, (160. - height) / 200., .5, height / 200.));
        options.outerSize.setHeight(height);
        options.frameRect.setHeight(height);
        QVERIFY2(MengsheeLatexAppearance::rebuild(document.get(), 0, reopened, {}, options, raw, &error), qPrintable(error));
        if (height == 70) {
            QCOMPARE(reopened->boundary(), largeBoundary);
            QCOMPARE(page->renderToImage(288, 288), full);
        }
    }
}

void LatexFrameClippingTest::rejectForeignOrUnboundSource()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("page.pdf"));
    QVERIFY(writePdf(path, 200, 200, {}));
    auto first = Poppler::Document::load(path);
    auto second = Poppler::Document::load(path);
    QVERIFY(first && second);
    auto page = first->page(0);
    auto otherPage = second->page(0);
    auto stamp = std::make_unique<Poppler::StampAnnotation>();
    auto other = std::make_unique<Poppler::StampAnnotation>();
    stamp->setUniqueName(QStringLiteral("same-name"));
    other->setUniqueName(QStringLiteral("same-name"));
    stamp->setBoundary(QRectF(.1, .1, .4, .4));
    other->setBoundary(stamp->boundary());
    page->addAnnotation(stamp.get());
    otherPage->addAnnotation(other.get());
    QVERIFY(stamp->setStampCustomPdf(path, 1));
    QVERIFY(other->setStampCustomPdf(path, 1));
    QString error;
    const auto raw = MengsheeLatexAppearance::captureRawSource(first.get(), 0, stamp.get(), &error);
    QVERIFY2(raw.isValid(), qPrintable(error));
    const QImage before = otherPage->renderToImage();
    QVERIFY(!MengsheeLatexAppearance::rebuild(second.get(), 0, other.get(), {}, {}, raw, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(otherPage->renderToImage(), before);
    auto duplicate = std::make_unique<Poppler::StampAnnotation>();
    duplicate->setUniqueName(stamp->uniqueName());
    duplicate->setBoundary(stamp->boundary());
    page->addAnnotation(duplicate.get());
    // Names are optional PDF metadata, not native identity. Duplicate or empty
    // names must not redirect an edit or prevent use of a genuinely bound handle.
    QVERIFY2(MengsheeLatexAppearance::captureRawSource(first.get(), 0, stamp.get(), &error).isValid(), qPrintable(error));
    auto unbound = std::make_unique<Poppler::StampAnnotation>();
    unbound->setUniqueName(stamp->uniqueName());
    QVERIFY(!MengsheeLatexAppearance::captureRawSource(first.get(), 0, unbound.get(), &error).isValid());
    QVERIFY(!MengsheeLatexAppearance::rebuild(first.get(), 0, unbound.get(), path, {}, {}, &error));
    QVERIFY(!MengsheeLatexAppearance::captureRawSource(first.get(), 1, stamp.get(), &error).isValid());
    other->setUniqueName({});
    QVERIFY2(MengsheeLatexAppearance::captureRawSource(second.get(), 0, other.get(), &error).isValid(), qPrintable(error));
}

QTEST_MAIN(LatexFrameClippingTest)
#include "latexframeclippingtest.moc"
