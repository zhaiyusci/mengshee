// SPDX-License-Identifier: AGPL-3.0-or-later
// Poppler engine module: exports the existing Poppler plugin through the
// dynamic module C ABI, and implements the narrow PdfDocument read path on top
// of the legacy PDFGenerator instead of duplicating the Poppler plumbing.
//
// PDFGenerator is an implementation detail of the module: the front end
// reaches it only through PdfDocument and the capability objects, and through
// the neutral hasFeature()/setWrapperDocument() hooks below. No
// Okular::Generator pointer crosses the module boundary. The narrow path opens
// and authenticates the document without building page models, then builds them
// one at a time for PdfGeneratorBase, which becomes their owner.

#include "generator_pdf.h"

#include "debug_pdf.h"
#include "pdfengine.h"
#include "pdfenginemodule.h"

#include <core/page.h>
#include <core/printoptionswidget.h>
#include <interfaces/configinterface.h>

#include <KConfigDialog>

#include <QImage>
#include <QSizeF>
#include <QtMath>

#include <exception>

namespace
{

using namespace Mengshee::Pdf;

class PopplerEngineModule final
    : public PDFGenerator
    , public PdfEngine
    , public PdfDocument
    , public PdfSaver
    , public PdfPrinter
    , public PdfSigner
    , public PdfOcr
    , public PdfCustomMetadata
    , public PdfConfigurable
    , public PdfPrintConfigurator
    , public PdfBackendActions
{
public:
    PopplerEngineModule()
        : PDFGenerator(nullptr, {})
    {
    }

    ~PopplerEngineModule() override
    {
        // Okular::Page models are created through createPageModel() and owned
        // by the narrow caller. close() only releases the native document and
        // the generator's page cache.
        close();
    }

    // --- PdfEngine --------------------------------------------------------

    EngineId id() const override
    {
        return QString::fromLatin1(PopplerEngineId);
    }

    bool hasFeature(Okular::Generator::GeneratorFeature feature) const override
    {
        // The legacy generator is the object that knows Poppler's feature set
        // (it also depends on how the Poppler build was configured); the module
        // only forwards the flags to the front end.
        return PDFGenerator::hasFeature(feature);
    }

    void setWrapperDocument(Okular::Document *wrapper) override
    {
        // The link editors, printing and annotation appearance resolve pages
        // through the owning Document, exactly like a forwarding
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

    PdfConfigurable *configurable() override
    {
        return this;
    }

    bool reparseConfig() override
    {
        return PDFGenerator::reparseConfig();
    }

    void addPages(KConfigDialog *dialog) override
    {
        PDFGenerator::addPages(dialog);
    }

    PdfPrintConfigurator *printConfigurator() override
    {
        return this;
    }

    PdfBackendActions *backendActions() override
    {
        return this;
    }

    Okular::PrintOptionsWidget *printConfigurationWidget() const override
    {
        return qobject_cast<Okular::PrintOptionsWidget *>(PDFGenerator::printConfigurationWidget());
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
        explicit LinkEditor(PopplerEngineModule *owner)
            : m_owner(owner)
        {
        }

        bool canEditLinks() const override
        {
            return m_owner->canEditPdfLinks();
        }

        bool setNamedDestination(const QString &name, int nativePage, double x, double y, QString *errorText) override
        {
            if (!m_owner->isValidPage(nativePage)) {
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
            // The legacy refresh applies the logical page order; this bridge
            // refreshes the native page directly.
            return m_owner->refreshPageLinksForEngine(page, nativePage, errorText);
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
            if (!page) {
                return false;
            }
            return m_owner->editExternalLinkDestination(page, area.left, area.top, area.right, area.bottom, url, errorText);
        }

        bool createExternalLink(int nativePage, const Okular::NormalizedRect &area, const QString &url, QString *errorText) override
        {
            Okular::Page *page = m_owner->legacyLinkPage(nativePage, errorText);
            if (!page) {
                return false;
            }
            return m_owner->createExternalLink(page, area.left, area.top, area.right, area.bottom, url, errorText);
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
            if (!page) {
                return false;
            }
            return m_owner->deletePdfLink(page, area.left, area.top, area.right, area.bottom, errorText);
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
            if (destinationName.isEmpty() && !m_owner->isValidPage(destinationNativePage)) {
                return setError(errorText, QStringLiteral("Invalid destination page number."));
            }
            return true;
        }

        PopplerEngineModule *m_owner;
    };

    PdfLinkEditor *linkEditor() override
    {
        return &m_linkEditor;
    }

    /**
     * Page-sequence editing on native page indices.
     *
     * The legacy Poppler implementation takes zero-based logical page numbers
     * and maps them through its own page order. The narrow front end only opens
     * documents in the identity order and resetPageTopologyCaches() restores
     * that identity after every live edit, so a native index is the same
     * logical index here. Every method forwards to the legacy implementation
     * and lets it create the Okular::Page models; those models belong to the
     * caller (PdfGeneratorBase / Okular::Document), never to this adapter.
     */
    class PageEditor final : public PdfPageEditor
    {
    public:
        explicit PageEditor(PopplerEngineModule *owner)
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
            const bool inserted = m_owner->guardedValue(
                false,
                [&] { return m_owner->insertBlankPageInDocument(afterNative, sizePoints.width(), sizePoints.height(), insertedPage, editId, errorText, heading); },
                errorText);
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
            const bool inserted = m_owner->guardedValue(
                false,
                [&] { return m_owner->duplicatePageInDocument(nativePage, resolveDestinationConflicts, insertedPage, editId, errorText); },
                errorText);
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
            const bool inserted = m_owner->guardedValue(
                false,
                [&] { return m_owner->insertPdfPageInDocument(afterNative, sourceFileName, sourcePage, resolveDestinationConflicts, insertedPage, editId, errorText); },
                errorText);
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
            return m_owner->guardedValue(
                false,
                [&] { return m_owner->appendPdfFileInDocument(sourceFileName, resolveDestinationConflicts, appendedPages, editIds, errorText); },
                errorText);
        }

        bool canCombinePdfFiles() const override
        {
            return m_owner->canCombinePdfFiles();
        }

        int pdfPageCount(const QString &fileName, QString *errorText) override
        {
            return m_owner->guardedValue(-1, [&] { return m_owner->pdfPageCount(fileName, errorText); }, errorText);
        }

        bool combinePdfFiles(const QStringList &inputFileNames, const QString &outputFileName, bool resolveDestinationConflicts, QString *errorText) override
        {
            return m_owner->guardedValue(
                false,
                [&] { return m_owner->combinePdfFiles(inputFileNames, outputFileName, resolveDestinationConflicts, errorText); },
                errorText);
        }

        bool canDeletePage() const override
        {
            return m_owner->canDeletePage();
        }

        bool detachPage(Okular::Page *page, int nativePage, Okular::Page **detachedPage, quint64 *editId, QString *errorText) override
        {
            if (!m_owner->guardedValue(false, [&] { return m_owner->detachPageInDocument(page, nativePage, editId, errorText); }, errorText)) {
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
            if (!m_owner->guardedValue(false, [&] { return m_owner->detachPageInDocument(page, nativePage, editId, errorText); }, errorText)) {
                return false;
            }
            if (detachedPage) {
                *detachedPage = page;
            }
            return true;
        }

        bool attachPage(int afterNative, quint64 editId, Okular::Page **insertedPage, int *insertedNativePage, QString *errorText) override
        {
            if (!m_owner->guardedValue(false, [&] { return m_owner->attachPageInDocument(afterNative, editId, insertedPage, errorText); }, errorText)) {
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
            return m_owner->guardedValue(false, [&] { return m_owner->movePageInDocument(fromNative, toNative, errorText); }, errorText);
        }

        bool canRotatePage() const override
        {
            return m_owner->canRotatePage();
        }

        bool rotatePage(Okular::Page *page, int nativePage, int rotationDegrees, Okular::Page **replacementPage, QString *errorText) override
        {
            return m_owner->guardedValue(false, [&] { return m_owner->rotatePageInDocument(page, nativePage, rotationDegrees, replacementPage, errorText); }, errorText);
        }

        bool rotatePageWithAnnotations(Okular::Page *page, int nativePage, int rotationDegrees, bool annotationsFollowPage, Okular::Page **replacementPage, QString *errorText) override
        {
            return m_owner->guardedValue(
                false,
                [&] { return m_owner->rotatePageWithAnnotationsInDocument(page, nativePage, rotationDegrees, annotationsFollowPage, replacementPage, errorText); },
                errorText);
        }

        bool canSetPageSize() const override
        {
            return m_owner->canSetPageSize();
        }

        bool adjustPageBox(Okular::Page *page, int nativePage, double x1Delta, double y1Delta, double x2Delta, double y2Delta, Okular::Page **replacementPage, QString *errorText) override
        {
            return m_owner->guardedValue(
                false,
                [&] { return m_owner->adjustPageBoxInDocument(page, nativePage, x1Delta, y1Delta, x2Delta, y2Delta, replacementPage, errorText); },
                errorText);
        }

    private:
        PopplerEngineModule *m_owner;
    };

    PdfPageEditor *pageEditor() override
    {
        return &m_pageEditor;
    }

    /**
     * Page identity of explicit link destinations. It is a separate object
     * because Poppler keeps two token namespaces: the Reading View
     * /MengsheeViews /PageId UUID exposed by PdfCustomMetadata::pageToken(),
     * and the page object reference ("number generation") that
     * PDFGenerator::createLinkFromPopplerLink() writes into
     * Okular::GotoAction::pageToken(). Forwarding to the legacy generator keeps
     * the generated token identical to the one written into the action.
     */
    class PageIdentity final : public PdfPageIdentity
    {
    public:
        explicit PageIdentity(PopplerEngineModule *owner)
            : m_owner(owner)
        {
        }

        QString pageToken(int nativePage) const override
        {
            if (!m_owner->isValidPage(nativePage)) {
                return {};
            }
            return m_owner->guardedValue(QString(), [&] { return m_owner->pageTokenForNativePage(nativePage); }, nullptr);
        }

        int nativePageForToken(const QString &token) const override
        {
            if (token.isEmpty()) {
                return -1;
            }
            return m_owner->guardedValue(-1, [&] { return m_owner->nativePageForPageToken(token); }, nullptr);
        }

    private:
        PopplerEngineModule *m_owner;
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
        return PDFGenerator::supportsOption(option);
    }

    bool save(const QString &fileName, Okular::SaveInterface::SaveOptions options, QString *errorText) override
    {
        return PDFGenerator::save(fileName, options, errorText);
    }

    Okular::Generator::SwapBackingFileResult swapBackingFile(const QString &newFileName, QList<Okular::Page *> &newPagesVector) override
    {
        return PDFGenerator::swapBackingFile(newFileName, newPagesVector);
    }

    bool canExportFlattenedPdf() const override
    {
        return PDFGenerator::canExportFlattenedPdf();
    }

    bool exportFlattenedPdf(const QString &fileName, QString *errorText, int *flattenedAnnotations, int *preservedAnnotations) override
    {
        return PDFGenerator::exportFlattenedPdf(fileName, errorText, flattenedAnnotations, preservedAnnotations);
    }

    Okular::ExportFormat::List exportFormats() const override
    {
        return PDFGenerator::exportFormats();
    }

    bool exportTo(const QString &fileName, const Okular::ExportFormat &format) override
    {
        return PDFGenerator::exportTo(fileName, format);
    }

    // --- PdfPrinter -------------------------------------------------------

    Okular::Document::PrintError print(QPrinter &printerDevice) override
    {
        return PDFGenerator::print(printerDevice);
    }

    // --- PdfSigner --------------------------------------------------------

    bool canSign() const override
    {
        return PDFGenerator::canSign();
    }

    std::pair<Okular::SigningResult, QString> sign(const Okular::NewSignatureData &data, int nativePage, const QString &fileName) override
    {
        // PDFGenerator::sign() treats data.page() as the logical page and maps
        // it through its own m_pageOrder. PdfGeneratorBase owns no page order
        // yet, so the resolved page it passes is the Okular page number and the
        // two are identity today (a narrow load requires the identity order).
        // Rebuild the data with that page; the legacy signer then applies its
        // mapping exactly once, as it did before the capability existed. A
        // future base-owned page order must expose a native-to-logical mapping
        // before this can accept a real native index. NewSignatureData is
        // deliberately non-copyable, hence the field-by-field copy.
        Okular::NewSignatureData resolvedData;
        resolvedData.setCertNickname(data.certNickname());
        resolvedData.setCertSubjectCommonName(data.certSubjectCommonName());
        resolvedData.setPassword(data.password());
        resolvedData.setPage(nativePage);
        resolvedData.setBoundingRectangle(data.boundingRectangle());
        resolvedData.setDocumentPassword(data.documentPassword());
        resolvedData.setReason(data.reason());
        resolvedData.setLocation(data.location());
        resolvedData.setBackgroundImagePath(data.backgroundImagePath());
        resolvedData.setFontSize(data.fontSize());
        resolvedData.setLeftFontSize(data.leftFontSize());
        resolvedData.setRequestedSignatureType(data.requestedSignatureType());
        return PDFGenerator::sign(resolvedData, fileName);
    }

    Okular::CertificateStore *certificateStore() const override
    {
        return PDFGenerator::certificateStore();
    }

    void setActiveCertificateBackend(Okular::CertificateInfo::Backend backend) override
    {
        PDFGenerator::setActiveCertificateBackend(backend);
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
        if (!isValidPage(nativePage)) {
            return setError(errorText, QStringLiteral("Invalid OCR page index."));
        }
        return guardedValue(false, [&] { return PDFGenerator::readOcrTextLayer(nativePage, words, errorText); }, errorText);
    }

    bool replaceTextLayer(int nativePage, const QList<Okular::OcrTextWord> &words, QString *errorText) override
    {
        if (errorText) {
            errorText->clear();
        }
        if (!isValidPage(nativePage)) {
            return setError(errorText, QStringLiteral("Invalid OCR page index."));
        }
        return guardedValue(false, [&] { return PDFGenerator::replaceOcrTextLayer(nativePage, words, errorText); }, errorText);
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
        try {
            return PDFGenerator::saveWithEnglishOcr(sourceFileName, outputFileName, legacyPages, skipPagesWithText, progress);
        } catch (const std::exception &exception) {
            Okular::OcrResult result;
            result.errorText = QString::fromLocal8Bit(exception.what());
            return result;
        } catch (...) {
            Okular::OcrResult result;
            result.errorText = QStringLiteral("The PDF engine failed because of an unknown internal error.");
            return result;
        }
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
        return PDFGenerator::canEditNumberedCalloutNumbering();
    }

    bool validateNumberedCalloutLabel(const QString &label, QString *errorText) const override
    {
        return guardedValue(false, [&] { return PDFGenerator::validateNumberedCalloutLabel(label, errorText); }, errorText);
    }

    QString numberedCalloutNumberingJson() const override
    {
        return guardedValue(QString(), [&] { return PDFGenerator::numberedCalloutNumberingJson(); }, nullptr);
    }

    bool setNumberedCalloutNumberingJson(const QString &json, QString *errorText) override
    {
        return guardedValue(false, [&] { return PDFGenerator::setNumberedCalloutNumberingJson(json, errorText); }, errorText);
    }

    bool canEditReadingViews() const override
    {
        return PDFGenerator::canEditReadingViews();
    }

    QList<Okular::ReadingView> readingViews(int nativePage, QString *errorText) const override
    {
        return guardedValue(QList<Okular::ReadingView>(), [&] { return PDFGenerator::readingViews(nativePage, errorText); }, errorText);
    }

    bool setReadingViews(int nativePage, const QList<Okular::ReadingView> &views, QString *errorText) override
    {
        return guardedValue(false, [&] { return PDFGenerator::setReadingViews(nativePage, views, errorText); }, errorText);
    }

    bool canGenerateReadingViews() const override
    {
        return PDFGenerator::canGenerateReadingViews();
    }

    Okular::ReadingViewGenerationResult generateReadingViews(const QString &sourceFileName,
                                                             const QList<int> &nativePages,
                                                             const Okular::ReadingViewEditingInterface::ProgressCallback &progress) override
    {
        return guardedValue(Okular::ReadingViewGenerationResult(), [&] { return PDFGenerator::generateReadingViews(sourceFileName, nativePages, progress); }, nullptr);
    }

    QString pageToken(int nativePage) const override
    {
        return guardedValue(QString(), [&] { return PDFGenerator::readingViewPageToken(nativePage); }, nullptr);
    }

    int pageForToken(const QString &token) const override
    {
        return guardedValue(-1, [&] { return PDFGenerator::readingViewPageForToken(token); }, nullptr);
    }

    // --- PdfDocument: loading and identity --------------------------------
    //
    // open*() only open and authenticate the native document. The caller builds
    // the Okular::Page models one at a time with createPageModel() and owns
    // them; the module never stores or frees a page model.

    OpenStatus openFile(const QString &fileName, const QString &password) override
    {
        try {
            close();
            prepareForLoad();
            return mapOpenResult(openDocumentForNarrowLoad(fileName, password));
        } catch (const std::exception &exception) {
            qCWarning(OkularPdfDebug) << "Could not open the PDF document:" << exception.what();
            return OpenStatus::Error;
        } catch (...) {
            qCWarning(OkularPdfDebug) << "Could not open the PDF document because of an unknown exception";
            return OpenStatus::Error;
        }
    }

    OpenStatus openData(const QByteArray &data, const QString &password) override
    {
        try {
            close();
            prepareForLoad();
            return mapOpenResult(openDocumentFromDataForNarrowLoad(data, password));
        } catch (const std::exception &exception) {
            qCWarning(OkularPdfDebug) << "Could not open the PDF data:" << exception.what();
            return OpenStatus::Error;
        } catch (...) {
            qCWarning(OkularPdfDebug) << "Could not open the PDF data because of an unknown exception";
            return OpenStatus::Error;
        }
    }

    void close() override
    {
        // Page models belong to the caller from createPageModel(); closing
        // releases only native state (the document, annotation proxy and page
        // caches). Never delete a model that was handed out.
        try {
            closeDocument();
        } catch (...) {
            qCWarning(OkularPdfDebug) << "Could not close the PDF document because of an unknown exception";
        }
    }

    // --- PdfDocument: geometry --------------------------------------------

    int pageCount() const override
    {
        return nativePageCount();
    }

    PageGeometry pageGeometry(int nativePage) const override
    {
        PageGeometry geometry;
        double width = 0.0;
        double height = 0.0;
        int rotation = 0;
        if (!nativePageGeometry(nativePage, &width, &height, &rotation)) {
            return geometry;
        }
        // Poppler Qt6 exposes the /CropBox through pageSizeF(); the media box
        // is not part of the public API, so both boxes are reported the same.
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
        try {
            if (!isValidPage(request.page)) {
                result.errorText = QStringLiteral("Invalid page number.");
                return result;
            }
            if (request.width <= 0 && request.height <= 0) {
                result.errorText = QStringLiteral("Invalid render size.");
                return result;
            }
            // A tile request uses the full page as coordinate space but only
            // allocates the tile rectangle. Validate the actual extent.
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

            // Display pixmaps are generated on PixmapGenerationThread. Use the
            // worker-safe renderer with the page model supplied by the front
            // end instead of the export-only renderToImage() path.
            if (request.pageModel) {
                QString error;
                QImage image = renderPageForWorker(request.pageModel,
                                                   request.width,
                                                   request.height,
                                                   request.region,
                                                   request.includeAnnotations,
                                                   request.applyViewRotation,
                                                   request.shouldAbort,
                                                   &error);
                if (image.isNull()) {
                    if (!(request.shouldAbort && request.shouldAbort())) {
                        result.errorText = error.isEmpty() ? QStringLiteral("Poppler failed to render the page.") : error;
                    }
                    return result;
                }
                result.image = image;
                result.regionApplied = !request.region.isNull();
                return result;
            }

            const QSizeF box = pageGeometry(request.page).cropBox;
            if (box.width() <= 0.0 || box.height() <= 0.0) {
                result.errorText = QStringLiteral("The page has no usable geometry.");
                return result;
            }

            // renderToImage() can only express a resolution, so derive it from
            // the requested pixel width (or height when only a height was
            // given). The narrow page model is at 72 DPI, so points == pixels
            // before this scaling.
            const double dpiValue = request.width > 0 ? 72.0 * request.width / box.width() : 72.0 * request.height / box.height();
            const int dpi = qBound(1, qRound(dpiValue), 1200);

            QString error;
            QImage image = renderToImage(request.page, dpi, request.includeAnnotations, &error);
            if (image.isNull()) {
                result.errorText = error.isEmpty() ? QStringLiteral("Poppler failed to render the page.") : error;
                return result;
            }
            if (request.shouldAbort && request.shouldAbort()) {
                return result;
            }

            // Normalize the DPI rounding to the exact pixel size the request
            // asked for. RenderRequest::region is intentionally ignored for
            // now: a tile request gets the whole page from renderToImage(), so
            // no out-of-page coordinates can reach Poppler. The tile pipeline
            // keeps using the legacy pixmap path.
            if (request.width > 0 && request.height > 0 && image.size() != QSize(request.width, request.height)) {
                image = image.scaled(QSize(request.width, request.height), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
            result.image = image;
            return result;
        } catch (const std::exception &exception) {
            result.image = QImage();
            result.errorText = QString::fromLocal8Bit(exception.what());
            return result;
        } catch (...) {
            result.image = QImage();
            result.errorText = QStringLiteral("Poppler failed to render the page because of an unknown exception.");
            return result;
        }
    }

    Okular::TextPage *textPage(int nativePage) override
    {
        try {
            return textPageForNumber(nativePage);
        } catch (const std::exception &exception) {
            qCWarning(OkularPdfDebug) << "Could not extract the PDF text:" << exception.what();
            return nullptr;
        } catch (...) {
            qCWarning(OkularPdfDebug) << "Could not extract the PDF text because of an unknown exception";
            return nullptr;
        }
    }

    // --- PdfDocument: page model loading ----------------------------------

    // Per-page creation. createPageModelForNarrowLoad() loads the native page,
    // annotations, links and form fields into one Okular::Page, so the five
    // load* hooks below stay no-ops.
    Okular::Page *createPageModel(int nativePage) override
    {
        try {
            return createPageModelForNarrowLoad(nativePage);
        } catch (const std::exception &exception) {
            qCWarning(OkularPdfDebug) << "Could not create the PDF page model:" << exception.what();
            return nullptr;
        } catch (...) {
            qCWarning(OkularPdfDebug) << "Could not create the PDF page model because of an unknown exception";
            return nullptr;
        }
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
        return PDFGenerator::annotationProxy();
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
        return PDFGenerator::fontsForPage(nativePage);
    }

    QByteArray fontData(const Okular::FontInfo &font) override
    {
        return PDFGenerator::requestFontData(font);
    }

    const QList<Okular::EmbeddedFile *> *embeddedFiles() const override
    {
        return PDFGenerator::embeddedFiles();
    }

    bool isAllowed(Okular::Permission permission) const override
    {
        return PDFGenerator::isAllowed(permission);
    }

    QVariant metaData(const QString &key, const QVariant &option) const override
    {
        return PDFGenerator::metaData(key, option);
    }

    Okular::Generator::PageLayout defaultPageLayout() const override
    {
        return PDFGenerator::defaultPageLayout();
    }

    bool defaultPageContinuous() const override
    {
        return PDFGenerator::defaultPageContinuous();
    }

    QAbstractItemModel *layersModel() const override
    {
        return PDFGenerator::layersModel();
    }

    Okular::Action *additionalDocumentAction(Okular::Document::DocumentAdditionalActionType type) override
    {
        return PDFGenerator::additionalDocumentAction(type);
    }

    Okular::BackendOpaqueAction::OpaqueActionResult opaqueAction(const Okular::BackendOpaqueAction *action) override
    {
        return PDFGenerator::opaqueAction(action);
    }

private:
    bool isValidPage(int nativePage) const
    {
        return nativePage >= 0 && nativePage < pageCount();
    }

    static bool setError(QString *errorText, const QString &message)
    {
        if (errorText) {
            *errorText = message;
        }
        return false;
    }

    /**
     * Runs a legacy capability call without letting a Poppler C++ exception
     * cross the engine boundary. Operations that already catch internally keep
     * their own error text; this only guarantees the interface contract.
     */
    template<typename Result, typename Operation>
    Result guardedValue(Result fallback, Operation &&operation, QString *errorText) const
    {
        try {
            return operation();
        } catch (const std::exception &exception) {
            if (errorText) {
                *errorText = QString::fromLocal8Bit(exception.what());
            }
            return fallback;
        } catch (...) {
            if (errorText) {
                *errorText = QStringLiteral("The PDF engine failed because of an unknown internal error.");
            }
            return fallback;
        }
    }

    /**
     * Resolves the Okular::Page model of a zero-based native page through the
     * wrapper document PdfGeneratorBase installs before every load. Fail with
     * an error instead of dereferencing an absent document or page.
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
        if (!isValidPage(nativePage)) {
            setError(errorText, QStringLiteral("Invalid page number."));
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
        if (result == Okular::Document::OpenSuccess) {
            return OpenStatus::Success;
        }
        return result == Okular::Document::OpenNeedsPassword ? OpenStatus::NeedsPassword : OpenStatus::Error;
    }

    LinkEditor m_linkEditor{this};
    PageIdentity m_pageIdentity{this};
    PageEditor m_pageEditor{this};
};

const EngineModuleDescriptor descriptor = {
    EngineModuleApiVersion,
    "poppler",
    "Poppler",
    "PDFGenerator + narrow PdfDocument read path",
    []() -> void * {
        auto *engine = new PopplerEngineModule;
        return static_cast<PdfEngine *>(engine);
    },
    [](void *engine) {
        auto *pdfEngine = static_cast<PdfEngine *>(engine);
        delete static_cast<PopplerEngineModule *>(pdfEngine);
    },
};

} // namespace

extern "C" MENGSHEE_PDF_ENGINE_MODULE_EXPORT const Mengshee::Pdf::EngineModuleDescriptor *mengshee_pdf_engine_module()
{
    return &descriptor;
}
