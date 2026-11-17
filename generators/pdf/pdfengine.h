// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Draft: the unified PDF engine boundary for the MuPDF and Poppler backends.
//
// This header is not part of the build yet. It is the concrete form of the
// design in docs/pdf-backend-architecture.md and is meant to be reviewed
// before the mechanical extraction starts.
//
// An engine normally lives in a separately loaded module DLL
// (MengsheePdfEngineMuPdf.dll / MengsheePdfEnginePoppler.dll) and is reached
// through the C ABI in pdfenginemodule.h and the selector in
// pdfengineregistry.h. The front end never links a backend library.
//
// Rules for both implementers and callers:
//
//  * The Okular model types (Page, Annotation, Action, TextPage, FontInfo,
//    DocumentInfo, DocumentSynopsis, FormField, OcrTextWord, ReadingView,
//    SaveInterface::SaveOptions, PdfLinkAppearance) are the data currency.
//    No backend type may appear in this header.
//  * Page numbers in this interface are always zero-based *native* PDF page
//    indices. The logical page order shown to the user is owned by
//    PdfGeneratorBase; the engine never sees a logical page number.
//  * Failures return false (or an explicit status enum) and write a
//    user-presentable message into errorText, which may be null. Exceptions
//    must never cross this boundary, including MuPDF's fz_throw.
//  * renderPage() and textPage() may run on a worker thread. Every other
//    method runs on the owning (GUI) thread. The engine is responsible for
//    its own locking; the base class serializes page-mutating calls.
//  * Capability objects are obtained through PdfEngine's getters, never with
//    a cross-module dynamic_cast. A concrete engine returns `this` or
//    nullptr, which keeps the interface usable across the module DLL boundary.

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QRect>
#include <QSet>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <core/action.h>
#include <core/annotations.h>
#include <core/area.h>
#include <core/document.h>
#include <core/fontinfo.h>
#include <core/generator.h>
#include <core/global.h>
#include <core/printpreviewrenderer.h>
#include <core/readingview.h>
#include <core/readingvieweditinginterface.h>
#include <interfaces/saveinterface.h>

#include <functional>
#include <memory>
#include <utility>

class KConfigDialog;
class QAbstractItemModel;
class QPrinter;

namespace Okular
{
class AnnotationProxy;
class CertificateStore;
class EmbeddedFile;
class Page;
class PrintOptionsWidget;
class TextPage;
}

namespace Mengshee
{
namespace Pdf
{

class PdfEngine;

/**
 * Destroys an engine through the owning module's destroy function. Never use
 * `delete` on a PdfEngine that came from PdfEngineRegistry: the module and the
 * front end are separate DLLs and the module owns the allocation.
 */
struct EngineDeleter {
    void (*destroy)(void *) = nullptr;

    void operator()(PdfEngine *engine) const noexcept;
};

using EnginePtr = std::unique_ptr<PdfEngine, EngineDeleter>;

/**
 * Stable identifier of a PDF engine. The module descriptor and the runtime
 * selection use the same string. Keep these values stable: they appear in the
 * user configuration, in test invocations and in module metadata.
 */
using EngineId = QString;
inline constexpr char MuPdfEngineId[] = "mupdf";
inline constexpr char PopplerEngineId[] = "poppler";

/**
 * Engine-neutral geometry of one page. All lengths are PDF points (1/72 in).
 *
 * cropBox is the visible box after applying the page's own /CropBox and is
 * what Okular::Page is built from. A NULL /CropBox means mediaBox.
 */
struct PageGeometry {
    QSizeF mediaBox;
    QSizeF cropBox;
    int rotation = 0; ///< /Rotate normalized to 0, 90, 180 or 270.
    QString label;
    double durationSeconds = 0.0;
};

/**
 * A render request in output pixels, mirroring Okular::PixmapRequest.
 *
 * width/height are the final pixel dimensions of the page. region is a tile
 * inside those dimensions; an invalid/null region means the whole page. The
 * engine maps points to pixels itself so that both renderers keep their
 * current sampling behavior.
 */
struct RenderRequest {
    int page = 0;
    /**
     * Full-page output coordinate space. For a tile request this can be much
     * larger than the allocated image; @ref region is the actual tile. Size /
     * memory guards must use region when it is non-null.
     */
    int width = 0;
    int height = 0;
    QRect region;
    bool includeAnnotations = false;
    bool forPrinting = false;
    bool applyViewRotation = false;
    Okular::Page *pageModel = nullptr;
    std::function<bool()> shouldAbort;
};

struct RenderResult {
    QImage image;
    QString errorText;
    /** True when image is already the requested tile region, not the page. */
    bool regionApplied = false;
};

enum class OpenStatus {
    Success,
    Error,
    NeedsPassword,
};

/**
 * Read side of a PDF document: identity, geometry, rendering, text and the
 * page/link/form state that becomes part of the Okular::Page model.
 */
class PdfDocument
{
public:
    virtual ~PdfDocument() = default;

