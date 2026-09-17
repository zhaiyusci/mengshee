/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QMimeDatabase>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

#include "../core/annotations.h"
#include "../core/document.h"
#include "../core/generator.h"
#include "../core/observer.h"
#include "../core/page.h"
#include "../part/imageexportdialog.h"
#include "../settings_core.h"

class ImageExportTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void renderDimensionsAndFormats();
    void displayRenderingAfterExport();
    void invalidRequests();
    void liveAnnotations();
    void livePageOrderAndRotation();
    void pageRanges();
    void outputNames();

private:
    QTemporaryDir m_dir;
    std::unique_ptr<Okular::Document> m_document;
};

void ImageExportTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    Okular::SettingsCore::instance(QStringLiteral("imageexporttest"));
    QVERIFY(m_dir.isValid());
    QPdfWriter writer(m_dir.filePath(QStringLiteral("source.pdf")));
    writer.setResolution(72);
    writer.setPageSize(QPageSize(QSizeF(144, 216), QPageSize::Point));
    writer.setPageMargins(QMarginsF(), QPageLayout::Point);
    QPainter painter(&writer);
    painter.fillRect(QRect(0, 0, 144, 216), Qt::white);
    painter.fillRect(QRect(10, 10, 40, 40), Qt::red);
    QVERIFY(writer.newPage());
    painter.fillRect(QRect(0, 0, 144, 216), Qt::white);
    painter.fillRect(QRect(10, 10, 40, 40), Qt::blue);
    painter.end();
}

void ImageExportTest::init()
{
    m_document = std::make_unique<Okular::Document>(nullptr);
    const QString path = m_dir.filePath(QStringLiteral("source.pdf"));
    QCOMPARE(m_document->openDocument(path, QUrl::fromLocalFile(path), QMimeDatabase().mimeTypeForName(QStringLiteral("application/pdf"))), Okular::Document::OpenSuccess);
    QVERIFY(m_document->canRenderToImage());
}

void ImageExportTest::cleanup()
{
    m_document->closeDocument();
    m_document.reset();
}

void ImageExportTest::renderDimensionsAndFormats()
{
    QString error;
    const QImage image = m_document->renderToImage(0, 144, true, &error);
    QVERIFY2(!image.isNull(), qPrintable(error));
    QVERIFY(error.isEmpty());
    QCOMPARE(image.size(), QSize(288, 432));
    QCOMPARE(image.pixelColor(40, 40), QColor(Qt::red));
    for (const QByteArray format : {QByteArray("png"), QByteArray("jpeg")}) {
        const QString path = m_dir.filePath(QString::fromLatin1(format));
        QSaveFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QImageWriter writer(&file, format);
        QVERIFY2(writer.write(image), qPrintable(writer.errorString()));
        QVERIFY(file.commit());
        QImageReader reader(path);
        QCOMPARE(reader.format(), format);
        QCOMPARE(reader.read().size(), image.size());
    }
}

void ImageExportTest::displayRenderingAfterExport()
{
    QString error;
    QVERIFY2(!m_document->renderToImage(0, 72, false, &error).isNull(), qPrintable(error));
    // Also exercise the normal asynchronous display path. Export alone does not
    // cover PixmapGenerationThread's virtual call to Generator::image().
    Okular::DocumentObserver observer;
    m_document->addObserver(&observer);
    const auto removeObserver = qScopeGuard([&] { m_document->removeObserver(&observer); });
    m_document->requestPixmaps({new Okular::PixmapRequest(&observer, 0, 144, 216, 1.0, 1, Okular::PixmapRequest::Asynchronous)});
    QTRY_VERIFY_WITH_TIMEOUT(m_document->page(0)->hasPixmap(&observer, 144, 216), 10000);
}

void ImageExportTest::invalidRequests()
{
    QString error;
    for (const int page : {-1, 2}) {
        QVERIFY(m_document->renderToImage(page, 72, true, &error).isNull());
        QVERIFY(!error.isEmpty());
    }
    for (const int dpi : {0, -1, 1000000}) {
        QVERIFY(m_document->renderToImage(0, dpi, true, &error).isNull());
        QVERIFY(!error.isEmpty());
    }
    m_document->closeDocument();
    QVERIFY(!m_document->canRenderToImage());
    QVERIFY(m_document->renderToImage(0, 72, true, &error).isNull());
    QVERIFY(!error.isEmpty());
}

