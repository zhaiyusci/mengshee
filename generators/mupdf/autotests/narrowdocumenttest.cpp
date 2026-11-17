// SPDX-License-Identifier: AGPL-3.0-or-later
// Narrow PdfDocument read-path test for the real MuPDF engine module: load it
// through the C ABI, then open, inspect, render, extract text, close and
// reopen a document without going through the legacy Okular::Generator API.

#include "pdfengine.h"
#include "pdfengineregistry.h"
#include "pdfutils.h"

#include <core/document.h>
#include <core/page.h>
#include <core/textpage.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTest>

using namespace Mengshee::Pdf;

class MuPdfNarrowDocumentTest : public QObject
{
    Q_OBJECT

private:
    static QString modulePath()
    {
        return QDir(QDir::fromNativeSeparators(QStringLiteral(MENGSHEE_MUPDF_ENGINE_DIR))).filePath(QStringLiteral("MengsheePdfEngineMuPdf.dll"));
    }

    static QString testPdf()
    {
        return QDir::fromNativeSeparators(QStringLiteral(MENGSHEE_TEST_PDF));
    }

    static EnginePtr createEngine(QString *error)
    {
        return PdfEngineRegistry::instance().createEngineFromModule(modulePath(), error);
    }

    static int status(OpenStatus value)
    {
        return static_cast<int>(value);
    }

private Q_SLOTS:
    void extractsSimpleMultipageText()
    {
        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *document = engine->document();
        QVERIFY(document);
        const QString file = QDir(QStringLiteral(QT_TESTCASE_SOURCEDIR)).filePath(QStringLiteral("../../autotests/data/simple-multipage.pdf"));
        QCOMPARE(status(document->openFile(file, QString())), status(OpenStatus::Success));
        QVERIFY(document->pageCount() > 15);
        Okular::TextPage *text = document->textPage(15);
        QVERIFY(text);
        qInfo() << text->text();
        QVERIFY2(text->text().contains(QStringLiteral("Page")), qPrintable(text->text()));
        delete text;
        document->close();
    }

    void rendersExtremeTile()
    {
        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *document = engine->document();
        QVERIFY(document);
        QCOMPARE(status(document->openFile(testPdf(), QString())), status(OpenStatus::Success));
        QCOMPARE(document->pageCount(), 3);

        RenderRequest request;
        request.page = 0;
        request.width = 61200;  // 10,000% of a 612pt page
        request.height = 79200;
        request.region = QRect(0, 0, 2000, 2000);
        request.includeAnnotations = true;
        const RenderResult result = document->renderPage(request);
        QVERIFY2(!result.image.isNull(), qPrintable(result.errorText));
        QCOMPARE(result.image.size(), request.region.size());
        QVERIFY(result.regionApplied);
        document->close();
    }

    void utilityUsesRealEngine()
    {
        PdfEngineRegistry::instance().addSearchPath(QDir::fromNativeSeparators(QStringLiteral(MENGSHEE_MUPDF_ENGINE_DIR)));
        const PageSizeResult result = firstPageLogicalSize(testPdf(), QStringLiteral("mupdf"));
        QVERIFY2(result.ok, qPrintable(result.errorText));
        QVERIFY(result.sizePoints.isValid());
        QVERIFY(result.sizePoints.width() > 0.0);
        QVERIFY(result.sizePoints.height() > 0.0);
    }

    void exposesNarrowDocument()
    {
        QVERIFY2(QFileInfo::exists(testPdf()), qPrintable(testPdf()));

        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        QCOMPARE(engine->id(), QStringLiteral("mupdf"));

        PdfDocument *document = engine->document();
        QVERIFY2(document, "the MuPDF engine module must expose its narrow PdfDocument");
        QCOMPARE(document->pageCount(), 0);
    }

    void createsPageModelsOnDemand()
    {
        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *document = engine->document();
        QVERIFY(document);

        QCOMPARE(status(document->openFile(testPdf(), QString())), status(OpenStatus::Success));
        const int pageCount = document->pageCount();
        QVERIFY(pageCount > 0);

        // Out-of-range requests must be rejected before reaching MuPDF.
        QVERIFY(document->createPageModel(-1) == nullptr);
        QVERIFY(document->createPageModel(pageCount) == nullptr);

        // The module builds the model on demand; the caller owns it.
        Okular::Page *page = document->createPageModel(0);
        QVERIFY2(page, "the MuPDF module must build page models on demand");
        QCOMPARE(page->number(), 0);
        QVERIFY(page->width() > 0.0);
        QVERIFY(page->height() > 0.0);
        const PageGeometry geometry = document->pageGeometry(0);
        QVERIFY(qAbs(page->width() - geometry.cropBox.width()) < 0.5);
        QVERIFY(qAbs(page->height() - geometry.cropBox.height()) < 0.5);
        bool hasAction = false;
        for (const Okular::ObjectRect *rect : page->objectRects()) {
            if (rect->objectType() == Okular::ObjectRect::Action) {
                hasAction = true;
                break;
            }
        }
        QVERIFY2(hasAction, "createPageModel must load the page link annotation from file2.pdf");
        delete page;

        document->close();
        QCOMPARE(document->pageCount(), 0);
    }

