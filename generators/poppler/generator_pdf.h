/*
    SPDX-FileCopyrightText: 2004-2008 Albert Astals Cid <aacid@kde.org>
    SPDX-FileCopyrightText: 2004 Enrico Ros <eros.kde@email.it>

    Work sponsored by the LiMux project of the city of Munich:
    SPDX-FileCopyrightText: 2017 Klarälvdalens Datakonsult AB a KDAB Group company <info@kdab.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef _OKULAR_GENERATOR_PDF_H_
#define _OKULAR_GENERATOR_PDF_H_

// #include "synctex/synctex_parser.h"

#include <poppler-qt6.h>
#include <poppler-version.h>

#include <QBitArray>
#include <QPointer>
#include <QVector>

#include <core/annotations.h>
#include <core/document.h>
#include <core/generator.h>
#include <core/numberedcalloutnumberinginterface.h>
#include <core/printoptionswidget.h>
#include <core/readingvieweditinginterface.h>
#include <interfaces/configinterface.h>
#include <interfaces/printinterface.h>
#include <interfaces/saveinterface.h>

#include <memory>
#include <unordered_map>

class PDFOptionsPage;
class PopplerAnnotationProxy;

/**
 * @short A generator that builds contents from a PDF document.
 *
 * All Generator features are supported and implemented by this one.
 * Internally this holds a reference to xpdf's core objects and provides
 * contents generation using the PDFDoc object and a couple of OutputDevices
 * called Okular::OutputDev and Okular::TextDev (both defined in gp_outputdev.h).
 *
 * For generating page contents we tell PDFDoc to render a page and grab
 * contents from out OutputDevs when rendering finishes.
 *
 */
class PDFGenerator : public Okular::Generator, public Okular::ConfigInterface, public Okular::PrintInterface, public Okular::SaveInterface, public Okular::PageInsertionInterface, public Okular::PdfLinkEditingInterface, public Okular::PdfOcrInterface, public Okular::ReadingViewEditingInterface, public Okular::NumberedCalloutNumberingInterface
{
    Q_OBJECT
    Q_INTERFACES(Okular::Generator)
    Q_INTERFACES(Okular::ConfigInterface)
    Q_INTERFACES(Okular::PrintInterface)
    Q_INTERFACES(Okular::SaveInterface)

public:
    PDFGenerator(QObject *parent, const QVariantList &args);
    ~PDFGenerator() override;

    // [INHERITED] load a document and fill up the pagesVector
    Okular::Document::OpenResult loadDocumentWithPassword(const QString &filePath, QList<Okular::Page *> &pagesVector, const QString &password) override;
    Okular::Document::OpenResult loadDocumentFromDataWithPassword(const QByteArray &fileData, QList<Okular::Page *> &pagesVector, const QString &password) override;
    void loadPages(QList<Okular::Page *> &pagesVector, int rotation = -1, bool clear = false);
    // [INHERITED] document information
    Okular::DocumentInfo generateDocumentInfo(const QSet<Okular::DocumentInfo::Key> &keys) const override;
    const Okular::DocumentSynopsis *generateDocumentSynopsis() override;
    bool setDocumentSynopsis(const Okular::DocumentSynopsis &synopsis, QString *errorText) override;
    Okular::FontInfo::List fontsForPage(int page) override;
    const QList<Okular::EmbeddedFile *> *embeddedFiles() const override;
    PageLayout defaultPageLayout() const override;
    bool defaultPageContinuous() const override;
    PageSizeMetric pagesSizeMetric() const override
    {
        return Pixels;
    }
    QAbstractItemModel *layersModel() const override;
    Okular::BackendOpaqueAction::OpaqueActionResult opaqueAction(const Okular::BackendOpaqueAction *action) override;

    // [INHERITED] document information
    bool isAllowed(Okular::Permission permission) const override;

    // [INHERITED] perform actions on document / pages
    QImage image(Okular::PixmapRequest *request) override;
    bool canRenderToImage() const override;
    QImage renderToImage(int page, int dpi, bool includeAnnotations, QString *error = nullptr) override;
    /**
     * Worker-thread rendering for the narrow engine. Uses the page model for
     * size/rotation and locks the Poppler document; no QObject/Document access.
     */
    QImage renderPageForWorker(Okular::Page *page, int width, int height, const QRect &region, bool includeAnnotations, bool applyViewRotation, const std::function<bool()> &shouldAbort, QString *error = nullptr);

    // [INHERITED] print page using an already configured kprinter
    Okular::Document::PrintError print(QPrinter &printer) override;

    // [INHERITED] reply to some metadata requests
    QVariant metaData(const QString &key, const QVariant &option) const override;

    // [INHERITED] reparse configuration
    bool reparseConfig() override;
    void addPages(KConfigDialog *) override;

