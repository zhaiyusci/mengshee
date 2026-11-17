// SPDX-License-Identifier: AGPL-3.0-or-later
// Capability contract test for the real MuPDF engine module: save, flatten,
// swap backing file, print, signing and the link/OCR/custom-metadata
// capabilities must be reached through the PdfSaver/PdfPrinter/PdfSigner/
// PdfLinkEditor/PdfOcr/PdfCustomMetadata objects, not through the legacy
// generator pointer. MuPDF keeps canSign() == false.

#include "pdfengine.h"
#include "pdfengineregistry.h"
#include "pdfgeneratorbase.h"

#include <core/action.h>
#include <core/document.h>
#include <core/generator.h>
#include <core/page.h>
#include <core/readingview.h>
#include <core/textpage.h>

#include <settings_core.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QUuid>
#include <QVariantList>

#include <memory>
#include <utility>

using namespace Mengshee::Pdf;

namespace
{

/** Exposes PdfGeneratorBase's protected swap and page-token hooks. */
class CapabilityProbe final : public PdfGeneratorBase
{
public:
    using PdfGeneratorBase::PdfGeneratorBase;
    using PdfGeneratorBase::pageNumberForPageToken;
    using PdfGeneratorBase::swapBackingFile;
};

QString copyOfTestPdf(const QTemporaryDir &directory)
{
    const QString source = QDir::fromNativeSeparators(QStringLiteral(MENGSHEE_TEST_PDF));
    const QString target = directory.filePath(QStringLiteral("mengshee-capability.pdf"));
    return QFile::copy(source, target) ? target : QString();
}

/** Page sizes are PDF points in the narrow base (72 DPI). */
bool nearlyEqual(double actual, double expected)
{
    return qAbs(actual - expected) < 0.01;
}

const Okular::Action *actionOf(const Okular::ObjectRect *rect)
{
    if (!rect || rect->objectType() != Okular::ObjectRect::Action) {
        return nullptr;
    }
    return static_cast<const Okular::Action *>(rect->object());
}

bool pageHasBrowseUrl(const Okular::Page *page, const QUrl &url)
{
    if (!page) {
        return false;
    }
    for (const Okular::ObjectRect *rect : page->objectRects()) {
        const Okular::Action *action = actionOf(rect);
        if (action && action->actionType() == Okular::Action::Browse && static_cast<const Okular::BrowseAction *>(action)->url() == url) {
            return true;
        }
    }
    return false;
}

bool pageHasNumericInternalGoto(const Okular::Page *page)
{
    if (!page) {
        return false;
    }
    for (const Okular::ObjectRect *rect : page->objectRects()) {
        const Okular::Action *action = actionOf(rect);
        if (!action || action->actionType() != Okular::Action::Goto) {
            continue;
        }
        const auto *gotoAction = static_cast<const Okular::GotoAction *>(action);
        // The fixture already has named links; a numeric destination is the
        // one without a destination name.
        if (!gotoAction->isExternal() && gotoAction->destinationName().isEmpty()) {
            return true;
        }
    }
    return false;
}

bool pageHasNamedGoto(const Okular::Page *page, const QString &name)
{
    if (!page) {
        return false;
    }
    for (const Okular::ObjectRect *rect : page->objectRects()) {
        const Okular::Action *action = actionOf(rect);
        if (!action || action->actionType() != Okular::Action::Goto) {
            continue;
        }
        const auto *gotoAction = static_cast<const Okular::GotoAction *>(action);
        if (!gotoAction->isExternal() && gotoAction->destinationName() == name) {
            return true;
        }
    }
    return false;
}

bool pageHasExternalGoto(const Okular::Page *page, const QString &fileName)
{
    if (!page) {
        return false;
    }
    for (const Okular::ObjectRect *rect : page->objectRects()) {
        const Okular::Action *action = actionOf(rect);
        if (!action || action->actionType() != Okular::Action::Goto) {
            continue;
        }
        const auto *gotoAction = static_cast<const Okular::GotoAction *>(action);
        if (gotoAction->isExternal() && gotoAction->fileName().contains(fileName)) {
            return true;
        }
    }
    return false;
}

/**
 * The unnamed internal GotoAction whose rectangle matches @p area. An explicit
 * destination is the link kind that carries (or, for MuPDF, resolves to) a page
 * identity; a named destination must not be mistaken for it.
 */
const Okular::GotoAction *internalGotoAt(const Okular::Page *page, const Okular::NormalizedRect &area)
{
    if (!page) {
        return nullptr;
    }
    for (const Okular::ObjectRect *rect : page->objectRects()) {
        const Okular::Action *action = actionOf(rect);
        if (!action || action->actionType() != Okular::Action::Goto) {
            continue;
        }
        const auto *gotoAction = static_cast<const Okular::GotoAction *>(action);
        if (gotoAction->isExternal() || !gotoAction->destinationName().isEmpty()) {
            continue;
        }
        const QRectF actual = rect->region().boundingRect();
        if (qAbs(actual.left() - area.left) < 0.005 && qAbs(actual.top() - area.top) < 0.005 && qAbs(actual.right() - area.right) < 0.005
            && qAbs(actual.bottom() - area.bottom) < 0.005) {
            return gotoAction;
        }
    }
    return nullptr;
}

} // namespace

class MuPdfCapabilityTest : public QObject
{
    Q_OBJECT

private:
    static QString engineDirectory()
    {
        return QDir::fromNativeSeparators(QStringLiteral(MENGSHEE_MUPDF_ENGINE_DIR));
    }

    static QString modulePath()
    {
        return QDir(engineDirectory()).filePath(QStringLiteral("MengsheePdfEngineMuPdf.dll"));
    }

    static QString testPdf()
    {
        return QDir::fromNativeSeparators(QStringLiteral(MENGSHEE_TEST_PDF));
    }

    static EnginePtr createEngine(QString *error)
    {
        return PdfEngineRegistry::instance().createEngineFromModule(modulePath(), error);
    }