    void opensRendersExtractsTextAndReopens()
    {
        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *document = engine->document();
        QVERIFY(document);

        QCOMPARE(status(document->openFile(testPdf(), QString())), status(OpenStatus::Success));
        const int pageCount = document->pageCount();
        QVERIFY2(pageCount > 0, "the test PDF should contain pages");

        const PageGeometry geometry = document->pageGeometry(0);
        QVERIFY2(geometry.cropBox.width() > 0.0, "page width should be positive");
        QVERIFY2(geometry.cropBox.height() > 0.0, "page height should be positive");
        QVERIFY(geometry.mediaBox.width() > 0.0);
        QVERIFY(geometry.mediaBox.height() > 0.0);
        QCOMPARE(geometry.rotation % 90, 0);
        QVERIFY(geometry.rotation >= 0 && geometry.rotation < 360);

        // Out-of-range indices are rejected instead of reaching MuPDF.
        QVERIFY(!document->pageGeometry(-1).cropBox.isValid());
        QVERIFY(!document->pageGeometry(pageCount).cropBox.isValid());
        QVERIFY(document->textPage(-1) == nullptr);
        QVERIFY(document->textPage(pageCount) == nullptr);
        RenderRequest outOfRange;
        outOfRange.page = pageCount;
        outOfRange.width = 100;
        outOfRange.height = 100;
        QVERIFY(document->renderPage(outOfRange).image.isNull());

        RenderRequest request;
        request.page = 0;
        request.width = 600;
        request.height = 800;
        const RenderResult rendered = document->renderPage(request);
        QVERIFY2(!rendered.image.isNull(), qPrintable(rendered.errorText));
        QCOMPARE(rendered.image.size(), QSize(600, 800));

        // An empty request size is an error, not a crash.
        RenderRequest noSize;
        noSize.page = 0;
        QVERIFY(document->renderPage(noSize).image.isNull());

        Okular::TextPage *text = document->textPage(0);
        QVERIFY2(text, "file2.pdf has a text layer");
        QVERIFY2(!text->text().isEmpty(), "the narrow text page should contain extracted text");
        delete text;

        // close() releases the page models, and the engine can open again.
        document->close();
        QCOMPARE(document->pageCount(), 0);
        QVERIFY(document->textPage(0) == nullptr);

        QCOMPARE(status(document->openFile(testPdf(), QString())), status(OpenStatus::Success));
        QCOMPARE(document->pageCount(), pageCount);
        QVERIFY(document->pageGeometry(0).cropBox.width() > 0.0);
        document->close();
        QCOMPARE(document->pageCount(), 0);
    }

    void opensFromData()
    {
        QFile file(testPdf());
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
        const QByteArray data = file.readAll();
        QVERIFY(!data.isEmpty());

        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *document = engine->document();
        QVERIFY(document);

        QCOMPARE(status(document->openData(data, QString())), status(OpenStatus::Success));
        QVERIFY(document->pageCount() > 0);
        QVERIFY(document->pageGeometry(0).cropBox.width() > 0.0);
        document->close();
    }

    void reportsMissingFile()
    {
        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *document = engine->document();
        QVERIFY(document);

        const QString missing = QDir::temp().filePath(QStringLiteral("mengshee-mupdf-narrow-missing.pdf"));
        QCOMPARE(status(document->openFile(missing, QString())), status(OpenStatus::Error));
        QCOMPARE(document->pageCount(), 0);
    }

    void forwardsFeatureFlags()
    {
        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));

        // PdfGeneratorBase copies these flags onto itself; the module forwards
        // them from its internal MuPDFGenerator. No legacy generator object is
        // reachable from the engine any more.
        QVERIFY(engine->hasFeature(Okular::Generator::TextExtraction));
        QVERIFY(engine->hasFeature(Okular::Generator::ReadRawData));
        QVERIFY(engine->hasFeature(Okular::Generator::Threaded));
        QVERIFY(engine->hasFeature(Okular::Generator::TiledRendering));
        QVERIFY(engine->hasFeature(Okular::Generator::SwapBackingFile));
        QVERIFY(engine->hasFeature(Okular::Generator::PrintNative));
        QVERIFY(!engine->hasFeature(Okular::Generator::FontInfo));
        QVERIFY(!engine->hasFeature(Okular::Generator::SupportsCancelling));
    }
};

QTEST_GUILESS_MAIN(MuPdfNarrowDocumentTest)

#include "narrowdocumenttest.moc"