void ImageExportTest::liveAnnotations()
{
    QString error;
    const QImage original = m_document->renderToImage(0, 72, false, &error);
    QVERIFY2(!original.isNull(), qPrintable(error));
    auto *annotation = new Okular::GeomAnnotation();
    annotation->setBoundingRectangle(Okular::NormalizedRect(0.4, 0.4, 0.8, 0.8));
    annotation->setGeometricalType(Okular::GeomAnnotation::InscribedSquare);
    annotation->setGeometricalInnerColor(Qt::green);
    annotation->style().setColor(Qt::green);
    m_document->addPageAnnotation(0, annotation);
    const QImage annotated = m_document->renderToImage(0, 72, true, &error);
    QVERIFY2(!annotated.isNull(), qPrintable(error));
    QVERIFY(annotated != original);
    QCOMPARE(m_document->renderToImage(0, 72, false, &error), original);
    // Toggling annotations for export must not leak into subsequent rendering.
    QCOMPARE(m_document->renderToImage(0, 72, true, &error), annotated);
    m_document->undo();
    QCOMPARE(m_document->renderToImage(0, 72, true, &error), original);
}

void ImageExportTest::livePageOrderAndRotation()
{
    QString error;
    const QImage first = m_document->renderToImage(0, 72, true, &error);
    const QImage second = m_document->renderToImage(1, 72, true, &error);
    QVERIFY(first != second);
    QVERIFY2(m_document->movePage(0, 1, &error), qPrintable(error));
    QCOMPARE(m_document->renderToImage(0, 72, true, &error), second);
    QCOMPARE(m_document->renderToImage(1, 72, true, &error), first);
    QVERIFY2(m_document->rotatePage(0, 90, &error), qPrintable(error));
    const QImage rotated = m_document->renderToImage(0, 72, true, &error);
    QCOMPARE(rotated.size(), QSize(216, 144));
    QCOMPARE(rotated.pixelColor(186, 30), QColor(Qt::blue));
    m_document->setRotation(1);
    const QImage twiceRotated = m_document->renderToImage(0, 72, true, &error);
    QCOMPARE(twiceRotated.size(), QSize(144, 216));
    QCOMPARE(twiceRotated.pixelColor(114, 186), QColor(Qt::blue));
    m_document->setRotation(0);
    quint64 editId = 0;
    QVERIFY2(m_document->insertBlankPage(0, 144, 216, &editId, &error), qPrintable(error));
    QCOMPARE(m_document->pages(), 3u);
    const QImage blank = m_document->renderToImage(1, 72, true, &error);
    QVERIFY2(!blank.isNull(), qPrintable(error));
    QCOMPARE(blank.pixelColor(blank.width() / 2, blank.height() / 2), QColor(Qt::white));
    QCOMPARE(m_document->renderToImage(2, 72, true, &error), first);
}

void ImageExportTest::pageRanges()
{
    QList<int> pages;
    QString error;
    QVERIFY(Okular::ImageExportUtils::parsePageRange(QStringLiteral(" 8, 1-3, 2-5, 8 "), 10, &pages, &error));
    QCOMPARE(pages, (QList<int> {0, 1, 2, 3, 4, 7}));
    QVERIFY(error.isEmpty());
    for (const QString &text : {QString(),
                                QStringLiteral("0"),
                                QStringLiteral("11"),
                                QStringLiteral("3-1"),
                                QStringLiteral("1,,2"),
                                QStringLiteral("1,"),
                                QStringLiteral("-2"),
                                QStringLiteral("1-"),
                                QStringLiteral("1;2"),
                                QStringLiteral("999999999999"),
                                QStringLiteral("1,2,3-invalid")}) {
        pages = {99};
        QVERIFY2(!Okular::ImageExportUtils::parsePageRange(text, 10, &pages, &error), qPrintable(text));
        QVERIFY(pages.isEmpty());
        QVERIFY(!error.isEmpty());
    }
}

void ImageExportTest::outputNames()
{
    using namespace Okular::ImageExportUtils;
    QCOMPARE(pageFileName(QStringLiteral("slides"), 0, QStringLiteral("png")), QStringLiteral("slides_0001.png"));
    QCOMPARE(pageFileName(QStringLiteral("slides"), 12345, QStringLiteral("jpg")), QStringLiteral("slides_12346.jpg"));
    QVERIFY(pageFileName(QStringLiteral("../bad"), 0, QStringLiteral("png")).isEmpty());
    QVERIFY(pageFileName(QStringLiteral("good"), -1, QStringLiteral("png")).isEmpty());
    for (const QString &name : {QString(),
                                QStringLiteral("../escape"),
                                QStringLiteral("bad\\name"),
                                QStringLiteral("NUL"),
                                QStringLiteral("COM1.txt"),
                                QStringLiteral("name."),
                                QStringLiteral(" name"),
                                QStringLiteral("bad:stream"),
                                QStringLiteral("bad\nname")}) {
        QVERIFY2(!isSafePrefix(name), qPrintable(name));
        QVERIFY(isSafePrefix(suggestedPrefix(name)));
    }
    QVERIFY(isSafePrefix(QStringLiteral("论文图片")));
}

QTEST_MAIN(ImageExportTest)
#include "imageexporttest.moc"