    /** Opens the module's native document and optionally builds every page model. */
    static bool openNativeDocument(PdfDocument *document, const QString &path, QList<Okular::Page *> *models, QString *error)
    {
        if (document->openFile(path, QString()) != OpenStatus::Success) {
            if (error) {
                *error = QStringLiteral("Could not open the capability test PDF.");
            }
            return false;
        }
        if (!models) {
            return true;
        }
        for (int nativePage = 0; nativePage < document->pageCount(); ++nativePage) {
            Okular::Page *page = document->createPageModel(nativePage);
            if (!page) {
                if (error) {
                    *error = QStringLiteral("Could not create the page model for page %1.").arg(nativePage);
                }
                return false;
            }
            models->append(page);
        }
        return true;
    }

    /**
     * The engine capability resolves a native page to its Okular::Page model
     * through the wrapper document PdfGeneratorBase installs. Outside the
     * application the test opens one on the same copy so the page lookup has
     * the models the base would own.
     */
    static std::unique_ptr<Okular::Document> openViewerDocument(const QString &path, QString *error)
    {
        auto document = std::make_unique<Okular::Document>(nullptr);
        const QMimeType mime = QMimeDatabase().mimeTypeForFile(path);
        if (document->openDocument(path, QUrl::fromLocalFile(path), mime) != Okular::Document::OpenSuccess || document->pages() == 0) {
            if (error) {
                *error = QStringLiteral("Could not open a viewer document for the capability page lookup.");
            }
            return nullptr;
        }
        return document;
    }

private Q_SLOTS:
    void initTestCase()
    {
        // Document owns Okular::SettingsCore's config; keep it isolated from
        // the developer's profile and away from the generator chooser dialog.
        QStandardPaths::setTestModeEnabled(true);
        Okular::SettingsCore::instance(QStringLiteral("mengshee-pdf-mupdf-capability-test"));
        // The viewer document used for the page lookup loads the single PDF
        // plugin, which selects its engine module through this path.
        qputenv("MENGSHEE_PDF_ENGINE_PATH", QDir::toNativeSeparators(engineDirectory()).toUtf8());
        qunsetenv("MENGSHEE_PDF_ENGINE");
        PdfEngineRegistry::instance().clearOverrideEngineId();
        PdfEngineRegistry::instance().setDefaultEngineId({});
    }

    void exposesWriteCapabilities()
    {
        QVERIFY2(QFileInfo::exists(testPdf()), qPrintable(testPdf()));

        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        QCOMPARE(engine->id(), QStringLiteral("mupdf"));

        PdfSaver *const saver = engine->saver();
        PdfPrinter *const printer = engine->printer();
        PdfSigner *const signer = engine->signer();
        QVERIFY2(saver, "the MuPDF module must expose its PdfSaver");
        QVERIFY2(printer, "the MuPDF module must expose its PdfPrinter");
        QVERIFY2(signer, "the MuPDF module must expose its PdfSigner, even without signing support");

        // Fixed capability expectations: MuPDF saves changes and does not sign.
        QVERIFY(saver->supportsOption(Okular::SaveInterface::SaveChanges));
        QVERIFY(!saver->supportsOption(Okular::SaveInterface::NoOption));
        QVERIFY(!signer->canSign());
        QVERIFY(signer->certificateStore() == nullptr);
    }

    void exposesEditorCapabilities()
    {
        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *const document = engine->document();
        QVERIFY(document);
        QVERIFY(document->openFile(testPdf(), QString()) == OpenStatus::Success);

        PdfLinkEditor *const linkEditor = engine->linkEditor();
        PdfOcr *const ocr = engine->ocr();
        PdfCustomMetadata *const customMetadata = engine->customMetadata();
        PdfPageEditor *const pageEditor = engine->pageEditor();
        QVERIFY2(linkEditor, "the MuPDF module must expose its PdfLinkEditor");
        QVERIFY2(ocr, "the MuPDF module must expose its PdfOcr");
        QVERIFY2(customMetadata, "the MuPDF module must expose its PdfCustomMetadata");
        QVERIFY2(pageEditor, "the MuPDF module must expose its PdfPageEditor");

        // Fixed capability expectations for MuPDF.
        QVERIFY(linkEditor->canEditLinks());
        QVERIFY(customMetadata->canEditNumberedCalloutNumbering());
        QVERIFY(customMetadata->canEditReadingViews());
        // OCR recognition and Reading View generation are both backed by the
        // optional Tesseract build, so they must agree.
        QCOMPARE(ocr->canRecognize(), customMetadata->canGenerateReadingViews());
        QVERIFY(pageEditor->canInsertBlankPage());
        QVERIFY(pageEditor->canInsertPageFromPdf());
        QVERIFY(pageEditor->canAppendPdfFile());
        QVERIFY(pageEditor->canCombinePdfFiles());
        QVERIFY(pageEditor->canDeletePage());
        QVERIFY(pageEditor->canMovePage());
        QVERIFY(pageEditor->canMovePageInDocument());
        QVERIFY(pageEditor->canRotatePage());
        QVERIFY(pageEditor->canSetPageSize());

        document->close();
    }

    void basePrefersCapabilities()
    {
        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfSigner *const signer = engine->signer();
        QVERIFY(signer);
        QVERIFY(engine->saver());
        QVERIFY(engine->printer());

        CapabilityProbe generator(std::move(engine), nullptr, QVariantList());

        QCOMPARE(generator.canSign(), signer->canSign());
        QVERIFY(!generator.canSign());
        QVERIFY(generator.certificateStore() == signer->certificateStore());
    }

