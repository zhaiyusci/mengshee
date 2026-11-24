// SPDX-License-Identifier: AGPL-3.0-or-later
// MuPDF engine module: exports the existing MuPDFGenerator through the dynamic
// module C ABI, and implements the narrow PdfDocument read path on top of the
// legacy MuPDFGenerator instead of duplicating the MuPDF plumbing.
//
// MuPDFGenerator is an implementation detail of the module: the front end
// reaches it only through PdfDocument and the capability objects, and through
// the neutral hasFeature()/setWrapperDocument() hooks below. No
// Okular::Generator pointer crosses the module boundary.

#include "generator_mupdf.h"

#include "pdfengine.h"
#include "pdfenginemodule.h"

#include <core/page.h>
#include <core/printoptionswidget.h>

#include <QImage>
#include <QSizeF>
#include <QtMath>

namespace
{

using namespace Mengshee::Pdf;

class MuPdfEngineModule final
    : public MuPDFGenerator
    , public PdfEngine
    , public PdfDocument
    , public PdfSaver
    , public PdfPrinter
    , public PdfSigner
    , public PdfOcr
    , public PdfCustomMetadata
    , public PdfPrintConfigurator
    , public PdfPrintPreviewProvider
    , public PdfBackendActions
{
public:
    MuPdfEngineModule()
        : MuPDFGenerator(nullptr, {})
    {
    }

    ~MuPdfEngineModule() override
    {
        // Okular::Page models are created by the narrow caller through
        // createPageModel() and owned by it. close() only releases the native
        // document and page cache.
        close();
    }

    // --- PdfEngine --------------------------------------------------------

    EngineId id() const override
    {
        return QString::fromLatin1(MuPdfEngineId);
    }

    bool hasFeature(Okular::Generator::GeneratorFeature feature) const override
    {
        // The legacy generator is the object that knows MuPDF's feature set;
        // the module only forwards the flags to the front end.
        return MuPDFGenerator::hasFeature(feature);
    }

    void setWrapperDocument(Okular::Document *wrapper) override
    {
        // The static link editors, printing and annotation appearance resolve
        // pages through the owning Document, exactly like a forwarding
        // Okular::Generator would.
        Okular::Generator::setWrapperDocument(wrapper);
    }

    PdfDocument *document() override
    {
        return this;
    }

    // --- PdfEngine write capabilities -------------------------------------
    //
    // The module is the only object that can reach the legacy generator's
    // protected/overridden write methods, so it implements the capability
    // interfaces itself instead of handing out a separate adapter.

    PdfSaver *saver() override
    {
        return this;
    }

    PdfPrinter *printer() override
    {
        return this;
    }

    PdfPrintConfigurator *printConfigurator() override
    {
        return this;
    }

    Okular::PrintOptionsWidget *printConfigurationWidget() const override
    {
        return MuPDFGenerator::printConfigurationWidget();
    }

    PdfPrintPreviewProvider *printPreviewProvider() override
    {
        return this;
    }

    PdfBackendActions *backendActions() override
    {
        return this;
    }

    QAbstractItemModel *layersModel() const override
    {
        return MuPDFGenerator::layersModel();
    }

    Okular::BackendOpaqueAction::OpaqueActionResult opaqueAction(const Okular::BackendOpaqueAction *action) override
    {
        return MuPDFGenerator::opaqueAction(action);
    }

    Okular::Action *additionalDocumentAction(Okular::Document::DocumentAdditionalActionType type) override
    {
        return MuPDFGenerator::additionalDocumentAction(type);
    }

    Okular::PrintPreviewRenderer createPrintPreviewRenderer(QString *error) override
    {
        return MuPDFGenerator::createPrintPreviewRenderer(error);
    }

    PdfSigner *signer() override
    {
        return this;
    }

    /**
     * Link editing is a separate capability object: Okular's
     * PdfLinkEditingInterface declares setNamedDestination() with the same
     * signature as PdfLinkEditor but with a one-based logical page number
     * instead of a zero-based native index. Keeping the capability in its own
     * class keeps both contracts exact instead of reinterpreting one override.
     */
    class LinkEditor final : public PdfLinkEditor
    {
    public:
        explicit LinkEditor(MuPdfEngineModule *owner)
            : m_owner(owner)
        {
        }

        bool canEditLinks() const override
        {
            return m_owner->canEditPdfLinks();
        }

        bool setNamedDestination(const QString &name, int nativePage, double x, double y, QString *errorText) override
        {
            if (!m_owner->isValidNativePage(nativePage)) {
                return setError(errorText, QStringLiteral("Invalid destination page number."));
            }
            // The legacy method takes a one-based logical number; native and
            // logical are the same index under the current identity order.
            return m_owner->setNamedDestination(name, nativePage + 1, x, y, errorText);
        }

        bool renameNamedDestination(const QString &oldName, const QString &newName, QString *errorText) override
        {
            return m_owner->renameNamedDestination(oldName, newName, errorText);
        }

        bool deleteNamedDestination(const QString &name, QString *errorText) override
        {
            return m_owner->deleteNamedDestination(name, errorText);
        }

        bool refreshPageLinks(int nativePage, QString *errorText) override
        {
            Okular::Page *page = m_owner->legacyLinkPage(nativePage, errorText);
            if (!page) {
                return false;
            }
            return m_owner->refreshPageLinks(page, nativePage, errorText);
        }

        bool editInternalLinkDestination(int nativePage,
                                         const Okular::NormalizedRect &area,
                                         const QString &destinationName,
                                         int destinationNativePage,
                                         double x,
                                         double y,
                                         QString *errorText) override
        {
            Okular::Page *page = m_owner->legacyLinkPage(nativePage, errorText);
            if (!page) {
                return false;
            }
            if (!checkDestination(destinationName, destinationNativePage, errorText)) {
                return false;
            }
            const int legacyPage = destinationName.isEmpty() ? destinationNativePage + 1 : 0;
            return m_owner->editInternalLinkDestination(page, area.left, area.top, area.right, area.bottom, destinationName, legacyPage, x, y, errorText);
        }

        bool createInternalLink(int nativePage,
                                const Okular::NormalizedRect &area,
                                const QString &destinationName,
                                int destinationNativePage,
                                double x,
                                double y,
                                Okular::PdfLinkAppearance appearance,
                                QString *errorText) override
        {
            Okular::Page *page = m_owner->legacyLinkPage(nativePage, errorText);
            if (!page) {
                return false;
            }
            if (!checkDestination(destinationName, destinationNativePage, errorText)) {
                return false;
            }
            const int legacyPage = destinationName.isEmpty() ? destinationNativePage + 1 : 0;
            return m_owner->createInternalLink(page, area.left, area.top, area.right, area.bottom, destinationName, legacyPage, x, y, errorText, appearance);
        }

        bool editExternalLinkDestination(int nativePage, const Okular::NormalizedRect &area, const QString &url, QString *errorText) override
        {
            Okular::Page *page = m_owner->legacyLinkPage(nativePage, errorText);
            return page && m_owner->editExternalLinkDestination(page, area.left, area.top, area.right, area.bottom, url, errorText);
        }

        bool createExternalLink(int nativePage, const Okular::NormalizedRect &area, const QString &url, QString *errorText) override
        {
            Okular::Page *page = m_owner->legacyLinkPage(nativePage, errorText);
            return page && m_owner->createExternalLink(page, area.left, area.top, area.right, area.bottom, url, errorText);
        }

        bool editRemoteLinkDestination(int nativePage,
                                       const Okular::NormalizedRect &area,
                                       const QString &fileName,
                                       int destinationFilePage,
                                       double x,
                                       double y,
                                       const QString &remoteDestinationName,
                                       QString *errorText) override
        {
            Okular::Page *page = m_owner->legacyLinkPage(nativePage, errorText);
            if (!page) {
                return false;
            }
            // destinationFilePage belongs to the other file and keeps the
            // one-based numbering the link format stores.
            return m_owner->editRemoteLinkDestination(page, area.left, area.top, area.right, area.bottom, fileName, destinationFilePage, x, y, errorText, remoteDestinationName);
        }

        bool createRemoteLink(int nativePage,
                              const Okular::NormalizedRect &area,
                              const QString &fileName,
                              int destinationFilePage,
                              double x,
                              double y,
                              const QString &remoteDestinationName,
                              QString *errorText) override
        {
            Okular::Page *page = m_owner->legacyLinkPage(nativePage, errorText);
            if (!page) {
                return false;
            }
            return m_owner->createRemoteLink(page, area.left, area.top, area.right, area.bottom, fileName, destinationFilePage, x, y, errorText, remoteDestinationName);
        }

        bool editLinkRectangle(int nativePage,
                               const Okular::NormalizedRect &oldArea,
                               const Okular::NormalizedRect &newArea,
                               Okular::PdfLinkAppearance appearance,
                               QString *errorText) override
        {
            Okular::Page *page = m_owner->legacyLinkPage(nativePage, errorText);
            if (!page) {
                return false;
            }
            return m_owner->editPdfLinkRectangle(page,
                                                oldArea.left,
                                                oldArea.top,
                                                oldArea.right,
                                                oldArea.bottom,
                                                newArea.left,
                                                newArea.top,
                                                newArea.right,
                                                newArea.bottom,
                                                errorText,
                                                appearance);
        }

        bool deleteLink(int nativePage, const Okular::NormalizedRect &area, QString *errorText) override
        {
            Okular::Page *page = m_owner->legacyLinkPage(nativePage, errorText);
            return page && m_owner->deletePdfLink(page, area.left, area.top, area.right, area.bottom, errorText);
        }

    private:
        static bool setError(QString *errorText, const QString &message)
        {
            if (errorText) {
                *errorText = message;
            }
            return false;
        }

        /** A numeric destination must be a native page; a name overrides it. */
        bool checkDestination(const QString &destinationName, int destinationNativePage, QString *errorText) const
        {
            if (destinationName.isEmpty() && !m_owner->isValidNativePage(destinationNativePage)) {
                return setError(errorText, QStringLiteral("Invalid destination page number."));
            }
            return true;
        }

        MuPdfEngineModule *m_owner;
    };

    PdfLinkEditor *linkEditor() override
    {
        return &m_linkEditor;
    }

    /**
     * Page-sequence editing on native page indices.
     *
     * The legacy MuPDF implementation already speaks native indices and owns
     * the native page cache; every method forwards to it unchanged. The only
     * additions are the native index of a newly inserted page and the
     * Okular::Page model the legacy method creates. Those models belong to the
     * caller (PdfGeneratorBase / Okular::Document): the adapter stores nothing
     * and never deletes a page that it handed out or was asked to detach.
     */
    class PageEditor final : public PdfPageEditor
    {
    public:
        explicit PageEditor(MuPdfEngineModule *owner)
            : m_owner(owner)
        {
        }

        bool canInsertBlankPage() const override
        {
            return m_owner->canInsertBlankPage();
        }

        bool insertBlankPage(int afterNative,
                             const QSizeF &sizePoints,
                             const QString &heading,
                             Okular::Page **insertedPage,
                             int *insertedNativePage,
                             quint64 *editId,
                             QString *errorText) override
        {
            if (!insertedPage || !editId) {
                clearError(errorText);
                return false;
            }
            const bool inserted = m_owner->insertBlankPageInDocument(afterNative, sizePoints.width(), sizePoints.height(), insertedPage, editId, errorText, heading);
            if (inserted && insertedNativePage) {
                *insertedNativePage = afterNative + 1;
            }
            return inserted;
        }

        bool canInsertPageFromPdf() const override
        {
            return m_owner->canInsertPageFromPdf();
        }

        bool duplicatePage(int nativePage,
                           bool resolveDestinationConflicts,
                           Okular::Page **insertedPage,
                           int *insertedNativePage,
                           quint64 *editId,
                           QString *errorText) override
        {
            if (!insertedPage || !editId) {
                clearError(errorText);
                return false;
            }
            const bool inserted = m_owner->duplicatePageInDocument(nativePage, resolveDestinationConflicts, insertedPage, editId, errorText);
            if (inserted && insertedNativePage) {
                *insertedNativePage = nativePage + 1;
            }
            return inserted;
        }

        bool insertPdfPage(int afterNative,
                           const QString &sourceFileName,
                           int sourcePage,
                           bool resolveDestinationConflicts,
                           Okular::Page **insertedPage,
                           int *insertedNativePage,
                           quint64 *editId,
                           QString *errorText) override
        {
            if (!insertedPage || !editId) {
                clearError(errorText);
                return false;
            }
            const bool inserted = m_owner->insertPdfPageInDocument(afterNative, sourceFileName, sourcePage, resolveDestinationConflicts, insertedPage, editId, errorText);
            if (inserted && insertedNativePage) {
                *insertedNativePage = afterNative + 1;
            }
            return inserted;
        }

        bool canAppendPdfFile() const override
        {
            return m_owner->canAppendPdfFile();
        }

        bool appendPdfFile(const QString &sourceFileName,
                           bool resolveDestinationConflicts,
                           QList<Okular::Page *> *appendedPages,
                           QList<quint64> *editIds,
                           QString *errorText) override
        {
            return m_owner->appendPdfFileInDocument(sourceFileName, resolveDestinationConflicts, appendedPages, editIds, errorText);
        }

        bool canCombinePdfFiles() const override
        {
            return m_owner->canCombinePdfFiles();
        }

        int pdfPageCount(const QString &fileName, QString *errorText) override
        {
            return m_owner->pdfPageCount(fileName, errorText);
        }

        bool combinePdfFiles(const QStringList &inputFileNames, const QString &outputFileName, bool resolveDestinationConflicts, QString *errorText) override
        {
            return m_owner->combinePdfFiles(inputFileNames, outputFileName, resolveDestinationConflicts, errorText);
        }

        bool canDeletePage() const override
        {
            return m_owner->canDeletePage();
        }

        bool detachPage(Okular::Page *page, int nativePage, Okular::Page **detachedPage, quint64 *editId, QString *errorText) override
        {
            if (!m_owner->detachPageInDocument(page, nativePage, editId, errorText)) {
                return false;
            }
            if (detachedPage) {
                // The caller already owns page; hand the same model back so the
                // capability contract is explicit about the retained ownership.
                *detachedPage = page;
            }
            return true;
        }

        bool detachPage(Okular::Page *page, int nativePage, Okular::Page **detachedPage, quint64 editId, QString *errorText) override
        {
            if (!m_owner->detachPageInDocument(page, nativePage, editId, errorText)) {
                return false;
            }
            if (detachedPage) {
                *detachedPage = page;
            }
            return true;
        }

        bool attachPage(int afterNative, quint64 editId, Okular::Page **insertedPage, int *insertedNativePage, QString *errorText) override
        {
            if (!m_owner->attachPageInDocument(afterNative, editId, insertedPage, errorText)) {
                return false;
            }
            if (insertedNativePage) {
                *insertedNativePage = afterNative + 1;
            }
            return true;
        }

        bool canMovePage() const override
        {
            return m_owner->canMovePage();
        }

        bool canMovePageInDocument() const override
        {
            return m_owner->canMovePageInDocument();
        }

        bool movePage(int fromNative, int toNative, QString *errorText) override
        {
            return m_owner->movePageInDocument(fromNative, toNative, errorText);
        }

        bool canRotatePage() const override
        {
            return m_owner->canRotatePage();
        }

        bool rotatePage(Okular::Page *page, int nativePage, int rotationDegrees, Okular::Page **replacementPage, QString *errorText) override
        {
            return m_owner->rotatePageInDocument(page, nativePage, rotationDegrees, replacementPage, errorText);
        }

        bool rotatePageWithAnnotations(Okular::Page *page, int nativePage, int rotationDegrees, bool annotationsFollowPage, Okular::Page **replacementPage, QString *errorText) override
        {
            return m_owner->rotatePageWithAnnotationsInDocument(page, nativePage, rotationDegrees, annotationsFollowPage, replacementPage, errorText);
        }

        bool canSetPageSize() const override
        {
            return m_owner->canSetPageSize();
        }

        bool adjustPageBox(Okular::Page *page, int nativePage, double x1Delta, double y1Delta, double x2Delta, double y2Delta, Okular::Page **replacementPage, QString *errorText) override
        {
            return m_owner->adjustPageBoxInDocument(page, nativePage, x1Delta, y1Delta, x2Delta, y2Delta, replacementPage, errorText);
        }

    private:
        MuPdfEngineModule *m_owner;
    };

    PdfPageEditor *pageEditor() override
    {
        return &m_pageEditor;
    }

    /**
     * Page identity of explicit link destinations. It is a separate object
     * because MuPDF keeps two token namespaces: the Reading View /PageId UUID
     * exposed by PdfCustomMetadata::pageToken() and the page-token resolver
     * Okular calls for GotoActions. They are the same value in the legacy
     * generator today, but the identity capability must keep forwarding to the
     * legacy resolver instead of duplicating its format here.
     */
    class PageIdentity final : public PdfPageIdentity
    {
    public:
        explicit PageIdentity(MuPdfEngineModule *owner)
            : m_owner(owner)
        {
        }

        QString pageToken(int nativePage) const override
        {
            if (!m_owner->isValidNativePage(nativePage)) {
                return {};
            }
            return m_owner->readingViewPageToken(nativePage);
        }

        int nativePageForToken(const QString &token) const override
        {
            if (token.isEmpty()) {
                return -1;
            }
            return m_owner->pageNumberForPageToken(token);
        }

    private:
        MuPdfEngineModule *m_owner;
    };

    PdfPageIdentity *pageIdentity() override
    {
        return &m_pageIdentity;
    }

    PdfOcr *ocr() override
    {
        return this;
    }

    PdfCustomMetadata *customMetadata() override
    {
        return this;
    }

    // --- PdfSaver ---------------------------------------------------------

    bool supportsOption(Okular::SaveInterface::SaveOption option) const override
    {
        return MuPDFGenerator::supportsOption(option);
    }

    bool save(const QString &fileName, Okular::SaveInterface::SaveOptions options, QString *errorText) override
    {
        return MuPDFGenerator::save(fileName, options, errorText);
    }

    Okular::Generator::SwapBackingFileResult swapBackingFile(const QString &newFileName, QList<Okular::Page *> &newPagesVector) override
    {
        return MuPDFGenerator::swapBackingFile(newFileName, newPagesVector);
    }

    bool canExportFlattenedPdf() const override
    {
        return MuPDFGenerator::canExportFlattenedPdf();
    }

    bool exportFlattenedPdf(const QString &fileName, QString *errorText, int *flattenedAnnotations, int *preservedAnnotations) override
    {
        return MuPDFGenerator::exportFlattenedPdf(fileName, errorText, flattenedAnnotations, preservedAnnotations);
    }

    Okular::ExportFormat::List exportFormats() const override
    {
        return MuPDFGenerator::exportFormats();
    }

    bool exportTo(const QString &fileName, const Okular::ExportFormat &format) override
    {
        return MuPDFGenerator::exportTo(fileName, format);
    }

    // --- PdfPrinter -------------------------------------------------------

    Okular::Document::PrintError print(QPrinter &printerDevice) override
    {
        return MuPDFGenerator::print(printerDevice);
    }

    // --- PdfSigner --------------------------------------------------------
    //
    // MuPDF has no signing implementation (see the backend architecture doc:
    // canSign() stays false). The capability is still exposed so the front end
    // can gate on a real object instead of a legacy dynamic_cast.

    bool canSign() const override
    {
        return MuPDFGenerator::canSign();
    }

    std::pair<Okular::SigningResult, QString> sign(const Okular::NewSignatureData &data, int nativePage, const QString &fileName) override
    {
        Q_UNUSED(data)
        Q_UNUSED(nativePage)
        Q_UNUSED(fileName)
        return {Okular::GenericSigningError, QStringLiteral("Signing is not supported by the MuPDF PDF engine.")};
    }

    Okular::CertificateStore *certificateStore() const override
    {
        return MuPDFGenerator::certificateStore();
    }

    void setActiveCertificateBackend(Okular::CertificateInfo::Backend backend) override
    {
        MuPDFGenerator::setActiveCertificateBackend(backend);
    }

    // --- PdfOcr -----------------------------------------------------------

    bool canRecognize() const override
    {
        return canPerformEnglishOcr();
    }

    bool readTextLayer(int nativePage, QList<Okular::OcrTextWord> *words, QString *errorText) override
    {
        if (errorText) {
            errorText->clear();
        }
        if (!isValidNativePage(nativePage) || !hasNativePage(nativePage)) {
            return setError(errorText, QStringLiteral("Invalid OCR page index."));
        }
        return readOcrTextLayer(nativePage, words, errorText);
    }

    bool replaceTextLayer(int nativePage, const QList<Okular::OcrTextWord> &words, QString *errorText) override
    {
        if (errorText) {
            errorText->clear();
        }
        if (!isValidNativePage(nativePage) || !hasNativePage(nativePage)) {
            return setError(errorText, QStringLiteral("Invalid OCR page index."));
        }
        return replaceOcrTextLayer(nativePage, words, errorText);
    }

    Okular::OcrResult saveWithOcr(const QString &sourceFileName,
                                  const QString &outputFileName,
                                  const QList<int> &nativePages,
                                  bool skipPagesWithText,
                                  const PdfOcr::ProgressCallback &progress) override
    {
        // The legacy writer consumes one-based logical page numbers; under the
        // current identity order a logical page is nativePage + 1. Page
        // skipping and the progress policy stay in the legacy implementation.
        QList<int> legacyPages;
        legacyPages.reserve(nativePages.size());
        for (int nativePage : nativePages) {
            legacyPages.append(nativePage + 1);
        }
        return saveWithEnglishOcr(sourceFileName, outputFileName, legacyPages, skipPagesWithText, progress);
    }

    // --- PdfCustomMetadata ------------------------------------------------
    //
    // These names and signatures are identical to the legacy
    // Okular::ReadingViewEditingInterface / NumberedCalloutNumberingInterface
    // methods, and the page argument is a zero-based page index in both. The
    // overrides below therefore serve the capability and the legacy interface
    // with the same behavior under the identity order.

    bool canEditNumberedCalloutNumbering() const override
    {
        return MuPDFGenerator::canEditNumberedCalloutNumbering();
    }

    bool validateNumberedCalloutLabel(const QString &label, QString *errorText) const override
    {
        return MuPDFGenerator::validateNumberedCalloutLabel(label, errorText);
    }

    QString numberedCalloutNumberingJson() const override
    {
        return MuPDFGenerator::numberedCalloutNumberingJson();
    }

    bool setNumberedCalloutNumberingJson(const QString &json, QString *errorText) override
    {
        return MuPDFGenerator::setNumberedCalloutNumberingJson(json, errorText);
    }

    bool canEditReadingViews() const override
    {
        return MuPDFGenerator::canEditReadingViews();
    }

    QList<Okular::ReadingView> readingViews(int nativePage, QString *errorText) const override
    {
        return MuPDFGenerator::readingViews(nativePage, errorText);
    }

    bool setReadingViews(int nativePage, const QList<Okular::ReadingView> &views, QString *errorText) override
    {
        return MuPDFGenerator::setReadingViews(nativePage, views, errorText);
    }

    bool canGenerateReadingViews() const override
    {
        return MuPDFGenerator::canGenerateReadingViews();
    }

    Okular::ReadingViewGenerationResult generateReadingViews(const QString &sourceFileName,
                                                             const QList<int> &nativePages,
                                                             const Okular::ReadingViewEditingInterface::ProgressCallback &progress) override
    {
        return MuPDFGenerator::generateReadingViews(sourceFileName, nativePages, progress);
    }

    QString pageToken(int nativePage) const override
    {
        return MuPDFGenerator::readingViewPageToken(nativePage);
    }

    int pageForToken(const QString &token) const override
    {
        return MuPDFGenerator::readingViewPageForToken(token);
    }

    // --- PdfDocument: loading and identity --------------------------------
    //
    // open*() only open and authenticate the native document. The caller
    // builds the Okular::Page models one at a time with createPageModel() and
    // owns them; the module never stores or frees a page model.

    OpenStatus openFile(const QString &fileName, const QString &password) override
    {
        close();
        prepareForLoad();
        return mapOpenResult(openDocumentForNarrowLoad(fileName, password));
    }

    OpenStatus openData(const QByteArray &data, const QString &password) override
    {
        close();
        prepareForLoad();
        return mapOpenResult(openDocumentFromDataForNarrowLoad(data, password));
    }

    void close() override
    {
        // Page models belong to the caller from createPageModel(); closing
        // releases only native state (the document and its page cache).
        closeDocument();
    }

    // --- PdfDocument: geometry --------------------------------------------

    int pageCount() const override
    {
        return nativePageCount();
    }

    PageGeometry pageGeometry(int nativePage) const override
    {
        PageGeometry geometry;
        if (nativePage < 0 || nativePage >= pageCount()) {
            return geometry;
        }
        double width = 0.0;
        double height = 0.0;
        int rotation = 0;
        if (!nativePageGeometry(nativePage, &width, &height, &rotation)) {
            return geometry;
        }
        // The legacy loader only keeps the transformed crop box; the media box
        // is reported as the crop box until the narrow geometry reports both.
        geometry.cropBox = QSizeF(width, height);
        geometry.mediaBox = geometry.cropBox;
        geometry.rotation = rotation;
        geometry.durationSeconds = 0.0;
        return geometry;
    }

    // --- PdfDocument: rendering -------------------------------------------

    RenderResult renderPage(const RenderRequest &request) override
    {
        RenderResult result;
        if (!isValidPage(request.page)) {
            result.errorText = QStringLiteral("Invalid page number.");
            return result;
        }
        if (request.width <= 0 && request.height <= 0) {
            result.errorText = QStringLiteral("Invalid render size.");
            return result;
        }
        // A tile request carries the full page size as the coordinate space,
        // but only the tile rectangle is allocated. Validate the actual render
        // extent, otherwise very high zoom is rejected even for a small tile.
        const bool tiled = !request.region.isNull();
        const qint64 renderWidth = tiled ? request.region.width() : request.width;
        const qint64 renderHeight = tiled ? request.region.height() : request.height;
        if (renderWidth <= 0 || renderHeight <= 0 || renderWidth > 30000 || renderHeight > 30000 || renderWidth * renderHeight > 100000000) {
            result.errorText = QStringLiteral("Oversized PDF render request.");
            return result;
        }
        if (request.shouldAbort && request.shouldAbort()) {
            return result;
        }

        const QSizeF box = pageGeometry(request.page).cropBox;
        if (box.width() <= 0.0 || box.height() <= 0.0) {
            result.errorText = QStringLiteral("The page has no usable geometry.");
            return result;
        }

        if (!request.region.isNull()) {
            QString error;
            QImage tile = renderRegionForWorker(request.page, request.width, request.height, request.region, request.includeAnnotations, request.shouldAbort, &error);
            if (tile.isNull()) {
                if (!(request.shouldAbort && request.shouldAbort())) {
                    result.errorText = error.isEmpty() ? QStringLiteral("MuPDF failed to render the tile.") : error;
                }
                return result;
            }
            result.image = tile;
            result.regionApplied = true;
            return result;
        }

        // The legacy path can only express a resolution, so derive it from the
        // requested pixel width (or height when only a height was given).
        const double dpiValue = request.width > 0 ? 72.0 * request.width / box.width() : 72.0 * request.height / box.height();
        const int dpi = qBound(1, qRound(dpiValue), 1200);

        QString error;
        QImage image = renderToImage(request.page, dpi, request.includeAnnotations, &error);
        if (image.isNull()) {
            result.errorText = error.isEmpty() ? QStringLiteral("MuPDF failed to render the page.") : error;
            return result;
        }
        if (request.shouldAbort && request.shouldAbort()) {
            return result;
        }

        // Normalize the DPI rounding to the exact pixel size the request asked
        // for. TODO(narrow): render tiles directly once region is forwarded.
        if (request.width > 0 && request.height > 0 && image.size() != QSize(request.width, request.height)) {
            image = image.scaled(QSize(request.width, request.height), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        // RenderRequest::region is intentionally ignored for now: a tile
        // request gets the whole page and no out-of-page coordinates can reach
        // MuPDF. The tile pipeline keeps using the legacy pixmap path.
        result.image = image;
        return result;
    }

    Okular::TextPage *textPage(int nativePage) override
    {
        if (!isValidPage(nativePage)) {
            return nullptr;
        }
        return MuPDFGenerator::textPageForNumber(nativePage);
    }

    // --- PdfDocument: page model loading ----------------------------------

    // Per-page creation. MuPDFGenerator::createPage() loads the native page,
    // annotations, links and form fields into one Okular::Page, so the five
    // load* hooks below stay no-ops.
    Okular::Page *createPageModel(int nativePage) override
    {
        if (!isValidPage(nativePage)) {
            return nullptr;
        }
        return MuPDFGenerator::createPageModel(nativePage);
    }

    void loadAnnotations(int, Okular::Page *) override
    {
    }

    void loadLinks(int, Okular::Page *) override
    {
    }

    void loadForms(int, Okular::Page *) override
    {
    }

    void loadTransition(int, Okular::Page *) override
    {
    }

    void loadPageActions(int, Okular::Page *) override
    {
    }

    // --- PdfDocument: forwarding to the legacy generator ------------------

    Okular::AnnotationProxy *annotationProxy() override
    {
        return MuPDFGenerator::annotationProxy();
    }

    Okular::DocumentInfo documentInfo(const QSet<Okular::DocumentInfo::Key> &keys) const override
    {
        return generateDocumentInfo(keys);
    }

    const Okular::DocumentSynopsis *synopsis() override
    {
        return generateDocumentSynopsis();
    }

    bool setSynopsis(const Okular::DocumentSynopsis &documentSynopsis, QString *errorText) override
    {
        return setDocumentSynopsis(documentSynopsis, errorText);
    }

    Okular::FontInfo::List fontsForPage(int nativePage) override
    {
        return MuPDFGenerator::fontsForPage(nativePage);
    }

    QByteArray fontData(const Okular::FontInfo &font) override
    {
        return MuPDFGenerator::requestFontData(font);
    }

    const QList<Okular::EmbeddedFile *> *embeddedFiles() const override
    {
        return MuPDFGenerator::embeddedFiles();
    }

    bool isAllowed(Okular::Permission permission) const override
    {
        return MuPDFGenerator::isAllowed(permission);
    }

    QVariant metaData(const QString &key, const QVariant &option) const override
    {
        return MuPDFGenerator::metaData(key, option);
    }

    Okular::Generator::PageLayout defaultPageLayout() const override
    {
        return MuPDFGenerator::defaultPageLayout();
    }

    bool defaultPageContinuous() const override
    {
        return MuPDFGenerator::defaultPageContinuous();
    }

private:

    bool isValidPage(int nativePage) const
    {
        return nativePage >= 0 && nativePage < pageCount();
    }

    bool isValidNativePage(int nativePage) const
    {
        return isValidPage(nativePage);
    }

    static bool setError(QString *errorText, const QString &message)
    {
        if (errorText) {
            *errorText = message;
        }
        return false;
    }

    /**
     * Resolves the Okular::Page model of a zero-based native page through the
     * wrapper document PdfGeneratorBase installs before every load. The page
     * cache must already hold the native page: the legacy link editors refresh
     * the page they edited and index that cache. Fail with an error instead of
     * dereferencing an absent page or native page.
     */
    Okular::Page *legacyLinkPage(int nativePage, QString *errorText) const
    {
        if (errorText) {
            errorText->clear();
        }
        const Okular::Document *wrapper = Okular::Generator::document();
        if (!wrapper) {
            setError(errorText, QStringLiteral("The PDF page is not attached to a viewer document."));
            return nullptr;
        }
        if (!isValidNativePage(nativePage)) {
            setError(errorText, QStringLiteral("Invalid page number."));
            return nullptr;
        }
        if (!hasNativePage(nativePage)) {
            setError(errorText, QStringLiteral("The native PDF page is not loaded for editing."));
            return nullptr;
        }
        Okular::Page *page = const_cast<Okular::Page *>(wrapper->page(nativePage));
        if (!page) {
            setError(errorText, QStringLiteral("Page %1 is not part of the open document.").arg(nativePage));
        }
        return page;
    }

    void prepareForLoad()
    {
        // Page models are sized from the generator DPI. At 72 DPI that size is
        // the PDF point size, which is what PageGeometry promises; renderPage()
        // asks for its own DPI anyway.
        setDPI(QSizeF(72.0, 72.0));
    }

    static OpenStatus mapOpenResult(Okular::Document::OpenResult result)
    {
        if (result == Okular::Document::OpenSuccess)
            return OpenStatus::Success;
        return result == Okular::Document::OpenNeedsPassword ? OpenStatus::NeedsPassword : OpenStatus::Error;
    }

    LinkEditor m_linkEditor{this};
    PageIdentity m_pageIdentity{this};
    PageEditor m_pageEditor{this};
};

const EngineModuleDescriptor descriptor = {
    EngineModuleApiVersion,
    "mupdf",
    "MuPDF",
    "MuPDFGenerator + narrow PdfDocument read path",
    []() -> void * {
        auto *engine = new MuPdfEngineModule;
        return static_cast<PdfEngine *>(engine);
    },
    [](void *engine) {
        auto *pdfEngine = static_cast<PdfEngine *>(engine);
        delete static_cast<MuPdfEngineModule *>(pdfEngine);
    },
};

} // namespace

extern "C" MENGSHEE_PDF_ENGINE_MODULE_EXPORT const Mengshee::Pdf::EngineModuleDescriptor *mengshee_pdf_engine_module()
{
    return &descriptor;
}
