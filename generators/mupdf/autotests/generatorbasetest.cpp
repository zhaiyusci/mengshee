// SPDX-License-Identifier: AGPL-3.0-or-later
// Tests for PdfGeneratorBase: the MuPDF narrow load does not build page models
// inside the module. The base asks the engine for one model per native page
// through PdfDocument::createPageModel(), owns the resulting list, and reads,
// renders, saves and prints strictly through PdfDocument and the capability
// objects. There is no legacy generator fallback.

#include "pdfengineregistry.h"
#include "pdfgeneratorbase.h"

#include <core/annotations.h>
#include <core/generator.h>
#include <core/page.h>
#include <core/readingview.h>
#include <core/textpage.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtMath>
#include <QUuid>

#include <utility>

using namespace Mengshee::Pdf;

/** Exposes the protected page mapping and rendering/text entry points. */
class TestablePdfGenerator final : public PdfGeneratorBase
{
public:
    using PdfGeneratorBase::PdfGeneratorBase;
    using PdfGeneratorBase::image;
    using PdfGeneratorBase::logicalPageForNative;
    using PdfGeneratorBase::nativePageForLogicalPage;
    using PdfGeneratorBase::nativePageForPage;
    using PdfGeneratorBase::pageNumberForPageToken;
    using PdfGeneratorBase::textPage;
};

namespace
{

/** Text of one page through the narrow text path. */
QString pageText(TestablePdfGenerator &generator, Okular::Page *page)
{
    if (!page) {
        return {};
    }
    Okular::TextRequest request(page);
    Okular::TextPage *const text = generator.textPage(&request);
    const QString result = text ? text->text() : QString();
    delete text;
    return result;
}

/** Creates a Reading View so the engine writes the page identity token. */
bool writePageIdentity(TestablePdfGenerator &generator, int logicalPage, QString *error)
{
    Okular::ReadingView view;
    view.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    view.number = 1;
    view.rectangle = Okular::NormalizedRect(0.10, 0.10, 0.90, 0.20);
    return generator.setReadingViews(logicalPage, {view}, error);
}

} // namespace

class PdfGeneratorBaseTest : public QObject
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

    static bool hasActionObjectRect(const Okular::Page *page)
    {
        for (const Okular::ObjectRect *rect : page->objectRects()) {
            if (rect->objectType() == Okular::ObjectRect::Action) {
                return true;
            }
        }
        return false;
    }

    static void verifyPageModels(const QList<Okular::Page *> &pages)
    {
        QVERIFY(!pages.isEmpty());
        for (int i = 0; i < pages.size(); ++i) {
            const Okular::Page *page = pages.at(i);
            QCOMPARE(page->number(), i);
            QVERIFY2(page->width() > 0.0 && page->height() > 0.0, "a page model must have usable geometry");
            // A4 in points; accept any normal document size.
            QVERIFY(page->width() > 100.0 && page->width() < 2000.0);
            QVERIFY(page->height() > 100.0 && page->height() < 2000.0);
            QVERIFY(page->orientation() >= Okular::Rotation0);
            QVERIFY(page->orientation() <= Okular::Rotation270);
        }
        // file2.pdf is A4 portrait.
        QVERIFY(pages.first()->height() > pages.first()->width());
    }

