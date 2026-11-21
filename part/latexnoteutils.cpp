#include "core/annotationframe.h"
/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "latexnoteutils.h"

#include <cmath>
#include <exception>
#include <memory>
#include <thread>
#include <utility>

#include <KLocalizedString>
#include <KMessageBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QPointer>
#include <QAbstractTextDocumentLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QTextCursor>
#include <QTextDocument>
#include <QUuid>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolTip>
#include <QWidget>

#include "core/annotations.h"
#include "core/document.h"
#include "core/latexnotegeometry.h"
#include "core/page.h"
#include "core/utils.h"
#include "gui/debug_ui.h"
#include "gui/guiutils.h"
#include "latexrenderer.h"
#include "latexsource.h"

namespace
{
LatexNoteUtils::RenderResult renderAppearancePdfNoThrow(const QString &latexInput, const QColor &textColor, double layoutWidthPoints, bool callout, double fontSizePoints) noexcept
{
    try {
        return LatexNoteUtils::renderAppearancePdf(latexInput, textColor, layoutWidthPoints, callout, fontSizePoints);
    } catch (const std::exception &exception) {
        LatexNoteUtils::RenderResult result;
        try {
            result.errorMessage = i18n("LaTeX rendering failed unexpectedly: %1", QString::fromLocal8Bit(exception.what()));
            qCCritical(OkularUiDebug) << result.errorMessage;
        } catch (...) {
        }
        return result;
    } catch (...) {
        LatexNoteUtils::RenderResult result;
        try {
            result.errorMessage = i18n("LaTeX rendering failed because of an unknown internal error.");
            qCCritical(OkularUiDebug) << result.errorMessage;
        } catch (...) {
        }
        return result;
    }
}

template<typename Worker> void startDetachedRenderWorker(QWidget *parent, Worker &&worker)
{
    try {
        std::thread(std::forward<Worker>(worker)).detach();
    } catch (const std::exception &exception) {
        KMessageBox::error(parent, i18n("Could not start the LaTeX rendering worker: %1", QString::fromLocal8Bit(exception.what())));
    } catch (...) {
        KMessageBox::error(parent, i18n("Could not start the LaTeX rendering worker because of an unknown internal error."));
    }
}

bool isFiniteUsableRect(const Okular::NormalizedRect &rect)
{
    return std::isfinite(rect.left) && std::isfinite(rect.top) && std::isfinite(rect.right) && std::isfinite(rect.bottom) && rect.width() > 0.0 && rect.height() > 0.0;
}

QString latexErrorMessage(GuiUtils::LatexRenderer::Error errorCode, const QString &latexOutput)
{
    switch (errorCode) {
    case GuiUtils::LatexRenderer::LatexFailed:
        return i18n("LaTeX rendering failed:\n%1", GuiUtils::LatexRenderer::compactErrorMessage(latexOutput));
    case GuiUtils::LatexRenderer::NoError:
        break;
    }
    return QString();
}

QString latexNoteBaseName(const QString &latexInput, const QColor &textColor, double layoutWidthPoints, double fontSizePoints, const QString &backendName)
{
    const bool fixedWidth = std::isfinite(layoutWidthPoints) && layoutWidthPoints > 0.0;
    const QString widthText = fixedWidth ? QString::number(layoutWidthPoints, 'f', 3) : QStringLiteral("0");
    const QString fontSizeText = std::isfinite(fontSizePoints) && fontSizePoints > 0.0 ? QString::number(fontSizePoints, 'f', 3) : QStringLiteral("0");
    // Geometry and appearance policy are part of the identity. Explicit rerenders
    // must rebuild through Mengshee's own appearance composer.
    const QString renderMode = fixedWidth ? QStringLiteral("fixed-width-content-v9-app-appearance") : QStringLiteral("natural-width-content-v9-app-appearance");
    const QString hashText = latexInput + QStringLiteral("|%1|%2|%3|%4|%5").arg(textColor.name(QColor::HexArgb), widthText, fontSizeText, renderMode, backendName);
    return QString::fromLatin1(QCryptographicHash::hash(hashText.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString latexTemporaryPath()
{
    // Allow headless tests to keep appearances in their isolated temporary tree.
    QString tempPath = qEnvironmentVariable("MENGSHEE_LATEX_TEMP_ROOT");
    if (tempPath.isEmpty()) {
        tempPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (tempPath.isEmpty()) {
            tempPath = QDir(QDir::tempPath()).filePath(QStringLiteral("mengshee"));
        }
        tempPath = QDir(tempPath).filePath(QStringLiteral("temporary"));
    }
    QDir().mkpath(tempPath);
    return QDir::cleanPath(tempPath);
}

QTemporaryDir *latexAppearanceSessionRoot()
{
    static const std::unique_ptr<QTemporaryDir> sessionRoot = []() {
        const QString baseLocation = latexTemporaryPath();
        auto tempDir = std::make_unique<QTemporaryDir>(QDir(baseLocation).filePath(QStringLiteral("mengshee-latex-appearances-XXXXXX")));
        tempDir->setAutoRemove(true);
        return tempDir;
    }();

    return sessionRoot.get();
}

QDir latexAppearanceSessionDir()
{
    QTemporaryDir *sessionRoot = latexAppearanceSessionRoot();
    if (!sessionRoot || !sessionRoot->isValid()) {
        return QDir(QDir(latexTemporaryPath()).filePath(QStringLiteral("mengshee-latex-appearances-unavailable/latex-notes")));
    }

    QDir rootDir(sessionRoot->path());
    rootDir.mkpath(QStringLiteral("latex-notes"));
    return QDir(rootDir.filePath(QStringLiteral("latex-notes")));
}

// An error is ordinary appearance content, not separate UI state. Do not use
// TeX to draw its own error: the backend may be unavailable or the source invalid.
LatexNoteUtils::RenderResult renderErrorAppearance(const QString &message, double layoutWidthPoints, double fontSizePoints)
{
    LatexNoteUtils::RenderResult result;
    result.errorMessage = message;
    const QString fileName = latexAppearanceSessionDir().filePath(QStringLiteral("error-%1.pdf").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QPdfWriter writer(fileName);
    writer.setResolution(72);

    QTextDocument text;
    text.documentLayout()->setPaintDevice(&writer);
    QFont font;
    font.setPointSizeF(std::isfinite(fontSizePoints) && fontSizePoints > 0.0 ? fontSizePoints : 10.0);
    text.setDefaultFont(font);
    text.setDocumentMargin(0);
    text.setPlainText(message);
    QTextCursor cursor(&text);
    cursor.select(QTextCursor::Document);
    QTextCharFormat format;
    format.setForeground(QColor(200, 0, 0));
    cursor.mergeCharFormat(format);
    text.setTextWidth(std::isfinite(layoutWidthPoints) && layoutWidthPoints > 0.0 ? layoutWidthPoints : 240.0);

    const QSizeF size = text.size();
    writer.setPageSize(QPageSize(size, QPageSize::Point, QString(), QPageSize::ExactMatch));
    writer.setPageMargins(QMarginsF(), QPageLayout::Point);
    QPainter painter(&writer);
    if (!painter.isActive()) {
        return result;
    }
    text.drawContents(&painter);
    if (!painter.end()) {
        return result;
    }
    result.ok = true; // A usable error AP; the source has not compiled successfully.
    result.pdfFileName = fileName;
    result.pdfSizePoints = size;
    return result;
}

QSizeF pageSizeInPoints(const Okular::Page *page)
{
    if (!page || page->width() <= 0.0 || page->height() <= 0.0) {
        return {};
    }

    // Page::width()/height() use the generator's internal scale. Resolve the
    // physical dimensions through the document, never through the current screen.
    return page->sizeInPoints();
}
}

namespace LatexNoteUtils
{
const Okular::StampAnnotation *annotationAsLatexStampAnnotation(const Okular::Annotation *annotation)
{
    if (!annotation || annotation->subType() != Okular::Annotation::AStamp || annotation->contents().trimmed().isEmpty() || !annotation->isOkularLatex()) {
        return nullptr;
    }

    const auto *stampAnnotation = static_cast<const Okular::StampAnnotation *>(annotation);
    if (!isFiniteUsableRect(stampAnnotation->boundingRectangle())) {
        return nullptr;
    }

    return stampAnnotation;
}

Okular::StampAnnotation *annotationAsLatexStampAnnotation(Okular::Annotation *annotation)
{
    return const_cast<Okular::StampAnnotation *>(annotationAsLatexStampAnnotation(static_cast<const Okular::Annotation *>(annotation)));
}

bool annotationIsLatex(const Okular::Annotation *annotation)
{
    return annotationAsLatexStampAnnotation(annotation) != nullptr;
}

QColor colorForLatexAnnotation(const Okular::Annotation *annotation)
{
    if (!annotation) {
        return Qt::black;
    }

    QColor textColor = annotation->latexTextColor();
    if (!textColor.isValid() || textColor.alpha() == 0) {
        textColor = Qt::black;
    }
    return textColor;
}

QColor fillColorForLatexAnnotation(const Okular::Annotation *annotation)
{
    if (!annotation || annotation->latexNoteType() == Okular::Annotation::LatexNotePlain) {
        return Qt::transparent;
    }
    const QColor color = annotation->latexFillColor();
    return color.isValid() && color.alpha() != 0 ? color : Qt::yellow;
}

QColor borderColorForLatexAnnotation(const Okular::Annotation *annotation)
{
    if (!annotation || annotation->latexNoteType() == Okular::Annotation::LatexNotePlain) {
        return Qt::transparent;
    }
    const QColor color = annotation->latexBorderColor();
    return color.isValid() && color.alpha() != 0 ? color : colorForLatexAnnotation(annotation);
}

double pageWidthInPoints(const Okular::Page *page)
{
    return pageSizeInPoints(page).width();
}

double pageHeightInPoints(const Okular::Page *page)
{
    return pageSizeInPoints(page).height();
}

double annotationWidthInPoints(const Okular::Annotation *annotation, const Okular::Page *page)
{
    return annotation && page ? Okular::AnnotationFrame::localSize(annotation->boundingRectangle(), pageSizeInPoints(page), annotation->appearanceRotation()).width() : 0.0;
}

double paddingForLatexAnnotation(const Okular::Annotation *annotation)
{
    if (!annotation || !std::isfinite(annotation->latexPadding()) || annotation->latexPadding() < 0.0) {
        return Okular::LatexNoteGeometry::defaultPaddingPoints();
    }
    return annotation->latexPadding();
}

double fontSizeForLatexAnnotation(const Okular::Annotation *annotation)
{
    if (!annotation || !std::isfinite(annotation->latexFontSize()) || annotation->latexFontSize() <= 0.0) {
        return 0.0;
    }
    return annotation->latexFontSize();
}

double layoutWidthForLatexAnnotation(const Okular::Annotation *annotation, const Okular::Page *page)
{
    if (!annotation) {
        return 0.0;
    }

    // The appearance PDF can include preview borders or overfull content.
    // Reconstructing a valid paragraph width from that outer frame would make
    // each subsequent edit grow the paragraph by the PDF's extra width.
    const double storedWidth = annotation->latexLayoutWidth();
    if (std::isfinite(storedWidth) && storedWidth > 0.0) {
        return storedWidth;
    }

    const double visibleWidthPoints = annotationWidthInPoints(annotation, page);
    return Okular::LatexNoteGeometry::layoutWidthForVisibleWidth(visibleWidthPoints, paddingForLatexAnnotation(annotation));
}

Okular::NormalizedRect frameForLatexContent(const Okular::Annotation *annotation,
                                          const Okular::Page *page,
                                          const Okular::NormalizedRect &anchorRect,
                                          const QSizeF &contentPdfSizePoints,
                                          double layoutWidthPoints)
{
    const QSizeF visualSize = Okular::LatexNoteGeometry::visualSizeForContent(contentPdfSizePoints, layoutWidthPoints, paddingForLatexAnnotation(annotation));
    return Okular::AnnotationFrame::withLocalSize(anchorRect, pageSizeInPoints(page), annotation->appearanceRotation(), visualSize);
}

bool applyRenderedLatexStampAnnotationAppearance(QWidget *parent,
                                                 Okular::Document *document,
                                                 int pageNumber,
                                                 Okular::StampAnnotation *stampAnnotation,
                                                 const QColor &textColor,
                                                 const QColor &fillColor,
                                                 const QColor &borderColor,
                                                 double layoutWidthPoints,
                                                 bool boxed,
                                                 bool prepareModification,
                                                 const RenderResult &rendered,
                                                 bool showErrors)
{
    if (!document || pageNumber == -1 || !stampAnnotation) {
        return false;
    }

    const Okular::Page *page = document->page(pageNumber);
    if (!page) {
        return false;
    }

    if (!rendered.ok) {
        if (showErrors) {
            KMessageBox::error(parent, rendered.errorMessage, i18n("LaTeX rendering failed"));
        } else {
            qCWarning(OkularUiDebug) << "LaTeX stamp async render failed:" << rendered.errorMessage;
        }
        return false;
    }

    if (!rendered.pdfSizePoints.isValid() || rendered.pdfSizePoints.isEmpty()) {
        if (showErrors) {
            KMessageBox::error(parent, i18n("Could not load the rendered LaTeX note PDF."), i18n("LaTeX rendering failed"));
        } else {
            qCWarning(OkularUiDebug) << "LaTeX stamp async render produced an invalid PDF size:" << rendered.pdfSizePoints;
        }
        return false;
    }

    const auto previousRect = stampAnnotation->boundingRectangle();
    const auto updatedRect = frameForLatexContent(stampAnnotation, page, previousRect, rendered.pdfSizePoints, layoutWidthPoints);
    const double targetBorderWidth = boxed ? qMax(0.0, stampAnnotation->style().width()) : 0.0;
    showRenderWarning(parent, rendered.warning);
    if (rendered.pdfFileName == stampAnnotation->latexAppearancePdfFileName() && updatedRect == stampAnnotation->boundingRectangle() && qAbs(stampAnnotation->latexLayoutWidth() - layoutWidthPoints) < 1e-3 &&
        stampAnnotation->isOkularLatex() && stampAnnotation->latexTextColor() == textColor && stampAnnotation->latexFillColor() == fillColor &&
        stampAnnotation->latexBorderColor() == borderColor && qAbs(stampAnnotation->style().width() - targetBorderWidth) < 1e-6 && !(stampAnnotation->flags() & Okular::Annotation::FixedRotation)) {
        if (prepareModification) {
            return true;
        }
    }

    if (prepareModification) {
        document->prepareToModifyAnnotationProperties(stampAnnotation);
    }
    stampAnnotation->setOkularLatex(true);
    stampAnnotation->setLatexNoteType(stampAnnotation->isOrderedCallout() ? Okular::Annotation::LatexNoteOrderedCallout
        : (stampAnnotation->isLatexCallout() ? Okular::Annotation::LatexNoteCallout : (boxed ? Okular::Annotation::LatexNoteBoxed : Okular::Annotation::LatexNotePlain)));
    stampAnnotation->setStampIconName(QStringLiteral("latex-notes"));
    stampAnnotation->setStampImagePath(QString());
    stampAnnotation->setLatexAppearancePdfFileName(rendered.pdfFileName);
    stampAnnotation->setLatexLayoutWidth(layoutWidthPoints);
    stampAnnotation->setLatexTextColor(textColor);
    stampAnnotation->setLatexFillColor(fillColor);
    stampAnnotation->setLatexBorderColor(borderColor);
    stampAnnotation->style().setWidth(targetBorderWidth);
    stampAnnotation->adjust({updatedRect.left-previousRect.left,updatedRect.top-previousRect.top},
        {updatedRect.right-previousRect.right,updatedRect.bottom-previousRect.bottom});
    stampAnnotation->setModificationDate(QDateTime::currentDateTime());
    qCDebug(OkularUiDebug) << "Updating LaTeX stamp appearance; source path:" << rendered.pdfFileName << "layout width:" << layoutWidthPoints << "pdf size:" << rendered.pdfSizePoints
                           << "rect:" << updatedRect.left << updatedRect.top << updatedRect.right << updatedRect.bottom;
    document->modifyPageAnnotationProperties(pageNumber, stampAnnotation);
    return true;
}

RenderResult renderAppearancePdf(const QString &latexInput, const QColor &textColor, double layoutWidthPoints)
{
    return renderAppearancePdf(latexInput, textColor, layoutWidthPoints, false, 0.0);
}

RenderResult renderAppearancePdf(const QString &latexInput, const QColor &textColor, double layoutWidthPoints, bool callout)
{
    return renderAppearancePdf(latexInput, textColor, layoutWidthPoints, callout, 0.0);
}

RenderResult renderAppearancePdf(const QString &latexInput, const QColor &textColor, double layoutWidthPoints, bool callout, double fontSizePoints)
{
    Q_UNUSED(callout);
    RenderResult result;
    if (latexInput.trimmed().isEmpty()) {
        result.errorMessage = i18n("LaTeX source is empty.");
        return result;
    }

    GuiUtils::LatexRenderer renderer;
    QString latexOutput;
    QString temporaryPdfFile;
    const QString layoutSource = GuiUtils::LatexSource::sourceForLayout(latexInput);
    const GuiUtils::LatexRenderer::Error errorCode = renderer.renderLatexToPdf(layoutSource, textColor, temporaryPdfFile, latexOutput, layoutWidthPoints, fontSizePoints);
    if (errorCode != GuiUtils::LatexRenderer::NoError) {
        qCWarning(OkularUiDebug) << "LaTeX note PDF render failed; backend:" << renderer.lastBackendName() << "layout width:" << layoutWidthPoints << "font size:" << fontSizePoints << "error:" << errorCode
                                 << "message:" << latexErrorMessage(errorCode, latexOutput);
        return renderErrorAppearance(GuiUtils::LatexRenderer::compactErrorMessage(latexOutput), layoutWidthPoints, fontSizePoints);
    }

    const QFileInfo temporaryPdfInfo(temporaryPdfFile);
    qCDebug(OkularUiDebug) << "LaTeX note PDF render finished; backend:" << renderer.lastBackendName() << "layout width:" << layoutWidthPoints << "temporary PDF:" << temporaryPdfFile << "exists:" << temporaryPdfInfo.exists()
                           << "bytes:" << temporaryPdfInfo.size();

    result.pdfSizePoints = GuiUtils::pdfPageSizeInPoints(temporaryPdfFile);
    if (!result.pdfSizePoints.isValid() || result.pdfSizePoints.isEmpty() || !std::isfinite(result.pdfSizePoints.width()) || !std::isfinite(result.pdfSizePoints.height())) {
        qCWarning(OkularUiDebug) << "LaTeX note rendered PDF has invalid size; temporary PDF:" << temporaryPdfFile << "size:" << result.pdfSizePoints;
        result.errorMessage = i18n("Could not load the rendered LaTeX note PDF.");
        return result;
    }
    qCDebug(OkularUiDebug) << "LaTeX note rendered PDF page size:" << result.pdfSizePoints;

    QDir dataDir = latexAppearanceSessionDir();
    if (!dataDir.exists()) {
        result.errorMessage = i18n("Could not create a temporary directory for LaTeX note appearances.");
        return result;
    }

    const QString noteBaseName = latexNoteBaseName(layoutSource, textColor, layoutWidthPoints, fontSizePoints, renderer.lastBackendName());
    const QString appearancePdfFileName = dataDir.filePath(QStringLiteral("%1.pdf").arg(noteBaseName));
    if (QFile::exists(appearancePdfFileName)) {
        QFile::remove(appearancePdfFileName);
    }
    if (temporaryPdfFile.isEmpty() || (!QFile::rename(temporaryPdfFile, appearancePdfFileName) && !QFile::copy(temporaryPdfFile, appearancePdfFileName))) {
        qCWarning(OkularUiDebug) << "Could not move rendered LaTeX note PDF; from:" << temporaryPdfFile << "to:" << appearancePdfFileName;
        result.errorMessage = i18n("Could not save the rendered LaTeX note PDF.");
        return result;
    }

    if (!QFile::exists(appearancePdfFileName)) {
        qCWarning(OkularUiDebug) << "LaTeX note appearance PDF is missing; target:" << appearancePdfFileName;
        result.errorMessage = i18n("Could not save the rendered LaTeX note PDF.");
        return result;
    }

    result.ok = true;
    result.pdfFileName = appearancePdfFileName;
    result.warning = renderer.lastWarning();
    result.warningMessage = warningText(result.warning);
    const QFileInfo appearancePdfInfo(appearancePdfFileName);
    qCDebug(OkularUiDebug) << "LaTeX note appearance PDF ready; path:" << result.pdfFileName << "exists:" << appearancePdfInfo.exists() << "bytes:" << appearancePdfInfo.size() << "page size:" << result.pdfSizePoints
                           << "warning:" << result.warningMessage;
    return result;
}

bool updateLatexStampAnnotationAppearance(QWidget *parent,
                                          Okular::Document *document,
                                          int pageNumber,
                                          Okular::StampAnnotation *stampAnnotation,
                                          const QColor &textColor,
                                          const QColor &fillColor,
                                          const QColor &borderColor,
                                          double layoutWidthPoints,
                                          bool boxed,
                                          bool prepareModification)
{
    if (!document || pageNumber == -1 || !stampAnnotation) {
        return false;
    }

    const Okular::Page *page = document->page(pageNumber);
    if (!page) {
        return false;
    }
    if (!std::isfinite(layoutWidthPoints) || layoutWidthPoints < 0.0) {
        const double storedWidth = stampAnnotation->latexLayoutWidth();
        layoutWidthPoints = std::isfinite(storedWidth) && storedWidth > 0.0 ? storedWidth : 0.0;
    }
    const RenderResult rendered = renderAppearancePdf(stampAnnotation->contents(), textColor, layoutWidthPoints, stampAnnotation->isLatexCallout(), fontSizeForLatexAnnotation(stampAnnotation));
    return applyRenderedLatexStampAnnotationAppearance(parent, document, pageNumber, stampAnnotation, textColor, fillColor, borderColor, layoutWidthPoints, boxed, prepareModification, rendered, true);
}

void updateLatexStampAnnotationAppearanceAsync(QWidget *parent,
                                               Okular::Document *document,
                                               int pageNumber,
                                               const QString &annotationUniqueName,
                                               const QString &latexInput,
                                               const QColor &textColor,
                                               const QColor &fillColor,
                                               const QColor &borderColor,
                                               double layoutWidthPoints,
                                               bool boxed)
{
    if (!document || pageNumber == -1 || annotationUniqueName.isEmpty() || latexInput.trimmed().isEmpty()) {
        return;
    }

    const Okular::Page *annotationPage = document->page(pageNumber);
    const double fontSizePoints = fontSizeForLatexAnnotation(annotationPage ? annotationPage->annotation(annotationUniqueName) : nullptr);
    QPointer<QWidget> parentGuard(parent);
    QPointer<Okular::Document> documentGuard(document);
    QPointer<QCoreApplication> applicationGuard(QCoreApplication::instance());
    if (!applicationGuard) {
        return;
    }
    startDetachedRenderWorker(parent, [applicationGuard, parentGuard, documentGuard, pageNumber, annotationUniqueName, latexInput, textColor, fillColor, borderColor, layoutWidthPoints, boxed, fontSizePoints]() mutable noexcept {
        try {
            RenderResult rendered = renderAppearancePdfNoThrow(latexInput, textColor, layoutWidthPoints, true, fontSizePoints);
            if (!applicationGuard) {
                return;
            }
            const bool queued = QMetaObject::invokeMethod(
                applicationGuard.data(),
                [parentGuard, documentGuard, pageNumber, annotationUniqueName, latexInput, textColor, fillColor, borderColor, layoutWidthPoints, boxed, fontSizePoints, rendered]() mutable {
                    if (!documentGuard) {
                        return;
                    }
                    const Okular::Page *page = documentGuard->page(pageNumber);
                    Okular::Annotation *annotation = page ? page->annotation(annotationUniqueName) : nullptr;
                    auto *stampAnnotation = annotationAsLatexStampAnnotation(annotation);
                    if (!stampAnnotation || stampAnnotation->contents() != latexInput || qAbs(fontSizeForLatexAnnotation(stampAnnotation) - fontSizePoints) > 1e-6) {
                        return;
                    }
                    applyRenderedLatexStampAnnotationAppearance(parentGuard.data(), documentGuard.data(), pageNumber, stampAnnotation, textColor, fillColor, borderColor, layoutWidthPoints, boxed, true, rendered, false);
                },
                Qt::QueuedConnection);
            if (!queued) {
                qCWarning(OkularUiDebug) << "Could not queue the LaTeX stamp annotation update";
            }
        } catch (const std::exception &exception) {
            qCCritical(OkularUiDebug) << "Unhandled exception in LaTeX stamp annotation worker:" << exception.what();
        } catch (...) {
            qCCritical(OkularUiDebug) << "Unknown exception in LaTeX stamp annotation worker";
        }
    });
}

QString warningText(const GuiUtils::LatexRenderWarning &warning)
{
    return warning.isValid() ? warning.message : QString();
}

void showRenderWarning(QWidget *parent, const QString &warningMessage)
{
    showRenderWarning(parent, warningMessage, QCursor::pos());
}

void showRenderWarning(QWidget *parent, const GuiUtils::LatexRenderWarning &warning)
{
    showRenderWarning(parent, warningText(warning));
}

void showRenderWarning(QWidget *parent, const QString &warningMessage, const QPoint &globalPosition)
{
    if (warningMessage.isEmpty()) {
        return;
    }

    QToolTip::showText(globalPosition, warningMessage, parent, QRect(), 7000);
}

void showRenderWarning(QWidget *parent, const GuiUtils::LatexRenderWarning &warning, const QPoint &globalPosition)
{
    showRenderWarning(parent, warningText(warning), globalPosition);
}
}