    virtual OpenStatus openFile(const QString &fileName, const QString &password) = 0;
    virtual OpenStatus openData(const QByteArray &data, const QString &password) = 0;
    virtual void close() = 0;

    /**
     * Per-page model creation. PdfGeneratorBase calls this for every native
     * page after openFile()/openData() succeeded and owns the returned model
     * (it hands the whole list to Okular::Document).
     *
     * The engine must load the page's native state and attach the data that
     * belongs to the Okular::Page model (annotations, links, forms, transition,
     * page actions). Returning nullptr reports failure: the caller discards the
     * partial model list, closes the document and fails the load. Default:
     * unsupported, so an engine must implement this to be usable.
     */
    virtual Okular::Page *createPageModel(int nativePage)
    {
        Q_UNUSED(nativePage)
        return nullptr;
    }

    /** Native page count, independent of whether models have been created. */
    virtual int pageCount() const = 0;
    virtual PageGeometry pageGeometry(int nativePage) const = 0;

    virtual RenderResult renderPage(const RenderRequest &request) = 0;
    virtual Okular::TextPage *textPage(int nativePage) = 0;

    /**
     * Fills the parts of an already-created Okular::Page that come from the
     * PDF page object. The base class creates the page from pageGeometry();
     * the engine only attaches data. Implementations must not free target.
     */
    virtual void loadAnnotations(int nativePage, Okular::Page *target) = 0;
    virtual void loadLinks(int nativePage, Okular::Page *target) = 0;
    virtual void loadForms(int nativePage, Okular::Page *target) = 0;
    virtual void loadTransition(int nativePage, Okular::Page *target) = 0;
    /**
     * Draft note: page and document actions can carry GoTo page numbers. The
     * current implementations build Okular::Action themselves; the target is
     * a neutral action description whose page numbers the base remaps. See
     * docs/pdf-backend-architecture.md, semantic normalization item 14.
     */
    virtual void loadPageActions(int nativePage, Okular::Page *target) = 0;

    /** Engine-owned; the base class returns it from Generator::annotationProxy(). */
    virtual Okular::AnnotationProxy *annotationProxy() = 0;

    virtual Okular::DocumentInfo documentInfo(const QSet<Okular::DocumentInfo::Key> &keys) const = 0;
    virtual const Okular::DocumentSynopsis *synopsis() = 0;
    virtual bool setSynopsis(const Okular::DocumentSynopsis &synopsis, QString *errorText) = 0;
    virtual Okular::FontInfo::List fontsForPage(int nativePage) = 0;
    virtual QByteArray fontData(const Okular::FontInfo &font) = 0;
    virtual const QList<Okular::EmbeddedFile *> *embeddedFiles() const = 0;

    virtual bool isAllowed(Okular::Permission permission) const = 0;
    virtual QVariant metaData(const QString &key, const QVariant &option) const = 0;
    virtual Okular::Generator::PageLayout defaultPageLayout() const = 0;
    virtual bool defaultPageContinuous() const = 0;

