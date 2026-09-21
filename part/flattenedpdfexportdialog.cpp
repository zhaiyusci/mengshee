/*
    SPDX-FileCopyrightText: 2026 Mengshee contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "flattenedpdfexportdialog.h"

#include "core/document.h"

#include <KLocalizedString>

#include <QApplication>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

using namespace Okular;

namespace
{
QString comparablePath(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

bool isSourceFile(const QString &destination, const QUrl &source)
{
    if (!source.isLocalFile()) {
        return false;
    }
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    // Hard-link identity and write-time races must also be checked by the backend.
    return comparablePath(destination).compare(comparablePath(source.toLocalFile()), sensitivity) == 0;
}

class BusyCursor
{
public:
    BusyCursor() { QApplication::setOverrideCursor(Qt::WaitCursor); }
    ~BusyCursor() { QApplication::restoreOverrideCursor(); }
};
}

void FlattenedPdfExportDialog::exportDocument(Document *document, const QUrl &sourceUrl, QWidget *parent)
{
    Q_ASSERT(qApp && QThread::currentThread() == qApp->thread());
    if (!qApp || QThread::currentThread() != qApp->thread()) {
        return;
    }
    static bool exporting = false;
    if (exporting) {
        return;
    }
    const QScopedValueRollback<bool> exportingGuard(exporting, true);
    QPointer<Document> guardedDocument(document);
    QPointer<QWidget> guardedParent(parent);
    // Copy URLs before entering any nested event loop.
    const QUrl originalUrl = sourceUrl;
    const QUrl documentUrl = document ? document->currentDocument() : QUrl();
    bool invalidated = false;
    QObject lifetime;
    if (document) {
        QObject::connect(document, &Document::aboutToClose, &lifetime, [&]() { invalidated = true; });
        QObject::connect(document, &QObject::destroyed, &lifetime, [&]() { invalidated = true; });
    }
    if (parent) {
        QObject::connect(parent, &QObject::destroyed, &lifetime, [&]() { invalidated = true; });
    }
    const auto documentIsValid = [&]() {
        return !invalidated && guardedDocument && guardedDocument->isOpened() && guardedDocument->currentDocument() == documentUrl && guardedDocument->canExportFlattenedPdf();
    };
    const QString title = i18nc("@title:window", "Export Flattened PDF");
    const auto prepareDialog = [&](QDialog &dialog) {
        // Stack dialogs deliberately have no QObject parent: the window/Part
        // can be destroyed while exec() is processing events.
        dialog.setWindowModality(Qt::ApplicationModal);
        dialog.setWindowTitle(title);
        if (guardedParent) {
            dialog.winId();
            guardedParent->window()->winId();
            dialog.windowHandle()->setTransientParent(guardedParent->window()->windowHandle());
            QObject::connect(guardedParent.data(), &QObject::destroyed, &dialog, &QDialog::reject);
        }
        if (guardedDocument) {
            QObject::connect(guardedDocument.data(), &Document::aboutToClose, &dialog, &QDialog::reject);
            QObject::connect(guardedDocument.data(), &QObject::destroyed, &dialog, &QDialog::reject);
        }
    };
    const auto showMessage = [&](QMessageBox::Icon icon, const QString &text, const QString &name) {
        QMessageBox message(icon, title, text, QMessageBox::Ok);
        message.setTextFormat(Qt::PlainText);
        message.setObjectName(name);
        prepareDialog(message);
        message.exec();
    };
    if (!documentIsValid()) {
        if (!invalidated) {
            showMessage(QMessageBox::Warning, i18n("This document cannot be exported as a flattened PDF. Documents with digital signatures are not supported."), QStringLiteral("flattenedPdfExportUnavailable"));
        }
        return;
    }

    QDialog confirmation;
    confirmation.setObjectName(QStringLiteral("flattenedPdfExportDialog"));
    prepareDialog(confirmation);
    auto layout = new QVBoxLayout(&confirmation);
    auto explanation = new QLabel(i18n("Create a final copy of all pages by merging visible drawing and text annotations into the page, preserving vectors. The original document remains editable.\n\nHidden annotations, links and forms are preserved. This is not secure redaction or tamper protection. Export is refused for digitally signed or unsupported documents.\n\nExport runs as a single synchronous operation. The application will be busy until it finishes; cancellation is not available during export."), &confirmation);
    explanation->setObjectName(QStringLiteral("flattenedPdfExportExplanation"));
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &confirmation);
    buttons->setObjectName(QStringLiteral("flattenedPdfExportButtons"));
    buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("flattenedPdfExportContinue"));
    buttons->button(QDialogButtonBox::Save)->setText(i18n("Export…"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("flattenedPdfExportCancel"));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &confirmation, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &confirmation, &QDialog::reject);
    if (confirmation.exec() != QDialog::Accepted || !documentIsValid()) {
        return;
    }

    QString baseName = QFileInfo(originalUrl.fileName()).completeBaseName();
    if (baseName.isEmpty()) {
        baseName = QStringLiteral("document");
    }
    const QString directory = originalUrl.isLocalFile() ? QFileInfo(originalUrl.toLocalFile()).absolutePath() : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QFileDialog chooser;
    chooser.setObjectName(QStringLiteral("flattenedPdfExportFileDialog"));
    prepareDialog(chooser);
    chooser.setOption(QFileDialog::DontUseNativeDialog);
    // Do our own explicit, testable confirmation after rejecting the source.
    chooser.setOption(QFileDialog::DontConfirmOverwrite);
    chooser.setAcceptMode(QFileDialog::AcceptSave);
    chooser.setFileMode(QFileDialog::AnyFile);
    chooser.setNameFilter(i18nc("@item:inlistbox", "PDF documents (*.pdf)"));
    chooser.setDefaultSuffix(QStringLiteral("pdf"));
    chooser.setDirectory(directory);
    chooser.selectFile(baseName + QStringLiteral("-flattened.pdf"));
    QString destination;
    while (documentIsValid()) {
        if (chooser.exec() != QDialog::Accepted || !documentIsValid() || chooser.selectedFiles().isEmpty()) {
            return;
        }
        destination = chooser.selectedFiles().first();
        if (isSourceFile(destination, originalUrl) || isSourceFile(destination, documentUrl)) {
            showMessage(QMessageBox::Warning, i18n("Choose a different file. The source document cannot be overwritten by a flattened copy."), QStringLiteral("flattenedPdfExportSourceWarning"));
            continue;
        }
        if (QFileInfo::exists(destination)) {
            QMessageBox overwrite(QMessageBox::Warning, title, i18n("The file '%1' already exists. Replace it with the flattened PDF?", destination), QMessageBox::Yes | QMessageBox::No);
            overwrite.setTextFormat(Qt::PlainText);
            overwrite.setObjectName(QStringLiteral("flattenedPdfExportOverwriteDialog"));
            overwrite.setDefaultButton(QMessageBox::No);
            overwrite.setEscapeButton(QMessageBox::No);
            overwrite.button(QMessageBox::Yes)->setObjectName(QStringLiteral("flattenedPdfExportOverwriteConfirm"));
            overwrite.button(QMessageBox::No)->setObjectName(QStringLiteral("flattenedPdfExportOverwriteCancel"));
            prepareDialog(overwrite);
            const int answer = overwrite.exec();
            if (!documentIsValid()) {
                return;
            }
            if (answer != QMessageBox::Yes) {
                continue;
            }
        }
        break;
    }
    // Recheck after the last nested event loop, including the overwrite prompt.
    if (!documentIsValid() || destination.isEmpty()) {
        return;
    }
    if (isSourceFile(destination, originalUrl) || isSourceFile(destination, documentUrl)) {
        showMessage(QMessageBox::Warning, i18n("Choose a different file. The source document cannot be overwritten by a flattened copy."), QStringLiteral("flattenedPdfExportSourceWarning"));
        return;
    }
    QString error;
    int flattened = 0;
    int preserved = 0;
    bool succeeded;
    {
        // No processEvents(), save(), reload(), or edits: the backend snapshots
        // the current live model and atomically writes the separate output.
        const BusyCursor busy;
        succeeded = guardedDocument->exportFlattenedPdf(destination, &error, &flattened, &preserved);
    }
    if (invalidated || !guardedDocument) {
        return;
    }
    if (!succeeded) {
        if (error.isEmpty()) {
            error = i18n("The flattened PDF could not be exported.");
        }
        showMessage(QMessageBox::Critical, error, QStringLiteral("flattenedPdfExportError"));
        return;
    }
    showMessage(QMessageBox::Information,
                i18n("Exported to: %1\nFlattened annotations: %2\nPreserved annotations: %3\nThe original document has not been changed.", destination, flattened, preserved),
                QStringLiteral("flattenedPdfExportResult"));
}
