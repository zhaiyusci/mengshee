// SPDX-License-Identifier: AGPL-3.0-or-later
#include "pdfgeneratorbase.h"

#include "pdfengine.h"

#include <core/annotations.h>
#include <core/document.h>
#include <core/page.h>

#include <QPrinter>
#include <QHash>
#include <QVector>
#include <QtMath>

namespace Mengshee
{
namespace Pdf
{
namespace
{

void clearError(QString *errorText)
{
    if (errorText) {
        errorText->clear();
    }
}

bool unsupported(QString *errorText, const char *operation)
{
    if (errorText) {
        *errorText = QStringLiteral("%1 is not supported by the active PDF engine.").arg(QString::fromLatin1(operation));
    }
    return false;
}

/**
 * Zero-based logical page index to zero-based native PDF page index.
 *
 * The logical order is owned by Okular::Document (or, in generator tests, by
 * the base itself); the native order is owned by the engine. They are related
 * by the mapping kept in PdfGeneratorBase::Private and must never be assumed
 * equal. Every narrow capability call uses a native index, every Okular entry
 * point receives a logical index.
 */

/**
 * One-based logical page number to zero-based native index. A destination name
 * overrides the page number, so callers only use this when no name is set.
 */

} // namespace

class PdfGeneratorBase::Private
{
public:
    explicit Private(EnginePtr value)
        : engine(std::move(value))
    {
    }

    /**
     * The narrow PdfDocument of the currently open document, or null when no
     * load succeeded yet. Never infer this from engine->document() alone: the
     * engine object exists before a document is loaded.
     */
    PdfDocument *activeDocument() const
    {
        return narrowDocumentActive && engine ? engine->document() : nullptr;
    }

    // Capability objects are owned by the engine and stable for its lifetime,
    // so they are looked up through the engine instead of being cached here.
    PdfSaver *saver() const
    {
        return engine ? engine->saver() : nullptr;
    }
    PdfPageEditor *pageEditor() const
    {
        return engine ? engine->pageEditor() : nullptr;
    }
    PdfLinkEditor *linkEditor() const
    {
        return engine ? engine->linkEditor() : nullptr;
    }
    PdfOcr *ocr() const
    {
        return engine ? engine->ocr() : nullptr;
    }
    PdfCustomMetadata *customMetadata() const
    {
        return engine ? engine->customMetadata() : nullptr;
    }

    EnginePtr engine;
    /// Set only when the last successful load built the page models through
    /// PdfDocument; cleared by closeDocument().
    bool narrowDocumentActive = false;

    /**
     * Page identity mapping. nativePageByModel is the authoritative
     * model-to-native mapping; logicalPageOrder records the current logical
     * order of those same models so a logical index can be resolved even when
     * no Okular::Document owns the models (generator tests) and so a native
     * index can be turned back into a logical one.
     *
     * Both are rebuilt by a load, updated by every page-topology operation in
     * this class and cleared on close. They are never derived from
     * Okular::Page::number().
     */
    QHash<Okular::Page *, int> nativePageByModel;
    QVector<Okular::Page *> logicalPageOrder;

    /**
     * Set when a backing-file swap handed new page models to Document. Which
     * Page* identity survives the swap is Document's decision, so the mapping
     * is rebuilt lazily from Document's page vector on the next lookup instead
     * of guessing here.
     */
    mutable bool pageMappingNeedsDocumentResync = false;

    void clearPageMapping()
    {
        nativePageByModel.clear();
        logicalPageOrder.clear();
        pageMappingNeedsDocumentResync = false;
    }

    void rebuildPageMapping(const QList<Okular::Page *> &pages)
    {
        nativePageByModel.clear();
        logicalPageOrder.clear();
        nativePageByModel.reserve(pages.size());
        logicalPageOrder.reserve(pages.size());
        for (int nativePage = 0; nativePage < pages.size(); ++nativePage) {
            Okular::Page *const page = pages.at(nativePage);
            if (!page) {
                continue;
            }
            nativePageByModel.insert(page, nativePage);
            logicalPageOrder.append(page);
        }
        pageMappingNeedsDocumentResync = false;
    }

    /** Moves the recorded native index of every page after @p nativePage by @p delta. */
    void shiftNativePagesAfter(int nativePage, int delta)
    {
        for (auto it = nativePageByModel.begin(); it != nativePageByModel.end(); ++it) {
            if (it.value() > nativePage) {
                it.value() += delta;
            }
        }
    }

    /** Records a newly inserted page at @p nativePage and logical position @p logicalIndex. */
    void insertPageMapping(Okular::Page *page, int nativePage, int logicalIndex)
    {
        if (!page || nativePage < 0) {
            return;
        }
        nativePageByModel.remove(page);
        shiftNativePagesAfter(nativePage - 1, 1);
        nativePageByModel.insert(page, nativePage);
        logicalPageOrder.insert(qBound(0, logicalIndex, logicalPageOrder.size()), page);
    }

    /** Drops a removed page and closes the native gap it leaves behind. */
    void removePageMapping(Okular::Page *page, int nativePage, int logicalIndex)
    {
        Okular::Page *const mappedPage = page;
        if (mappedPage) {
            nativePageByModel.remove(mappedPage);
        }
        const int logical = page && logicalPageOrder.value(logicalIndex, nullptr) != page ? logicalPageOrder.indexOf(page) : logicalIndex;
        if (logical >= 0 && logical < logicalPageOrder.size() && (!page || logicalPageOrder.at(logical) == page)) {
            logicalPageOrder.removeAt(logical);
        } else if (page) {
            logicalPageOrder.removeAll(page);
        }
        if (nativePage >= 0) {
            shiftNativePagesAfter(nativePage, -1);
        }
    }