    // Optional read capabilities. Engines that do not implement them keep the
    // defaults; the base class still exposes the corresponding Okular methods.
    virtual QAbstractItemModel *layersModel() const
    {
        return nullptr;
    }

    virtual Okular::Action *additionalDocumentAction(Okular::Document::DocumentAdditionalActionType)
    {
        return nullptr;
    }

    virtual Okular::BackendOpaqueAction::OpaqueActionResult opaqueAction(const Okular::BackendOpaqueAction *)
    {
        return Okular::BackendOpaqueAction::DoNothing;
    }
};

/**
 * Stable identity of one page inside the engine's native document.
 *
 * A PDF link with an explicit destination names the target page object, not a
 * page number. Okular::GotoAction keeps that identity in pageToken() and
 * resolves it through Generator::pageNumberForPageToken() when the target is
 * needed, so inserting, removing or moving pages cannot leave the link
 * pointing at the wrong page.
 *
 * The token format is private to the engine and must be exactly the one the
 * engine writes into the link actions it builds; the front end and the user
 * never interpret it. pageToken() and nativePageForToken() are inverse for the
 * pages of the currently open document: an invalid or unidentified page yields
 * an empty token, and an unknown or ambiguous token resolves to -1.
 *
 * This is the identity used by link destinations. The Reading View page
 * identity (PdfCustomMetadata::pageToken()) is a different namespace and keeps
 * its own token on engines that write both.
 *
 * nativePage is a zero-based native PDF page index, following the page-number
 * convention of this interface; the base class owns the logical page order.
 */
class PdfPageIdentity
{
public:
    virtual ~PdfPageIdentity() = default;

    /** Page identity of native page @p nativePage, or empty when it has none. */
    virtual QString pageToken(int nativePage) const = 0;

    /** Native page named by @p token, or -1 when it is unknown or ambiguous. */
    virtual int nativePageForToken(const QString &token) const = 0;
};

/**
 * Write side: saving the live document and producing derived PDF files.
 */
class PdfSaver
{
public:
    virtual ~PdfSaver() = default;

    virtual bool supportsOption(Okular::SaveInterface::SaveOption option) const = 0;
    virtual bool save(const QString &fileName, Okular::SaveInterface::SaveOptions options, QString *errorText) = 0;

    /**
     * Points the live document at a copy of itself at newFileName. newPages is
     * filled only when the result is ReloadInternalData.
     */
    virtual Okular::Generator::SwapBackingFileResult swapBackingFile(const QString &newFileName, QList<Okular::Page *> &newPages) = 0;

    virtual bool canExportFlattenedPdf() const = 0;
    virtual bool exportFlattenedPdf(const QString &fileName, QString *errorText, int *flattenedAnnotations, int *preservedAnnotations) = 0;

    // Text export belongs to the Poppler engine today; the defaults keep the
    // MuPDF engine free of an export menu entry.
    virtual Okular::ExportFormat::List exportFormats() const
    {
        return {};
    }

    virtual bool exportTo(const QString &fileName, const Okular::ExportFormat &format)
    {
        Q_UNUSED(fileName)
        Q_UNUSED(format)
        return false;
    }
};

/**
 * Page-sequence editing on native page indices.
 *
 * All insertions take the native page to insert after, where -1 means before
 * the first page. Every operation that can later be undone reports an edit
 * identity; the base class owns the undo/redo map and the Okular::Page models,
 * the engine owns the native page objects and, where needed, an opaque
 * snapshot keyed by that identity.
 *
 * The base class translates the logical page order to native indices before
 * calling these methods and rebuilds the logical order afterwards.
 *
 * Transitional page-model exchange: Okular's page-editing API hands
 * Okular::Page models between the generator and the owning Document. The
 * capability reports the model it created for every insertion or attachment
 * (insertedPage), the retired model for a detach (detachedPage), and the
 * replacement model for rotate/resize (replacementPage). The engine allocates
 * those models exactly as the legacy generator did and the caller becomes
 * their owner; the engine must not store or delete them after returning. For
 * detach the caller already owns the input page and keeps that ownership, so
 * detachedPage simply hands the same model back. Native page indices are
 * reported through insertedNativePage so the base can build its logical page
 * map without relying on Okular::Page::number(). Remove the page-model and
 * native-page out parameters together with the legacy bridge once the base
 * owns page construction.
 *
 * The current backends still take zero-based logical page numbers inside their
 * legacy implementations. The contract keeps native page numbers here; under
 * the base-owned identity page order that the front end enforces today the two
 * are the same index. The modules rely on that invariant (Poppler validates it
 * when it builds a narrow-load page model) and must translate once the base
 * owns a real page order.
 */
class PdfPageEditor
{
public:
    virtual ~PdfPageEditor() = default;