    void savesAndReopensThroughBase()
    {
        QTemporaryDir directory;
        QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));
        const QString savedPath = directory.filePath(QStringLiteral("mengshee-mupdf-capability-save.pdf"));

        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *const sourceDocument = engine->document();
        QVERIFY(sourceDocument);

        CapabilityProbe generator(std::move(engine), nullptr, QVariantList());
        QList<Okular::Page *> pages;
        QCOMPARE(generator.loadDocumentWithPassword(testPdf(), pages, QString()), Okular::Document::OpenSuccess);
        QVERIFY2(!pages.isEmpty(), "the test PDF should contain pages");
        const int pageCount = pages.size();

        QVERIFY(generator.supportsOption(Okular::SaveInterface::SaveChanges));
        QVERIFY(!generator.supportsOption(Okular::SaveInterface::NoOption));

        QString saveError;
        QVERIFY2(generator.save(savedPath, Okular::SaveInterface::SaveChanges, &saveError), qPrintable(saveError));
        QVERIFY(QFileInfo(savedPath).size() > 0);

        qDeleteAll(pages);
        pages.clear();
        generator.closeDocument();

        // Reopen the produced file through a fresh base: the page count and the
        // page content must survive the save.
        EnginePtr reopenedEngine = createEngine(&error);
        QVERIFY2(reopenedEngine, qPrintable(error));
        PdfDocument *const reopenedDocument = reopenedEngine->document();
        QVERIFY(reopenedDocument);
        CapabilityProbe reopened(std::move(reopenedEngine), nullptr, QVariantList());

        QList<Okular::Page *> reopenedPages;
        QCOMPARE(reopened.loadDocumentWithPassword(savedPath, reopenedPages, QString()), Okular::Document::OpenSuccess);
        QCOMPARE(reopenedPages.size(), pageCount);
        QVERIFY(reopenedPages.first()->width() > 0.0);
        QVERIFY(reopenedPages.first()->height() > 0.0);
        QVERIFY2(!reopened.renderToImage(0, 72, false).isNull(), "the saved page must still render");

        Okular::TextPage *const text = reopenedDocument->textPage(0);
        QVERIFY2(text, "the saved PDF must keep its text layer");
        QVERIFY2(!text->text().isEmpty(), "the saved PDF must keep its text content");
        delete text;

        qDeleteAll(reopenedPages);
        reopenedPages.clear();
        reopened.closeDocument();
    }

    void swapsBackingFileThroughBase()
    {
        QTemporaryDir directory;
        QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));
        const QString swappedPath = directory.filePath(QStringLiteral("mengshee-mupdf-capability-swap.pdf"));

        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));

        CapabilityProbe generator(std::move(engine), nullptr, QVariantList());
        QList<Okular::Page *> pages;
        QCOMPARE(generator.loadDocumentWithPassword(testPdf(), pages, QString()), Okular::Document::OpenSuccess);
        QVERIFY(!pages.isEmpty());
        const int pageCount = pages.size();

        QString saveError;
        QVERIFY2(generator.save(swappedPath, Okular::SaveInterface::SaveChanges, &saveError), qPrintable(saveError));

        // The base must reach the capability's swap implementation; the MuPDF
        // legacy swap builds the replacement page models itself.
        QList<Okular::Page *> replacementPages;
        QCOMPARE(generator.swapBackingFile(swappedPath, replacementPages), Okular::Generator::SwapBackingFileReloadInternalData);
        QCOMPARE(replacementPages.size(), pageCount);

        qDeleteAll(replacementPages);
        replacementPages.clear();
        qDeleteAll(pages);
        pages.clear();
        generator.closeDocument();
    }

    void exportsFlattenedPdfThroughBase()
    {
        QTemporaryDir directory;
        QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));
        const QString flattenedPath = directory.filePath(QStringLiteral("mengshee-mupdf-capability-flattened.pdf"));

        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));

        CapabilityProbe generator(std::move(engine), nullptr, QVariantList());
        QList<Okular::Page *> pages;
        QCOMPARE(generator.loadDocumentWithPassword(testPdf(), pages, QString()), Okular::Document::OpenSuccess);
        QVERIFY(!pages.isEmpty());
        const int pageCount = pages.size();
        QVERIFY(generator.canExportFlattenedPdf());

        QString flattenError;
        int flattened = -1;
        int preserved = -1;
        QVERIFY2(generator.exportFlattenedPdf(flattenedPath, &flattenError, &flattened, &preserved), qPrintable(flattenError));
        // file2.pdf has no flattenable annotations, so the result is an empty
        // flatten. It must still be a complete PDF with the same page count.
        QVERIFY(flattened >= 0);
        QVERIFY(preserved >= 0);
        QVERIFY(QFileInfo(flattenedPath).size() > 0);

        qDeleteAll(pages);
        pages.clear();
        generator.closeDocument();

        EnginePtr reopenedEngine = createEngine(&error);
        QVERIFY2(reopenedEngine, qPrintable(error));
        CapabilityProbe reopened(std::move(reopenedEngine), nullptr, QVariantList());
        QList<Okular::Page *> reopenedPages;
        QCOMPARE(reopened.loadDocumentWithPassword(flattenedPath, reopenedPages, QString()), Okular::Document::OpenSuccess);
        QCOMPARE(reopenedPages.size(), pageCount);
        QVERIFY2(!reopened.renderToImage(0, 72, false).isNull(), "the flattened PDF must still render");
        qDeleteAll(reopenedPages);
        reopenedPages.clear();
        reopened.closeDocument();
    }

    /**
     * Drives the whole Okular::PageInsertionInterface surface through
     * PdfGeneratorBase. The capability creates the Okular::Page models; the
     * test owns them exactly like Document does and frees every retired model
     * after the backend forgot it. Nothing may be deleted by the module.
     */
    void editsPageSequenceThroughBase()
    {
        QTemporaryDir directory;
        QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));
        const QString input = copyOfTestPdf(directory);
        QVERIFY2(!input.isEmpty(), "could not copy the test PDF");
        const QString savedPath = directory.filePath(QStringLiteral("mengshee-mupdf-capability-pages.pdf"));

        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        QVERIFY2(engine->pageEditor(), "the MuPDF module must expose its PdfPageEditor");

        CapabilityProbe generator(std::move(engine), nullptr, QVariantList());
        QList<Okular::Page *> pages;
        QCOMPARE(generator.loadDocumentWithPassword(input, pages, QString()), Okular::Document::OpenSuccess);
        QCOMPARE(pages.size(), 3);

        QVERIFY(generator.canInsertBlankPage());
        QVERIFY(generator.canInsertPageFromPdf());
        QVERIFY(generator.canDeletePage());
        QVERIFY(generator.canMovePage());
        QVERIFY(generator.canMovePageInDocument());
        QVERIFY(generator.canRotatePage());
        QVERIFY(generator.canSetPageSize());
        QVERIFY(generator.canCombinePdfFiles());
        QVERIFY(generator.canAppendPdfFile());

        error.clear();
        QCOMPARE(generator.pdfPageCount(input, &error), 3);
        const QString combinedPath = directory.filePath(QStringLiteral("mengshee-mupdf-capability-combined.pdf"));
        error.clear();
        QVERIFY2(generator.combinePdfFiles({input, input}, combinedPath, true, &error), qPrintable(error));
        QVERIFY(QFileInfo(combinedPath).size() > 0);
        error.clear();
        QCOMPARE(generator.pdfPageCount(combinedPath, &error), 6);

        // A null required output keeps the legacy contract: the call fails
        // before the native document is edited.
        quint64 nullEditId = 0;
        QVERIFY(!generator.insertBlankPageInDocument(0, 300.0, 200.0, nullptr, &nullEditId, &error));
        QVERIFY(!generator.duplicatePageInDocument(0, true, nullptr, &nullEditId, &error));
        QVERIFY(!generator.insertPdfPageInDocument(0, input, 1, true, nullptr, &nullEditId, &error));
        QVERIFY(!generator.attachPageInDocument(0, 1, nullptr, &error));
        QVERIFY(!generator.detachPageInDocument(pages[0], 0, static_cast<quint64 *>(nullptr), &error));
        QVERIFY(!generator.rotatePageInDocument(pages[0], 0, 90, nullptr, &error));
        QVERIFY(!generator.adjustPageBoxInDocument(pages[0], 0, 0.0, 0.0, 10.0, 10.0, nullptr, &error));
        QCOMPARE(pages.size(), 3);

        // 1. Insert a blank page after page 0.
        Okular::Page *blank = nullptr;
        quint64 blankEditId = 0;
        error.clear();
        QVERIFY2(generator.insertBlankPageInDocument(0, 300.0, 200.0, &blank, &blankEditId, &error), qPrintable(error));
        QVERIFY(blank);
        QVERIFY(blankEditId != 0);
        QCOMPARE(blank->number(), 1);
        QVERIFY(nearlyEqual(blank->width(), 300.0));
        QVERIFY(nearlyEqual(blank->height(), 200.0));
        pages.insert(1, blank);

        // 2. Resize its visible box; the backend returns a replacement model
        // and the test (like Document) deletes the retired one.
        Okular::Page *resized = nullptr;
        error.clear();
        QVERIFY2(generator.adjustPageBoxInDocument(pages[1], 1, 0.0, 0.0, 10.0, 20.0, &resized, &error), qPrintable(error));
        QVERIFY(resized);
        QVERIFY(resized != pages[1]);
        QVERIFY(nearlyEqual(resized->width(), 310.0));
        QVERIFY(nearlyEqual(resized->height(), 220.0));
        delete pages[1];
        pages[1] = resized;

        // 3. Plain rotation returns another replacement model.
        Okular::Page *rotated = nullptr;
        error.clear();
        QVERIFY2(generator.rotatePageInDocument(pages[1], 1, 90, &rotated, &error), qPrintable(error));
        QVERIFY(rotated);
        QVERIFY(rotated != pages[1]);
        QCOMPARE(rotated->orientation(), Okular::Rotation90);
        QVERIFY(nearlyEqual(rotated->width(), 220.0));
        QVERIFY(nearlyEqual(rotated->height(), 310.0));
        delete pages[1];
        pages[1] = rotated;

        // 4. Duplicate page 0 after itself.
        const double sourceWidth = pages[0]->width();
        const double sourceHeight = pages[0]->height();
        Okular::Page *duplicate = nullptr;
        quint64 duplicateEditId = 0;
        error.clear();
        QVERIFY2(generator.duplicatePageInDocument(0, true, &duplicate, &duplicateEditId, &error), qPrintable(error));
        QVERIFY(duplicate);
        QVERIFY(duplicateEditId != 0);
        QCOMPARE(duplicate->number(), 1);
        QVERIFY(nearlyEqual(duplicate->width(), sourceWidth));
        QVERIFY(nearlyEqual(duplicate->height(), sourceHeight));
        pages.insert(1, duplicate);

        // 5. Import page 2 (one-based) of the source file at the end.
        Okular::Page *imported = nullptr;
        quint64 importEditId = 0;
        error.clear();
        QVERIFY2(generator.insertPdfPageInDocument(pages.size() - 1, input, 2, true, &imported, &importEditId, &error), qPrintable(error));
        QVERIFY(imported);
        QVERIFY(importEditId != 0);
        QCOMPARE(imported->number(), pages.size());
        QVERIFY(nearlyEqual(imported->width(), sourceWidth));
        QVERIFY(nearlyEqual(imported->height(), sourceHeight));
        pages.append(imported);

        // 6. Live move: destination_pageNumber is the final index.
        Okular::Page *moved = pages.takeAt(0);
        error.clear();
        QVERIFY2(generator.movePageInDocument(0, 2, &error), qPrintable(error));
        pages.insert(2, moved);

        // 7. Rotate a page together with its annotations (the legacy variant
        // also returns a replacement model).
        Okular::Page *annotatedRotation = nullptr;
        error.clear();
        QVERIFY2(generator.rotatePageWithAnnotationsInDocument(pages[2], 2, 90, false, &annotatedRotation, &error), qPrintable(error));
        QVERIFY(annotatedRotation);
        QVERIFY(annotatedRotation != pages[2]);
        QCOMPARE(annotatedRotation->orientation(), Okular::Rotation90);
        delete pages[2];
        pages[2] = annotatedRotation;

        // 8. Detach a page and restore it through its edit identity.
        const int detachIndex = 4;
        Okular::Page *retired = pages.takeAt(detachIndex);
        quint64 detachEditId = 0;
        error.clear();
        QVERIFY2(generator.detachPageInDocument(retired, detachIndex, &detachEditId, &error), qPrintable(error));
        QVERIFY(detachEditId != 0);
        delete retired;

        Okular::Page *restored = nullptr;
        error.clear();
        QVERIFY2(generator.attachPageInDocument(1, detachEditId, &restored, &error), qPrintable(error));
        QVERIFY(restored);
        QCOMPARE(restored->number(), 2);
        pages.insert(2, restored);

        // 9. The edit-identity detach overload removes a restored page, and the
        // same identity can restore it again.
        retired = pages.takeAt(2);
        error.clear();
        QVERIFY2(generator.detachPageInDocument(retired, 2, detachEditId, &error), qPrintable(error));
        delete retired;
        restored = nullptr;
        error.clear();
        QVERIFY2(generator.attachPageInDocument(1, detachEditId, &restored, &error), qPrintable(error));
        QVERIFY(restored);
        QCOMPARE(restored->number(), 2);
        pages.insert(2, restored);

        // 10. Delete a page: detach it and keep it detached.
        retired = pages.takeAt(5);
        quint64 deleteEditId = 0;
        error.clear();
        QVERIFY2(generator.detachPageInDocument(retired, 5, &deleteEditId, &error), qPrintable(error));
        QVERIFY(deleteEditId != 0);
        delete retired;
        QCOMPARE(pages.size(), 5);

        // 11. Save the live document and reopen it through a fresh base.
        QString saveError;
        QVERIFY2(generator.save(savedPath, Okular::SaveInterface::SaveChanges, &saveError), qPrintable(saveError));
        QVERIFY(QFileInfo(savedPath).size() > 0);
        qDeleteAll(pages);
        pages.clear();
        generator.closeDocument();

        EnginePtr reopenedEngine = createEngine(&error);
        QVERIFY2(reopenedEngine, qPrintable(error));
        CapabilityProbe reopened(std::move(reopenedEngine), nullptr, QVariantList());
        QList<Okular::Page *> reopenedPages;
        QCOMPARE(reopened.loadDocumentWithPassword(savedPath, reopenedPages, QString()), Okular::Document::OpenSuccess);
        QCOMPARE(reopenedPages.size(), 5);
        for (int pageIndex = 0; pageIndex < reopenedPages.size(); ++pageIndex) {
            QVERIFY(reopenedPages.at(pageIndex));
            QCOMPARE(reopenedPages.at(pageIndex)->number(), pageIndex);
            QVERIFY2(!reopened.renderToImage(pageIndex, 36, false).isNull(), "an edited page must still render after saving");
        }
        qDeleteAll(reopenedPages);
        reopenedPages.clear();
        reopened.closeDocument();
    }

    /** Batch append when the engine exposes it; otherwise the capability must
     * still reject the call without touching the live document. */
    void appendsWholePdfThroughBase()
    {
        QTemporaryDir directory;
        QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));
        const QString input = copyOfTestPdf(directory);
        QVERIFY2(!input.isEmpty(), "could not copy the test PDF");
        const QString savedPath = directory.filePath(QStringLiteral("mengshee-mupdf-capability-appended.pdf"));

        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        QVERIFY2(engine->pageEditor(), "the MuPDF module must expose its PdfPageEditor");

        CapabilityProbe generator(std::move(engine), nullptr, QVariantList());
        QList<Okular::Page *> pages;
        QCOMPARE(generator.loadDocumentWithPassword(input, pages, QString()), Okular::Document::OpenSuccess);
        const int originalCount = pages.size();
        QVERIFY(originalCount > 0);

        QList<Okular::Page *> appendedPages;
        QList<quint64> appendedIds;
        QString appendError;
        if (!generator.canAppendPdfFile()) {
            QVERIFY(!generator.appendPdfFileInDocument(input, true, &appendedPages, &appendedIds, &appendError));
            QVERIFY(appendedPages.isEmpty());
            QVERIFY(appendedIds.isEmpty());
            qDeleteAll(pages);
            pages.clear();
            generator.closeDocument();
            return;
        }

        QVERIFY2(generator.appendPdfFileInDocument(input, true, &appendedPages, &appendedIds, &appendError), qPrintable(appendError));
        QCOMPARE(appendedPages.size(), originalCount);
        QCOMPARE(appendedIds.size(), appendedPages.size());
        for (int pageIndex = 0; pageIndex < appendedPages.size(); ++pageIndex) {
            QVERIFY(appendedPages.at(pageIndex));
            QCOMPARE(appendedPages.at(pageIndex)->number(), originalCount + pageIndex);
        }
        qDeleteAll(appendedPages);
        appendedPages.clear();

        QString saveError;
        QVERIFY2(generator.save(savedPath, Okular::SaveInterface::SaveChanges, &saveError), qPrintable(saveError));
        qDeleteAll(pages);
        pages.clear();
        generator.closeDocument();

        EnginePtr reopenedEngine = createEngine(&error);
        QVERIFY2(reopenedEngine, qPrintable(error));
        CapabilityProbe reopened(std::move(reopenedEngine), nullptr, QVariantList());
        QList<Okular::Page *> reopenedPages;
        QCOMPARE(reopened.loadDocumentWithPassword(savedPath, reopenedPages, QString()), Okular::Document::OpenSuccess);
        QCOMPARE(reopenedPages.size(), 2 * originalCount);
        qDeleteAll(reopenedPages);
        reopenedPages.clear();
        reopened.closeDocument();
    }

    void editsNamedDestinationsThroughCapability()
    {
        QTemporaryDir directory;
        QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));
        const QString input = copyOfTestPdf(directory);
        QVERIFY2(!input.isEmpty(), "could not copy the test PDF");
        const QString savedPath = directory.filePath(QStringLiteral("mengshee-mupdf-capability-named-dest.pdf"));

        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *const document = engine->document();
        QVERIFY(document);
        PdfLinkEditor *const links = engine->linkEditor();
        QVERIFY(links);

        QList<Okular::Page *> models;
        QVERIFY2(openNativeDocument(document, input, &models, &error), qPrintable(error));

        const QString name = QStringLiteral("mengshee-capability-target");
        const QString renamed = QStringLiteral("mengshee-capability-target-renamed");
        QVERIFY2(links->setNamedDestination(name, 0, 0.25, 0.5, &error), qPrintable(error));
        QVERIFY2(links->renameNamedDestination(name, renamed, &error), qPrintable(error));
        // The rename moved the destination: the old name must be gone.
        error.clear();
        QVERIFY(!links->renameNamedDestination(name, QStringLiteral("mengshee-capability-other"), &error));
        QVERIFY(!error.isEmpty());

        // An out-of-range native page fails cleanly and reports an error.
        error.clear();
        QVERIFY(!links->setNamedDestination(QStringLiteral("mengshee-capability-invalid"), static_cast<int>(document->pageCount()), 0.0, 0.0, &error));
        QVERIFY(!error.isEmpty());

        // Persist and reopen: the renamed destination must still exist.
        QString saveError;
        QVERIFY2(engine->saver()->save(savedPath, Okular::SaveInterface::SaveChanges, &saveError), qPrintable(saveError));
        qDeleteAll(models);
        models.clear();
        document->close();

        EnginePtr reopenedEngine = createEngine(&error);
        QVERIFY2(reopenedEngine, qPrintable(error));
        PdfDocument *const reopenedDocument = reopenedEngine->document();
        QVERIFY(reopenedDocument);
        QList<Okular::Page *> reopenedModels;
        QVERIFY2(openNativeDocument(reopenedDocument, savedPath, &reopenedModels, &error), qPrintable(error));
        PdfLinkEditor *const reopenedLinks = reopenedEngine->linkEditor();
        QVERIFY(reopenedLinks);
        QVERIFY2(reopenedLinks->renameNamedDestination(renamed, QStringLiteral("mengshee-capability-target-final"), &error), qPrintable(error));
        QVERIFY2(reopenedLinks->deleteNamedDestination(QStringLiteral("mengshee-capability-target-final"), &error), qPrintable(error));
        error.clear();
        QVERIFY(!reopenedLinks->renameNamedDestination(QStringLiteral("mengshee-capability-missing"), QStringLiteral("mengshee-capability-nope"), &error));
        qDeleteAll(reopenedModels);
        reopenedModels.clear();
        reopenedDocument->close();
    }

    void editsLinksThroughCapability()
    {
        QTemporaryDir directory;
        QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));
        const QString input = copyOfTestPdf(directory);
        QVERIFY2(!input.isEmpty(), "could not copy the test PDF");
        const QString savedPath = directory.filePath(QStringLiteral("mengshee-mupdf-capability-link.pdf"));

        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *const document = engine->document();
        QVERIFY(document);
        PdfLinkEditor *const links = engine->linkEditor();
        QVERIFY(links);

        QList<Okular::Page *> models;
        QVERIFY2(openNativeDocument(document, input, &models, &error), qPrintable(error));
        QVERIFY(document->pageCount() >= 2);

        std::unique_ptr<Okular::Document> viewer = openViewerDocument(input, &error);
        QVERIFY2(viewer, qPrintable(error));
        QVERIFY(viewer->page(0));
        engine->setWrapperDocument(viewer.get());

        // External link: create, verify on the model, edit the target, move it,
        // refresh and delete it.
        const Okular::NormalizedRect externalArea(0.10, 0.10, 0.30, 0.20);
        const QUrl firstUrl(QStringLiteral("https://example.com/mengshee-capability-link"));
        const QUrl secondUrl(QStringLiteral("https://example.com/mengshee-capability-link-edited"));
        QVERIFY2(links->createExternalLink(0, externalArea, firstUrl.toString(), &error), qPrintable(error));
        QVERIFY2(pageHasBrowseUrl(viewer->page(0), firstUrl), "the created external link must appear on the page model");
        QVERIFY2(links->editExternalLinkDestination(0, externalArea, secondUrl.toString(), &error), qPrintable(error));
        QVERIFY2(pageHasBrowseUrl(viewer->page(0), secondUrl), "the edited external link must update the page model");
        const Okular::NormalizedRect movedArea(0.12, 0.12, 0.32, 0.22);
        QVERIFY2(links->editLinkRectangle(0, externalArea, movedArea, Okular::PdfLinkAppearance(), &error), qPrintable(error));
        QVERIFY2(links->refreshPageLinks(0, &error), qPrintable(error));
        QVERIFY2(pageHasBrowseUrl(viewer->page(0), secondUrl), "refreshing links must keep the edited external link");
        QVERIFY2(links->deleteLink(0, movedArea, &error), qPrintable(error));
        QVERIFY(!pageHasBrowseUrl(viewer->page(0), secondUrl));

        // Internal numeric link.
        const Okular::NormalizedRect internalArea(0.40, 0.40, 0.60, 0.50);
        QVERIFY2(links->createInternalLink(0, internalArea, QString(), 1, 0.5, 0.5, Okular::PdfLinkAppearance(), &error), qPrintable(error));
        QVERIFY2(pageHasNumericInternalGoto(viewer->page(0)), "the created internal link must appear on the page model");
        QVERIFY2(links->deleteLink(0, internalArea, &error), qPrintable(error));
        QVERIFY(!pageHasNumericInternalGoto(viewer->page(0)));

        // Internal link to a named destination, then edit it to another name.
        const QString destination = QStringLiteral("mengshee-capability-destination");
        QVERIFY2(links->setNamedDestination(destination, 0, 0.25, 0.5, &error), qPrintable(error));
        const Okular::NormalizedRect namedArea(0.62, 0.62, 0.80, 0.70);
        QVERIFY2(links->createInternalLink(0, namedArea, destination, 0, 0.25, 0.5, Okular::PdfLinkAppearance(), &error), qPrintable(error));
        QVERIFY2(pageHasNamedGoto(viewer->page(0), destination), "the named destination link must resolve to the destination name");
        const QString editedDestination = QStringLiteral("mengshee-capability-destination-edited");
        QVERIFY2(links->setNamedDestination(editedDestination, 1, 0.5, 0.5, &error), qPrintable(error));
        QVERIFY2(links->editInternalLinkDestination(0, namedArea, editedDestination, 0, 0.5, 0.5, &error), qPrintable(error));
        QVERIFY2(pageHasNamedGoto(viewer->page(0), editedDestination), "editing the internal destination must update the page model");
        QVERIFY2(links->deleteLink(0, namedArea, &error), qPrintable(error));
        QVERIFY2(links->deleteNamedDestination(destination, &error), qPrintable(error));
        QVERIFY2(links->deleteNamedDestination(editedDestination, &error), qPrintable(error));

        // Remote link: the destination belongs to the other file.
        const Okular::NormalizedRect remoteArea(0.42, 0.12, 0.58, 0.20);
        const QString remoteFile = QStringLiteral("mengshee-capability-other.pdf");
        QVERIFY2(links->createRemoteLink(0, remoteArea, remoteFile, 2, 0.1, 0.2, QString(), &error), qPrintable(error));
        QVERIFY2(pageHasExternalGoto(viewer->page(0), remoteFile), "the created remote link must appear on the page model");
        QVERIFY2(links->editRemoteLinkDestination(0, remoteArea, remoteFile, 3, 0.2, 0.3, QString(), &error), qPrintable(error));
        QVERIFY2(pageHasExternalGoto(viewer->page(0), remoteFile), "the edited remote link must stay on the page model");
        QVERIFY2(links->deleteLink(0, remoteArea, &error), qPrintable(error));

        // Save and reopen: the external link must survive the write.
        QVERIFY2(links->createExternalLink(0, externalArea, firstUrl.toString(), &error), qPrintable(error));
        QString saveError;
        QVERIFY2(engine->saver()->save(savedPath, Okular::SaveInterface::SaveChanges, &saveError), qPrintable(saveError));
        engine->setWrapperDocument(nullptr);
        qDeleteAll(models);
        models.clear();
        document->close();

        EnginePtr reopenedEngine = createEngine(&error);
        QVERIFY2(reopenedEngine, qPrintable(error));
        PdfDocument *const reopenedDocument = reopenedEngine->document();
        QVERIFY(reopenedDocument);
        QVERIFY2(reopenedDocument->openFile(savedPath, QString()) == OpenStatus::Success, "the saved PDF must reopen");
        Okular::Page *reopenedPage = reopenedDocument->createPageModel(0);
        QVERIFY(reopenedPage);
        QVERIFY2(pageHasBrowseUrl(reopenedPage, firstUrl), "the saved PDF must keep the created external link");
        delete reopenedPage;
        reopenedDocument->close();
    }

    void resolvesPageIdentityThroughActionTargets()
    {
        QTemporaryDir directory;
        QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));
        const QString input = copyOfTestPdf(directory);
        QVERIFY2(!input.isEmpty(), "could not copy the test PDF");
        const QString savedPath = directory.filePath(QStringLiteral("mengshee-mupdf-identity.pdf"));

        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *const document = engine->document();
        QVERIFY(document);
        PdfLinkEditor *const links = engine->linkEditor();
        PdfCustomMetadata *const metadata = engine->customMetadata();
        PdfPageIdentity *const identity = engine->pageIdentity();
        QVERIFY(links);
        QVERIFY(metadata);
        QVERIFY2(identity, "the MuPDF module must expose its PdfPageIdentity");

        QList<Okular::Page *> models;
        QVERIFY2(openNativeDocument(document, input, &models, &error), qPrintable(error));
        QVERIFY(document->pageCount() >= 2);
        // A page without /MengsheeViews metadata has no MuPDF page identity.
        QCOMPARE(identity->pageToken(-1), QString());
        QCOMPARE(identity->pageToken(document->pageCount()), QString());
        QCOMPARE(identity->nativePageForToken(QString()), -1);
        QCOMPARE(identity->nativePageForToken(QStringLiteral("not-a-page-token")), -1);
        QCOMPARE(identity->pageToken(1), QString());

        // The legacy MuPDF token is the Reading View /PageId UUID: writing a
        // Reading View is what creates the page identity, and the capability
        // must expose exactly that value, not a private format.
        Okular::ReadingView view;
        view.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        view.number = 1;
        view.rectangle = Okular::NormalizedRect(0.10, 0.10, 0.90, 0.20);
        QVERIFY2(metadata->setReadingViews(1, {view}, &error), qPrintable(error));
        const QString token = identity->pageToken(1);
        QVERIFY2(!token.isEmpty(), "writing Reading Views must give the page its PageId identity");
        QCOMPARE(token, metadata->pageToken(1));
        QCOMPARE(identity->nativePageForToken(token), 1);
        QCOMPARE(identity->pageToken(0), QString());

        std::unique_ptr<Okular::Document> viewer = openViewerDocument(input, &error);
        QVERIFY2(viewer, qPrintable(error));
        engine->setWrapperDocument(viewer.get());

        // An explicit goto link from page 0 to native page 1. The legacy MuPDF
        // link loader records the resolved viewport instead of a GotoAction
        // page token, so the action's target page is the value the identity has
        // to name until loadLinks() starts carrying tokens.
        const Okular::NormalizedRect area(0.31, 0.31, 0.49, 0.41);
        QVERIFY2(links->createInternalLink(0, area, QString(), 1, 0.0, 0.0, Okular::PdfLinkAppearance(), &error), qPrintable(error));
        const Okular::GotoAction *const action = internalGotoAt(viewer->page(0), area);
        QVERIFY2(action, "the created explicit goto must appear on the page model");
        QVERIFY(!action->isExternal());
        QVERIFY(action->destinationName().isEmpty());
        QVERIFY2(action->destViewport().isValid(), "MuPDF keeps the resolved target viewport on the action");
        QCOMPARE(action->destViewport().pageNumber, 1);
        QCOMPARE(identity->nativePageForToken(token), action->destViewport().pageNumber);

        // Persist and reopen: the /PageId token and the action target survive.
        QVERIFY2(engine->saver()->save(savedPath, Okular::SaveInterface::SaveChanges, &error), qPrintable(error));
        engine->setWrapperDocument(nullptr);
        qDeleteAll(models);
        models.clear();
        document->close();

        EnginePtr reopenedEngine = createEngine(&error);
        QVERIFY2(reopenedEngine, qPrintable(error));
        PdfDocument *const reopenedDocument = reopenedEngine->document();
        QVERIFY(reopenedDocument);
        PdfPageIdentity *const reopenedIdentity = reopenedEngine->pageIdentity();
        QVERIFY(reopenedIdentity);
        QList<Okular::Page *> reopenedModels;
        QVERIFY2(openNativeDocument(reopenedDocument, savedPath, &reopenedModels, &error), qPrintable(error));
        QCOMPARE(reopenedIdentity->pageToken(1), token);
        QCOMPARE(reopenedIdentity->nativePageForToken(token), 1);

        std::unique_ptr<Okular::Document> reopenedViewer = openViewerDocument(savedPath, &error);
        QVERIFY2(reopenedViewer, qPrintable(error));
        const Okular::GotoAction *const reopenedAction = internalGotoAt(reopenedViewer->page(0), area);
        QVERIFY2(reopenedAction, "the saved PDF must keep the explicit goto link");
        QVERIFY(reopenedAction->destViewport().isValid());
        QCOMPARE(reopenedAction->destViewport().pageNumber, 1);

        // The shared front end resolves the same engine page identity token.
        EnginePtr baseEngine = createEngine(&error);
        QVERIFY2(baseEngine, qPrintable(error));
        CapabilityProbe base(std::move(baseEngine), nullptr, QVariantList());
        QList<Okular::Page *> basePages;
        QCOMPARE(base.loadDocumentWithPassword(savedPath, basePages, QString()), Okular::Document::OpenSuccess);
        QVERIFY(basePages.size() >= 2);
        QCOMPARE(base.pageNumberForPageToken(token), 1);
        QCOMPARE(base.pageNumberForPageToken(QStringLiteral("not-a-page-token")), -1);
        qDeleteAll(basePages);
        basePages.clear();
        base.closeDocument();

        qDeleteAll(reopenedModels);
        reopenedModels.clear();
        reopenedDocument->close();
    }

    void readsOcrLayerThroughCapability()
    {
        QTemporaryDir directory;
        QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));
        const QString input = copyOfTestPdf(directory);
        QVERIFY2(!input.isEmpty(), "could not copy the test PDF");

        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *const document = engine->document();
        QVERIFY(document);
        PdfOcr *const ocr = engine->ocr();
        QVERIFY(ocr);

        QList<Okular::Page *> models;
        QVERIFY2(openNativeDocument(document, input, &models, &error), qPrintable(error));
        const int pageCount = document->pageCount();

        // file2.pdf has no OCR text layer: reading must either return an empty
        // list or fail with an error, but never crash or leak an exception.
        QList<Okular::OcrTextWord> words;
        error.clear();
        const bool read = ocr->readTextLayer(0, &words, &error);
        if (!read) {
            QVERIFY2(!error.isEmpty(), "a failed OCR layer read must report an error");
        }

        // Out-of-range pages fail cleanly.
        words.clear();
        error.clear();
        QVERIFY(!ocr->readTextLayer(-1, &words, &error));
        QVERIFY(!error.isEmpty());
        error.clear();
        QVERIFY(!ocr->readTextLayer(pageCount, &words, &error));
        QVERIFY(!error.isEmpty());
        error.clear();
        QVERIFY(!ocr->replaceTextLayer(pageCount, QList<Okular::OcrTextWord>(), &error));
        QVERIFY(!error.isEmpty());

        qDeleteAll(models);
        models.clear();
        document->close();
    }

    void editsCustomMetadataThroughCapability()
    {
        QTemporaryDir directory;
        QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));
        const QString input = copyOfTestPdf(directory);
        QVERIFY2(!input.isEmpty(), "could not copy the test PDF");

        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *const document = engine->document();
        QVERIFY(document);
        PdfCustomMetadata *const metadata = engine->customMetadata();
        QVERIFY(metadata);
        QList<Okular::Page *> models;
        QVERIFY2(openNativeDocument(document, input, &models, &error), qPrintable(error));

        // Numbered callout numbering: no payload, then a valid one, then removal.
        QVERIFY(metadata->numberedCalloutNumberingJson().isEmpty());
        if (metadata->canEditNumberedCalloutNumbering()) {
            const QString json = QStringLiteral("{\"version\":1,\"pattern\":\"#{n}\",\"restartPerPage\":true}");
            QVERIFY2(metadata->setNumberedCalloutNumberingJson(json, &error), qPrintable(error));
            QCOMPARE(metadata->numberedCalloutNumberingJson(), json);
            QVERIFY2(metadata->setNumberedCalloutNumberingJson(QString(), &error), qPrintable(error));
            QVERIFY(metadata->numberedCalloutNumberingJson().isEmpty());
            error.clear();
            QVERIFY(!metadata->setNumberedCalloutNumberingJson(QStringLiteral("not-json"), &error));
            QVERIFY(!error.isEmpty());
        }

        // Reading Views: write one, read it back, follow its page token and clear it.
        if (metadata->canEditReadingViews()) {
            error.clear();
            QVERIFY(metadata->readingViews(0, &error).isEmpty());
            QVERIFY2(error.isEmpty(), qPrintable(error));

            Okular::ReadingView view;
            view.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            view.number = 1;
            view.rectangle = Okular::NormalizedRect(0.10, 0.10, 0.90, 0.20);
            QVERIFY2(metadata->setReadingViews(0, {view}, &error), qPrintable(error));

            const QList<Okular::ReadingView> readBack = metadata->readingViews(0, &error);
            QCOMPARE(readBack.size(), 1);
            QCOMPARE(readBack.constFirst().id, view.id);
            QCOMPARE(readBack.constFirst().number, view.number);
            QVERIFY(readBack.constFirst().rectangle == view.rectangle);

            const QString token = metadata->pageToken(0);
            QVERIFY2(!token.isEmpty(), "writing Reading Views must create the stable page token");
            QCOMPARE(metadata->pageForToken(token), 0);
            QCOMPARE(metadata->pageForToken(QUuid::createUuid().toString(QUuid::WithoutBraces)), -1);
            QCOMPARE(metadata->pageToken(static_cast<int>(document->pageCount())), QString());

            QVERIFY2(metadata->setReadingViews(0, QList<Okular::ReadingView>(), &error), qPrintable(error));
            QVERIFY(metadata->readingViews(0, &error).isEmpty());
        }

        qDeleteAll(models);
        models.clear();
        document->close();
    }

    void pageDependentCapabilitiesFailSafelyWithoutDocument()
    {
        QTemporaryDir directory;
        QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));
        const QString input = copyOfTestPdf(directory);
        QVERIFY2(!input.isEmpty(), "could not copy the test PDF");

        QString error;
        EnginePtr engine = createEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        PdfDocument *const document = engine->document();
        QVERIFY(document);
        QList<Okular::Page *> models;
        QVERIFY2(openNativeDocument(document, input, &models, &error), qPrintable(error));

        PdfLinkEditor *const links = engine->linkEditor();
        QVERIFY(links);

        // No wrapper document: page-dependent editing must fail with an error,
        // not dereference a null page.
        error.clear();
        QVERIFY(!links->createExternalLink(0, Okular::NormalizedRect(0.1, 0.1, 0.2, 0.2), QStringLiteral("https://example.com"), &error));
        QVERIFY(!error.isEmpty());
        error.clear();
        QVERIFY(!links->refreshPageLinks(0, &error));
        QVERIFY(!error.isEmpty());
        error.clear();
        QVERIFY(!links->deleteLink(0, Okular::NormalizedRect(0.1, 0.1, 0.2, 0.2), &error));
        QVERIFY(!error.isEmpty());

        qDeleteAll(models);
        models.clear();
        document->close();
    }
};

QTEST_MAIN(MuPdfCapabilityTest)

#include "capabilitytest.moc"