    // [INHERITED] text exporting
    Okular::ExportFormat::List exportFormats() const override;
    bool exportTo(const QString &fileName, const Okular::ExportFormat &format) override;
    bool canExportFlattenedPdf() const override;
    bool exportFlattenedPdf(const QString &fileName, QString *errorText, int *flattenedAnnotations, int *preservedAnnotations) override;

    // [INHERITED] print interface
    Okular::PrintOptionsWidget *printConfigurationWidget() const override;

    // [INHERITED] save interface
    bool supportsOption(SaveOption) const override;
    bool save(const QString &fileName, SaveOptions options, QString *errorText) override;
    Okular::AnnotationProxy *annotationProxy() const override;
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
    bool rotatePageInDocument(Okular::Page *page, int pageNumber, int rotationDegrees, Okular::Page **replacementPage, QString *errorText) override;
    bool canSetPageSize() const override;
    bool adjustPageBoxInDocument(Okular::Page *page, int pageNumber, double x1Delta, double y1Delta, double x2Delta, double y2Delta, Okular::Page **replacementPage, QString *errorText) override;
    bool canEditNumberedCalloutNumbering() const override;
    bool validateNumberedCalloutLabel(const QString &label, QString *errorText) const override;
    QString numberedCalloutNumberingJson() const override;
    bool setNumberedCalloutNumberingJson(const QString &json, QString *errorText) override;
    bool canEditReadingViews() const override;
    bool canGenerateReadingViews() const override;
    Okular::ReadingViewGenerationResult generateReadingViews(const QString &sourceFileName,
                                                             const QList<int> &pageNumbers,
                                                             const Okular::ReadingViewEditingInterface::ProgressCallback &progress) override;
    QList<Okular::ReadingView> readingViews(int pageNumber, QString *errorText) const override;
    bool setReadingViews(int pageNumber, const QList<Okular::ReadingView> &views, QString *errorText) override;
    QString readingViewPageToken(int pageNumber) const override;
    int readingViewPageForToken(const QString &token) const override;
    /**
     * Page identity of an explicit link destination: the page object reference
     * ("number generation") the PDF itself uses and that
     * createLinkFromPopplerLink() writes into Okular::GotoAction::pageToken().
     * Empty for an invalid page or when no document is open.
     */
    QString pageTokenForNativePage(int nativePage) const;
    /**
     * Resolves a token produced by pageTokenForNativePage() to a zero-based
     * native page index, or -1. This is the native-page form of the private
     * Generator::pageNumberForPageToken() override, which maps the result
     * through the live logical page order for the legacy path.
     */
    int nativePageForPageToken(const QString &token) const;
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
    bool canPerformEnglishOcr() const override;
    bool readOcrTextLayer(int pageNumber, QList<Okular::OcrTextWord> *words, QString *errorText) override;
    bool replaceOcrTextLayer(int pageNumber, const QList<Okular::OcrTextWord> &words, QString *errorText) override;
    Okular::OcrResult saveWithEnglishOcr(const QString &sourceFileName,
                                         const QString &outputFileName,
                                         const QList<int> &pageNumbers,
                                         bool skipPagesWithText,
                                         const Okular::PdfOcrInterface::ProgressCallback &progress) override;

    bool canSign() const override;
    std::pair<Okular::SigningResult, QString> sign(const Okular::NewSignatureData &oData, const QString &rFilename) override;

    Okular::CertificateStore *certificateStore() const override;

    QByteArray requestFontData(const Okular::FontInfo &font) override;

    /**
     * Phase-1 narrow read support for the engine module.
     *
     * openDocumentForNarrowLoad()/openDocumentFromDataForNarrowLoad() open and
     * authenticate the PDF without building Okular::Page models. The caller
     * creates them one at a time with createPageModelForNarrowLoad() and owns
     * them. The legacy loadDocument* methods keep building every model through
     * init()/loadPages() and their behavior is unchanged.
     */
    Okular::Document::OpenResult openDocumentForNarrowLoad(const QString &filePath, const QString &password);
    Okular::Document::OpenResult openDocumentFromDataForNarrowLoad(const QByteArray &fileData, const QString &password);

    /** Native page count, or 0 when no document is open. */
    int nativePageCount() const;

    /**
     * Geometry of one native page in PDF points.
     *
     * width/height are Poppler::Page::pageSizeF(), that is the page's /CropBox
     * with its own /Rotate already applied, so they are the displayed page
     * size. rotation is /Rotate normalized to 0, 90, 180 or 270.
     */
    bool nativePageGeometry(int nativePage, double *width, double *height, int *rotation) const;

    /**
     * Builds one Okular::Page model for native page @p nativePage, including
     * annotations, link rectangles, form fields, transition and page actions.
     * Ownership of the returned model passes to the caller; the generator
     * never stores or frees it.
     */
    Okular::Page *createPageModelForNarrowLoad(int nativePage);