private Q_SLOTS:
    void forwardsReadPath()
    {
        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));

        PdfGeneratorBase generator(std::move(engine), nullptr, {});
        QVERIFY(generator.hasEngine());
        QCOMPARE(generator.engineId(), QStringLiteral("mupdf"));
        // The base copies the engine's feature flags onto itself instead of
        // holding a pointer to the backend generator.
        QVERIFY(generator.hasFeature(Okular::Generator::TextExtraction));
        QVERIFY(generator.hasFeature(Okular::Generator::TiledRendering));
        QVERIFY(generator.hasFeature(Okular::Generator::SwapBackingFile));

        QList<Okular::Page *> pages;
        QCOMPARE(generator.loadDocumentWithPassword(testPdf(), pages, QString()), Okular::Document::OpenSuccess);
        QVERIFY(!pages.isEmpty());

        const QImage image = generator.renderToImage(0, 72, false);
        QVERIFY(!image.isNull());

        const Okular::DocumentInfo info = generator.generateDocumentInfo({});
        Q_UNUSED(info)

        qDeleteAll(pages);
        generator.closeDocument();
    }

    void narrowLoadBuildsPageModelsInBase()
    {
        QVERIFY2(QFileInfo::exists(testPdf()), qPrintable(testPdf()));

        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));

        // Keep the narrow document pointer: the base owns the engine after the
        // move. pageCount() proves the module still knows its native pages while
        // the models are held by the base.
        PdfDocument *const pdfDocument = engine->document();
        QVERIFY2(pdfDocument, "the MuPDF engine module must expose its narrow PdfDocument");
        QCOMPARE(pdfDocument->pageCount(), 0);

        PdfGeneratorBase generator(std::move(engine), nullptr, {});
        QVERIFY(generator.hasEngine());
        QCOMPARE(generator.engineId(), QStringLiteral("mupdf"));

        QList<Okular::Page *> pages;
        QCOMPARE(generator.loadDocumentWithPassword(testPdf(), pages, QString()), Okular::Document::OpenSuccess);
        const int nativePageCount = pdfDocument->pageCount();
        QVERIFY2(nativePageCount > 1, "file2.pdf has multiple pages");
        QCOMPARE(pages.size(), nativePageCount);
        verifyPageModels(pages);

        // The first page carries a /Link annotation in file2.pdf; createPageModel()
        // must have loaded it into an action object rect.
        QVERIFY2(hasActionObjectRect(pages.first()), "createPageModel must attach the page links");

        const QImage firstImage = generator.renderToImage(0, 72, false);
        QVERIFY2(!firstImage.isNull(), "the narrow render path must stay usable after the handover");
        QVERIFY(qAbs(firstImage.width() - qCeil(pages.first()->width())) <= 1);
        QVERIFY(qAbs(firstImage.height() - qCeil(pages.first()->height())) <= 1);

        const QImage secondImage = generator.renderToImage(1, 72, false);
        QVERIFY2(!secondImage.isNull(), "the second page must render through the base");
        QVERIFY(qAbs(secondImage.width() - qCeil(pages.at(1)->width())) <= 1);
        QVERIFY(qAbs(secondImage.height() - qCeil(pages.at(1)->height())) <= 1);

        // Closing the generator must leave the base-owned models alone: the
        // Document deletes them after closeDocument(). Keep them alive across
        // the close and delete them exactly once here.
        generator.closeDocument();
        QCOMPARE(pdfDocument->pageCount(), 0);
        verifyPageModels(pages);

        qDeleteAll(pages);
        pages.clear();

        // Reopen after close, then reload without an explicit close in between:
        // neither may free the previous models twice or leave dangling state.
        QCOMPARE(generator.loadDocumentWithPassword(testPdf(), pages, QString()), Okular::Document::OpenSuccess);
        QCOMPARE(pages.size(), nativePageCount);
        QCOMPARE(pdfDocument->pageCount(), nativePageCount);
        verifyPageModels(pages);
        qDeleteAll(pages);
        pages.clear();

        QCOMPARE(generator.loadDocumentWithPassword(testPdf(), pages, QString()), Okular::Document::OpenSuccess);
        QCOMPARE(pages.size(), nativePageCount);
        QVERIFY2(!generator.renderToImage(pages.size() - 1, 72, false).isNull(), "the last page must render after a reload");
        qDeleteAll(pages);
        pages.clear();
        generator.closeDocument();

        // Opening from raw data uses the same per-page construction.
        QFile file(testPdf());
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
        const QByteArray data = file.readAll();
        QVERIFY(!data.isEmpty());
        QCOMPARE(generator.loadDocumentFromDataWithPassword(data, pages, QString()), Okular::Document::OpenSuccess);
        QCOMPARE(pages.size(), nativePageCount);
        QCOMPARE(pdfDocument->pageCount(), nativePageCount);
        verifyPageModels(pages);
        qDeleteAll(pages);
        pages.clear();
        generator.closeDocument();
    }

    void narrowReadPathUsesPdfDocument()
    {
        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *const pdfDocument = engine->document();
        QVERIFY(pdfDocument);

        TestablePdfGenerator generator(std::move(engine), nullptr, {});
        QList<Okular::Page *> pages;
        QCOMPARE(generator.loadDocumentWithPassword(testPdf(), pages, QString()), Okular::Document::OpenSuccess);
        QVERIFY(!pages.isEmpty());
        QCOMPARE(pages.size(), pdfDocument->pageCount());

        // Read metadata/capabilities now come from the narrow PdfDocument once
        // a narrow load has happened.
        QCOMPARE(generator.pagesSizeMetric(), Okular::Generator::Points);
        QVERIFY(generator.canRenderToImage());
        const Okular::DocumentInfo info = generator.generateDocumentInfo({});
        QCOMPARE(info.get(Okular::DocumentInfo::MimeType), QStringLiteral("application/pdf"));

        const Okular::FontInfo::List fonts = generator.fontsForPage(0);
        QVERIFY2(!fonts.isEmpty(), "file2.pdf uses fonts on its first page");
        bool extractedFontData = false;
        for (const Okular::FontInfo &font : fonts) {
            if (!generator.requestFontData(font).isEmpty()) {
                extractedFontData = true;
                break;
            }
        }
        QVERIFY2(extractedFontData, "the narrow font path must return the embedded font data");

        const QList<Okular::EmbeddedFile *> *embedded = generator.embeddedFiles();
        QVERIFY(embedded);
        Q_UNUSED(embedded)

        QCOMPARE(generator.defaultPageLayout(), Okular::Generator::NoLayout);
        QVERIFY(!generator.defaultPageContinuous());
        QVERIFY2(generator.annotationProxy(), "the narrow document must expose its annotation proxy");
        QVERIFY(generator.isAllowed(Okular::AllowPrint));
        QVERIFY(generator.metaData(QStringLiteral("DocumentTitle"), {}).isValid());

        Q_UNUSED(generator.generateDocumentSynopsis())
        Okular::DocumentSynopsis emptySynopsis;
        QVERIFY2(generator.setDocumentSynopsis(emptySynopsis, &error), qPrintable(error));

        Okular::TextRequest textRequest(pages.first());
        Okular::TextPage *text = generator.textPage(&textRequest);
        QVERIFY2(text, "the narrow text path must return a text page");
        QVERIFY2(!text->text().isEmpty(), "file2.pdf has a text layer");
        delete text;

        // Both the narrow image request and renderToImage now go through
        // PdfDocument::renderPage; they must agree on the output size.
        const QImage referenceImage = generator.renderToImage(0, 72, true, &error);
        QVERIFY2(!referenceImage.isNull(), qPrintable(error));
        Okular::PixmapRequest imageRequest(nullptr, 0, referenceImage.width(), referenceImage.height(), 1, 0, Okular::PixmapRequest::NoFeature);
        const QImage narrowImage = generator.image(&imageRequest);
        QVERIFY2(!narrowImage.isNull(), "the narrow image path must render a non-tile request");
        QCOMPARE(narrowImage.size(), referenceImage.size());

        // Tile requests render the whole page through PdfDocument and crop the
        // requested normalized region in the front end.
        Okular::PixmapRequest tileRequest(nullptr, 0, referenceImage.width(), referenceImage.height(), 1, 0, Okular::PixmapRequest::NoFeature);
        tileRequest.setTile(true);
        tileRequest.setNormalizedRect(Okular::NormalizedRect(0, 0, .5, .5));
        const QImage tileImage = generator.image(&tileRequest);
        QVERIFY2(!tileImage.isNull(), "tile requests must render through the narrow path");
        QCOMPARE(tileImage.size(), tileRequest.normalizedRect().geometry(tileRequest.width(), tileRequest.height()).size());

        qDeleteAll(pages);
        pages.clear();
        generator.closeDocument();
    }

    void createPageModelLoadsAnnotations()
    {
        QTemporaryDir directory;
        QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));
        const QString annotatedPdf = directory.filePath(QStringLiteral("annotated.pdf"));

        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));

        PdfGeneratorBase generator(std::move(engine), nullptr, {});
        QVERIFY(generator.hasEngine());
        QVERIFY2(generator.annotationProxy(), "the MuPDF engine must expose its annotation proxy");

        QList<Okular::Page *> pages;
        QCOMPARE(generator.loadDocumentWithPassword(testPdf(), pages, QString()), Okular::Document::OpenSuccess);
        QVERIFY(!pages.isEmpty());

        Okular::TextAnnotation annotation;
        annotation.setTextType(Okular::TextAnnotation::InPlace);
        annotation.setContents(QStringLiteral("createPageModel annotation"));
        annotation.setBoundingRectangle({.1, .4, .9, .7});
        annotation.style().setColor(Qt::red);
        generator.annotationProxy()->notifyAddition(&annotation, 0);

        QVERIFY2(generator.save(annotatedPdf, Okular::SaveInterface::SaveChanges, &error), qPrintable(error));

        qDeleteAll(pages);
        pages.clear();
        generator.closeDocument();

        // Reloading the saved file must attach the annotation while the base
        // builds the page models page by page.
        QCOMPARE(generator.loadDocumentWithPassword(annotatedPdf, pages, QString()), Okular::Document::OpenSuccess);
        QVERIFY(!pages.isEmpty());
        QCOMPARE(pages.first()->annotations().size(), 1);
        QCOMPARE(pages.first()->annotations().first()->contents(), annotation.contents());
        QVERIFY(!generator.renderToImage(0, 72, true).isNull());

        qDeleteAll(pages);
        pages.clear();
        generator.closeDocument();
    }

    void recoversFromNarrowLoadFailure()
    {
        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *const pdfDocument = engine->document();
        QVERIFY(pdfDocument);

        PdfGeneratorBase generator(std::move(engine), nullptr, {});

        QList<Okular::Page *> pages;
        const QString missing = QDir::temp().filePath(QStringLiteral("mengshee-base-missing.pdf"));
        QFile::remove(missing);
        QCOMPARE(generator.loadDocumentWithPassword(missing, pages, QString()), Okular::Document::OpenError);
        QVERIFY(pages.isEmpty());
        QCOMPARE(pdfDocument->pageCount(), 0);

        // A failed narrow open must leave the engine reusable.
        QCOMPARE(generator.loadDocumentWithPassword(testPdf(), pages, QString()), Okular::Document::OpenSuccess);
        QVERIFY(!pages.isEmpty());
        QCOMPARE(pages.size(), pdfDocument->pageCount());
        qDeleteAll(pages);
        pages.clear();
        generator.closeDocument();
    }

    void pageReorderKeepsNativeMapping()
    {
        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));

        TestablePdfGenerator generator(std::move(engine), nullptr, {});
        QList<Okular::Page *> pages;
        QCOMPARE(generator.loadDocumentWithPassword(testPdf(), pages, QString()), Okular::Document::OpenSuccess);
        QCOMPARE(pages.size(), 3);

        // The logical order starts as the native order; each page model is
        // mapped to its native page, not to Page::number().
        for (int i = 0; i < pages.size(); ++i) {
            QCOMPARE(generator.nativePageForPage(pages.at(i)), i);
            QCOMPARE(generator.nativePageForLogicalPage(i), i);
            QCOMPARE(generator.logicalPageForNative(i), i);
        }

        const QString textA = pageText(generator, pages.at(0));
        const QString textB = pageText(generator, pages.at(1));
        const QString textC = pageText(generator, pages.at(2));
        QVERIFY2(!textA.isEmpty(), "file2.pdf page 0 must have a text layer");
        QVERIFY2(!textB.isEmpty(), "file2.pdf page 1 must have a text layer");
        QVERIFY(textA != textB);
        const QImage imageA = generator.renderToImage(0, 72, false, &error);
        QVERIFY2(!imageA.isNull(), qPrintable(error));
        const QImage imageB = generator.renderToImage(1, 72, false, &error);
        QVERIFY2(!imageB.isNull(), qPrintable(error));

        // Give native page 0 a page identity token before it moves.
        QVERIFY2(writePageIdentity(generator, 0, &error), qPrintable(error));
        const QString pageToken = generator.readingViewPageToken(0);
        QVERIFY2(!pageToken.isEmpty(), "writing a Reading View must give the page its identity token");
        QCOMPARE(generator.pageNumberForPageToken(pageToken), 0);

        // Move logical/native page 0 to position 2 and mirror Document's page
        // vector in the test.
        QVERIFY2(generator.movePageInDocument(0, 2, &error), qPrintable(error));
        pages.move(0, 2);
        QCOMPARE(generator.nativePageForLogicalPage(0), 0);
        QCOMPARE(generator.nativePageForLogicalPage(2), 2);
        QCOMPARE(generator.logicalPageForNative(2), 2);
        QCOMPARE(generator.nativePageForPage(pages.at(2)), 2);

        // The moved model is still native page 0 (now at logical 2): its text,
        // its render and its page token all point at the same page.
        QCOMPARE(pageText(generator, pages.at(2)), textA);
        const QImage movedA = generator.renderToImage(2, 72, false, &error);
        QVERIFY2(!movedA.isNull(), qPrintable(error));
        QCOMPARE(movedA.size(), imageA.size());
        QVERIFY2(movedA == imageA, "the reordered page must still render its own native page");
        QCOMPARE(pageText(generator, pages.at(0)), textB);
        QCOMPARE(generator.renderToImage(0, 72, false, &error).size(), imageB.size());
        QCOMPARE(generator.pageNumberForPageToken(pageToken), 2);
        QCOMPARE(generator.readingViewPageForToken(pageToken), 2);

        // Insert a blank page after logical page 0. The inserted model is
        // reported at native page 1 and all later pages shift by one.
        Okular::Page *blankPage = nullptr;
        quint64 insertEditId = 0;
        QVERIFY2(generator.insertBlankPageInDocument(0, 300.0, 200.0, &blankPage, &insertEditId, &error), qPrintable(error));
        QVERIFY(blankPage);
        QCOMPARE(generator.nativePageForPage(blankPage), 1);
        pages.insert(1, blankPage);
        QCOMPARE(pages.size(), 4);
        QVERIFY(pageText(generator, pages.at(1)).isEmpty());
        QVERIFY(!generator.renderToImage(1, 72, false, &error).isNull());
        QCOMPARE(pageText(generator, pages.at(2)), textC);
        QCOMPARE(pageText(generator, pages.at(3)), textA);
        QCOMPARE(generator.pageNumberForPageToken(pageToken), 3);

        // Duplicate logical page 0 (text B) at position 1.
        Okular::Page *duplicatePage = nullptr;
        quint64 duplicateEditId = 0;
        QVERIFY2(generator.duplicatePageInDocument(0, false, &duplicatePage, &duplicateEditId, &error), qPrintable(error));
        QVERIFY(duplicatePage);
        QCOMPARE(generator.nativePageForPage(duplicatePage), 1);
        pages.insert(1, duplicatePage);
        QCOMPARE(pages.size(), 5);
        QCOMPARE(pageText(generator, pages.at(1)), textB);
        QCOMPARE(pageText(generator, pages.at(3)), textC);

        // The duplicate is undo-detached and restored, which exercises the
        // detach/attach mapping updates around the native gap.
        QVERIFY2(generator.detachPageInDocument(duplicatePage, 1, duplicateEditId, &error), qPrintable(error));
        pages.removeAt(1);
        QCOMPARE(pages.size(), 4);
        QCOMPARE(generator.nativePageForPage(duplicatePage), -1);
        QCOMPARE(generator.nativePageForLogicalPage(1), 1);
        QVERIFY(pageText(generator, pages.at(1)).isEmpty());
        Okular::Page *attachedPage = nullptr;
        QVERIFY2(generator.attachPageInDocument(0, duplicateEditId, &attachedPage, &error), qPrintable(error));
        QVERIFY(attachedPage);
        QCOMPARE(generator.nativePageForPage(attachedPage), 1);
        pages.insert(1, attachedPage);
        QCOMPARE(pages.size(), 5);
        QCOMPARE(pageText(generator, pages.at(1)), textB);
        QCOMPARE(pageText(generator, pages.at(4)), textA);
        QCOMPARE(generator.pageNumberForPageToken(pageToken), 4);

        // Rotate the moved page: the replacement model inherits its native
        // page, so the render size is transposed while the token target stays.
        Okular::Page *const retiredA = pages.at(4);
        Okular::Page *rotatedA = nullptr;
        QVERIFY2(generator.rotatePageInDocument(retiredA, 4, 90, &rotatedA, &error), qPrintable(error));
        QVERIFY(rotatedA);
        QCOMPARE(generator.nativePageForPage(rotatedA), 4);
        QCOMPARE(generator.nativePageForPage(retiredA), -1);
        pages[4] = rotatedA;
        delete retiredA;
        const QImage rotatedImage = generator.renderToImage(4, 72, false, &error);
        QVERIFY2(!rotatedImage.isNull(), qPrintable(error));
        QCOMPARE(rotatedImage.size(), QSize(imageA.height(), imageA.width()));
        QCOMPARE(pageText(generator, pages.at(4)), textA);
        QCOMPARE(generator.pageNumberForPageToken(pageToken), 4);

        // Unknown pages are rejected without a Page::number() fallback.
        Okular::Page orphanPage(0, 100.0, 100.0, Okular::Rotation0);
        QCOMPARE(generator.nativePageForPage(&orphanPage), -1);
        Okular::TextRequest orphanRequest(&orphanPage);
        QVERIFY(generator.textPage(&orphanRequest) == nullptr);
        QVERIFY(generator.renderToImage(-1, 72, false, &error).isNull());
        QVERIFY(generator.renderToImage(pages.size(), 72, false, &error).isNull());
        QVERIFY(!error.isEmpty());
        QVERIFY(!generator.movePageInDocument(0, pages.size(), &error));
        QVERIFY(!generator.rotatePageInDocument(&orphanPage, 0, 90, &rotatedA, &error));

        // Closing clears the mapping: no page or token resolves any more and
        // rendering reports the closed document instead of rendering page 0.
        for (Okular::Page *page : std::as_const(pages)) {
            delete page;
        }
        pages.clear();
        generator.closeDocument();
        QCOMPARE(generator.nativePageForLogicalPage(0), -1);
        QCOMPARE(generator.nativePageForPage(&orphanPage), -1);
        QCOMPARE(generator.pageNumberForPageToken(pageToken), -1);
        QCOMPARE(generator.readingViewPageForToken(pageToken), -1);
        QVERIFY(generator.renderToImage(0, 72, false, &error).isNull());
        QVERIFY(!error.isEmpty());
    }

    void reportsMissingEngine()
    {
        EnginePtr empty;
        PdfGeneratorBase generator(std::move(empty), nullptr, {});
        QVERIFY(!generator.hasEngine());
        QVERIFY(generator.engineId().isEmpty());

        QSignalSpy errors(&generator, &Okular::Generator::error);
        QList<Okular::Page *> pages;
        QCOMPARE(generator.loadDocumentWithPassword(QStringLiteral("missing.pdf"), pages, QString()), Okular::Document::OpenError);
        QCOMPARE(errors.count(), 1);
        QVERIFY(pages.isEmpty());
    }
};

QTEST_GUILESS_MAIN(PdfGeneratorBaseTest)

#include "generatorbasetest.moc"
