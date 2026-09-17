/*
    SPDX-FileCopyrightText: 2026 Mengshee contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "imageexportdialog.h"

#include "core/document.h"
#include "core/observer.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QImageWriter>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QSpinBox>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

#include <KLocalizedString>

#include <algorithm>
#include <limits>

using namespace Okular;

bool ImageExportUtils::parsePageRange(const QString &text, int pageCount, QList<int> *pages, QString *error)
{
    if (pages) {
        pages->clear();
    }
    if (error) {
        error->clear();
    }
    auto fail = [&]() {
        if (error) {
            *error = i18n("Enter page numbers between 1 and %1, for example 1-4, 8, 11-13. Empty items, reversed ranges and open-ended ranges are not allowed.", pageCount);
        }
        return false;
    };
    if (!pages || pageCount <= 0 || text.trimmed().isEmpty()) {
        return fail();
    }
    static const QRegularExpression itemPattern(QStringLiteral("^\\s*([0-9]+)\\s*(?:-\\s*([0-9]+)\\s*)?$"));
    // Validate every item before expanding anything: an invalid late item must
    // never return a partially parsed selection.
    QList<QPair<int, int>> ranges;
    const QStringList items = text.split(QLatin1Char(','), Qt::KeepEmptyParts);
    for (const QString &item : items) {
        const auto match = itemPattern.match(item);
        if (!match.hasMatch()) {
            return fail();
        }
        bool firstOk = false;
        bool lastOk = false;
        const int first = match.captured(1).toInt(&firstOk);
        const int last = match.captured(2).isEmpty() ? first : match.captured(2).toInt(&lastOk);
        if (match.captured(2).isEmpty()) {
            lastOk = firstOk;
        }
        if (!firstOk || !lastOk || first < 1 || last < first || last > pageCount) {
            return fail();
        }
        ranges.append({first - 1, last - 1});
    }
    // Merge first, keeping expansion bounded by the document page count even
    // for a range repeated thousands of times.
    std::sort(ranges.begin(), ranges.end());
    int lastAdded = -1;
    for (const auto &range : ranges) {
        for (int page = qMax(range.first, lastAdded + 1); page <= range.second; ++page) {
            pages->append(page);
        }
        lastAdded = qMax(lastAdded, range.second);
    }
    return !pages->isEmpty();
}

bool ImageExportUtils::isSafePrefix(const QString &prefix)
{
    if (prefix.isEmpty() || prefix.size() > 80 || prefix != prefix.trimmed() || prefix.endsWith(QLatin1Char('.')) || prefix == QLatin1String(".") || prefix == QLatin1String("..")) {
        return false;
    }
    for (const QChar ch : prefix) {
        if (ch.category() == QChar::Other_Control || ch.category() == QChar::Other_Format || QStringLiteral("<>:\"/\\|?*").contains(ch)) {
            return false;
        }
    }
    static const QRegularExpression reserved(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³])(?:\\.|$)"), QRegularExpression::CaseInsensitiveOption);
    return !reserved.match(prefix).hasMatch();
}

QString ImageExportUtils::suggestedPrefix(const QString &baseName)
{
    QString prefix = baseName;
    for (QChar &ch : prefix) {
        if (ch.category() == QChar::Other_Control || ch.category() == QChar::Other_Format || QStringLiteral("<>:\"/\\|?*").contains(ch)) {
            ch = QLatin1Char('_');
        }
    }
    prefix = prefix.trimmed().left(80);
    while (prefix.endsWith(QLatin1Char('.')) || prefix.endsWith(QLatin1Char(' '))) {
        prefix.chop(1);
    }
    return isSafePrefix(prefix) ? prefix : QStringLiteral("document");
}

QString ImageExportUtils::pageFileName(const QString &prefix, int pageZeroBased, const QString &extension)
{
    if (!isSafePrefix(prefix) || pageZeroBased < 0 || (extension != QLatin1String("png") && extension != QLatin1String("jpg"))) {
        return {};
    }
    return QStringLiteral("%1_%2.%3").arg(prefix, QString::number(qint64(pageZeroBased) + 1).rightJustified(4, QLatin1Char('0')), extension);
}

namespace
{
// Modal windows do not stop queued edits or file-watch callbacks. Stop exporting
// if the live model changes between pages, rather than mixing two revisions.
class ExportDocumentObserver : public DocumentObserver
{
public:
    explicit ExportDocumentObserver(Document *document)
        : m_document(document)
    {
        document->addObserver(this);
        m_armed = true; // addObserver sends the initial setup synchronously.
    }
    ~ExportDocumentObserver() override
    {
        if (m_document) {
            m_document->removeObserver(this);
        }
    }
    void notifySetup(const QList<Page *> &, int flags) override
    {
        changed |= m_armed && (flags & (DocumentChanged | NewLayoutForPages | UrlChanged));
    }
    void notifyPageChanged(int, int flags) override
    {
        // A completed asynchronous rotation job sends Pixmap | Annotations,
        // even without a new edit. Actual annotation edits send Annotations;
        // an actual rotation change is caught by notifySetup(NewLayoutForPages).
        changed |= m_armed && flags == Annotations;
    }
    void notifyContentsCleared(int flags) override
    {
        changed |= m_armed && (flags & (Pixmap | Annotations));
    }
    bool changed = false;

private:
    QPointer<Document> m_document;
    bool m_armed = false;
};

// Keep stack dialogs out of the parent's QObject tree: event processing may
// delete the caller's window. A transient relationship still supplies normal
// window placement/stacking, without giving it ownership of stack storage.
void setDialogWindow(QDialog &dialog, QWidget *parent)
{
    dialog.setWindowModality(Qt::ApplicationModal);
    if (parent) {
        dialog.winId();
        parent->window()->winId();
        dialog.windowHandle()->setTransientParent(parent->window()->windowHandle());
    }
}
}

void ImageExportDialog::exportDocument(Document *document, int currentPageZeroBased, const QString &suggestedBaseName, QWidget *parent)
{
    Q_ASSERT(qApp && QThread::currentThread() == qApp->thread());
    if (!qApp || QThread::currentThread() != qApp->thread()) {
        return;
    }
    // Nested event loops must not start a second export, even for another view.
    static bool exporting = false;
    if (exporting) {
        return;
    }
    const QScopedValueRollback<bool> exportingGuard(exporting, true);
    QPointer<Document> guardedDocument(document);
    QPointer<QWidget> guardedParent(parent);
    if (!document || !document->isOpened() || !document->canRenderToImage() || document->pages() == 0 || document->pages() > uint(std::numeric_limits<int>::max())) {
        QMessageBox::warning(parent, i18n("Export Pages as Images"), i18n("This document cannot be exported as images."));
        return;
    }
    const int pageCount = int(document->pages());
    const QUrl sourceUrl = document->currentDocument();
    bool invalidated = false;
    QObject lifetime;
    // This flag never resets: a close followed by reopening the same URL and
    // same number of pages is still a different document.
    QObject::connect(document, &Document::aboutToClose, &lifetime, [&]() { invalidated = true; });
    QObject::connect(document, &QObject::destroyed, &lifetime, [&]() { invalidated = true; });
    if (parent) {
        QObject::connect(parent, &QObject::destroyed, &lifetime, [&]() { invalidated = true; });
    }
    const auto invalidate = [&]() { invalidated = true; };
    QObject::connect(document, &Document::formTextChangedByUndoRedo, &lifetime, invalidate);
    QObject::connect(document, &Document::formListChangedByUndoRedo, &lifetime, invalidate);
    QObject::connect(document, &Document::formComboChangedByUndoRedo, &lifetime, invalidate);
    QObject::connect(document, &Document::formButtonsChangedByUndoRedo, &lifetime, invalidate);
    QObject::connect(document, &Document::refreshFormWidget, &lifetime, invalidate);
    if (auto *layers = document->layersModel()) {
        QObject::connect(layers, &QAbstractItemModel::dataChanged, &lifetime, invalidate);
        QObject::connect(layers, &QAbstractItemModel::modelReset, &lifetime, invalidate);
        QObject::connect(layers, &QAbstractItemModel::rowsInserted, &lifetime, invalidate);
        QObject::connect(layers, &QAbstractItemModel::rowsRemoved, &lifetime, invalidate);
        QObject::connect(layers, &QAbstractItemModel::layoutChanged, &lifetime, invalidate);
    }
    ExportDocumentObserver observer(document);
    const auto documentIsValid = [&]() {
        return !invalidated && !observer.changed && guardedDocument && guardedDocument->isOpened() && guardedDocument->pages() == uint(pageCount) && guardedDocument->currentDocument() == sourceUrl && guardedDocument->canRenderToImage();
    };

    QDialog settings;
    settings.setObjectName(QStringLiteral("imageExportDialog"));
    setDialogWindow(settings, parent);
    settings.setWindowTitle(i18n("Export Pages as Images"));
    auto layout = new QVBoxLayout(&settings);
    auto form = new QFormLayout;
    layout->addLayout(form);

    auto selection = new QComboBox(&settings);
    selection->setObjectName(QStringLiteral("imageExportSelection"));
    selection->addItem(i18n("Current page"));
    selection->addItem(i18n("All pages"));
    selection->addItem(i18n("Custom page range"));
    const bool validCurrentPage = currentPageZeroBased >= 0 && currentPageZeroBased < pageCount;
    selection->setCurrentIndex(validCurrentPage ? 0 : 1);
    form->addRow(i18n("Pages:"), selection);
    auto range = new QLineEdit(&settings);
    range->setObjectName(QStringLiteral("imageExportRange"));
    range->setPlaceholderText(i18n("For example: 1-4, 8, 11-13"));
    range->setEnabled(false);
    form->addRow(i18n("Page range:"), range);
    QObject::connect(selection, &QComboBox::currentIndexChanged, &settings, [=](int index) { range->setEnabled(index == 2); });

    auto format = new QComboBox(&settings);
    format->setObjectName(QStringLiteral("imageExportFormat"));
    const auto supportedFormats = QImageWriter::supportedImageFormats();
    if (supportedFormats.contains("png")) {
        format->addItem(i18n("PNG (lossless)"), QByteArray("png"));
    }
    if (supportedFormats.contains("jpeg") || supportedFormats.contains("jpg")) {
        format->addItem(i18n("JPEG"), QByteArray("jpeg"));
    }
    if (format->count() == 0) {
        QMessageBox::warning(&settings, settings.windowTitle(), i18n("No PNG or JPEG image writer is available."));
        return;
    }
    form->addRow(i18n("Format:"), format);
    auto dpi = new QSpinBox(&settings);
    dpi->setObjectName(QStringLiteral("imageExportDpi"));
    dpi->setRange(36, 1200);
    dpi->setValue(300);
    form->addRow(i18n("Resolution (DPI):"), dpi);
    auto quality = new QSpinBox(&settings);
    quality->setObjectName(QStringLiteral("imageExportQuality"));
    quality->setRange(1, 100);
    quality->setValue(90);
    quality->setEnabled(format->currentData().toByteArray() == "jpeg");
    form->addRow(i18n("JPEG quality:"), quality);
    QObject::connect(format, &QComboBox::currentIndexChanged, &settings, [=]() { quality->setEnabled(format->currentData().toByteArray() == "jpeg"); });
    auto annotations = new QCheckBox(i18n("Include annotations"), &settings);
    annotations->setObjectName(QStringLiteral("imageExportAnnotations"));
    annotations->setChecked(true);
    form->addRow(QString(), annotations);

    auto directoryRow = new QHBoxLayout;
    auto directory = new QLineEdit(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation), &settings);
    directory->setObjectName(QStringLiteral("imageExportDirectory"));
    auto browse = new QPushButton(i18n("Browse…"), &settings);
    directoryRow->addWidget(directory);
    directoryRow->addWidget(browse);
    form->addRow(i18n("Output directory:"), directoryRow);
    QObject::connect(browse, &QPushButton::clicked, &settings, [&]() {
        // An application-owned chooser is closed along with the settings when
        // the document disappears; no native asynchronous dialog is required.
        QFileDialog chooser(&settings, i18n("Choose Output Directory"), directory->text());
        chooser.setOption(QFileDialog::DontUseNativeDialog);
        chooser.setFileMode(QFileDialog::Directory);
        chooser.setOption(QFileDialog::ShowDirsOnly);
        chooser.setWindowModality(Qt::ApplicationModal);
        QObject::connect(&settings, &QDialog::rejected, &chooser, &QDialog::reject);
        if (chooser.exec() == QDialog::Accepted && documentIsValid() && !chooser.selectedFiles().isEmpty()) {
            directory->setText(chooser.selectedFiles().first());
        }
    });
    auto prefix = new QLineEdit(ImageExportUtils::suggestedPrefix(suggestedBaseName), &settings);
    prefix->setObjectName(QStringLiteral("imageExportPrefix"));
    prefix->setMaxLength(80);
    form->addRow(i18n("Filename prefix:"), prefix);
    auto explanation = new QLabel(
        i18n(
            "One file per page, using the original page number (for example document_0001.png).\nCancellation takes effect between pages. Files already written are kept.\nHigh resolutions may require substantial memory for a single page."),
        &settings);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &settings);
    buttons->button(QDialogButtonBox::Save)->setText(i18n("Export"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &settings, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &settings, &QDialog::reject);
    QObject::connect(document, &Document::aboutToClose, &settings, &QDialog::reject);
    QObject::connect(document, &QObject::destroyed, &settings, &QDialog::reject);
    if (parent) {
        QObject::connect(parent, &QObject::destroyed, &settings, &QDialog::reject);
    }

    QList<int> pages;
    QString outputDirectory;
    while (true) {
        if (!documentIsValid() || settings.exec() != QDialog::Accepted || !documentIsValid()) {
            return;
        }
        QString error;
        pages.clear();
        if (selection->currentIndex() == 0) {
            if (validCurrentPage) {
                pages.append(currentPageZeroBased);
            } else {
                error = i18n("There is no current page. Choose all pages or a custom page range.");
            }
        } else if (selection->currentIndex() == 1) {
            pages.reserve(pageCount);
            for (int page = 0; page < pageCount; ++page) {
                pages.append(page);
            }
        } else {
            ImageExportUtils::parsePageRange(range->text(), pageCount, &pages, &error);
        }
        if (error.isEmpty() && !ImageExportUtils::isSafePrefix(prefix->text())) {
            error = i18n("Enter a filename prefix of 1–80 characters without path separators, control characters or <>:\"|?*. Reserved names and trailing dots or spaces are not allowed.");
        }
        const QFileInfo directoryInfo(directory->text());
        if (error.isEmpty() && (directory->text().trimmed().isEmpty() || !directoryInfo.isDir())) {
            error = i18n("Choose an existing output directory.");
        }
        if (!error.isEmpty()) {
            QMessageBox::warning(&settings, settings.windowTitle(), error);
            continue;
        }
        outputDirectory = directoryInfo.canonicalFilePath();
        if (!outputDirectory.isEmpty()) {
            break;
        }
        QMessageBox::warning(&settings, settings.windowTitle(), i18n("The output directory is no longer available."));
    }

    // Snapshot all settings before entering the progress dialog's event loop.
    const QByteArray imageFormat = format->currentData().toByteArray();
    const QString extension = imageFormat == "jpeg" ? QStringLiteral("jpg") : QStringLiteral("png");
    const QString filePrefix = prefix->text();
    const int resolution = dpi->value();
    const int jpegQuality = quality->value();
    const bool includeAnnotations = annotations->isChecked();
    QProgressDialog progress(i18n("Preparing image export…"), i18n("Cancel"), 0, int(pages.size()));
    progress.setObjectName(QStringLiteral("imageExportProgress"));
    setDialogWindow(progress, guardedParent);
    progress.setWindowTitle(i18n("Export Pages as Images"));
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.show();
    int written = 0;
    bool canceled = false;
    bool overwriteAll = false;
    QSet<QString> authorizedTargets;
    QString failure;
    const auto authorizeTarget = [&](const QString &path) {
        const QFileInfo target(path);
        if (target.isSymLink() || (target.exists() && !target.isFile())) {
            failure = i18n("Cannot replace this output path because it is not a regular file:\n%1", path);
            return false;
        }
        if (target.exists() && !overwriteAll && !authorizedTargets.contains(path)) {
            const auto answer = QMessageBox::question(&progress,
                                                      i18n("Replace Existing Image?"),
                                                      i18n("The following file already exists:\n%1\n\nReplace it? “Yes to All” allows replacement of all existing images in this export.", path),
                                                      QMessageBox::Yes | QMessageBox::YesToAll | QMessageBox::Cancel,
                                                      QMessageBox::Cancel);
            overwriteAll = answer == QMessageBox::YesToAll;
            if (answer != QMessageBox::Yes && !overwriteAll) {
                canceled = true;
                return false;
            }
            authorizedTargets.insert(path);
        }
        // Confirmation runs a nested event loop. Recheck the kind afterwards.
        const QFileInfo checkedTarget(path);
        if (checkedTarget.isSymLink() || (checkedTarget.exists() && !checkedTarget.isFile())) {
            failure = i18n("Cannot replace this output path because it is not a regular file:\n%1", path);
            return false;
        }
        return true;
    };

    for (const int page : pages) {
        progress.setLabelText(i18n("Exporting page %1 of %2…\n%3 file(s) written. Cancel stops after the current page; written files are kept.", page + 1, pageCount, written));
        progress.setValue(written); // A modal QProgressDialog may process events here.
        QApplication::processEvents();
        if (!documentIsValid()) {
            failure = i18n("The document or its window was closed or changed. Export stopped.");
            break;
        }
        if (progress.wasCanceled()) {
            canceled = true;
            break;
        }
        const QString path = QDir(outputDirectory).filePath(ImageExportUtils::pageFileName(filePrefix, page, extension));
        if (!authorizeTarget(path)) {
            break;
        }
        // Overwrite confirmation is another nested event loop.
        if (!documentIsValid()) {
            failure = i18n("The document or its window was closed or changed. Export stopped.");
            break;
        }
        if (progress.wasCanceled()) {
            canceled = true;
            break;
        }
        QString renderError;
        const QImage image = guardedDocument->renderToImage(page, resolution, includeAnnotations, &renderError);
        if (!documentIsValid()) {
            failure = i18n("The document or its window was closed or changed. Export stopped.");
            break;
        }
        if (image.isNull()) {
            failure = i18n("Could not render page %1: %2", page + 1, renderError.isEmpty() ? i18n("The renderer returned an empty image.") : renderError);
            break;
        }
        QSaveFile file(path);
        // Never fall back to truncating the destination on filesystems where
        // atomic replacement is unavailable.
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly)) {
            failure = i18n("Could not open %1 for writing: %2", path, file.errorString());
            break;
        }
        QImageWriter writer(&file, imageFormat);
        if (imageFormat == "jpeg") {
            writer.setQuality(jpegQuality);
        }
        if (!writer.write(image)) {
            failure = i18n("Could not write %1: %2", path, writer.errorString());
            file.cancelWriting();
            break;
        }
        // Detect a destination created while rendering/encoding, rather than
        // silently overwriting it. QSaveFile itself is not a no-clobber API;
        // another process can still race the final check and atomic rename.
        const QFileInfo finalTarget(path);
        if (finalTarget.isSymLink() || (finalTarget.exists() && !finalTarget.isFile())) {
            failure = i18n("Cannot replace this output path because it is not a regular file:\n%1", path);
            file.cancelWriting();
            break;
        }
        if (!authorizeTarget(path)) {
            file.cancelWriting();
            break;
        }
        if (!documentIsValid()) {
            failure = i18n("The document or its window was closed or changed. Export stopped.");
            file.cancelWriting();
            break;
        }
        if (progress.wasCanceled()) {
            canceled = true;
            file.cancelWriting();
            break;
        }
        if (!file.commit()) {
            failure = i18n("Could not save %1 atomically: %2", path, file.errorString());
            break;
        }
        ++written;
        // The image and writer are destroyed before the next page is rendered.
    }
    progress.hide();
    // Do not display another modal window during teardown of the caller.
    if (parent && !guardedParent) {
        return;
    }
    if (!failure.isEmpty()) {
        QMessageBox::warning(guardedParent, i18n("Image Export Stopped"), i18n("%1\n\n%2 file(s) were written to:\n%3\nFiles already written have been kept.", failure, written, outputDirectory));
    } else if (canceled) {
        QMessageBox::information(guardedParent, i18n("Image Export Canceled"), i18n("%1 file(s) were written to:\n%2\nFiles already written have been kept.", written, outputDirectory));
    } else {
        QMessageBox::information(guardedParent, i18n("Image Export Complete"), i18n("%1 file(s) were written to:\n%2", written, outputDirectory));
    }
}