    virtual bool canInsertBlankPage() const = 0;

    /**
     * Inserts a blank page after @p afterNative (-1 prepends). The heading is
     * written at the top of the inserted page as part of the same edit.
     */
    virtual bool insertBlankPage(int afterNative,
                                 const QSizeF &sizePoints,
                                 const QString &heading,
                                 Okular::Page **insertedPage,
                                 int *insertedNativePage,
                                 quint64 *editId,
                                 QString *errorText) = 0;

    virtual bool canInsertPageFromPdf() const = 0;
    virtual bool duplicatePage(int nativePage,
                               bool resolveDestinationConflicts,
                               Okular::Page **insertedPage,
                               int *insertedNativePage,
                               quint64 *editId,
                               QString *errorText) = 0;

    /** @p sourcePage is one-based, like Okular::PageInsertionInterface. */
    virtual bool insertPdfPage(int afterNative,
                               const QString &sourceFileName,
                               int sourcePage,
                               bool resolveDestinationConflicts,
                               Okular::Page **insertedPage,
                               int *insertedNativePage,
                               quint64 *editId,
                               QString *errorText) = 0;

    virtual bool canAppendPdfFile() const
    {
        return false;
    }

    /**
     * Appends every page of @p sourceFileName in source order. The appended
     * native pages continue at the end of the current document, so their
     * indices follow the current native page count.
     */
    virtual bool appendPdfFile(const QString &sourceFileName,
                               bool resolveDestinationConflicts,
                               QList<Okular::Page *> *appendedPages,
                               QList<quint64> *editIds,
                               QString *errorText)
    {
        Q_UNUSED(sourceFileName)
        Q_UNUSED(resolveDestinationConflicts)
        Q_UNUSED(appendedPages)
        Q_UNUSED(editIds)
        clearError(errorText);
        return false;
    }

    virtual bool canCombinePdfFiles() const = 0;
    virtual int pdfPageCount(const QString &fileName, QString *errorText) = 0;
    virtual bool combinePdfFiles(const QStringList &inputFileNames, const QString &outputFileName, bool resolveDestinationConflicts, QString *errorText) = 0;

    virtual bool canDeletePage() const = 0;

    /**
     * Detaches @p nativePage. @p page is the caller-owned model of that page;
     * the legacy implementations use it to drop their annotation bookkeeping
     * and may accept null. On success @p detachedPage receives the model the
     * caller must keep (normally @p page); the engine never deletes it.
     */
    virtual bool detachPage(Okular::Page *page, int nativePage, Okular::Page **detachedPage, quint64 *editId, QString *errorText) = 0;
    virtual bool detachPage(Okular::Page *page, int nativePage, Okular::Page **detachedPage, quint64 editId, QString *errorText) = 0;

    /** Restores a detached page after @p afterNative (-1 prepends). */
    virtual bool attachPage(int afterNative,
                            quint64 editId,
                            Okular::Page **insertedPage,
                            int *insertedNativePage,
                            QString *errorText) = 0;

    virtual bool canMovePage() const = 0;
    /** Live move without serializing and reloading the PDF. */
    virtual bool canMovePageInDocument() const
    {
        return false;
    }
    virtual bool movePage(int fromNative, int toNative, QString *errorText) = 0;

