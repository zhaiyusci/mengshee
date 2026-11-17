// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "mengshee_pdf_runtime_export.h"
#include <core/generator.h>
#include <core/numberedcalloutnumberinginterface.h>
#include <core/readingvieweditinginterface.h>
#include <interfaces/saveinterface.h>
#include <memory>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

class MuPDFAnnotationProxy;

class MENGSHEE_PDF_RUNTIME_EXPORT MuPDFGenerator : public Okular::Generator, public Okular::PrintPreviewInterface,
                       public Okular::SaveInterface,
                       public Okular::PageInsertionInterface,
                       public Okular::ReadingViewEditingInterface,
                       public Okular::NumberedCalloutNumberingInterface,
                       public Okular::PdfLinkEditingInterface,
                       public Okular::PdfOcrInterface
{
    Q_OBJECT
    Q_INTERFACES(Okular::Generator Okular::SaveInterface)
public:
    MuPDFGenerator(QObject *parent, const QVariantList &args);
    ~MuPDFGenerator() override;
    Okular::Document::OpenResult loadDocumentWithPassword(const QString &, QList<Okular::Page *> &, const QString &) override;
    Okular::Document::OpenResult loadDocumentFromDataWithPassword(const QByteArray &, QList<Okular::Page *> &, const QString &) override;

    /**
     * Transitional phase-1 narrow-load support for the engine module.
     *
     * Opens and authenticates the document without building Okular::Page
     * models; the caller creates them one at a time with createPageModel() and
     * owns them. The legacy loadDocument* methods keep building every model
     * themselves through finishOpen().
     */
    Okular::Document::OpenResult openDocumentForNarrowLoad(const QString &path, const QString &password);
    Okular::Document::OpenResult openDocumentFromDataForNarrowLoad(const QByteArray &data, const QString &password);

    /**
     * Builds one Okular::Page model for native page @p index: loads the native
     * page into the generator cache and attaches its annotations, links and
     * form fields. Returns nullptr for an invalid index or a MuPDF error.
     * Ownership of the returned model passes to the caller.
     */
    Okular::Page *createPageModel(int index);

    SwapBackingFileResult swapBackingFile(const QString &, QList<Okular::Page *> &) override;
    Okular::DocumentInfo generateDocumentInfo(const QSet<Okular::DocumentInfo::Key> &) const override;
    Okular::FontInfo::List fontsForPage(int) override;
    QByteArray requestFontData(const Okular::FontInfo &) override;
    const QList<Okular::EmbeddedFile *> *embeddedFiles() const override;
    PageLayout defaultPageLayout() const override;
    bool defaultPageContinuous() const override;
    PageSizeMetric pagesSizeMetric() const override
    {
        return Points;
    }
    const Okular::DocumentSynopsis *generateDocumentSynopsis() override;
    bool setDocumentSynopsis(const Okular::DocumentSynopsis &, QString *) override;
    bool canExportFlattenedPdf() const override;
    bool exportFlattenedPdf(const QString &, QString *, int *, int *) override;
    QVariant metaData(const QString &key, const QVariant &option) const override;
    bool isAllowed(Okular::Permission permission) const override;
    Okular::Document::PrintError print(QPrinter &printer) override;
    Okular::PrintPreviewRenderer createPrintPreviewRenderer(QString *error) override;
    QImage image(Okular::PixmapRequest *) override;
    bool canRenderToImage() const override
    {
        return true;
    }
    QImage renderToImage(int page, int dpi, bool includeAnnotations, QString *error = nullptr) override;
    /**
     * Render only @p region of the page at the full-page scale described by
     * @p width and @p height. Used by the narrow tile path so one tile does
     * not allocate the whole zoomed page.
     */
    QImage renderRegionForWorker(int page, int width, int height, const QRect &region, bool includeAnnotations, const std::function<bool()> &shouldAbort, QString *error = nullptr);
    bool supportsOption(SaveOption) const override;
    bool save(const QString &, SaveOptions, QString *) override;
    Okular::AnnotationProxy *annotationProxy() const override;
    bool canInsertBlankPage() const override;
    bool insertBlankPageInDocument(int insertAfterPageNumber, double width, double height, Okular::Page **insertedPage, quint64 *editId, QString *errorText, const QString &heading = QString()) override;
    bool canInsertPageFromPdf() const override;
    bool duplicatePageInDocument(int pageNumber, bool resolveDestinationConflicts, Okular::Page **insertedPage, quint64 *editId, QString *errorText) override;
    bool insertPdfPageInDocument(int insertAfterPageNumber, const QString &insertedFileName, int pageToInsert, bool resolveDestinationConflicts, Okular::Page **insertedPage, quint64 *editId, QString *errorText) override;
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

    bool canEditNumberedCalloutNumbering() const override;
    bool validateNumberedCalloutLabel(const QString &label, QString *errorText) const override;
    QString numberedCalloutNumberingJson() const override;
    bool setNumberedCalloutNumberingJson(const QString &json, QString *errorText) override;
    bool canEditReadingViews() const override;
    bool canGenerateReadingViews() const override;
    Okular::ReadingViewGenerationResult generateReadingViews(const QString &sourceFileName, const QList<int> &pageNumbers, const Okular::ReadingViewEditingInterface::ProgressCallback &progress) override;
    QList<Okular::ReadingView> readingViews(int pageNumber, QString *errorText) const override;
    bool setReadingViews(int pageNumber, const QList<Okular::ReadingView> &views, QString *errorText) override;
    QString readingViewPageToken(int pageNumber) const override;
    int readingViewPageForToken(const QString &token) const override;
    /**
     * Public narrow-interface view of the legacy page-token resolver.
     *
     * Okular calls this protected Generator hook for GotoAction page tokens;
     * the MuPDF engine module also forwards it through PdfPageIdentity so the
     * shared front end can resolve the same token format. The token is the
     * per-page /MengsheeViews /PageId UUID written by setReadingViews(); a
     * page without that metadata has no identity and resolves to -1.
     */
    int pageNumberForPageToken(const QString &token) const override
    {
        return readingViewPageForToken(token);
    }

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
                            Okular::PdfLinkAppearance appearance = {}) override;
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
                                   const QString &remoteDestinationName = QString()) override;
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
                          const QString &remoteDestinationName = QString()) override;
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
                              Okular::PdfLinkAppearance appearance = {}) override;
    bool deletePdfLink(Okular::Page *sourcePage, double linkLeft, double linkTop, double linkRight, double linkBottom, QString *errorText) override;

    bool refreshPageLinks(Okular::Page *, int, QString *) override;
    bool canPerformEnglishOcr() const override;
    bool readOcrTextLayer(int pageNumber, QList<Okular::OcrTextWord> *words, QString *errorText) override;
    bool replaceOcrTextLayer(int pageNumber, const QList<Okular::OcrTextWord> &words, QString *errorText) override;
    Okular::OcrResult saveWithEnglishOcr(const QString &sourceFileName, const QString &outputFileName, const QList<int> &pageNumbers, bool skipPagesWithText, const Okular::PdfOcrInterface::ProgressCallback &progress) override;

