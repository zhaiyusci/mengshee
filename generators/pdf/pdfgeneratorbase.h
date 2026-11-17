// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Draft: the single Okular-facing PDF front end. Every PDF backend implements
// PdfEngine (pdfengine.h); this class is the only place that talks to
// Okular::Generator and the optional PDF interfaces in core/.
//
// This header is not part of the build yet. See
// docs/pdf-backend-architecture.md for the migration plan.

#include "pdfengine.h"

#include <core/generator.h>
#include <core/numberedcalloutnumberinginterface.h>
#include <core/readingvieweditinginterface.h>
#include <interfaces/saveinterface.h>

#include <memory>

class QPrinter;

namespace Mengshee
{
namespace Pdf
{

/**
 * Shared PDF generator front end.
 *
 * Responsibilities:
 *  - own the engine and the Okular::Page models;
 *  - own the logical page order and translate it to native indices;
 *  - own edit identities, undo/redo bookkeeping and page-model rebuilding;
 *  - implement capability policy (what is exposed when an engine lacks a
 *    capability), progress/cancellation, and error text;
 *  - be the single implementation of Okular's PDF interfaces.
 *
 * Backend-specific Okular interfaces are exposed by the single plugin class,
 * which implements the union and gates each capability on the active engine:
 *  - Okular::ConfigInterface adds pages only when the engine has a
 *    PdfConfigurable (MuPDF adds none);
 *  - Okular::PrintInterface returns the engine widget or a
 *    DefaultPrintOptionsWidget;
 *  - Okular::PrintPreviewInterface returns the engine renderer or an empty
 *    renderer that the print dialog already handles.
 * Signature support is gated by canSign()/sign() through PdfSigner.
 */
class PdfGeneratorBase : public Okular::Generator,
                         public Okular::SaveInterface,
                         public Okular::PageInsertionInterface,
                         public Okular::PdfLinkEditingInterface,
                         public Okular::PdfOcrInterface,
                         public Okular::ReadingViewEditingInterface,
                         public Okular::NumberedCalloutNumberingInterface
{
    Q_OBJECT
    Q_INTERFACES(Okular::Generator Okular::SaveInterface)

public:
    /**
     * Takes ownership of an engine created by PdfEngineRegistry. The plugin
     * chooses the engine, so the front end has no compile-time backend.
     * engine may be null when no module is available or the selected module
     * failed to load; opening a document then reports the registry error
     * instead of failing plugin construction.
     */
    PdfGeneratorBase(EnginePtr engine, QObject *parent, const QVariantList &args);
    ~PdfGeneratorBase() override;

    bool hasEngine() const;
    QString engineId() const;

    // Okular::Generator
    Okular::Document::OpenResult loadDocumentWithPassword(const QString &fileName, QList<Okular::Page *> &pagesVector, const QString &password) override;
    Okular::Document::OpenResult loadDocumentFromDataWithPassword(const QByteArray &fileData, QList<Okular::Page *> &pagesVector, const QString &password) override;
    Okular::DocumentInfo generateDocumentInfo(const QSet<Okular::DocumentInfo::Key> &keys) const override;
    Okular::FontInfo::List fontsForPage(int page) override;
    QByteArray requestFontData(const Okular::FontInfo &font) override;
    const QList<Okular::EmbeddedFile *> *embeddedFiles() const override;
    PageLayout defaultPageLayout() const override;
    bool defaultPageContinuous() const override;
    PageSizeMetric pagesSizeMetric() const override;
    const Okular::DocumentSynopsis *generateDocumentSynopsis() override;
    bool setDocumentSynopsis(const Okular::DocumentSynopsis &synopsis, QString *errorText) override;
    QVariant metaData(const QString &key, const QVariant &option) const override;
    bool isAllowed(Okular::Permission permission) const override;
    Okular::Document::PrintError print(QPrinter &printer) override;
    QImage image(Okular::PixmapRequest *request) override;
    bool canRenderToImage() const override;
    QImage renderToImage(int page, int dpi, bool includeAnnotations, QString *error = nullptr) override;
    QAbstractItemModel *layersModel() const override;
    Okular::BackendOpaqueAction::OpaqueActionResult opaqueAction(const Okular::BackendOpaqueAction *action) override;
    Okular::Action *additionalDocumentAction(Okular::Document::DocumentAdditionalActionType type) override;
    Okular::ExportFormat::List exportFormats() const override;
    bool exportTo(const QString &fileName, const Okular::ExportFormat &format) override;
    bool canSign() const override;
    std::pair<Okular::SigningResult, QString> sign(const Okular::NewSignatureData &data, const QString &fileName) override;
    Okular::CertificateStore *certificateStore() const override;

    bool canExportFlattenedPdf() const override;
    bool exportFlattenedPdf(const QString &fileName, QString *errorText, int *flattenedAnnotations, int *preservedAnnotations) override;

    // Okular::SaveInterface
    bool supportsOption(Okular::SaveInterface::SaveOption option) const override;
    bool save(const QString &fileName, Okular::SaveInterface::SaveOptions options, QString *errorText) override;
    Okular::AnnotationProxy *annotationProxy() const override;

    // Okular::PageInsertionInterface
    bool canInsertBlankPage() const override;
    bool insertBlankPageInDocument(int insertAfterPageNumber, double width, double height, Okular::Page **insertedPage, quint64 *editId, QString *errorText, const QString &heading = QString()) override;
    bool canInsertPageFromPdf() const override;
    bool duplicatePageInDocument(int pageNumber, bool resolveDestinationConflicts, Okular::Page **insertedPage, quint64 *editId, QString *errorText) override;
    bool insertPdfPageInDocument(int insertAfterPageNumber,
                                 const QString &insertedFileName,
                                 int pageToInsert,
                                 bool resolveDestinationConflicts,
                                 Okular::Page **insertedPage,
                                 quint64 *editId,
                                 QString *errorText) override;
    bool canAppendPdfFile() const override;
    bool appendPdfFileInDocument(const QString &sourceFileName, bool resolveDestinationConflicts, QList<Okular::Page *> *appendedPages, QList<quint64> *editIds, QString *errorText) override;
    bool canCombinePdfFiles() const override;
    int pdfPageCount(const QString &inputFileName, QString *errorText) override;
    bool combinePdfFiles(const QStringList &inputFileNames, const QString &outputFileName, bool resolveDestinationConflicts, QString *errorText) override;
    bool canDeletePage() const override;
    bool detachPageInDocument(Okular::Page *page, int pageNumber, quint64 *editId, QString *errorText) override;
    bool detachPageInDocument(Okular::Page *page, int pageNumber, quint64 editId, QString *errorText) override;
    bool attachPageInDocument(int insertAfterPageNumber, quint64 editId, Okular::Page **insertedPage, QString *errorText) override;
    bool canMovePage() const override;
    bool canMovePageInDocument() const override;
    bool movePageInDocument(int sourcePageNumber, int destinationPageNumber, QString *errorText) override;
    bool canRotatePage() const override;
    bool rotatePageWithAnnotationsInDocument(Okular::Page *page, int pageNumber, int rotationDegrees, bool followPage, Okular::Page **replacementPage, QString *errorText) override;
    bool rotatePageInDocument(Okular::Page *page, int pageNumber, int rotationDegrees, Okular::Page **replacementPage, QString *errorText) override;
    bool canSetPageSize() const override;
    bool adjustPageBoxInDocument(Okular::Page *page, int pageNumber, double x1Delta, double y1Delta, double x2Delta, double y2Delta, Okular::Page **replacementPage, QString *errorText) override;

    // Okular::PdfLinkEditingInterface
    bool canEditPdfLinks() const override;
    bool setNamedDestination(const QString &name, int pageNumber, double normalizedX, double normalizedY, QString *errorText) override;
    bool renameNamedDestination(const QString &oldName, const QString &newName, QString *errorText) override;
    bool deleteNamedDestination(const QString &name, QString *errorText) override;
    bool editInternalLinkDestination(Okular::Page *sourcePage,
                                     double linkLeft,
                                     double linkTop,
                                     double linkRight,
                                     double linkBottom,
                                     const QString &destinationName,
                                     int destinationPageNumber,
                                     double destinationX,
                                     double destinationY,
                                     QString *errorText) override;
    bool createInternalLink(Okular::Page *sourcePage,
                            double linkLeft,
                            double linkTop,
                            double linkRight,
                            double linkBottom,
                            const QString &destinationName,
                            int destinationPageNumber,
                            double destinationX,
                            double destinationY,
                            QString *errorText,
                            Okular::PdfLinkAppearance appearance) override;
    bool editExternalLinkDestination(Okular::Page *sourcePage, double linkLeft, double linkTop, double linkRight, double linkBottom, const QString &url, QString *errorText) override;
    bool createExternalLink(Okular::Page *sourcePage, double linkLeft, double linkTop, double linkRight, double linkBottom, const QString &url, QString *errorText) override;
    bool editRemoteLinkDestination(Okular::Page *sourcePage,
                                   double linkLeft,
                                   double linkTop,
                                   double linkRight,
                                   double linkBottom,
                                   const QString &fileName,
                                   int destinationPageNumber,
                                   double destinationX,
                                   double destinationY,
                                   QString *errorText,
                                   const QString &remoteDestinationName) override;
    bool createRemoteLink(Okular::Page *sourcePage,
                          double linkLeft,
                          double linkTop,
                          double linkRight,
                          double linkBottom,
                          const QString &fileName,
                          int destinationPageNumber,
                          double destinationX,
                          double destinationY,
                          QString *errorText,
                          const QString &remoteDestinationName) override;
    bool editPdfLinkRectangle(Okular::Page *sourcePage,
                              double oldLinkLeft,
                              double oldLinkTop,
                              double oldLinkRight,
                              double oldLinkBottom,
                              double newLinkLeft,
                              double newLinkTop,
                              double newLinkRight,
                              double newLinkBottom,
                              QString *errorText,
                              Okular::PdfLinkAppearance appearance) override;
    bool deletePdfLink(Okular::Page *sourcePage, double linkLeft, double linkTop, double linkRight, double linkBottom, QString *errorText) override;
    bool refreshPageLinks(Okular::Page *page, int pageNumber, QString *errorText) override;

    // Okular::PdfOcrInterface
    bool canPerformEnglishOcr() const override;
    bool readOcrTextLayer(int pageNumber, QList<Okular::OcrTextWord> *words, QString *errorText) override;
    bool replaceOcrTextLayer(int pageNumber, const QList<Okular::OcrTextWord> &words, QString *errorText) override;
    Okular::OcrResult saveWithEnglishOcr(const QString &sourceFileName,
                                         const QString &outputFileName,
                                         const QList<int> &pageNumbers,
                                         bool skipPagesWithText,
                                         const Okular::PdfOcrInterface::ProgressCallback &progress) override;

    // Okular::ReadingViewEditingInterface
    bool canEditReadingViews() const override;
    bool canGenerateReadingViews() const override;
    Okular::ReadingViewGenerationResult generateReadingViews(const QString &sourceFileName,
                                                             const QList<int> &pageNumbers,
                                                             const Okular::ReadingViewEditingInterface::ProgressCallback &progress) override;
    QList<Okular::ReadingView> readingViews(int pageNumber, QString *errorText) const override;
    bool setReadingViews(int pageNumber, const QList<Okular::ReadingView> &views, QString *errorText) override;
    QString readingViewPageToken(int pageNumber) const override;
    int readingViewPageForToken(const QString &token) const override;

    // Okular::NumberedCalloutNumberingInterface
    bool canEditNumberedCalloutNumbering() const override;
    bool validateNumberedCalloutLabel(const QString &label, QString *errorText) const override;
    QString numberedCalloutNumberingJson() const override;
    bool setNumberedCalloutNumberingJson(const QString &json, QString *errorText) override;

protected:
    PdfEngine *engine() const;
    PdfConfigurable *configurable() const;
    PdfPrintConfigurator *printConfigurator() const;
    PdfPrintPreviewProvider *printPreviewProvider() const;

    /**
     * Page-number mapping. Okular speaks zero-based logical page indices; the
     * narrow PdfDocument and every capability speak zero-based native page
     * indices. The two orders are kept by Document and by the engine
     * respectively and must never be assumed equal, not even through
     * Okular::Page::number(): a page model's number is the current logical
     * index, not its native index.
     *
     * nativePageForPage() returns the native index of a page model, or -1 when
     * the model does not belong to the open document.
     * nativePageForLogicalPage() resolves the model Document holds at a logical
     * index first, so it always follows the live page order.
     * logicalPageForNative() is the inverse of that lookup and returns -1 for a
     * native page that is not part of the current logical order.
     *
     * The helpers are protected instead of private so the generator tests can
     * assert the mapping without going through a rendering side effect.
     */
    int nativePageForPage(Okular::Page *page) const;
    int nativePageForLogicalPage(int logicalPage) const;
    int logicalPageForNative(int nativePage) const;

    bool doCloseDocument() override;
    Okular::TextPage *textPage(Okular::TextRequest *request) override;
    Okular::Generator::SwapBackingFileResult swapBackingFile(const QString &newFileName, QList<Okular::Page *> &newPagesVector) override;
    int pageNumberForPageToken(const QString &token) const override;

private:
    /** One-based logical page number to a zero-based native index, or -1. */
    int nativePageForOneBasedPage(int pageNumber) const;
    /**
     * Resolves the logical "insert after" position used by Okular (-1 prepends)
     * to the native page to insert after. Fails instead of guessing when the
     * position is outside the current logical order.
     */
    bool resolveInsertAfterPage(int logicalInsertAfterPage, int *nativeAfter, QString *errorText) const;
    /** Model of a native page, or null; used where no logical index is at hand. */
    Okular::Page *pageModelForNative(int nativePage) const;
    /** Rebuilds the mapping after a backing-file swap when Document owns it. */
    void syncPageMapping() const;

    /**
     * Narrow load through the mandatory PdfDocument. Opens the document,
     * requires a non-zero page count and builds every page model with
     * createPageModel(). The base owns the resulting list and hands it to
     * Okular::Document. A null document, an open failure, a zero page count or
     * a missing page model fails the load: there is no second load path. Never
     * returns a page list the caller does not own.
     */
    Okular::Document::OpenResult loadDocumentThroughEngine(PdfDocument *pdfDocument,
                                                           const QString &fileName,
                                                           const QByteArray &fileData,
                                                           bool fromData,
                                                           QList<Okular::Page *> &pagesVector,
                                                           const QString &password);

    class Private;
    std::unique_ptr<Private> d;
};

} // namespace Pdf
} // namespace Mengshee