    virtual bool canRotatePage() const = 0;
    /** Plain /Rotate change. */
    virtual bool rotatePage(Okular::Page *page, int nativePage, int rotationDegrees, Okular::Page **replacementPage, QString *errorText) = 0;
    /** Rotates the page and adjusts its annotations; mirrors the legacy variant. */
    virtual bool rotatePageWithAnnotations(Okular::Page *page,
                                           int nativePage,
                                           int rotationDegrees,
                                           bool annotationsFollowPage,
                                           Okular::Page **replacementPage,
                                           QString *errorText) = 0;

    virtual bool canSetPageSize() const = 0;
    virtual bool adjustPageBox(Okular::Page *page,
                               int nativePage,
                               double x1Delta,
                               double y1Delta,
                               double x2Delta,
                               double y2Delta,
                               Okular::Page **replacementPage,
                               QString *errorText) = 0;

protected:
    static void clearError(QString *errorText)
    {
        if (errorText) {
            errorText->clear();
        }
    }
};

/**
 * Named-destination and link-annotation editing.
 *
 * nativePage is the page that owns the link. Areas are in the native page's
 * normalized, unrotated coordinates, matching the current backends. A link
 * destination page inside this document is a native page index; a remote
 * destination page is a page number in the other file and is not resolved
 * here.
 */
class PdfLinkEditor
{
public:
    virtual ~PdfLinkEditor() = default;

    virtual bool canEditLinks() const = 0;

    virtual bool setNamedDestination(const QString &name, int nativePage, double x, double y, QString *errorText) = 0;
    virtual bool renameNamedDestination(const QString &oldName, const QString &newName, QString *errorText) = 0;
    virtual bool deleteNamedDestination(const QString &name, QString *errorText) = 0;

    virtual bool refreshPageLinks(int nativePage, QString *errorText) = 0;

    virtual bool editInternalLinkDestination(int nativePage,
                                             const Okular::NormalizedRect &area,
                                             const QString &destinationName,
                                             int destinationNativePage,
                                             double x,
                                             double y,
                                             QString *errorText) = 0;
    virtual bool createInternalLink(int nativePage,
                                    const Okular::NormalizedRect &area,
                                    const QString &destinationName,
                                    int destinationNativePage,
                                    double x,
                                    double y,
                                    Okular::PdfLinkAppearance appearance,
                                    QString *errorText) = 0;
    virtual bool editExternalLinkDestination(int nativePage, const Okular::NormalizedRect &area, const QString &url, QString *errorText) = 0;
    virtual bool createExternalLink(int nativePage, const Okular::NormalizedRect &area, const QString &url, QString *errorText) = 0;
    virtual bool editRemoteLinkDestination(int nativePage,
                                           const Okular::NormalizedRect &area,
                                           const QString &fileName,
                                           int destinationFilePage,
                                           double x,
                                           double y,
                                           const QString &remoteDestinationName,
                                           QString *errorText) = 0;
    virtual bool createRemoteLink(int nativePage,
                                  const Okular::NormalizedRect &area,
                                  const QString &fileName,
                                  int destinationFilePage,
                                  double x,
                                  double y,
                                  const QString &remoteDestinationName,
                                  QString *errorText) = 0;
    virtual bool editLinkRectangle(int nativePage,
                                   const Okular::NormalizedRect &oldArea,
                                   const Okular::NormalizedRect &newArea,
                                   Okular::PdfLinkAppearance appearance,
                                   QString *errorText) = 0;
    virtual bool deleteLink(int nativePage, const Okular::NormalizedRect &area, QString *errorText) = 0;
};

/**
 * The invisible OCR text layer. Recognition and PDF writing are engine work;
 * page selection, "skip pages that already have text", progress and
 * cancellation policy live in the base class.
 */
class PdfOcr
{
public:
    using ProgressCallback = Okular::PdfOcrInterface::ProgressCallback;

    virtual ~PdfOcr() = default;