    /**
     * Text of one native page. Unlike textPage(TextRequest *), this does not go
     * through the logical page order, because PdfDocument page numbers are
     * native indices.
     */
    Okular::TextPage *textPageForNumber(int nativePage);

    /**
     * Capability bridge for the narrow front end. Refreshes the link objects of
     * one native page without going through the logical page order; the
     * public Okular interface keeps its logical mapping above.
     */
    bool refreshPageLinksForEngine(Okular::Page *sourcePage, int nativePageNumber, QString *errorText);

    static void okularToPoppler(const Okular::NewSignatureData &oData, Poppler::PDFConverter::NewSignatureData *pData);

    enum DocumentAdditionalActionType {
        CloseDocument,
        SaveDocumentStart,
        SaveDocumentFinish,
        PrintDocumentStart,
        PrintDocumentFinish,
    };
    Okular::Action *additionalDocumentAction(Okular::Document::DocumentAdditionalActionType type) override;

protected:
    SwapBackingFileResult swapBackingFile(QString const &newFileName, QList<Okular::Page *> &newPagesVector) override;
    bool doCloseDocument() override;
    Okular::TextPage *textPage(Okular::TextRequest *request) override;

private:
    Okular::Document::OpenResult init(QList<Okular::Page *> &pagesVector, const QString &password);
    // Shared by init() (legacy, builds every page model) and
    // initForNarrowLoad() (opens and authenticates only).
    Okular::Document::OpenResult prepareOpen(const QString &password, int *pageCount);
    void finishOpen();
    Okular::Document::OpenResult initForNarrowLoad(const QString &password);
    // Signatures that belong to no page; loadPages() puts them on page 0.
    QList<Okular::FormField *> pageLessSignatureFields();
    int nativePageForLogicalPage(int logicalPage) const;
    bool pageOrderIsIdentity() const;
    std::vector<int> oneBasedPageOrder() const;
    // additionalFormFields are adopted by the created page, like the fields of
    // the native page. They let the narrow loader put page-less signatures on
    // page 0 before setFormFields() takes ownership for the first time.
    Okular::Page *createPageModel(int logicalPageNumber, QList<Okular::FormField *> additionalFormFields = {});
    void forgetPageModel(Okular::Page *page);
    void resetPageTopologyCaches();
    quint64 rememberLivePageState(const std::shared_ptr<void> &state);
    bool refreshPdfLinkObjects(Okular::Page *sourcePage, int nativePageNumber, QString *errorText);
    bool refreshPageLinks(Okular::Page *page, int pageNumber, QString *errorText) override;
    int pageNumberForPageToken(const QString &pageToken) const override;

    // create the document synopsis hierarchy
    void addSynopsisChildren(const QList<Poppler::OutlineItem> &outlineItems, QDomNode *parentDestination);
    // fetch annotations from the pdf file and add they to the page
    void addAnnotations(Poppler::Page *popplerPage, Okular::Page *page);
    // fetch the transition information and add it to the page
    void addTransition(Poppler::Page *pdfPage, Okular::Page *page);
    // fetch the poppler page form fields
    QList<Okular::FormField *> getFormFields(Poppler::Page *popplerPage);

    Okular::TextPage *abstractTextPage(const std::vector<std::unique_ptr<Poppler::TextBox>> &text, double height, double width, int rot);

    void resolveMediaLinkReferences(Okular::Page *page);
    void resolveMediaLinkReference(Okular::Action *action);

    bool setDocumentRenderHints();

    // poppler dependent stuff
    std::unique_ptr<Poppler::Document> pdfdoc;

    void xrefReconstructionHandler();

    // misc variables for document info and synopsis caching
    QString documentFilePath;
    bool docSynopsisDirty;
    bool xrefReconstructed;
    bool hasVisibleOverprint;
    Okular::DocumentSynopsis docSyn;
    mutable bool docEmbeddedFilesDirty;
    mutable QList<Okular::EmbeddedFile *> docEmbeddedFiles;
    int nextFontPage;
    PopplerAnnotationProxy *annotProxy;
    mutable std::unique_ptr<Okular::CertificateStore> certStore;
    // the hash below only contains annotations that were present on the file at open time
    // this is enough for what we use it for
    QHash<Okular::Annotation *, Poppler::Annotation *> annotationsOnOpenHash;

    QBitArray rectsGenerated;
    QVector<int> m_pageOrder;
    quint64 m_nextLivePageEditId = 1;
    QHash<quint64, std::shared_ptr<void>> m_livePageStates;

    QPointer<PDFOptionsPage> pdfOptionsPage;

    bool documentHasPassword = false;
    QHash<int, Okular::Action *> m_additionalDocumentActions;
    void setAdditionalDocumentAction(Okular::Document::DocumentAdditionalActionType type, Okular::Action *action);
    std::shared_ptr<void> m_signatureSettingsHandle;
};

#endif
