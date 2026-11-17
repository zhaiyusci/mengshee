// SPDX-License-Identifier: AGPL-3.0-or-later
// Narrow PdfDocument read-path test for the real Poppler engine module: load it
// through the C ABI, then open, inspect, render, extract text, close and reopen
// a document without going through the legacy Okular::Generator page loading.

#include "pdfengine.h"
#include "pdfengineregistry.h"
#include "pdfgeneratorbase.h"

#include <core/document.h>
#include <core/form.h>
#include <core/page.h>
#include <core/textpage.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTest>
#include <QVariantList>

#include <memory>
#include <utility>

using namespace Mengshee::Pdf;

namespace
{
// Exposes the protected Okular::Generator text hook. generateTextPage() cannot
// be used here: without an owning Okular::Document it deletes the text page
// right after attaching it, leaving the page with a dangling pointer.
class GeneratorBaseProbe : public Mengshee::Pdf::PdfGeneratorBase
{
public:
    using PdfGeneratorBase::PdfGeneratorBase;

    Okular::TextPage *textPageFor(Okular::Page *page)
    {
        Okular::TextRequest request(page);
        return textPage(&request);
    }
};
} // namespace

class PopplerNarrowDocumentTest : public QObject
{
    Q_OBJECT

private:
    static QString modulePath()
    {
        return QDir::fromNativeSeparators(QStringLiteral(MENGSHEE_POPPLER_ENGINE_PATH));
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

    static bool hasActionRect(const Okular::Page *page)
    {
        for (const Okular::ObjectRect *rect : page->objectRects()) {
            if (rect->objectType() == Okular::ObjectRect::Action) {
                return true;
            }
        }
        return false;
    }

    // One-page 300x200 pt PDF with /Rotate 90. The geometry must report the
    // displayed (rotated) size 200x300 and rotation 90, and the Okular::Page
    // model must carry the same orientation.
    static QByteArray rotatedPdf()
    {
        QByteArray body = "%PDF-1.4\n";
        QList<int> offsets;
        const auto appendObject = [&body, &offsets](const QByteArray &object) {
            offsets.append(body.size());
            body.append(object);
        };
        appendObject("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
        appendObject("2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");
        appendObject("3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 200] /Rotate 90 >>\nendobj\n");

        const int xrefOffset = body.size();
        body.append("xref\n0 4\n0000000000 65535 f \n");
        for (int offset : std::as_const(offsets)) {
            body.append(QByteArray::number(offset).rightJustified(10, '0'));
            body.append(" 00000 n \n");
        }
        body.append("trailer\n<< /Size 4 /Root 1 0 R >>\nstartxref\n");
        body.append(QByteArray::number(xrefOffset));
        body.append("\n%%EOF\n");
        return body;
    }

private Q_SLOTS:
    void exposesNarrowDocument()
    {
        QVERIFY2(QFileInfo::exists(testPdf()), qPrintable(testPdf()));

        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        QCOMPARE(engine->id(), QStringLiteral("poppler"));

        PdfDocument *document = engine->document();
        QVERIFY2(document, "the Poppler engine module must expose its narrow PdfDocument");
        QCOMPARE(document->pageCount(), 0);
        QVERIFY(document->createPageModel(0) == nullptr);
        QVERIFY(document->textPage(0) == nullptr);
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

        // Out-of-range requests must be rejected before reaching Poppler.
        QVERIFY(document->createPageModel(-1) == nullptr);
        QVERIFY(document->createPageModel(pageCount) == nullptr);

        // The module builds the model on demand; the caller owns it.
        Okular::Page *page = document->createPageModel(0);
        QVERIFY2(page, "the Poppler module must build page models on demand");
        QCOMPARE(page->number(), 0);
        QVERIFY(page->width() > 0.0);
        QVERIFY(page->height() > 0.0);
        const PageGeometry geometry = document->pageGeometry(0);
        QVERIFY(qAbs(page->width() - geometry.cropBox.width()) < 0.5);
        QVERIFY(qAbs(page->height() - geometry.cropBox.height()) < 0.5);
        bool hasAction = hasActionRect(page);
        QVERIFY2(hasAction, "createPageModel must load the page link from file2.pdf");
        qInfo() << "page 0:" << page->annotations().size() << "annotations," << page->formFields().size() << "form fields," << page->objectRects().size() << "object rects";
        delete page;

        // Every page of file2.pdf must produce a complete model.
        for (int i = 1; i < pageCount; ++i) {
            Okular::Page *other = document->createPageModel(i);
            QVERIFY2(other, qPrintable(QStringLiteral("page %1").arg(i)));
            QCOMPARE(other->number(), i);
            QVERIFY(other->width() > 0.0);
            QVERIFY(other->height() > 0.0);
            delete other;
        }

        document->close();
        QCOMPARE(document->pageCount(), 0);
        QVERIFY(document->createPageModel(0) == nullptr);
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

        // Out-of-range indices are rejected instead of reaching Poppler.
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

        // A region request is ignored (whole page) instead of crashing.
        RenderRequest tiled = request;
        tiled.region = QRect(10, 10, 50, 50);
        QVERIFY(!document->renderPage(tiled).image.isNull());

        // An empty request size is an error, not a crash.
        RenderRequest noSize;
        noSize.page = 0;
        QVERIFY(document->renderPage(noSize).image.isNull());

        Okular::TextPage *text = document->textPage(0);
        QVERIFY2(text, "file2.pdf has a text layer");
        QVERIFY2(!text->text().isEmpty(), "the narrow text page should contain extracted text");
        delete text;

        // close() releases the native state, and the engine can open again.
        document->close();
        QCOMPARE(document->pageCount(), 0);
        QVERIFY(document->textPage(0) == nullptr);

        QCOMPARE(status(document->openFile(testPdf(), QString())), status(OpenStatus::Success));
        QCOMPARE(document->pageCount(), pageCount);
        QVERIFY(document->pageGeometry(0).cropBox.width() > 0.0);
        Okular::TextPage *reopenedText = document->textPage(0);
        QVERIFY(reopenedText);
        delete reopenedText;
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
        Okular::Page *page = document->createPageModel(0);
        QVERIFY(page);
        delete page;
        document->close();
    }

    void closeKeepsHandedOutPageModelsAlive()
    {
        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *document = engine->document();
        QVERIFY(document);

        QCOMPARE(status(document->openFile(testPdf(), QString())), status(OpenStatus::Success));
        Okular::Page *page = document->createPageModel(0);
        QVERIFY(page);
        Okular::TextPage *text = document->textPage(0);
        QVERIFY(text);
        page->setTextPage(text);
        QVERIFY(page->hasTextPage());

        // close() releases the native document, never a page model the caller
        // owns; the model stays readable and deletable afterwards.
        document->close();
        QCOMPARE(document->pageCount(), 0);
        QCOMPARE(page->number(), 0);
        QVERIFY(page->width() > 0.0);
        QVERIFY(hasActionRect(page));
        delete page;
    }

    void reportsRotatedPageGeometry()
    {
        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *document = engine->document();
        QVERIFY(document);

        QCOMPARE(status(document->openData(rotatedPdf(), QString())), status(OpenStatus::Success));
        QCOMPARE(document->pageCount(), 1);

        const PageGeometry geometry = document->pageGeometry(0);
        QCOMPARE(geometry.rotation, 90);
        // pageSizeF() applies /Rotate: 300x200 becomes 200x300.
        QVERIFY(qAbs(geometry.cropBox.width() - 200.0) < 0.01);
        QVERIFY(qAbs(geometry.cropBox.height() - 300.0) < 0.01);

        Okular::Page *page = document->createPageModel(0);
        QVERIFY(page);
        QCOMPARE(page->orientation(), Okular::Rotation90);
        QVERIFY(qAbs(page->width() - geometry.cropBox.width()) < 0.5);
        QVERIFY(qAbs(page->height() - geometry.cropBox.height()) < 0.5);
        delete page;

        RenderRequest request;
        request.page = 0;
        request.width = 600;
        request.height = 800;
        const RenderResult rendered = document->renderPage(request);
        QVERIFY2(!rendered.image.isNull(), qPrintable(rendered.errorText));
        QCOMPARE(rendered.image.size(), QSize(600, 800));
        document->close();
    }

    void loadsAnnotationsFormsAndLinks()
    {
        // formSamples.pdf: widget annotations and form fields on page 0. The
        // link rectangles are covered by file2.pdf in createsPageModelsOnDemand().
        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *document = engine->document();
        QVERIFY(document);

        QCOMPARE(status(document->openFile(QStringLiteral(MENGSHEE_FORM_TEST_PDF), QString())), status(OpenStatus::Success));
        QVERIFY(document->pageCount() > 0);
        Okular::Page *formPage = document->createPageModel(0);
        QVERIFY(formPage);
        QVERIFY2(!formPage->annotations().isEmpty(), "createPageModel must load page annotations");
        QVERIFY2(!formPage->formFields().isEmpty(), "createPageModel must load form fields");
        QVERIFY2(!formPage->objectRects().isEmpty(), "the form annotations must provide page object rects");
        delete formPage;
        document->close();
    }

    void loadsPageAnnotations()
    {
        // file1.pdf carries a text annotation on page 0.
        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *document = engine->document();
        QVERIFY(document);

        QCOMPARE(status(document->openFile(QStringLiteral(MENGSHEE_ANNOTATION_TEST_PDF), QString())), status(OpenStatus::Success));
        Okular::Page *page = document->createPageModel(0);
        QVERIFY(page);
        QVERIFY2(!page->annotations().isEmpty(), "createPageModel must load annotations from file1.pdf");
        delete page;
        document->close();
    }

    void loadsSignatureFormFields()
    {
        // pdf_with_signature.pdf has a signature field on page 0. This covers the
        // page-less-signature bookkeeping: an attached signature must stay on its
        // page and must not be duplicated onto page 0.
        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *document = engine->document();
        QVERIFY(document);

        QCOMPARE(status(document->openFile(QStringLiteral(MENGSHEE_SIGNATURE_TEST_PDF), QString())), status(OpenStatus::Success));
        Okular::Page *page = document->createPageModel(0);
        QVERIFY(page);

        int signatureFields = 0;
        for (const Okular::FormField *field : page->formFields()) {
            if (dynamic_cast<const Okular::FormFieldSignature *>(field)) {
                ++signatureFields;
            }
        }
        QCOMPARE(signatureFields, 1);
        delete page;
        document->close();
    }

    void forwardsDocumentServicesToTheLegacyGenerator()
    {
        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *document = engine->document();
        QVERIFY(document);

        QCOMPARE(status(document->openFile(testPdf(), QString())), status(OpenStatus::Success));

        QSet<Okular::DocumentInfo::Key> keys;
        keys << Okular::DocumentInfo::MimeType << Okular::DocumentInfo::Pages;
        const Okular::DocumentInfo info = document->documentInfo(keys);
        QCOMPARE(info.get(Okular::DocumentInfo::MimeType), QStringLiteral("application/pdf"));
        QCOMPARE(info.get(Okular::DocumentInfo::Pages), QString::number(document->pageCount()));

        QVERIFY(document->annotationProxy() != nullptr);
        QVERIFY(document->embeddedFiles() != nullptr);
        QVERIFY2(!document->fontsForPage(0).isEmpty(), "file2.pdf embeds fonts on page 1");
        QCOMPARE(document->metaData(QStringLiteral("DocumentHasPassword"), QVariant()).toString(), QStringLiteral("no"));
        QVERIFY(document->isAllowed(Okular::AllowPrint));

        const Okular::Generator::PageLayout layout = document->defaultPageLayout();
        QVERIFY(layout >= Okular::Generator::NoLayout && layout <= Okular::Generator::TwoPageRight);
        (void)document->defaultPageContinuous();
        // file2.pdf has no outline; just make sure the forwarding does not crash.
        (void)document->synopsis();
        document->close();
    }

    void generatorBaseDrivesTheNarrowDocument()
    {
        // Integration check for the piece the plugin test does not cover: after
        // a narrow load, PdfGeneratorBase must render and extract text through
        // PdfDocument::renderPage()/textPage() instead of the legacy generator.
        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        auto base = std::make_unique<GeneratorBaseProbe>(std::move(engine), nullptr, QVariantList());

        QList<Okular::Page *> pages;
        QCOMPARE(base->loadDocumentWithPassword(testPdf(), pages, QString()), Okular::Document::OpenSuccess);
        QVERIFY(!pages.isEmpty());
        // Point-sized models prove the narrow path is active; Poppler's legacy
        // path reports Pixels. The request below has no Page, which the narrow
        // renderer does not need but the legacy renderer would dereference.
        QCOMPARE(base->pagesSizeMetric(), Okular::Generator::Points);

        QSet<Okular::DocumentInfo::Key> keys;
        keys << Okular::DocumentInfo::MimeType << Okular::DocumentInfo::Pages;
        QCOMPARE(base->generateDocumentInfo(keys).get(Okular::DocumentInfo::MimeType), QStringLiteral("application/pdf"));
        QVERIFY(base->annotationProxy() != nullptr);

        Okular::PixmapRequest request(nullptr, 0, 600, 800, 1.0, 0, Okular::PixmapRequest::NoFeature);
        const QImage image = base->image(&request);
        QVERIFY2(!image.isNull(), "PdfGeneratorBase must render through PdfDocument::renderPage()");
        QCOMPARE(image.size(), QSize(600, 800));

        Okular::TextPage *text = base->textPageFor(pages.first());
        QVERIFY2(text, "PdfGeneratorBase must extract text through PdfDocument::textPage()");
        QVERIFY(!text->text().isEmpty());
        delete text;

        base->closeDocument();
        qDeleteAll(pages);
    }

    void reportsMissingFile()
    {
        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *document = engine->document();
        QVERIFY(document);

        const QString missing = QDir::temp().filePath(QStringLiteral("mengshee-poppler-narrow-missing.pdf"));
        QCOMPARE(status(document->openFile(missing, QString())), status(OpenStatus::Error));
        QCOMPARE(document->pageCount(), 0);
    }

    void forwardsFeatureFlags()
    {
        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));

        // PdfGeneratorBase copies these flags onto itself; the module forwards
        // them from its internal PDFGenerator. No legacy generator object is
        // reachable from the engine any more.
        QVERIFY(engine->hasFeature(Okular::Generator::TextExtraction));
        QVERIFY(engine->hasFeature(Okular::Generator::ReadRawData));
        QVERIFY(engine->hasFeature(Okular::Generator::FontInfo));
        QVERIFY(engine->hasFeature(Okular::Generator::Threaded));
        QVERIFY(engine->hasFeature(Okular::Generator::TiledRendering));
        QVERIFY(engine->hasFeature(Okular::Generator::SwapBackingFile));
        QVERIFY(engine->hasFeature(Okular::Generator::SupportsCancelling));
        QVERIFY(!engine->hasFeature(Okular::Generator::PageSizes));
    }
};

QTEST_GUILESS_MAIN(PopplerNarrowDocumentTest)

#include "narrowdocumenttest.moc"
