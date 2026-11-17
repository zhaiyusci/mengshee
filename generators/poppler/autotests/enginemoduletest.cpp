// SPDX-License-Identifier: AGPL-3.0-or-later
// Smoke test: load the Poppler engine module copy through the C ABI, check the
// capability surface and the forwarded feature flags, and open/render a
// document through the narrow PdfDocument path.

#include "pdfengineregistry.h"

#include <core/generator.h>
#include <core/page.h>

#include <QFileInfo>
#include <QTest>

using namespace Mengshee::Pdf;

class PopplerEngineModuleTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void moduleIsLoadable()
    {
        const QString path = QStringLiteral(MENGSHEE_POPPLER_ENGINE_PATH);
        QVERIFY2(QFileInfo::exists(path), qPrintable(path));

        QString error;
        EnginePtr engine = PdfEngineRegistry::instance().createEngineFromModule(path, &error);
        QVERIFY2(engine, qPrintable(error));
        QCOMPARE(engine->id(), QStringLiteral("poppler"));

        // The mandatory document interface and the capability surface replace
        // the removed legacy generator bridge.
        QVERIFY2(engine->document(), "the Poppler module must expose its PdfDocument");
        QVERIFY2(engine->saver(), "the Poppler module must expose its PdfSaver");
        QVERIFY2(engine->printer(), "the Poppler module must expose its PdfPrinter");
        QVERIFY2(engine->signer(), "the Poppler module must expose its PdfSigner");
        QVERIFY2(engine->pageEditor(), "the Poppler module must expose its PdfPageEditor");
        QVERIFY2(engine->linkEditor(), "the Poppler module must expose its PdfLinkEditor");
        QVERIFY2(engine->backendActions(), "the Poppler module must expose its PdfBackendActions");
        QVERIFY2(engine->configurable(), "the Poppler module must expose its PdfConfigurable");
        QVERIFY2(engine->printConfigurator(), "the Poppler module must expose its PdfPrintConfigurator");

        // Feature flags are forwarded from the internal PDFGenerator.
        QVERIFY(engine->hasFeature(Okular::Generator::TextExtraction));
        QVERIFY(engine->hasFeature(Okular::Generator::ReadRawData));
        QVERIFY(engine->hasFeature(Okular::Generator::FontInfo));
        QVERIFY(engine->hasFeature(Okular::Generator::Threaded));
        QVERIFY(engine->hasFeature(Okular::Generator::TiledRendering));
        QVERIFY(engine->hasFeature(Okular::Generator::SwapBackingFile));
        QVERIFY(engine->hasFeature(Okular::Generator::SupportsCancelling));
        // Poppler does not report a page-size hint to the front end.
        QVERIFY(!engine->hasFeature(Okular::Generator::PageSizes));
    }

    void opensAndRendersDocument()
    {
        QString error;
        EnginePtr engine = PdfEngineRegistry::instance().createEngineFromModule(QStringLiteral(MENGSHEE_POPPLER_ENGINE_PATH), &error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *const document = engine->document();
        QVERIFY(document);

        QVERIFY(document->openFile(QStringLiteral(MENGSHEE_TEST_PDF), QString()) == OpenStatus::Success);
        QVERIFY2(document->pageCount() > 0, "the test PDF should contain pages");

        RenderRequest request;
        request.page = 0;
        request.width = 600;
        request.height = 800;
        const RenderResult rendered = document->renderPage(request);
        QVERIFY2(!rendered.image.isNull(), qPrintable(rendered.errorText));

        document->close();
        QCOMPARE(document->pageCount(), 0);
    }
};

QTEST_MAIN(PopplerEngineModuleTest)

#include "enginemoduletest.moc"