    virtual bool canRecognize() const = 0;
    virtual bool readTextLayer(int nativePage, QList<Okular::OcrTextWord> *words, QString *errorText) = 0;
    virtual bool replaceTextLayer(int nativePage, const QList<Okular::OcrTextWord> &words, QString *errorText) = 0;

    virtual Okular::OcrResult saveWithOcr(const QString &sourceFileName,
                                          const QString &outputFileName,
                                          const QList<int> &nativePages,
                                          bool skipPagesWithText,
                                          const ProgressCallback &progress) = 0;
};

/**
 * Mengshee's non-painting document metadata: numbered-callout settings and
 * Reading Views. Key names, JSON schema versions and size limits are shared
 * constants in the front end; the engine only reads and writes the payloads.
 */
class PdfCustomMetadata
{
public:
    virtual ~PdfCustomMetadata() = default;

    virtual bool canEditNumberedCalloutNumbering() const = 0;
    virtual bool validateNumberedCalloutLabel(const QString &label, QString *errorText) const = 0;
    virtual QString numberedCalloutNumberingJson() const = 0;
    virtual bool setNumberedCalloutNumberingJson(const QString &json, QString *errorText) = 0;

    virtual bool canEditReadingViews() const = 0;
    virtual QList<Okular::ReadingView> readingViews(int nativePage, QString *errorText) const = 0;
    virtual bool setReadingViews(int nativePage, const QList<Okular::ReadingView> &views, QString *errorText) = 0;

    virtual bool canGenerateReadingViews() const
    {
        return false;
    }
    virtual Okular::ReadingViewGenerationResult generateReadingViews(const QString &sourceFileName,
                                                                     const QList<int> &nativePages,
                                                                     const Okular::ReadingViewEditingInterface::ProgressCallback &progress)
    {
        Q_UNUSED(sourceFileName)
        Q_UNUSED(nativePages)
        Q_UNUSED(progress)
        Okular::ReadingViewGenerationResult result;
        result.errorText = QStringLiteral("Automatic View generation is not supported by this PDF engine.");
        return result;
    }

    /** Stable page identity, or an empty string when no identity has been written. */
    virtual QString pageToken(int nativePage) const = 0;
    /** Resolves a token to a native page, or -1 when it is unknown or ambiguous. */
    virtual int pageForToken(const QString &token) const = 0;
};

/**
 * Native printing. The Poppler engine prints through Poppler's own device;
 * the MuPDF engine prints through its vector print device.
 */
class PdfPrinter
{
public:
    virtual ~PdfPrinter() = default;

    virtual Okular::Document::PrintError print(QPrinter &printer) = 0;
};

/** Poppler today: the PDF options page in the print dialog. */
class PdfPrintConfigurator
{
public:
    virtual ~PdfPrintConfigurator() = default;

    virtual Okular::PrintOptionsWidget *printConfigurationWidget() const = 0;
};

/** MuPDF today: an immutable snapshot renderer for the print preview. */
class PdfPrintPreviewProvider
{
public:
    virtual ~PdfPrintPreviewProvider() = default;

    virtual Okular::PrintPreviewRenderer createPrintPreviewRenderer(QString *errorText) = 0;
};

/** Poppler today; MuPDF may gain it later without changing this interface. */
class PdfSigner
{
public:
    virtual ~PdfSigner() = default;

    virtual bool canSign() const = 0;
    /**
     * nativePage is the already-resolved page of data.page(); the engine must
     * not interpret data.page() as a native index.
     *
     * Transitional note: PdfGeneratorBase owns no logical page order yet, so
     * the page it resolves is the Okular model page number. Under the current
     * identity order that is also the native index; the engine's legacy
     * implementation still applies its own logical-to-native mapping exactly
     * once. A base-owned page order must supply the real native index here
     * before the engine can use it as one.
     */
    virtual std::pair<Okular::SigningResult, QString> sign(const Okular::NewSignatureData &data, int nativePage, const QString &fileName) = 0;
    virtual Okular::CertificateStore *certificateStore() const = 0;
    virtual void setActiveCertificateBackend(Okular::CertificateInfo::Backend backend) = 0;
};

/** Poppler today: the PDF settings page in the configuration dialog. */
class PdfConfigurable
{
public:
    virtual ~PdfConfigurable() = default;