protected:
    bool doCloseDocument() override;
    Okular::TextPage *textPage(Okular::TextRequest *) override;
    /** Extracts the text layer of a native page without needing a page model. */
    Okular::TextPage *textPageForNumber(int number);
    /** Native page count for the narrow PdfDocument path. */
    int nativePageCount() const;
    /** Native page geometry in PDF points for the narrow PdfDocument path. */
    bool nativePageGeometry(int index, double *width, double *height, int *rotation) const;
    /**
     * True when the narrow page cache already holds native page @p index.
     * createPageModel() fills it; link refreshing and OCR editing use the
     * cached native page and must not index the list blindly when a caller
     * skipped model creation.
     */
    bool hasNativePage(int index) const;

private:
    friend class MuPDFAnnotationProxy;
    friend class MuPDFFormAccess;
    friend class MuPDFEmbeddedFile;
    Okular::Document::OpenResult openFromData(const QByteArray &data);
    Okular::Document::OpenResult authenticateOpen(const QString &password);
    Okular::Document::OpenResult finishOpen(QList<Okular::Page *> &, const QString &);
    QImage render(int page, int width, int height, bool annotations, QString *error, const QRect &region = {}, fz_cookie *cookie = nullptr);
    void clearDisplayList();
    fz_display_list *pageDisplayList(int number, pdf_page *page, fz_cookie *cookie);
    void clearAnnotationLists();
    QHash<pdf_obj *, fz_display_list *> m_annotationLists;
    // Keep only the most recently rendered page, not every visited page.
    fz_display_list *m_displayList = nullptr;
    int m_displayListPage = -1;
    fz_context *m_context = nullptr;
    pdf_document *m_document = nullptr;
    QByteArray m_data;
    bool m_previewOriginalDataValid = true;
    std::unique_ptr<MuPDFAnnotationProxy> m_annotations;
    QList<pdf_page *> m_pages;
    QString m_editError;
    QHash<quint64, pdf_obj *> m_detached;
    quint64 m_nextEdit = 1;
    Okular::Page *createPage(int index);
    bool operation(const std::function<void()> &body, QString *error, bool changesDocument = true);
    bool readOperation(const std::function<void()> &body, QString *error) { return operation(body, error, false); }
    pdf_obj *linkDestination(const QString &, int, double, double, bool remote = false);
    bool updateLink(Okular::Page *, const QRectF &, bool create, const char *kind, const QString &, int, double, double, QString *, const QString &remoteName = {}, Okular::PdfLinkAppearance appearance = {});
    void loadLinks(pdf_page *native, Okular::Page *page);
    void renderLinkAppearance(pdf_page *, pdf_obj *, fz_device *, bool printing);
    void renderAnnotations(pdf_page *, fz_pixmap *, fz_matrix, fz_cookie *);
    void reportAnnotationError(const QString &);
    bool m_annotationWarningShown = false;
    void loadForms(pdf_page *, Okular::Page *);
    /**
     * Field ids in the order of the AcroForm /CO calculation array. The core
     * Document executes the CalculateField actions of these fields; this is
     * the same contract the Poppler backend fulfils.
     */
    QList<int> formCalculateOrder() const;
    Okular::DocumentSynopsis m_synopsis;
    mutable QList<Okular::EmbeddedFile *> m_embeddedFiles;
    mutable bool m_embeddedLoaded = false;
};