    /** Applies the native shift of a page move; the Page* identity does not change. */
    void movePageMapping(int fromNative, int toNative)
    {
        if (fromNative == toNative || fromNative < 0 || toNative < 0) {
            return;
        }
        Okular::Page *const movedPage = nativePageByModel.key(fromNative, nullptr);
        for (auto it = nativePageByModel.begin(); it != nativePageByModel.end(); ++it) {
            int &nativePage = it.value();
            if (fromNative < toNative) {
                if (nativePage > fromNative && nativePage <= toNative) {
                    --nativePage;
                }
            } else {
                if (nativePage >= toNative && nativePage < fromNative) {
                    ++nativePage;
                }
            }
        }
        if (movedPage) {
            nativePageByModel.insert(movedPage, toNative);
        }
        if (!logicalPageOrder.isEmpty() && fromNative < logicalPageOrder.size() && toNative < logicalPageOrder.size()) {
            Okular::Page *const page = logicalPageOrder.takeAt(fromNative);
            logicalPageOrder.insert(toNative, page);
        }
    }
};

PdfGeneratorBase::PdfGeneratorBase(EnginePtr engine, QObject *parent, const QVariantList &args)
    : Okular::Generator(parent, args)
    , d(new Private(std::move(engine)))
{
    // The wrapper is the object Document talks to; mirror the engine's
    // features so search, text extraction, tiled rendering, font data and
    // backing-file swap keep working. The engine forwards them from the
    // backend generator it owns internally; the front end only sees flags.
    if (d->engine) {
        for (const GeneratorFeature feature : {Threaded,
                                               TextExtraction,
                                               ReadRawData,
                                               FontInfo,
                                               PageSizes,
                                               PrintNative,
                                               PrintPostscript,
                                               PrintToFile,
                                               TiledRendering,
                                               SwapBackingFile,
                                               SupportsCancelling}) {
            if (d->engine->hasFeature(feature)) {
                setFeature(feature);
            }
        }
    }

    // The shell shows this in the window caption so the actually loaded engine
    // is visible without opening a log or a settings dialog.
    QString backendName = d->engine ? d->engine->id() : QString();
    if (backendName == QLatin1String(MuPdfEngineId)) {
        backendName = QStringLiteral("MuPDF");
    } else if (backendName == QLatin1String(PopplerEngineId)) {
        backendName = QStringLiteral("Poppler");
    }
    setProperty("mengsheePdfEngine", backendName);
}

PdfGeneratorBase::~PdfGeneratorBase() = default;

bool PdfGeneratorBase::hasEngine() const
{
    return d->engine && d->engine->document();
}

PdfEngine *PdfGeneratorBase::engine() const
{
    return d->engine.get();
}

PdfConfigurable *PdfGeneratorBase::configurable() const
{
    return d->engine ? d->engine->configurable() : nullptr;
}

PdfPrintConfigurator *PdfGeneratorBase::printConfigurator() const
{
    return d->engine ? d->engine->printConfigurator() : nullptr;
}

PdfPrintPreviewProvider *PdfGeneratorBase::printPreviewProvider() const
{
    return d->engine ? d->engine->printPreviewProvider() : nullptr;
}

QString PdfGeneratorBase::engineId() const
{
    return d->engine ? d->engine->id() : QString();
}

void PdfGeneratorBase::syncPageMapping() const
{
    if (!d->pageMappingNeedsDocumentResync) {
        return;
    }
    const Okular::Document *const doc = document();
    if (!doc) {
        d->pageMappingNeedsDocumentResync = false;
        d->clearPageMapping();
        return;
    }
    const int pageCount = static_cast<int>(doc->pages());
    QList<Okular::Page *> pages;
    pages.reserve(pageCount);
    bool mappingMatches = pageCount == d->logicalPageOrder.size();
    for (int logicalPage = 0; logicalPage < pageCount; ++logicalPage) {
        Okular::Page *const page = const_cast<Okular::Page *>(doc->page(logicalPage));
        pages.append(page);
        if (!page || !d->nativePageByModel.contains(page)) {
            mappingMatches = false;
        }
    }
    // Document may still be publishing the swap result, or may have adopted the
    // new page data into the existing models (the Page* identity is unchanged
    // then). In both cases the mapping is still valid; only a real replacement
    // of the model objects requires a rebuild, in native order.
    d->pageMappingNeedsDocumentResync = false;
    if (!mappingMatches) {
        d->rebuildPageMapping(pages);
    }
}

int PdfGeneratorBase::nativePageForPage(Okular::Page *page) const
{
    if (!page) {
        return -1;
    }
    syncPageMapping();
    return d->nativePageByModel.value(page, -1);
}

Okular::Page *PdfGeneratorBase::pageModelForNative(int nativePage) const
{
    if (nativePage < 0) {
        return nullptr;
    }
    syncPageMapping();
    for (auto it = d->nativePageByModel.cbegin(); it != d->nativePageByModel.cend(); ++it) {
        if (it.value() == nativePage) {
            return it.key();
        }
    }
    return nullptr;
}

int PdfGeneratorBase::nativePageForLogicalPage(int logicalPage) const
{
    if (logicalPage < 0) {
        return -1;
    }
    syncPageMapping();
    if (const Okular::Document *const doc = document()) {
        return logicalPage < static_cast<int>(doc->pages()) ? nativePageForPage(const_cast<Okular::Page *>(doc->page(logicalPage))) : -1;
    }
    return logicalPage < d->logicalPageOrder.size() ? d->nativePageByModel.value(d->logicalPageOrder.at(logicalPage), -1) : -1;
}

int PdfGeneratorBase::logicalPageForNative(int nativePage) const
{
    if (nativePage < 0) {
        return -1;
    }
    syncPageMapping();
    if (const Okular::Document *const doc = document()) {
        const int pageCount = doc->pages();
        for (int logicalPage = 0; logicalPage < pageCount; ++logicalPage) {
            if (nativePageForPage(const_cast<Okular::Page *>(doc->page(logicalPage))) == nativePage) {
                return logicalPage;
            }
        }
        return -1;
    }
    for (int logicalPage = 0; logicalPage < d->logicalPageOrder.size(); ++logicalPage) {
        if (d->nativePageByModel.value(d->logicalPageOrder.at(logicalPage), -1) == nativePage) {
            return logicalPage;
        }
    }
    return -1;
}

int PdfGeneratorBase::nativePageForOneBasedPage(int pageNumber) const
{
    return pageNumber <= 0 ? -1 : nativePageForLogicalPage(pageNumber - 1);
}

bool PdfGeneratorBase::resolveInsertAfterPage(int logicalInsertAfterPage, int *nativeAfter, QString *errorText) const
{
    if (!nativeAfter) {
        return false;
    }
    *nativeAfter = -1;
    if (logicalInsertAfterPage == -1) {
        return true;
    }
    const int logicalPageCount = document() ? document()->pages() : d->logicalPageOrder.size();
    if (logicalInsertAfterPage < -1 || logicalInsertAfterPage >= logicalPageCount) {
        if (errorText) {
            *errorText = QStringLiteral("The insertion position is outside the open document.");
        }
        return false;
    }
    const int nativePage = nativePageForLogicalPage(logicalInsertAfterPage);
    if (nativePage < 0) {
        if (errorText) {
            *errorText = QStringLiteral("The insertion position is not part of the open document.");
        }
        return false;
    }
    *nativeAfter = nativePage;
    return true;
}

Okular::Document::OpenResult PdfGeneratorBase::loadDocumentWithPassword(const QString &fileName, QList<Okular::Page *> &pagesVector, const QString &password)
{
    d->narrowDocumentActive = false;
    d->clearPageMapping();
    if (PdfDocument *pdfDocument = d->engine ? d->engine->document() : nullptr) {
        return loadDocumentThroughEngine(pdfDocument, fileName, QByteArray(), false, pagesVector, password);
    }
    Q_EMIT error(QStringLiteral("No PDF engine is available."), 0);
    return Okular::Document::OpenError;
}

Okular::Document::OpenResult PdfGeneratorBase::loadDocumentFromDataWithPassword(const QByteArray &fileData, QList<Okular::Page *> &pagesVector, const QString &password)
{
    d->narrowDocumentActive = false;
    d->clearPageMapping();
    if (PdfDocument *pdfDocument = d->engine ? d->engine->document() : nullptr) {
        return loadDocumentThroughEngine(pdfDocument, QString(), fileData, true, pagesVector, password);
    }
    Q_EMIT error(QStringLiteral("No PDF engine is available."), 0);
    return Okular::Document::OpenError;
}

Okular::Document::OpenResult PdfGeneratorBase::loadDocumentThroughEngine(PdfDocument *pdfDocument,
                                                                        const QString &fileName,
                                                                        const QByteArray &fileData,
                                                                        bool fromData,
                                                                        QList<Okular::Page *> &pagesVector,
                                                                        const QString &password)
{
    Q_ASSERT(pdfDocument);
    d->clearPageMapping();

    // The engine's backend generator owns the native document and uses the
    // owning Document for page-model lookup, annotation appearance and
    // printing. Hand it over before the document is opened; the engine decides
    // internally how to store it.
    d->engine->setWrapperDocument(const_cast<Okular::Document *>(document()));

    const OpenStatus status = fromData ? pdfDocument->openData(fileData, password) : pdfDocument->openFile(fileName, password);
    if (status != OpenStatus::Success) {
        pdfDocument->close();
        d->engine->setWrapperDocument(nullptr);
        return status == OpenStatus::NeedsPassword ? Okular::Document::OpenNeedsPassword : Okular::Document::OpenError;
    }

    // The base owns the page models: ask the engine for one model per native
    // page. createPageModel() loads the native page state and attaches
    // annotations, links and forms.
    QList<Okular::Page *> pages;
    const int pageCount = pdfDocument->pageCount();
    for (int i = 0; i < pageCount; ++i) {
        Okular::Page *page = pdfDocument->createPageModel(i);
        if (!page) {
            // A partial model list is unusable; discard it and fail the load
            // instead of falling back to another load path.
            qDeleteAll(pages);
            pdfDocument->close();
            d->engine->setWrapperDocument(nullptr);
            Q_EMIT error(QStringLiteral("The PDF engine could not build the page models."), 0);
            return Okular::Document::OpenError;
        }
        pages.append(page);
    }

    if (pageCount == 0 || pages.isEmpty()) {
        qDeleteAll(pages);
        pdfDocument->close();
        d->engine->setWrapperDocument(nullptr);
        Q_EMIT error(QStringLiteral("The PDF document has no pages."), 0);
        return Okular::Document::OpenError;
    }

    pagesVector = std::move(pages);
    // The narrow load built the models in native order and they start in that
    // same order logically; record the mapping before Document can reorder.
    d->rebuildPageMapping(pagesVector);
    // The models were built at the module's 72 DPI, where a model size is the
    // PDF point size. Keep the base in the same metric instead of the screen
    // DPI the Document set before opening.
    setDPI(QSizeF(72.0, 72.0));
    d->narrowDocumentActive = true;
    return Okular::Document::OpenSuccess;
}

Okular::DocumentInfo PdfGeneratorBase::generateDocumentInfo(const QSet<Okular::DocumentInfo::Key> &keys) const
{
    if (PdfDocument *pdfDocument = d->activeDocument()) {
        return pdfDocument->documentInfo(keys);
    }
    return Okular::DocumentInfo();
}

Okular::FontInfo::List PdfGeneratorBase::fontsForPage(int page)
{
    if (PdfDocument *pdfDocument = d->activeDocument()) {
        // -1 is the document-level font list; every other value is a logical
        // page index that Document took from the current page order.
        const int nativePage = page < 0 ? -1 : nativePageForLogicalPage(page);
        if (page >= 0 && nativePage < 0) {
            return Okular::FontInfo::List();
        }
        return pdfDocument->fontsForPage(nativePage);
    }
    return Okular::FontInfo::List();
}

QByteArray PdfGeneratorBase::requestFontData(const Okular::FontInfo &font)
{
    if (PdfDocument *pdfDocument = d->activeDocument()) {
        return pdfDocument->fontData(font);
    }
    return QByteArray();
}

const QList<Okular::EmbeddedFile *> *PdfGeneratorBase::embeddedFiles() const
{
    if (PdfDocument *pdfDocument = d->activeDocument()) {
        return pdfDocument->embeddedFiles();
    }
    return nullptr;
}

PdfGeneratorBase::PageLayout PdfGeneratorBase::defaultPageLayout() const
{
    if (PdfDocument *pdfDocument = d->activeDocument()) {
        return pdfDocument->defaultPageLayout();
    }
    return Okular::Generator::NoLayout;
}

bool PdfGeneratorBase::defaultPageContinuous() const
{
    if (PdfDocument *pdfDocument = d->activeDocument()) {
        return pdfDocument->defaultPageContinuous();
    }
    return false;
}

PdfGeneratorBase::PageSizeMetric PdfGeneratorBase::pagesSizeMetric() const
{
    // Page models are built at 72 DPI on the only remaining load path, so their
    // model size is the PDF point size.
    return Okular::Generator::Points;
}

const Okular::DocumentSynopsis *PdfGeneratorBase::generateDocumentSynopsis()
{
    if (PdfDocument *pdfDocument = d->activeDocument()) {
        return pdfDocument->synopsis();
    }
    return nullptr;
}

bool PdfGeneratorBase::setDocumentSynopsis(const Okular::DocumentSynopsis &synopsis, QString *errorText)
{
    clearError(errorText);
    if (PdfDocument *pdfDocument = d->activeDocument()) {
        return pdfDocument->setSynopsis(synopsis, errorText);
    }
    return unsupported(errorText, "Setting the document outline");
}

QVariant PdfGeneratorBase::metaData(const QString &key, const QVariant &option) const
{
    if (PdfDocument *pdfDocument = d->activeDocument()) {
        return pdfDocument->metaData(key, option);
    }
    return QVariant();
}

bool PdfGeneratorBase::isAllowed(Okular::Permission permission) const
{
    if (PdfDocument *pdfDocument = d->activeDocument()) {
        return pdfDocument->isAllowed(permission);
    }
    return false;
}

Okular::Document::PrintError PdfGeneratorBase::print(QPrinter &printer)
{
    if (PdfPrinter *const pdfPrinter = d->engine ? d->engine->printer() : nullptr) {
        return pdfPrinter->print(printer);
    }
    return Okular::Document::UnknownPrintError;
}

QImage PdfGeneratorBase::image(Okular::PixmapRequest *request)
{
    PdfDocument *const pdfDocument = d->activeDocument();
    if (!pdfDocument || !request) {
        // A null request is not meaningful for the renderer; in particular do
        // not dereference it.
        return {};
    }

    // A pixmap request carries both the logical page number and, when the
    // Document issued it, the page model. Prefer the model: it is the page that
    // is actually being rendered and it is mapped even if the logical order and
    // the native order differ. Outside a Document (generator tests) the request
    // has no page model; PixmapRequest does not initialize it, so never read it
    // without one.
    Okular::Page *const requestPage = document() ? request->page() : nullptr;
    int nativePage = requestPage ? nativePageForPage(requestPage) : -1;
    if (nativePage < 0) {
        nativePage = nativePageForLogicalPage(request->pageNumber());
    }
    if (nativePage < 0) {
        return {};
    }

    RenderRequest narrowRequest;
    narrowRequest.page = nativePage;
    narrowRequest.width = request->width();
    narrowRequest.height = request->height();
    narrowRequest.includeAnnotations = true;
    narrowRequest.pageModel = requestPage;
    narrowRequest.shouldAbort = [request]() {
        return request->shouldAbortRender();
    };
    if (request->isTile()) {
        narrowRequest.region = request->normalizedRect().geometry(request->width(), request->height());
    }

    const RenderResult result = pdfDocument->renderPage(narrowRequest);
    QImage image = result.image;
    if (image.isNull()) {
        return {};
    }
    if (request->isTile() && !result.regionApplied) {
        const QRect region = narrowRequest.region.intersected(image.rect());
        if (region.isEmpty()) {
            return {};
        }
        image = image.copy(region);
    }
    return request->shouldAbortRender() ? QImage() : image;
}

bool PdfGeneratorBase::canRenderToImage() const
{
    return d->activeDocument() != nullptr;
}

QImage PdfGeneratorBase::renderToImage(int page, int dpi, bool includeAnnotations, QString *error)
{
    if (PdfDocument *pdfDocument = d->activeDocument()) {
        // Document::renderToImage() hands us a logical page index; the engine
        // renders native pages only.
        const int nativePage = nativePageForLogicalPage(page);
        if (nativePage < 0) {
            if (error) {
                *error = QStringLiteral("The requested page is not part of the open document.");
            }
            return {};
        }
        const PageGeometry geometry = pdfDocument->pageGeometry(nativePage);
        QSizeF size = geometry.cropBox;
        if (!size.isValid() || size.isEmpty()) {
            size = geometry.mediaBox;
        }
        if (!size.isValid() || size.isEmpty() || dpi <= 0) {
            if (error) {
                *error = QStringLiteral("The requested PDF page has an invalid size.");
            }
            return {};
        }

        Okular::Page *modelPage = pageModelForNative(nativePage);
        if (modelPage && (static_cast<int>(modelPage->rotation()) % 2) != 0) {
            size.transpose();
        }

        const int width = qCeil(size.width() * dpi / 72.0);
        const int height = qCeil(size.height() * dpi / 72.0);
        RenderRequest request;
        request.page = nativePage;
        request.width = width;
        request.height = height;
        request.includeAnnotations = includeAnnotations;
        request.pageModel = modelPage;
        request.applyViewRotation = true;
        RenderResult result = pdfDocument->renderPage(request);
        if (result.image.isNull() && error) {
            *error = result.errorText.isEmpty() ? QStringLiteral("The PDF engine failed to render the page.") : result.errorText;
        }
        return result.image;
    }

    if (error) {
        *error = d->engine ? QStringLiteral("No PDF document is open.") : QStringLiteral("No PDF engine is available.");
    }
    return {};
}

QAbstractItemModel *PdfGeneratorBase::layersModel() const
{
    if (PdfBackendActions *actions = d->engine ? d->engine->backendActions() : nullptr) {
        return actions->layersModel();
    }
    return nullptr;
}

Okular::BackendOpaqueAction::OpaqueActionResult PdfGeneratorBase::opaqueAction(const Okular::BackendOpaqueAction *action)
{
    if (PdfBackendActions *actions = d->engine ? d->engine->backendActions() : nullptr) {
        return actions->opaqueAction(action);
    }
    return Okular::BackendOpaqueAction::DoNothing;
}

Okular::Action *PdfGeneratorBase::additionalDocumentAction(Okular::Document::DocumentAdditionalActionType type)
{
    if (PdfBackendActions *actions = d->engine ? d->engine->backendActions() : nullptr) {
        return actions->additionalDocumentAction(type);
    }
    return nullptr;
}

Okular::ExportFormat::List PdfGeneratorBase::exportFormats() const
{
    if (PdfSaver *const pdfSaver = d->engine ? d->engine->saver() : nullptr) {
        return pdfSaver->exportFormats();
    }
    return Okular::ExportFormat::List();
}

bool PdfGeneratorBase::exportTo(const QString &fileName, const Okular::ExportFormat &format)
{
    if (PdfSaver *const pdfSaver = d->engine ? d->engine->saver() : nullptr) {
        return pdfSaver->exportTo(fileName, format);
    }
    return false;
}

bool PdfGeneratorBase::canSign() const
{
    PdfSigner *const pdfSigner = d->engine ? d->engine->signer() : nullptr;
    return pdfSigner && pdfSigner->canSign();
}

std::pair<Okular::SigningResult, QString> PdfGeneratorBase::sign(const Okular::NewSignatureData &data, const QString &fileName)
{
    PdfSigner *const pdfSigner = d->engine ? d->engine->signer() : nullptr;
    if (!pdfSigner) {
        return {Okular::GenericSigningError, QStringLiteral("Signing is not supported by the active PDF engine.")};
    }

    // The signer owns the certificate backend selection; keep it in sync before
    // the call.
    pdfSigner->setActiveCertificateBackend(activeCertificateBackend());
    if (!pdfSigner->canSign()) {
        return {Okular::GenericSigningError, QStringLiteral("Signing is not supported by the active PDF engine.")};
    }

    // PdfSigner takes the already-resolved native page; the page model carried
    // by the request is logical.
    const int nativePage = nativePageForLogicalPage(data.page());
    if (nativePage < 0) {
        return {Okular::GenericSigningError, QStringLiteral("The signature page is not part of the open document.")};
    }
    return pdfSigner->sign(data, nativePage, fileName);
}

Okular::CertificateStore *PdfGeneratorBase::certificateStore() const
{
    PdfSigner *const pdfSigner = d->engine ? d->engine->signer() : nullptr;
    return pdfSigner ? pdfSigner->certificateStore() : nullptr;
}

bool PdfGeneratorBase::canExportFlattenedPdf() const
{
    PdfSaver *const pdfSaver = d->engine ? d->engine->saver() : nullptr;
    return pdfSaver && pdfSaver->canExportFlattenedPdf();
}

bool PdfGeneratorBase::exportFlattenedPdf(const QString &fileName, QString *errorText, int *flattenedAnnotations, int *preservedAnnotations)
{
    clearError(errorText);
    if (PdfSaver *const pdfSaver = d->engine ? d->engine->saver() : nullptr) {
        return pdfSaver->exportFlattenedPdf(fileName, errorText, flattenedAnnotations, preservedAnnotations);
    }
    return unsupported(errorText, "Flattened PDF export");
}

bool PdfGeneratorBase::supportsOption(Okular::SaveInterface::SaveOption option) const
{
    PdfSaver *const pdfSaver = d->engine ? d->engine->saver() : nullptr;
    return pdfSaver && pdfSaver->supportsOption(option);
}

bool PdfGeneratorBase::save(const QString &fileName, Okular::SaveInterface::SaveOptions options, QString *errorText)
{
    clearError(errorText);
    if (PdfSaver *const pdfSaver = d->engine ? d->engine->saver() : nullptr) {
        return pdfSaver->save(fileName, options, errorText);
    }
    return unsupported(errorText, "Saving");
}

Okular::AnnotationProxy *PdfGeneratorBase::annotationProxy() const
{
    // The proxy belongs to the engine, not to the open page list; the legacy
    // generators expose it before and after a load alike. Return it whenever a
    // document capability exists, and let an engine without one return null.
    if (PdfDocument *const pdfDocument = d->engine ? d->engine->document() : nullptr) {
        return pdfDocument->annotationProxy();
    }
    return nullptr;
}

// Page-sequence editing through PdfPageEditor. Okular hands us logical page
// indices and expects a Page model back; the capability works on native page
// indices. The mapping helpers translate both ways, and every successful edit
// updates the mapping before returning. The capability hands back the
// Okular::Page models the backend implementation creates; they are passed to
// the caller unchanged, which becomes their owner. When an engine has no
// PdfPageEditor every operation fails with an "unsupported" error; there is no
// second implementation to fall back to.
bool PdfGeneratorBase::canInsertBlankPage() const
{
    PdfPageEditor *const pageEditor = d->pageEditor();
    return pageEditor && pageEditor->canInsertBlankPage();
}

bool PdfGeneratorBase::insertBlankPageInDocument(int insertAfterPageNumber,
                                                 double width,
                                                 double height,
                                                 Okular::Page **insertedPage,
                                                 quint64 *editId,
                                                 QString *errorText,
                                                 const QString &heading)
{
    clearError(errorText);
    PdfPageEditor *const pageEditor = d->pageEditor();
    if (!pageEditor) {
        return unsupported(errorText, "Inserting a blank page");
    }
    if (!insertedPage) {
        return false;
    }
    int nativeAfter = -1;
    if (!resolveInsertAfterPage(insertAfterPageNumber, &nativeAfter, errorText)) {
        return false;
    }
    int insertedNativePage = -1;
    Okular::Page *page = nullptr;
    if (!pageEditor->insertBlankPage(nativeAfter, QSizeF(width, height), heading, &page, &insertedNativePage, editId, errorText)) {
        return false;
    }
    if (!page || insertedNativePage < 0) {
        if (errorText) {
            *errorText = QStringLiteral("The PDF engine did not report the inserted page.");
        }
        return false;
    }
    d->insertPageMapping(page, insertedNativePage, insertAfterPageNumber + 1);
    *insertedPage = page;
    return true;
}

bool PdfGeneratorBase::canInsertPageFromPdf() const
{
    PdfPageEditor *const pageEditor = d->pageEditor();
    return pageEditor && pageEditor->canInsertPageFromPdf();
}

bool PdfGeneratorBase::duplicatePageInDocument(int pageNumber, bool resolveDestinationConflicts, Okular::Page **insertedPage, quint64 *editId, QString *errorText)
{
    clearError(errorText);
    PdfPageEditor *const pageEditor = d->pageEditor();
    if (!pageEditor) {
        return unsupported(errorText, "Duplicating a page");
    }
    if (!insertedPage) {
        return false;
    }
    const int nativePage = nativePageForLogicalPage(pageNumber);
    if (nativePage < 0) {
        if (errorText) {
            *errorText = QStringLiteral("The page to duplicate is not part of the open document.");
        }
        return false;
    }
    int insertedNativePage = -1;
    Okular::Page *page = nullptr;
    if (!pageEditor->duplicatePage(nativePage, resolveDestinationConflicts, &page, &insertedNativePage, editId, errorText)) {
        return false;
    }
    if (!page || insertedNativePage < 0) {
        if (errorText) {
            *errorText = QStringLiteral("The PDF engine did not report the duplicated page.");
        }
        return false;
    }
    d->insertPageMapping(page, insertedNativePage, pageNumber + 1);
    *insertedPage = page;
    return true;
}

bool PdfGeneratorBase::insertPdfPageInDocument(int insertAfterPageNumber,
                                               const QString &insertedFileName,
                                               int pageToInsert,
                                               bool resolveDestinationConflicts,
                                               Okular::Page **insertedPage,
                                               quint64 *editId,
                                               QString *errorText)
{
    clearError(errorText);
    PdfPageEditor *const pageEditor = d->pageEditor();
    if (!pageEditor) {
        return unsupported(errorText, "Inserting a page from a PDF");
    }
    if (!insertedPage) {
        return false;
    }
    int nativeAfter = -1;
    if (!resolveInsertAfterPage(insertAfterPageNumber, &nativeAfter, errorText)) {
        return false;
    }
    // pageToInsert stays one-based in both contracts; only the insertion
    // position of this document is a native page index.
    int insertedNativePage = -1;
    Okular::Page *page = nullptr;
    if (!pageEditor->insertPdfPage(nativeAfter,
                                   insertedFileName,
                                   pageToInsert,
                                   resolveDestinationConflicts,
                                   &page,
                                   &insertedNativePage,
                                   editId,
                                   errorText)) {
        return false;
    }
    if (!page || insertedNativePage < 0) {
        if (errorText) {
            *errorText = QStringLiteral("The PDF engine did not report the inserted page.");
        }
        return false;
    }
    d->insertPageMapping(page, insertedNativePage, insertAfterPageNumber + 1);
    *insertedPage = page;
    return true;
}

bool PdfGeneratorBase::canAppendPdfFile() const
{
    PdfPageEditor *const pageEditor = d->pageEditor();
    return pageEditor && pageEditor->canAppendPdfFile();
}

bool PdfGeneratorBase::appendPdfFileInDocument(const QString &sourceFileName,
                                               bool resolveDestinationConflicts,
                                               QList<Okular::Page *> *appendedPages,
                                               QList<quint64> *editIds,
                                               QString *errorText)
{
    clearError(errorText);
    PdfPageEditor *const pageEditor = d->pageEditor();
    if (!pageEditor) {
        return unsupported(errorText, "Appending a PDF file");
    }
    PdfDocument *const pdfDocument = d->activeDocument();
    if (!pdfDocument || !appendedPages) {
        return false;
    }
    // Appended native pages continue at the end of the current native
    // document, so their indices follow the current native page count. Record
    // that count before the engine appends; the returned models are in source
    // order.
    const int firstAppendedNativePage = pdfDocument->pageCount();
    if (firstAppendedNativePage < 0) {
        return false;
    }
    if (!pageEditor->appendPdfFile(sourceFileName, resolveDestinationConflicts, appendedPages, editIds, errorText)) {
        return false;
    }
    const int appendedCount = appendedPages->size();
    for (int i = 0; i < appendedCount; ++i) {
        Okular::Page *const page = appendedPages->at(i);
        if (!page) {
            continue;
        }
        d->nativePageByModel.insert(page, firstAppendedNativePage + i);
        if (!d->logicalPageOrder.contains(page)) {
            d->logicalPageOrder.append(page);
        }
    }
    return true;
}

bool PdfGeneratorBase::canCombinePdfFiles() const
{
    PdfPageEditor *const pageEditor = d->pageEditor();
    return pageEditor && pageEditor->canCombinePdfFiles();
}

int PdfGeneratorBase::pdfPageCount(const QString &inputFileName, QString *errorText)
{
    clearError(errorText);
    PdfPageEditor *const pageEditor = d->pageEditor();
    if (!pageEditor) {
        unsupported(errorText, "Reading a PDF page count");
        return -1;
    }
    return pageEditor->pdfPageCount(inputFileName, errorText);
}

bool PdfGeneratorBase::combinePdfFiles(const QStringList &inputFileNames, const QString &outputFileName, bool resolveDestinationConflicts, QString *errorText)
{
    clearError(errorText);
    PdfPageEditor *const pageEditor = d->pageEditor();
    if (!pageEditor) {
        return unsupported(errorText, "Combining PDF files");
    }
    return pageEditor->combinePdfFiles(inputFileNames, outputFileName, resolveDestinationConflicts, errorText);
}

bool PdfGeneratorBase::canDeletePage() const
{
    PdfPageEditor *const pageEditor = d->pageEditor();
    return pageEditor && pageEditor->canDeletePage();
}

bool PdfGeneratorBase::detachPageInDocument(Okular::Page *page, int pageNumber, quint64 *editId, QString *errorText)
{
    clearError(errorText);
    PdfPageEditor *const pageEditor = d->pageEditor();
    if (!pageEditor) {
        return unsupported(errorText, "Deleting a page");
    }
    int nativePage = page ? nativePageForPage(page) : nativePageForLogicalPage(pageNumber);
    if (nativePage < 0) {
        if (errorText) {
            *errorText = QStringLiteral("The page to delete is not part of the open document.");
        }
        return false;
    }
    // The caller keeps ownership of the input page (Document deletes it after
    // the page topology changed), so the detached model reported by the
    // capability is informational here.
    Okular::Page *detachedPage = nullptr;
    if (!pageEditor->detachPage(page, nativePage, &detachedPage, editId, errorText)) {
        return false;
    }
    d->removePageMapping(page ? page : detachedPage, nativePage, pageNumber);
    if (detachedPage && detachedPage != page) {
        d->nativePageByModel.remove(detachedPage);
    }
    return true;
}

bool PdfGeneratorBase::detachPageInDocument(Okular::Page *page, int pageNumber, quint64 editId, QString *errorText)
{
    clearError(errorText);
    PdfPageEditor *const pageEditor = d->pageEditor();
    if (!pageEditor) {
        return unsupported(errorText, "Deleting a page");
    }
    int nativePage = page ? nativePageForPage(page) : nativePageForLogicalPage(pageNumber);
    if (nativePage < 0) {
        if (errorText) {
            *errorText = QStringLiteral("The page to delete is not part of the open document.");
        }
        return false;
    }
    Okular::Page *detachedPage = nullptr;
    if (!pageEditor->detachPage(page, nativePage, &detachedPage, editId, errorText)) {
        return false;
    }
    d->removePageMapping(page ? page : detachedPage, nativePage, pageNumber);
    if (detachedPage && detachedPage != page) {
        d->nativePageByModel.remove(detachedPage);
    }
    return true;
}

bool PdfGeneratorBase::attachPageInDocument(int insertAfterPageNumber, quint64 editId, Okular::Page **insertedPage, QString *errorText)
{
    clearError(errorText);
    PdfPageEditor *const pageEditor = d->pageEditor();
    if (!pageEditor) {
        return unsupported(errorText, "Restoring a page");
    }
    if (!insertedPage) {
        return false;
    }
    int nativeAfter = -1;
    if (!resolveInsertAfterPage(insertAfterPageNumber, &nativeAfter, errorText)) {
        return false;
    }
    int insertedNativePage = -1;
    Okular::Page *page = nullptr;
    if (!pageEditor->attachPage(nativeAfter, editId, &page, &insertedNativePage, errorText)) {
        return false;
    }
    if (!page || insertedNativePage < 0) {
        if (errorText) {
            *errorText = QStringLiteral("The PDF engine did not report the restored page.");
        }
        return false;
    }
    d->insertPageMapping(page, insertedNativePage, insertAfterPageNumber + 1);
    *insertedPage = page;
    return true;
}

bool PdfGeneratorBase::canMovePage() const
{
    PdfPageEditor *const pageEditor = d->pageEditor();
    return pageEditor && pageEditor->canMovePage();
}

bool PdfGeneratorBase::canMovePageInDocument() const
{
    PdfPageEditor *const pageEditor = d->pageEditor();
    return pageEditor && pageEditor->canMovePageInDocument();
}

bool PdfGeneratorBase::movePageInDocument(int sourcePageNumber, int destinationPageNumber, QString *errorText)
{
    clearError(errorText);
    PdfPageEditor *const pageEditor = d->pageEditor();
    if (!pageEditor) {
        return unsupported(errorText, "Moving a page");
    }
    const int sourceNative = nativePageForLogicalPage(sourcePageNumber);
    const int destinationNative = nativePageForLogicalPage(destinationPageNumber);
    if (sourceNative < 0 || destinationNative < 0) {
        if (errorText) {
            *errorText = QStringLiteral("The page to move is not part of the open document.");
        }
        return false;
    }
    if (!pageEditor->movePage(sourceNative, destinationNative, errorText)) {
        return false;
    }
    d->movePageMapping(sourceNative, destinationNative);
    return true;
}

bool PdfGeneratorBase::canRotatePage() const
{
    PdfPageEditor *const pageEditor = d->pageEditor();
    return pageEditor && pageEditor->canRotatePage();
}

bool PdfGeneratorBase::rotatePageWithAnnotationsInDocument(Okular::Page *page, int pageNumber, int rotationDegrees, bool followPage, Okular::Page **replacementPage, QString *errorText)
{
    clearError(errorText);
    PdfPageEditor *const pageEditor = d->pageEditor();
    if (!pageEditor) {
        return unsupported(errorText, "Rotating a page");
    }
    if (!replacementPage) {
        return false;
    }
    int nativePage = page ? nativePageForPage(page) : nativePageForLogicalPage(pageNumber);
    if (nativePage < 0) {
        if (errorText) {
            *errorText = QStringLiteral("The page to rotate is not part of the open document.");
        }
        return false;
    }
    Okular::Page *replacement = nullptr;
    if (!pageEditor->rotatePageWithAnnotations(page, nativePage, rotationDegrees, followPage, &replacement, errorText)) {
        return false;
    }
    if (!replacement) {
        if (errorText) {
            *errorText = QStringLiteral("The PDF engine did not report the rotated page.");
        }
        return false;
    }
    // The replacement model takes over the retired model's native page.
    d->nativePageByModel.insert(replacement, nativePage);
    if (replacement != page) {
        d->nativePageByModel.remove(page);
    }
    if (pageNumber >= 0 && pageNumber < d->logicalPageOrder.size()) {
        d->logicalPageOrder[pageNumber] = replacement;
    }
    if (replacementPage) {
        *replacementPage = replacement;
    }
    return true;
}

bool PdfGeneratorBase::rotatePageInDocument(Okular::Page *page, int pageNumber, int rotationDegrees, Okular::Page **replacementPage, QString *errorText)
{
    clearError(errorText);
    PdfPageEditor *const pageEditor = d->pageEditor();
    if (!pageEditor) {
        return unsupported(errorText, "Rotating a page");
    }
    if (!replacementPage) {
        return false;
    }
    int nativePage = page ? nativePageForPage(page) : nativePageForLogicalPage(pageNumber);
    if (nativePage < 0) {
        if (errorText) {
            *errorText = QStringLiteral("The page to rotate is not part of the open document.");
        }
        return false;
    }
    Okular::Page *replacement = nullptr;
    if (!pageEditor->rotatePage(page, nativePage, rotationDegrees, &replacement, errorText)) {
        return false;
    }
    if (!replacement) {
        if (errorText) {
            *errorText = QStringLiteral("The PDF engine did not report the rotated page.");
        }
        return false;
    }
    d->nativePageByModel.insert(replacement, nativePage);
    if (replacement != page) {
        d->nativePageByModel.remove(page);
    }
    if (pageNumber >= 0 && pageNumber < d->logicalPageOrder.size()) {
        d->logicalPageOrder[pageNumber] = replacement;
    }
    if (replacementPage) {
        *replacementPage = replacement;
    }
    return true;
}

bool PdfGeneratorBase::canSetPageSize() const
{
    PdfPageEditor *const pageEditor = d->pageEditor();
    return pageEditor && pageEditor->canSetPageSize();
}

bool PdfGeneratorBase::adjustPageBoxInDocument(Okular::Page *page,
                                               int pageNumber,
                                               double x1Delta,
                                               double y1Delta,
                                               double x2Delta,
                                               double y2Delta,
                                               Okular::Page **replacementPage,
                                               QString *errorText)
{
    clearError(errorText);
    PdfPageEditor *const pageEditor = d->pageEditor();
    if (!pageEditor) {
        return unsupported(errorText, "Changing the page size");
    }
    if (!replacementPage) {
        return false;
    }
    int nativePage = page ? nativePageForPage(page) : nativePageForLogicalPage(pageNumber);
    if (nativePage < 0) {
        if (errorText) {
            *errorText = QStringLiteral("The page to resize is not part of the open document.");
        }
        return false;
    }
    Okular::Page *replacement = nullptr;
    if (!pageEditor->adjustPageBox(page, nativePage, x1Delta, y1Delta, x2Delta, y2Delta, &replacement, errorText)) {
        return false;
    }
    if (!replacement) {
        if (errorText) {
            *errorText = QStringLiteral("The PDF engine did not report the resized page.");
        }
        return false;
    }
    d->nativePageByModel.insert(replacement, nativePage);
    if (replacement != page) {
        d->nativePageByModel.remove(page);
    }
    if (pageNumber >= 0 && pageNumber < d->logicalPageOrder.size()) {
        d->logicalPageOrder[pageNumber] = replacement;
    }
    if (replacementPage) {
        *replacementPage = replacement;
    }
    return true;
}

// Link editing through PdfLinkEditor.
//
// Okular's public link API speaks one-based logical page numbers
// (setNamedDestination, the internal link destination) and hands over page
// models; PdfLinkEditor speaks zero-based native indices. The page model is
// resolved through the mapping (never through Page::number()) and the one-based
// values are converted to logical first.
bool PdfGeneratorBase::canEditPdfLinks() const
{
    PdfLinkEditor *const linkEditor = d->linkEditor();
    return linkEditor && linkEditor->canEditLinks();
}

bool PdfGeneratorBase::setNamedDestination(const QString &name, int pageNumber, double normalizedX, double normalizedY, QString *errorText)
{
    clearError(errorText);
    PdfLinkEditor *const linkEditor = d->linkEditor();
    if (!linkEditor) {
        return unsupported(errorText, "Editing PDF links");
    }
    const int nativePage = nativePageForOneBasedPage(pageNumber);
    if (nativePage < 0) {
        if (errorText) {
            *errorText = QStringLiteral("The destination page is not part of the open document.");
        }
        return false;
    }
    return linkEditor->setNamedDestination(name, nativePage, normalizedX, normalizedY, errorText);
}

bool PdfGeneratorBase::renameNamedDestination(const QString &oldName, const QString &newName, QString *errorText)
{
    clearError(errorText);
    if (d->linkEditor()) {
        return d->linkEditor()->renameNamedDestination(oldName, newName, errorText);
    }
    return unsupported(errorText, "Editing PDF links");
}

bool PdfGeneratorBase::deleteNamedDestination(const QString &name, QString *errorText)
{
    clearError(errorText);
    if (d->linkEditor()) {
        return d->linkEditor()->deleteNamedDestination(name, errorText);
    }
    return unsupported(errorText, "Editing PDF links");
}

bool PdfGeneratorBase::editInternalLinkDestination(Okular::Page *sourcePage,
                                                   double linkLeft,
                                                   double linkTop,
                                                   double linkRight,
                                                   double linkBottom,
                                                   const QString &destinationName,
                                                   int destinationPageNumber,
                                                   double destinationX,
                                                   double destinationY,
                                                   QString *errorText)
{
    clearError(errorText);
    if (d->linkEditor()) {
        // A destination name overrides the page number, so the one-based to
        // native conversion only matters for the numeric form.
        const int nativePage = sourcePage ? nativePageForPage(sourcePage) : -1;
        return d->linkEditor()->editInternalLinkDestination(nativePage,
                                                          Okular::NormalizedRect(linkLeft, linkTop, linkRight, linkBottom),
                                                          destinationName,
                                                          nativePageForOneBasedPage(destinationPageNumber),
                                                          destinationX,
                                                          destinationY,
                                                          errorText);
    }
    return unsupported(errorText, "Editing PDF links");
}

bool PdfGeneratorBase::createInternalLink(Okular::Page *sourcePage,
                                          double linkLeft,
                                          double linkTop,
                                          double linkRight,
                                          double linkBottom,
                                          const QString &destinationName,
                                          int destinationPageNumber,
                                          double destinationX,
                                          double destinationY,
                                          QString *errorText,
                                          Okular::PdfLinkAppearance appearance)
{
    clearError(errorText);
    if (d->linkEditor()) {
        const int nativePage = sourcePage ? nativePageForPage(sourcePage) : -1;
        return d->linkEditor()->createInternalLink(nativePage,
                                                 Okular::NormalizedRect(linkLeft, linkTop, linkRight, linkBottom),
                                                 destinationName,
                                                 nativePageForOneBasedPage(destinationPageNumber),
                                                 destinationX,
                                                 destinationY,
                                                 appearance,
                                                 errorText);
    }
    return unsupported(errorText, "Editing PDF links");
}

bool PdfGeneratorBase::editExternalLinkDestination(Okular::Page *sourcePage, double linkLeft, double linkTop, double linkRight, double linkBottom, const QString &url, QString *errorText)
{
    clearError(errorText);
    if (d->linkEditor()) {
        return d->linkEditor()->editExternalLinkDestination(sourcePage ? nativePageForPage(sourcePage) : -1,
                                                          Okular::NormalizedRect(linkLeft, linkTop, linkRight, linkBottom),
                                                          url,
                                                          errorText);
    }
    return unsupported(errorText, "Editing PDF links");
}

bool PdfGeneratorBase::createExternalLink(Okular::Page *sourcePage, double linkLeft, double linkTop, double linkRight, double linkBottom, const QString &url, QString *errorText)
{
    clearError(errorText);
    if (d->linkEditor()) {
        return d->linkEditor()->createExternalLink(sourcePage ? nativePageForPage(sourcePage) : -1,
                                                 Okular::NormalizedRect(linkLeft, linkTop, linkRight, linkBottom),
                                                 url,
                                                 errorText);
    }
    return unsupported(errorText, "Editing PDF links");
}

bool PdfGeneratorBase::editRemoteLinkDestination(Okular::Page *sourcePage,
                                                 double linkLeft,
                                                 double linkTop,
                                                 double linkRight,
                                                 double linkBottom,
                                                 const QString &fileName,
                                                 int destinationPageNumber,
                                                 double destinationX,
                                                 double destinationY,
                                                 QString *errorText,
                                                 const QString &remoteDestinationName)
{
    clearError(errorText);
    if (d->linkEditor()) {
        // destinationPageNumber belongs to the other file, so it is not a
        // native index of this document: pass the one-based number through
        // unchanged, exactly like the legacy interface does.
        return d->linkEditor()->editRemoteLinkDestination(sourcePage ? nativePageForPage(sourcePage) : -1,
                                                        Okular::NormalizedRect(linkLeft, linkTop, linkRight, linkBottom),
                                                        fileName,
                                                        destinationPageNumber,
                                                        destinationX,
                                                        destinationY,
                                                        remoteDestinationName,
                                                        errorText);
    }
    return unsupported(errorText, "Editing PDF links");
}

bool PdfGeneratorBase::createRemoteLink(Okular::Page *sourcePage,
                                        double linkLeft,
                                        double linkTop,
                                        double linkRight,
                                        double linkBottom,
                                        const QString &fileName,
                                        int destinationPageNumber,
                                        double destinationX,
                                        double destinationY,
                                        QString *errorText,
                                        const QString &remoteDestinationName)
{
    clearError(errorText);
    if (d->linkEditor()) {
        return d->linkEditor()->createRemoteLink(sourcePage ? nativePageForPage(sourcePage) : -1,
                                               Okular::NormalizedRect(linkLeft, linkTop, linkRight, linkBottom),
                                               fileName,
                                               destinationPageNumber,
                                               destinationX,
                                               destinationY,
                                               remoteDestinationName,
                                               errorText);
    }
    return unsupported(errorText, "Editing PDF links");
}

bool PdfGeneratorBase::editPdfLinkRectangle(Okular::Page *sourcePage,
                                            double oldLinkLeft,
                                            double oldLinkTop,
                                            double oldLinkRight,
                                            double oldLinkBottom,
                                            double newLinkLeft,
                                            double newLinkTop,
                                            double newLinkRight,
                                            double newLinkBottom,
                                            QString *errorText,
                                            Okular::PdfLinkAppearance appearance)
{
    clearError(errorText);
    if (d->linkEditor()) {
        return d->linkEditor()->editLinkRectangle(sourcePage ? nativePageForPage(sourcePage) : -1,
                                                Okular::NormalizedRect(oldLinkLeft, oldLinkTop, oldLinkRight, oldLinkBottom),
                                                Okular::NormalizedRect(newLinkLeft, newLinkTop, newLinkRight, newLinkBottom),
                                                appearance,
                                                errorText);
    }
    return unsupported(errorText, "Editing PDF links");
}

bool PdfGeneratorBase::deletePdfLink(Okular::Page *sourcePage, double linkLeft, double linkTop, double linkRight, double linkBottom, QString *errorText)
{
    clearError(errorText);
    if (d->linkEditor()) {
        return d->linkEditor()->deleteLink(sourcePage ? nativePageForPage(sourcePage) : -1,
                                         Okular::NormalizedRect(linkLeft, linkTop, linkRight, linkBottom),
                                         errorText);
    }
    return unsupported(errorText, "Editing PDF links");
}

bool PdfGeneratorBase::refreshPageLinks(Okular::Page *page, int pageNumber, QString *errorText)
{
    clearError(errorText);
    if (d->linkEditor()) {
        int nativePage = page ? nativePageForPage(page) : -1;
        if (nativePage < 0) {
            nativePage = nativePageForLogicalPage(pageNumber);
        }
        if (nativePage < 0) {
            if (errorText) {
                *errorText = QStringLiteral("The page whose links should be refreshed is not part of the open document.");
            }
            return false;
        }
        return d->linkEditor()->refreshPageLinks(nativePage, errorText);
    }
    return unsupported(errorText, "Editing PDF links");
}

// OCR, Reading Views and Numbered Callout metadata through PdfOcr and
// PdfCustomMetadata.
//
// Okular page numbers here are zero-based logical indices, except for the
// saveWithEnglishOcr page list, which is one-based. PdfOcr/PdfCustomMetadata
// speak zero-based native indices, so incoming page numbers are translated to
// native and an identity returned by a capability (page token, reading-view
// token) is translated back to the current logical page number.
bool PdfGeneratorBase::canPerformEnglishOcr() const
{
    PdfOcr *const pdfOcr = d->ocr();
    return pdfOcr && pdfOcr->canRecognize();
}

bool PdfGeneratorBase::readOcrTextLayer(int pageNumber, QList<Okular::OcrTextWord> *words, QString *errorText)
{
    clearError(errorText);
    PdfOcr *const pdfOcr = d->ocr();
    if (!pdfOcr) {
        return unsupported(errorText, "Reading the OCR text layer");
    }
    const int nativePage = nativePageForLogicalPage(pageNumber);
    if (nativePage < 0) {
        if (errorText) {
            *errorText = QStringLiteral("The page is not part of the open document.");
        }
        return false;
    }
    return pdfOcr->readTextLayer(nativePage, words, errorText);
}

bool PdfGeneratorBase::replaceOcrTextLayer(int pageNumber, const QList<Okular::OcrTextWord> &words, QString *errorText)
{
    clearError(errorText);
    PdfOcr *const pdfOcr = d->ocr();
    if (!pdfOcr) {
        return unsupported(errorText, "Writing the OCR text layer");
    }
    const int nativePage = nativePageForLogicalPage(pageNumber);
    if (nativePage < 0) {
        if (errorText) {
            *errorText = QStringLiteral("The page is not part of the open document.");
        }
        return false;
    }
    return pdfOcr->replaceTextLayer(nativePage, words, errorText);
}

Okular::OcrResult PdfGeneratorBase::saveWithEnglishOcr(const QString &sourceFileName,
                                                       const QString &outputFileName,
                                                       const QList<int> &pageNumbers,
                                                       bool skipPagesWithText,
                                                       const Okular::PdfOcrInterface::ProgressCallback &progress)
{
    PdfOcr *const pdfOcr = d->ocr();
    if (!pdfOcr) {
        Okular::OcrResult result;
        result.errorText = QStringLiteral("OCR is not supported by the active PDF engine.");
        return result;
    }

    QList<int> nativePages;
    nativePages.reserve(pageNumbers.size());
    for (int pageNumber : pageNumbers) {
        nativePages.append(nativePageForOneBasedPage(pageNumber));
    }
    return pdfOcr->saveWithOcr(sourceFileName, outputFileName, nativePages, skipPagesWithText, progress);
}

bool PdfGeneratorBase::canEditReadingViews() const
{
    PdfCustomMetadata *const metadata = d->customMetadata();
    return metadata && metadata->canEditReadingViews();
}

bool PdfGeneratorBase::canGenerateReadingViews() const
{
    PdfCustomMetadata *const metadata = d->customMetadata();
    return metadata && metadata->canGenerateReadingViews();
}

Okular::ReadingViewGenerationResult PdfGeneratorBase::generateReadingViews(const QString &sourceFileName,
                                                                          const QList<int> &pageNumbers,
                                                                          const Okular::ReadingViewEditingInterface::ProgressCallback &progress)
{
    PdfCustomMetadata *const metadata = d->customMetadata();
    if (!metadata) {
        Okular::ReadingViewGenerationResult result;
        result.errorText = QStringLiteral("Reading View generation is not supported by the active PDF engine.");
        return result;
    }

    QList<int> nativePages;
    nativePages.reserve(pageNumbers.size());
    for (int page : pageNumbers) {
        nativePages.append(nativePageForLogicalPage(page));
    }
    return metadata->generateReadingViews(sourceFileName, nativePages, progress);
}

QList<Okular::ReadingView> PdfGeneratorBase::readingViews(int pageNumber, QString *errorText) const
{
    clearError(errorText);
    PdfCustomMetadata *const metadata = d->customMetadata();
    if (!metadata) {
        return QList<Okular::ReadingView>();
    }
    const int nativePage = nativePageForLogicalPage(pageNumber);
    return nativePage < 0 ? QList<Okular::ReadingView>() : metadata->readingViews(nativePage, errorText);
}

bool PdfGeneratorBase::setReadingViews(int pageNumber, const QList<Okular::ReadingView> &views, QString *errorText)
{
    clearError(errorText);
    PdfCustomMetadata *const metadata = d->customMetadata();
    if (!metadata) {
        return unsupported(errorText, "Editing Reading Views");
    }
    const int nativePage = nativePageForLogicalPage(pageNumber);
    if (nativePage < 0) {
        if (errorText) {
            *errorText = QStringLiteral("The page is not part of the open document.");
        }
        return false;
    }
    return metadata->setReadingViews(nativePage, views, errorText);
}

QString PdfGeneratorBase::readingViewPageToken(int pageNumber) const
{
    PdfCustomMetadata *const metadata = d->customMetadata();
    if (!metadata) {
        return QString();
    }
    const int nativePage = nativePageForLogicalPage(pageNumber);
    return nativePage < 0 ? QString() : metadata->pageToken(nativePage);
}

int PdfGeneratorBase::readingViewPageForToken(const QString &token) const
{
    // pageForToken() returns a native index; the caller expects the current
    // logical page number. -1 means the page is unknown or no longer present.
    PdfCustomMetadata *const metadata = d->customMetadata();
    if (!metadata) {
        return -1;
    }
    const int nativePage = metadata->pageForToken(token);
    return nativePage < 0 ? -1 : logicalPageForNative(nativePage);
}

bool PdfGeneratorBase::canEditNumberedCalloutNumbering() const
{
    PdfCustomMetadata *const metadata = d->customMetadata();
    return metadata && metadata->canEditNumberedCalloutNumbering();
}

bool PdfGeneratorBase::validateNumberedCalloutLabel(const QString &label, QString *errorText) const
{
    clearError(errorText);
    PdfCustomMetadata *const metadata = d->customMetadata();
    if (!metadata) {
        return unsupported(errorText, "Numbered Callout numbering");
    }
    return metadata->validateNumberedCalloutLabel(label, errorText);
}

QString PdfGeneratorBase::numberedCalloutNumberingJson() const
{
    PdfCustomMetadata *const metadata = d->customMetadata();
    return metadata ? metadata->numberedCalloutNumberingJson() : QString();
}

bool PdfGeneratorBase::setNumberedCalloutNumberingJson(const QString &json, QString *errorText)
{
    clearError(errorText);
    PdfCustomMetadata *const metadata = d->customMetadata();
    if (!metadata) {
        return unsupported(errorText, "Numbered Callout numbering");
    }
    return metadata->setNumberedCalloutNumberingJson(json, errorText);
}

bool PdfGeneratorBase::doCloseDocument()
{
    d->narrowDocumentActive = false;
    d->clearPageMapping();
    // Pages created through createPageModel() belong to Okular::Document,
    // which deletes them after this method returns. close() only releases the
    // engine's native state, never the base-owned page list.
    if (PdfDocument *const pdfDocument = d->engine ? d->engine->document() : nullptr) {
        pdfDocument->close();
    }
    if (d->engine) {
        d->engine->setWrapperDocument(nullptr);
    }
    return true;
}

Okular::TextPage *PdfGeneratorBase::textPage(Okular::TextRequest *request)
{
    if (PdfDocument *pdfDocument = d->activeDocument()) {
        if (!request || !request->page()) {
            return nullptr;
        }
        // The request carries the page model, so resolve the native page from
        // the model instead of trusting Page::number().
        const int nativePage = nativePageForPage(request->page());
        if (nativePage < 0) {
            return nullptr;
        }
        return pdfDocument->textPage(nativePage);
    }
    return nullptr;
}

Okular::Generator::SwapBackingFileResult PdfGeneratorBase::swapBackingFile(const QString &newFileName, QList<Okular::Page *> &newPagesVector)
{
    if (PdfSaver *const pdfSaver = d->saver()) {
        const Okular::Generator::SwapBackingFileResult result = pdfSaver->swapBackingFile(newFileName, newPagesVector);
        if (result == Okular::Generator::SwapBackingFileReloadInternalData) {
            if (document()) {
                // Document decides whether the old Page* identity survives (it
                // adopts the new page data when the topology did not change) or
                // the replacement models are taken over. Rebuild lazily from
                // its vector so the mapping always matches the models it keeps.
                d->pageMappingNeedsDocumentResync = true;
            } else if (!newPagesVector.isEmpty()) {
                // No Document owns the list here; the caller keeps the models
                // exactly as returned, in native order.
                d->rebuildPageMapping(newPagesVector);
            }
        }
        return result;
    }
    return Okular::Generator::SwapBackingFileError;
}

int PdfGeneratorBase::pageNumberForPageToken(const QString &token) const
{
    if (token.isEmpty()) {
        return -1;
    }
    // A link destination carries the engine's page identity in
    // GotoAction::pageToken(). The engine resolves it to a native page; the
    // Document caller needs the current logical page number.
    if (PdfPageIdentity *identity = d->engine ? d->engine->pageIdentity() : nullptr) {
        const int nativePage = identity->nativePageForToken(token);
        if (nativePage >= 0) {
            return logicalPageForNative(nativePage);
        }
    }
    // Fallback for engines without the page-identity capability (and for
    // Reading View tokens, which are a separate namespace): the
    // custom-metadata capability resolves its own identity and also speaks
    // native indices.
    if (PdfCustomMetadata *const metadata = d->customMetadata()) {
        const int nativePage = metadata->pageForToken(token);
        return nativePage < 0 ? -1 : logicalPageForNative(nativePage);
    }
    return -1;
}

} // namespace Pdf
} // namespace Mengshee