    virtual bool reparseConfig() = 0;
    virtual void addPages(KConfigDialog *dialog) = 0;
};

/** Layers and document-level optional actions exposed by the backend. */
class PdfBackendActions
{
public:
    virtual ~PdfBackendActions() = default;

    virtual QAbstractItemModel *layersModel() const = 0;
    virtual Okular::BackendOpaqueAction::OpaqueActionResult opaqueAction(const Okular::BackendOpaqueAction *action) = 0;
    virtual Okular::Action *additionalDocumentAction(Okular::Document::DocumentAdditionalActionType type) = 0;
};

/**
 * The engine handle. One concrete object implements PdfDocument plus the
 * capabilities it supports and returns them from the getters below.
 *
 * PdfDocument is mandatory; every other getter defaults to nullptr, which the
 * front end treats as "this engine does not provide that operation". This is
 * deliberately not a dynamic_cast: engines and front end may live in
 * different DLLs.
 */
class PdfEngine
{
public:
    virtual ~PdfEngine() = default;

    virtual EngineId id() const = 0;

    /**
     * Feature flags of the engine's internal Okular::Generator implementation.
     *
     * Okular asks its Generator for these flags (threaded rendering, text
     * extraction, font data, native printing, ...). PdfGeneratorBase mirrors
     * them onto itself because it, not the module's generator, is the object
     * Okular attaches to the Document. The flags only depend on the backend and
     * are stable for the lifetime of one engine instance. An engine that wraps
     * no backend generator keeps the default: no features.
     */
    virtual bool hasFeature(Okular::Generator::GeneratorFeature feature) const
    {
        Q_UNUSED(feature)
        return false;
    }

    /**
     * Binds the owning Okular::Document that page-dependent capabilities use to
     * resolve Okular::Page models and document state: link editing looks the
     * page up in it, printing reads the page range and orientation from it, and
     * the annotation implementation reads the page rotation. This is the
     * module-boundary counterpart of Okular::Generator::setWrapperDocument(),
     * which Okular has for a forwarding generator and its inner generator.
     *
     * PdfGeneratorBase sets it before every load and clears it on close; the
     * document is not owned here. Default: no-op, for engines that do not wrap
     * a backend generator. The front end never treats it as an optional
     * capability, so it does not get a getter.
     */
    virtual void setWrapperDocument(Okular::Document *document)
    {
        Q_UNUSED(document)
    }

    virtual PdfDocument *document() = 0;
    virtual PdfSaver *saver()
    {
        return nullptr;
    }
    virtual PdfPageEditor *pageEditor()
    {
        return nullptr;
    }
    virtual PdfLinkEditor *linkEditor()
    {
        return nullptr;
    }
    virtual PdfOcr *ocr()
    {
        return nullptr;
    }
    virtual PdfCustomMetadata *customMetadata()
    {
        return nullptr;
    }
    /**
     * Page identity of explicit link destinations. Engines that write a page
     * token into the GotoActions they build implement this; the front end
     * falls back to the Reading View page token when it is null.
     */
    virtual PdfPageIdentity *pageIdentity()
    {
        return nullptr;
    }
    virtual PdfPrinter *printer()
    {
        return nullptr;
    }
    virtual PdfPrintConfigurator *printConfigurator()
    {
        return nullptr;
    }
    virtual PdfPrintPreviewProvider *printPreviewProvider()
    {
        return nullptr;
    }
    virtual PdfSigner *signer()
    {
        return nullptr;
    }
    virtual PdfConfigurable *configurable()
    {
        return nullptr;
    }
    virtual PdfBackendActions *backendActions()
    {
        return nullptr;
    }
};

inline void EngineDeleter::operator()(PdfEngine *engine) const noexcept
{
    if (engine && destroy) {
        destroy(engine);
    }
}

} // namespace Pdf
} // namespace Mengshee
