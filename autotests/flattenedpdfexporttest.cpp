/*
    SPDX-FileCopyrightText: 2026 Mengshee contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QPainter>
#include <QPdfWriter>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <filesystem>
#include <memory>

#include "../core/annotations.h"
#include "../core/document.h"
#include "../core/page.h"
#include "../part/flattenedpdfexportdialog.h"
#include "../settings_core.h"

namespace
{
QByteArray contents(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray hash(const QString &path)
{
    return QCryptographicHash::hash(contents(path), QCryptographicHash::Sha256);
}

bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QString appearanceDifference(const QImage &expected, const QImage &actual, const QImage &unannotated)
{
    if (expected.size() != actual.size() || expected.size() != unannotated.size()) {
        return QStringLiteral("Live and flattened appearance dimensions differ");
    }
    qint64 affectedPixels = 0;
    qint64 channelError = 0;
    qint64 largeDifferences = 0;
    for (int y = 0; y < expected.height(); ++y) {
        for (int x = 0; x < expected.width(); ++x) {
            const QColor reference = expected.pixelColor(x, y);
            const QColor baked = actual.pixelColor(x, y);
            const QColor background = unannotated.pixelColor(x, y);
            if (reference != background || baked != background) {
                ++affectedPixels;
            }
            const int red = qAbs(reference.red() - baked.red());
            const int green = qAbs(reference.green() - baked.green());
            const int blue = qAbs(reference.blue() - baked.blue());
            channelError += red + green + blue;
            largeDifferences += qMax(red, qMax(green, blue)) > 32;
        }
    }
    // Normalize against annotation-affected pixels, NOT the entire white page:
    // a missing thin stroke must never pass just because the page is mostly blank.
    // Allow a small AA fringe, but not misplaced/opaque/missing appearances.
    const double meanError = affectedPixels ? double(channelError) / (3.0 * affectedPixels) : 0;
    if (!affectedPixels || meanError > 2.0 || largeDifferences > qMax<qint64>(4, affectedPixels / 100)) {
        return QStringLiteral("Flattened appearance differs from live rendering: %1 affected pixels, mean channel error %2, %3 differences above 32")
            .arg(affectedPixels).arg(meanError).arg(largeDifferences);
    }
    return {};
}

// Opt-in inspection artifacts only: ordinary test runs leave no evidence
// directory. The source PDF is explicitly labelled on-disk because live edits
// are represented by before-with-annotations.png, not by that original file.
QString saveEvidence(const QString &caseName, Okular::Document &sourceDocument, const QString &sourcePath, const QString &outputPath, const QImage &baked)
{
    const QString root = qEnvironmentVariable("MENGSHEE_FLATTEN_TEST_OUTPUT");
    if (root.isEmpty()) {
        return {};
    }
    const QString directory = QDir(root).filePath(caseName);
    if (!QDir().mkpath(directory)) {
        return QStringLiteral("Could not create evidence directory: %1").arg(directory);
    }
    const QDir evidence(directory);
    QString error;
    const QImage before = sourceDocument.renderToImage(0, 72, true, &error);
    if (before.isNull()) {
        return QStringLiteral("Could not render annotated evidence: %1").arg(error);
    }
    if (!before.save(evidence.filePath(QStringLiteral("before-with-annotations.png")), "PNG")
        || !baked.save(evidence.filePath(QStringLiteral("after-without-annotations.png")), "PNG")
        || !writeBytes(evidence.filePath(QStringLiteral("source-on-disk.pdf")), contents(sourcePath))
        || !writeBytes(evidence.filePath(QStringLiteral("flattened.pdf")), contents(outputPath))) {
        return QStringLiteral("Could not write export evidence in: %1").arg(directory);
    }
    return {};
}

QByteArray stream(const QByteArray &commands, const QByteArray &dictionary = {})
{
    return "<< " + dictionary + " /Length " + QByteArray::number(commands.size()) + " >>\nstream\n" + commands + "\nendstream";
}

// Objects are deliberately small, inspectable PDF syntax; no external fixture
// generator, Poppler private headers, or test-time Python dependency is needed.
bool writePdf(const QString &path, const QList<QByteArray> &objects)
{
    QByteArray pdf("%PDF-1.7\n");
    QList<qint64> offsets{0};
    for (qsizetype i = 0; i < objects.size(); ++i) {
        offsets.append(pdf.size());
        pdf += QByteArray::number(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const qint64 xref = pdf.size();
    pdf += "xref\n0 " + QByteArray::number(offsets.size()) + "\n0000000000 65535 f \n";
    for (qsizetype i = 1; i < offsets.size(); ++i) {
        pdf += QByteArray::number(offsets[i]).rightJustified(10, '0') + " 00000 n \n";
    }
    pdf += "trailer\n<< /Size " + QByteArray::number(offsets.size()) + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
    return writeBytes(path, pdf);
}

Okular::Document::OpenResult openPdf(Okular::Document &document, const QString &path)
{
    const auto result = document.openDocument(path, QUrl::fromLocalFile(path), QMimeDatabase().mimeTypeForName(QStringLiteral("application/pdf")));
    if (result == Okular::Document::OpenSuccess) {
        // Document metadata remembers viewer rotation by basename and size.
        // Keep tests independent of prior runs without touching shared docdata.
        document.setRotation(0);
    }
    return result;
}

std::filesystem::path nativePath(const QString &path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(QFile::encodeName(path).constData());
#endif
}

QList<QByteArray> singleAnnotationPdf(const QByteArray &annotation)
{
    return {"<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << >> /Contents 4 0 R /Annots [5 0 R] >>",
            stream("1 1 1 rg 0 0 200 200 re f\n"),
            annotation};
}

struct UiResult {
    QString failure;
    QString summary;
    int confirmations = 0;
    int choosers = 0;
    int warnings = 0;
    int overwrites = 0;
    int results = 0;
};
enum UiScenario { Success, Cancel, Source, RefuseOverwrite, AcceptOverwrite, CloseAndReopen };

UiResult driveUi(Okular::Document &document, const QString &source, const QString &destination, UiScenario scenario)
{
    UiResult result;
    QTimer driver;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &driver, [] { qFatal("Flattened PDF export UI exceeded the 15-second modal timeout"); });
    QObject::connect(&driver, &QTimer::timeout, &driver, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) {
            return;
        }
        const QString name = dialog->objectName();
        if (name == QLatin1String("flattenedPdfExportDialog")) {
            ++result.confirmations;
            if (scenario == Cancel) {
                dialog->reject();
                return;
            }
            auto *button = dialog->findChild<QPushButton *>(QStringLiteral("flattenedPdfExportContinue"));
            if (!button) {
                result.failure = QStringLiteral("Missing export confirmation button");
                dialog->reject();
                return;
            }
            button->click();
        } else if (auto *chooser = qobject_cast<QFileDialog *>(dialog)) {
            ++result.choosers;
            if (result.choosers > 1) {
                chooser->reject();
                return;
            }
            if (chooser->objectName() != QLatin1String("flattenedPdfExportFileDialog") || !chooser->selectedFiles().value(0).endsWith(QLatin1String("flattened-source-flattened.pdf"))) {
                result.failure = QStringLiteral("Unexpected chooser identity or suggested filename");
                chooser->reject();
                return;
            }
            if (scenario == CloseAndReopen) {
                document.closeDocument();
                if (openPdf(document, source) != Okular::Document::OpenSuccess) {
                    result.failure = QStringLiteral("Could not reopen source during modal chooser");
                }
                return;
            }
            chooser->selectFile(scenario == Source ? source : destination);
            QMetaObject::invokeMethod(chooser, "accept", Qt::DirectConnection);
        } else if (auto *message = qobject_cast<QMessageBox *>(dialog)) {
            if (name == QLatin1String("flattenedPdfExportOverwriteDialog")) {
                ++result.overwrites;
                const auto answer = scenario == AcceptOverwrite ? QMessageBox::Yes : QMessageBox::No;
                if (auto *button = message->button(answer)) {
                    button->click();
                } else {
                    result.failure = QStringLiteral("Missing explicit overwrite choice");
                    message->reject();
                }
            } else if (name == QLatin1String("flattenedPdfExportSourceWarning")) {
                ++result.warnings;
                message->accept();
            } else if (name == QLatin1String("flattenedPdfExportResult")) {
                ++result.results;
                result.summary = message->text();
                message->accept();
            } else {
                result.failure = QStringLiteral("Unexpected export message: ") + message->text();
                message->reject();
            }
        } else {
            result.failure = QStringLiteral("Unexpected modal dialog: ") + name;
            dialog->reject();
        }
    });
    driver.start(0);
    watchdog.start(15000);
    Okular::FlattenedPdfExportDialog::exportDocument(&document, QUrl::fromLocalFile(source));
    driver.stop();
    watchdog.stop();
    return result;
}
}

class FlattenedPdfExportTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void liveAnnotations_data();
    void liveAnnotations();
    void pageOrderAndNativeRotation();
    void invalidDestinations();
    void hardLinkSource();
    void unsupportedAndSigned_data();
    void unsupportedAndSigned();
    void encryptedDocument();
    void appearanceAndPreservedObjects();
    void uiWorkflow_data();
    void uiWorkflow();
    void preservedWidgetStacking_data();
    void preservedWidgetStacking();
    void latexStamp_data();
    void latexStamp();
    void missingAppearance_data();
    void missingAppearance();

private:
    QTemporaryDir m_dir;
    QString m_source;
    std::unique_ptr<Okular::Document> m_document;
};

void FlattenedPdfExportTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    Okular::SettingsCore::instance(QStringLiteral("flattenedpdfexporttest"));
    QVERIFY(m_dir.isValid());
    m_source = m_dir.filePath(QStringLiteral("flattened-source.pdf"));
    QPdfWriter writer(m_source);
    writer.setResolution(72);
    writer.setPageSize(QPageSize(QSizeF(144, 216), QPageSize::Point));
    writer.setPageMargins(QMarginsF(), QPageLayout::Point);
    QPainter painter(&writer);
    QVERIFY(painter.isActive());
    painter.fillRect(QRect(0, 0, 144, 216), Qt::white);
    painter.fillRect(QRect(10, 10, 40, 40), Qt::red);
    QVERIFY(writer.newPage());
    painter.fillRect(QRect(0, 0, 144, 216), Qt::white);
    painter.fillRect(QRect(10, 10, 40, 40), Qt::blue);
    QVERIFY(painter.end());
}

void FlattenedPdfExportTest::init()
{
    m_document = std::make_unique<Okular::Document>(nullptr);
    QCOMPARE(openPdf(*m_document, m_source), Okular::Document::OpenSuccess);
    QVERIFY(m_document->canExportFlattenedPdf());
}

void FlattenedPdfExportTest::cleanup()
{
    m_document->setRotation(0);
    m_document->closeDocument();
    m_document.reset();
}

void FlattenedPdfExportTest::liveAnnotations_data()
{
    QTest::addColumn<int>("kind");
    QTest::addColumn<int>("nativeRotation");
    QTest::newRow("unsaved-geometry") << 0 << 0;
    QTest::newRow("unsaved-vector-free-text") << 1 << 0;
    QTest::newRow("unsaved-ink") << 2 << 0;
    QTest::newRow("rotated-live-geometry") << 0 << 90;
    QTest::newRow("rotated-live-ink") << 2 << 90;
}

void FlattenedPdfExportTest::liveAnnotations()
{
    QFETCH(int, kind);
    QFETCH(int, nativeRotation);
    const QByteArray beforeHash = hash(m_source);
    QString error;
    if (nativeRotation != 0) {
        QVERIFY2(m_document->rotatePage(0, nativeRotation, &error), qPrintable(error));
    }
    const QImage before = m_document->renderToImage(0, 72, false, &error);
    QVERIFY2(!before.isNull(), qPrintable(error));
    Okular::Annotation *annotation = nullptr;
    if (kind == 0) {
        auto *geometry = new Okular::GeomAnnotation;
        geometry->setGeometricalType(Okular::GeomAnnotation::InscribedSquare);
        geometry->setGeometricalInnerColor(Qt::green);
        annotation = geometry;
    } else if (kind == 1) {
        auto *text = new Okular::TextAnnotation;
        text->setTextType(Okular::TextAnnotation::InPlace);
        text->setContents(QStringLiteral("VECTOR FINAL"));
        text->setTextFont(QFont(QStringLiteral("Helvetica"), 12));
        text->setTextColor(Qt::black);
        annotation = text;
    } else {
        auto *ink = new Okular::InkAnnotation;
        ink->setInkPaths({{Okular::NormalizedPoint(0.4, 0.4), Okular::NormalizedPoint(0.6, 0.7), Okular::NormalizedPoint(0.8, 0.4)}});
        annotation = ink;
    }
    annotation->setBoundingRectangle(Okular::NormalizedRect(0.35, 0.35, 0.9, 0.8));
    annotation->style().setColor(Qt::green);
    annotation->style().setWidth(3);
    if (kind == 2) {
        // Generated translucent ink appearances contain nested Form streams;
        // these must be lifted to indirect objects in the serialized PDF.
        annotation->style().setOpacity(0.5);
    }
    m_document->addPageAnnotation(0, annotation);
    const auto annotations = m_document->page(0)->annotations();
    QCOMPARE(annotations.size(), 1);
    QVERIFY(m_document->canUndo());
    QVERIFY(!m_document->canRedo());
    QSignalSpy undoChanged(m_document.get(), &Okular::Document::canUndoChanged);
    QSignalSpy redoChanged(m_document.get(), &Okular::Document::canRedoChanged);
    const QString output = m_dir.filePath(QStringLiteral("定稿-%1-旋转%2.pdf").arg(kind).arg(nativeRotation));
    int flattened = -1, preserved = -1;
    QVERIFY2(m_document->exportFlattenedPdf(output, &error, &flattened, &preserved), qPrintable(error));
    QVERIFY(error.isEmpty());
    QCOMPARE(flattened, 1);
    QCOMPARE(preserved, 0);
    QCOMPARE(hash(m_source), beforeHash);
    QCOMPARE(m_document->page(0)->annotations(), annotations);
    QVERIFY(m_document->canUndo());
    QVERIFY(!m_document->canRedo());
    QCOMPARE(undoChanged.count(), 0);
    QCOMPARE(redoChanged.count(), 0);
    QCOMPARE(m_document->renderToImage(0, 72, false, &error), before);
    Okular::Document exported(nullptr);
    QCOMPARE(openPdf(exported, output), Okular::Document::OpenSuccess);
    QCOMPARE(exported.pages(), 2u);
    QVERIFY(exported.page(0)->annotations().isEmpty());
    const QImage baked = exported.renderToImage(0, 72, false, &error);
    QVERIFY2(!baked.isNull(), qPrintable(error));
    QVERIFY(baked != before);
    QCOMPARE(exported.renderToImage(0, 72, true, &error), baked);
    const QString evidenceError = saveEvidence(QString::fromLatin1(QTest::currentDataTag()), *m_document, m_source, output, baked);
    QVERIFY2(evidenceError.isEmpty(), qPrintable(evidenceError));
    // Deliberately render live annotations only AFTER export. Rendering them
    // before export could generate an AP and hide a missing-AP export defect.
    const QImage live = m_document->renderToImage(0, 72, true, &error);
    QVERIFY2(!live.isNull(), qPrintable(error));
    const QString difference = appearanceDifference(live, baked, before);
    QVERIFY2(difference.isEmpty(), qPrintable(difference));
    if (kind == 1) {
        // Searchable text proves the export did not merely rasterize the page.
        exported.requestTextPage(0);
        QVERIFY2(exported.page(0)->text().contains(QLatin1String("VECTOR")), qPrintable(exported.page(0)->text()));
    }
    m_document->undo();
    QVERIFY(m_document->page(0)->annotations().isEmpty());
    QVERIFY(m_document->canRedo());
    m_document->redo();
    QCOMPARE(m_document->page(0)->annotations().size(), 1);
    QCOMPARE(hash(m_source), beforeHash);
    exported.closeDocument();
}

void FlattenedPdfExportTest::pageOrderAndNativeRotation()
{
    const QByteArray originalHash = hash(m_source);
    QString error;
    QVERIFY2(m_document->movePage(0, 1, &error), qPrintable(error));
    QVERIFY2(m_document->rotatePage(0, 90, &error), qPrintable(error));
    const QImage expectedFirst = m_document->renderToImage(0, 72, false, &error);
    const QImage expectedSecond = m_document->renderToImage(1, 72, false, &error);
    QCOMPARE(expectedFirst.size(), QSize(216, 144));
    QCOMPARE(expectedFirst.pixelColor(186, 30), QColor(Qt::blue));
    // Viewer-only rotation is not a source edit and must not be baked in.
    m_document->setRotation(1);
    const QString output = m_dir.filePath(QStringLiteral("page-order.pdf"));
    QVERIFY2(m_document->exportFlattenedPdf(output, &error), qPrintable(error));
    QCOMPARE(hash(m_source), originalHash);
    Okular::Document exported(nullptr);
    QCOMPARE(openPdf(exported, output), Okular::Document::OpenSuccess);
    QCOMPARE(exported.pages(), 2u);
    QCOMPARE(exported.renderToImage(0, 72, false, &error), expectedFirst);
    QCOMPARE(exported.renderToImage(1, 72, false, &error), expectedSecond);
    m_document->setRotation(0);
    exported.closeDocument();
}

void FlattenedPdfExportTest::invalidDestinations()
{
    const QByteArray originalHash = hash(m_source);
    QString error;
    for (const QString &path : {QString(), m_source, m_dir.path(), m_dir.filePath(QStringLiteral("missing-directory/output.pdf"))}) {
        int flattened = 99, preserved = 99;
        QVERIFY(!m_document->exportFlattenedPdf(path, &error, &flattened, &preserved));
        QVERIFY(!error.isEmpty());
        QCOMPARE(flattened, 0);
        QCOMPARE(preserved, 0);
        QCOMPARE(hash(m_source), originalHash);
    }
    m_document->closeDocument();
    QVERIFY(!m_document->canExportFlattenedPdf());
    QVERIFY(!m_document->exportFlattenedPdf(m_dir.filePath(QStringLiteral("closed.pdf")), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFileInfo::exists(m_dir.filePath(QStringLiteral("closed.pdf"))));
}

void FlattenedPdfExportTest::hardLinkSource()
{
    const QByteArray originalHash = hash(m_source);
    const QString alias = m_dir.filePath(QStringLiteral("hardlink.pdf"));
    std::error_code ec;
    std::filesystem::create_hard_link(nativePath(m_source), nativePath(alias), ec);
    if (ec) {
        QSKIP(qPrintable(QStringLiteral("Test filesystem cannot create hard links: %1").arg(QString::fromStdString(ec.message()))));
    }
    QString error;
    QVERIFY(!m_document->exportFlattenedPdf(alias, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(hash(m_source), originalHash);
    QCOMPARE(hash(alias), originalHash);
    QVERIFY(QFile::remove(alias));
}

void FlattenedPdfExportTest::unsupportedAndSigned_data()
{
    QTest::addColumn<QByteArray>("annotation");
    QTest::newRow("visible-text-nozoom") << QByteArray("<< /Type /Annot /Subtype /Text /Rect [30 30 60 60] /F 8 /Contents (Unsupported NoZoom) >>");
    QTest::newRow("digital-signature") << QByteArray("<< /Type /Annot /Subtype /Widget /FT /Sig /T (Signature) /Rect [30 30 60 60] /V << /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached /ByteRange [0 0 0 0] /Contents <> >> >>");
}

void FlattenedPdfExportTest::unsupportedAndSigned()
{
    QFETCH(QByteArray, annotation);
    m_document->closeDocument();
    const QString source = m_dir.filePath(QStringLiteral("refused.pdf"));
    QVERIFY(writePdf(source, singleAnnotationPdf(annotation)));
    QCOMPARE(openPdf(*m_document, source), Okular::Document::OpenSuccess);
    QVERIFY(m_document->canExportFlattenedPdf()); // Capability does not scan signatures.
    const QByteArray sourceHash = hash(source);
    const QString output = m_dir.filePath(QStringLiteral("existing-target.pdf"));
    const QByteArray sentinel("Existing destination must survive a failed export.\n");
    QVERIFY(writeBytes(output, sentinel));
    QString error;
    int flattened = 99, preserved = 99;
    QVERIFY(!m_document->exportFlattenedPdf(output, &error, &flattened, &preserved));
    QVERIFY(!error.isEmpty());
    QCOMPARE(flattened, 0);
    QCOMPARE(preserved, 0);
    QCOMPARE(contents(output), sentinel);
    QCOMPARE(hash(source), sourceHash);
}

void FlattenedPdfExportTest::encryptedDocument()
{
    // A one-page RC4-128 PDF with empty user password and nonempty owner
    // password. Embedded once so CI requires neither qpdf nor Python/pypdf.
    const QByteArray encrypted = QByteArray::fromBase64(
        "JVBERi0xLjMKJeLjz9MKMSAwIG9iago8PAovUHJvZHVjZXIgPDQzNDQxYTI3MzA+Cj4+CmVuZG9iagoyIDAgb2JqCjw8Ci9UeXBlIC9QYWdlcwovQ291bnQgMQovS2lkcyBbIDQgMCBSIF0KPj4KZW5kb2JqCjMgMCBvYmoKPDwKL1R5cGUgL0NhdGFsb2cKL1BhZ2VzIDIgMCBSCj4+CmVuZG9iago0IDAgb2JqCjw8Ci9UeXBlIC9QYWdlCi9SZXNvdXJjZXMgPDwKPj4KL01lZGlhQm94IFsgMC4wIDAuMCAyMDAgMjAwIF0KL1BhcmVudCAyIDAgUgo+PgplbmRvYmoKNSAwIG9iago8PAovViAyCi9SIDMKL0xlbmd0aCAxMjgKL1AgNDI5NDk2NzI5MgovRmlsdGVyIC9TdGFuZGFyZAovTyA8ODlkZjMwZmRjNzYwOGJiYzg5ODFkYjkyNTkxYzFlNzZmODczNjY5ZjY3YWI2M2EyZWZkOTlkYTY3NzMxYzJiZT4KL1UgPGRjNDRjMDdkODJjMGZhMDhmMjBhN2E4NGMxM2IwODE1MjhiZjRlNWU0ZTc1OGE0MTY0MDA0ZTU2ZmZmYTAxMDg+Cj4+CmVuZG9iagp4cmVmCjAgNgowMDAwMDAwMDAwIDY1NTM1IGYgCjAwMDAwMDAwMTUgMDAwMDAgbiAKMDAwMDAwMDA1OSAwMDAwMCBuIAowMDAwMDAwMTE4IDAwMDAwIG4gCjAwMDAwMDAxNjcgMDAwMDAgbiAKMDAwMDAwMDI2MSAwMDAwMCBuIAp0cmFpbGVyCjw8Ci9TaXplIDYKL1Jvb3QgMyAwIFIKL0luZm8gMSAwIFIKL0lEIFsgPDM1Mzk2MzMyMzA2MjYyNjE2NTYzMzgzMjY1MzE2MjM1NjMzNjMzNjM2MTYyNjU2NjM1NjE2NjYxNjU2NjMxMzE+IDwzNTM5NjMzMjMwNjI2MjYxNjU2MzM4MzI2NTMxNjIzNTYzMzYzMzYzNjE2MjY1NjYzNTYxNjY2MTY1NjYzMTMxPiBdCi9FbmNyeXB0IDUgMCBSCj4+CnN0YXJ0eHJlZgo0NzYKJSVFT0YK");
    const QString source = m_dir.filePath(QStringLiteral("encrypted.pdf"));
    QVERIFY(writeBytes(source, encrypted));
    m_document->closeDocument();
    QCOMPARE(openPdf(*m_document, source), Okular::Document::OpenSuccess);
    QVERIFY(m_document->canExportFlattenedPdf());
    QString error;
    const QString output = m_dir.filePath(QStringLiteral("encrypted-output.pdf"));
    QVERIFY(!m_document->exportFlattenedPdf(output, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFileInfo::exists(output));
    QCOMPARE(contents(source), encrypted);
}

void FlattenedPdfExportTest::appearanceAndPreservedObjects()
{
    // Explicit AP state selects translucent blue, not the unused opaque red
    // appearance. Nonidentity Matrix exercises transformed BBox placement.
    const QList<QByteArray> objects = {
        "<< /Type /Catalog /Pages 2 0 R /Outlines 11 0 R /AcroForm 14 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << /Font << /Helv 4 0 R >> >> /Contents 6 0 R /Annots [5 0 R 8 0 R 9 0 R 10 0 R] >>",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        "<< /Type /Annot /Subtype /Square /Rect [40 40 140 100] /F 4 /AP << /N << /On 7 0 R /Off 15 0 R >> >> /AS /On >>",
        stream("1 1 1 rg 0 0 200 200 re f\n"),
        stream("/GS gs 0 0 1 rg 0 0 100 50 re f\n", "/Type /XObject /Subtype /Form /BBox [0 0 100 50] /Matrix [0 1 -1 0 50 0] /Resources << /ExtGState << /GS << /Type /ExtGState /ca 0.5 /CA 0.5 >> >> >>"),
        "<< /Type /Annot /Subtype /Square /Rect [150 20 180 50] /F 2 /Contents (Hidden annotation) /C [1 0 0] >>",
        "<< /Type /Annot /Subtype /Link /Rect [10 10 30 30] /Border [0 0 0] /A << /S /URI /URI (https://example.org/flattened-test) >> >>",
        "<< /Type /Annot /Subtype /Widget /FT /Tx /T (PreservedField) /V (Keep me) /Rect [10 150 100 180] /F 4 /DA (/Helv 12 Tf 0 g) /P 3 0 R >>",
        "<< /Type /Outlines /First 12 0 R /Last 12 0 R /Count 1 >>",
        "<< /Title (Preserved bookmark) /Parent 11 0 R /Dest [3 0 R /Fit] >>",
        "null",
        "<< /Fields [10 0 R] /DA (/Helv 12 Tf 0 g) /DR << /Font << /Helv 4 0 R >> >> >>",
        stream("1 0 0 rg 0 0 100 50 re f\n", "/Type /XObject /Subtype /Form /BBox [0 0 100 50] /Resources << >>")};
    const QString source = m_dir.filePath(QStringLiteral("appearance.pdf"));
    QVERIFY(writePdf(source, objects));
    m_document->closeDocument();
    QCOMPARE(openPdf(*m_document, source), Okular::Document::OpenSuccess);
    const QByteArray beforeHash = hash(source);
    QString error;
    const QImage original = m_document->renderToImage(0, 72, true, &error);
    QVERIFY2(!original.isNull(), qPrintable(error));
    const QString output = m_dir.filePath(QStringLiteral("appearance-flat.pdf"));
    int flattened = 0, preserved = 0;
    QVERIFY2(m_document->exportFlattenedPdf(output, &error, &flattened, &preserved), qPrintable(error));
    QCOMPARE(flattened, 1);
    QCOMPARE(preserved, 3); // hidden annotation, link, widget
    QCOMPARE(hash(source), beforeHash);
    Okular::Document exported(nullptr);
    QCOMPARE(openPdf(exported, output), Okular::Document::OpenSuccess);
    const QImage baked = exported.renderToImage(0, 72, false, &error);
    QVERIFY2(!baked.isNull(), qPrintable(error));
    const QString evidenceError = saveEvidence(QStringLiteral("appearance-matrix-alpha"), *m_document, source, output, baked);
    QVERIFY2(evidenceError.isEmpty(), qPrintable(evidenceError));
    const QColor center = baked.pixelColor(90, 130);
    QVERIFY(center.blue() > 245);
    QVERIFY(center.red() > 110 && center.red() < 145);
    QVERIFY(center.green() > 110 && center.green() < 145);
    QCOMPARE(baked.copy(QRect(35, 95, 110, 70)), original.copy(QRect(35, 95, 110, 70)));
    bool foundHidden = false;
    for (auto *annotation : exported.page(0)->annotations()) {
        if (annotation->contents() == QLatin1String("Hidden annotation")) {
            foundHidden = true;
            QVERIFY(annotation->flags() & Okular::Annotation::Hidden);
        }
    }
    QVERIFY(foundHidden);
    QCOMPARE(exported.page(0)->formFields().size(), 1);
    const auto *synopsis = exported.documentSynopsis();
    QVERIFY(synopsis);
    QVERIFY(synopsis->toString().contains(QLatin1String("Preserved bookmark")));
    // URI is outside all content streams, so its preservation is also visible
    // in the serialized object graph without relying on link hit-test geometry.
    QVERIFY(contents(output).contains("https://example.org/flattened-test"));
    exported.closeDocument();
}

void FlattenedPdfExportTest::uiWorkflow_data()
{
    QTest::addColumn<int>("scenarioValue");
    QTest::newRow("continue-choose-result") << int(Success);
    QTest::newRow("cancel-confirmation") << int(Cancel);
    QTest::newRow("refuse-source-file") << int(Source);
    QTest::newRow("refuse-existing-file-overwrite") << int(RefuseOverwrite);
    QTest::newRow("confirm-existing-file-overwrite") << int(AcceptOverwrite);
    QTest::newRow("close-reopen-during-chooser") << int(CloseAndReopen);
}

void FlattenedPdfExportTest::uiWorkflow()
{
    QFETCH(int, scenarioValue);
    const auto scenario = UiScenario(scenarioValue);
    QTemporaryDir outputDirectory;
    QVERIFY(outputDirectory.isValid());
    const QString output = outputDirectory.filePath(QStringLiteral("定稿.pdf"));
    const QByteArray sentinel("Do not overwrite without explicit consent.");
    const bool exists = scenario == RefuseOverwrite || scenario == AcceptOverwrite;
    if (exists) {
        QVERIFY(writeBytes(output, sentinel));
    }
    const QByteArray originalHash = hash(m_source);
    const UiResult result = driveUi(*m_document, m_source, output, scenario);
    QVERIFY2(result.failure.isEmpty(), qPrintable(result.failure));
    QCOMPARE(result.confirmations, 1);
    QCOMPARE(result.overwrites, exists ? 1 : 0);
    QCOMPARE(result.warnings, scenario == Source ? 1 : 0);
    const bool success = scenario == Success || scenario == AcceptOverwrite;
    QCOMPARE(result.results, success ? 1 : 0);
    QCOMPARE(hash(m_source), originalHash);
    QVERIFY(!QApplication::overrideCursor());
    if (success) {
        QVERIFY(result.summary.contains(output));
        QVERIFY(contents(output).startsWith("%PDF-"));
        Okular::Document exported(nullptr);
        QCOMPARE(openPdf(exported, output), Okular::Document::OpenSuccess);
        QCOMPARE(exported.pages(), 2u);
        exported.closeDocument();
    } else if (exists) {
        QCOMPARE(contents(output), sentinel);
    } else {
        QVERIFY(!QFileInfo::exists(output));
    }
}

void FlattenedPdfExportTest::preservedWidgetStacking_data()
{
    QTest::addColumn<bool>("widgetFirst");
    QTest::addColumn<bool>("overlap");
    QTest::addColumn<bool>("expectedSuccess");
    QTest::newRow("overlapping-widget-before-stamp-refused") << true << true << false;
    QTest::newRow("overlapping-widget-after-stamp-preserved") << false << true << true;
    QTest::newRow("disjoint-widget-before-stamp-preserved") << true << false << true;
}

void FlattenedPdfExportTest::preservedWidgetStacking()
{
    QFETCH(bool, widgetFirst);
    QFETCH(bool, overlap);
    QFETCH(bool, expectedSuccess);
    const QByteArray order = widgetFirst ? "6 0 R 5 0 R" : "5 0 R 6 0 R";
    const QByteArray widgetRect = overlap ? "40 40 100 100" : "120 120 180 180";
    const QList<QByteArray> objects = {
        "<< /Type /Catalog /Pages 2 0 R /AcroForm 9 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << >> /Contents 4 0 R /Annots [" + order + "] >>",
        stream("1 1 1 rg 0 0 200 200 re f\n"),
        "<< /Type /Annot /Subtype /Stamp /Rect [40 40 100 100] /F 4 /AP << /N 7 0 R >> >>",
        "<< /Type /Annot /Subtype /Widget /FT /Tx /T (StackingField) /V (Keep me) /Rect [" + widgetRect + "] /F 4 /DA (/Helv 12 Tf 0 g) /AP << /N 10 0 R >> /P 3 0 R >>",
        stream("0 0 1 rg 0 0 60 60 re f\n", "/Type /XObject /Subtype /Form /BBox [0 0 60 60] /Resources << >>"),
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        "<< /Fields [6 0 R] /DA (/Helv 12 Tf 0 g) /DR << /Font << /Helv 8 0 R >> >> >>",
        stream("1 0 0 rg 0 0 60 60 re f\n", "/Type /XObject /Subtype /Form /BBox [0 0 60 60] /Resources << >>")};
    const QString source = m_dir.filePath(QStringLiteral("stacking-source.pdf"));
    const QString output = m_dir.filePath(QStringLiteral("stacking-target.pdf"));
    m_document->closeDocument();
    QVERIFY(writePdf(source, objects));
    QCOMPARE(openPdf(*m_document, source), Okular::Document::OpenSuccess);
    const QByteArray sourceHash = hash(source);
    const QByteArray sentinel("Destination must survive unsafe z-order rejection.");
    QVERIFY(writeBytes(output, sentinel));
    QString error;
    int flattened = -1, preserved = -1;
    const bool succeeded = m_document->exportFlattenedPdf(output, &error, &flattened, &preserved);
    QCOMPARE(succeeded, expectedSuccess);
    QCOMPARE(hash(source), sourceHash);
    if (!expectedSuccess) {
        QVERIFY(!error.isEmpty());
        QCOMPARE(contents(output), sentinel);
        QCOMPARE(flattened, 0);
        QCOMPARE(preserved, 0);
        return;
    }
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(flattened, 1);
    QCOMPARE(preserved, 1);
    Okular::Document exported(nullptr);
    QCOMPARE(openPdf(exported, output), Okular::Document::OpenSuccess);
    QCOMPARE(exported.page(0)->formFields().size(), 1);
    const QImage baked = exported.renderToImage(0, 72, true, &error);
    QVERIFY2(!baked.isNull(), qPrintable(error));
    QCOMPARE(baked.pixelColor(70, 130), QColor(overlap ? Qt::red : Qt::blue));
    exported.closeDocument();
}

void FlattenedPdfExportTest::latexStamp_data()
{
    QTest::addColumn<int>("nativeRotation");
    QTest::newRow("latex-stamp-0") << 0;
    QTest::newRow("latex-stamp-90") << 90;
    QTest::newRow("latex-stamp-180") << 180;
    QTest::newRow("latex-stamp-270") << 270;
}

void FlattenedPdfExportTest::latexStamp()
{
    QFETCH(int, nativeRotation);
    QString error;
    const QByteArray originalHash = hash(m_source);
    if (nativeRotation != 0) {
        QVERIFY2(m_document->rotatePage(0, nativeRotation, &error), qPrintable(error));
    }
    const QImage background = m_document->renderToImage(0, 72, false, &error);
    QVERIFY2(!background.isNull(), qPrintable(error));
    const QString appearanceFile = m_dir.filePath(QStringLiteral("latex-note-appearance-%1.pdf").arg(nativeRotation));
    QVERIFY(QFile::copy(QStringLiteral(":/mengshee/data/latex-default-note.pdf"), appearanceFile));
    auto *stamp = new Okular::StampAnnotation;
    stamp->setBoundingRectangle(Okular::NormalizedRect(0.2, 0.25, 0.8, 0.55));
    stamp->setContents(QStringLiteral("Rotated LaTeX final copy"));
    stamp->setOkularLatex(true);
    QVERIFY(stamp->flags() & Okular::Annotation::FixedRotation);
    stamp->setLatexNoteType(Okular::Annotation::LatexNoteBoxed);
    stamp->setLatexAppearancePdfFileName(appearanceFile);
    stamp->setLatexLayoutWidth(120.0);
    stamp->setLatexPadding(3.0);
    stamp->setLatexTextColor(Qt::black);
    stamp->setLatexFillColor(QColor(QStringLiteral("#ffff00")));
    stamp->setLatexBorderColor(Qt::red);
    stamp->style().setWidth(2.0);
    m_document->addPageAnnotation(0, stamp);
    const auto originalAnnotations = m_document->page(0)->annotations();
    const QString stampName = stamp->uniqueName();
    QVERIFY(m_document->canUndo());
    QVERIFY(!m_document->canRedo());
    QSignalSpy undoChanged(m_document.get(), &Okular::Document::canUndoChanged);
    QSignalSpy redoChanged(m_document.get(), &Okular::Document::canRedoChanged);
    const QString output = m_dir.filePath(QStringLiteral("latex-flat-%1.pdf").arg(nativeRotation));
    int flattened = -1, preserved = -1;
    // No live annotated render before export: importing the existing vector
    // appearance must work without launching StemTeX or painting the page first.
    QVERIFY2(m_document->exportFlattenedPdf(output, &error, &flattened, &preserved), qPrintable(error));
    QCOMPARE(flattened, 1);
    QCOMPARE(preserved, 0);
    QCOMPARE(hash(m_source), originalHash);
    QCOMPARE(m_document->page(0)->annotations(), originalAnnotations);
    QVERIFY(stamp->isOkularLatex());
    QVERIFY(stamp->flags() & Okular::Annotation::FixedRotation);
    QVERIFY(m_document->canUndo());
    QVERIFY(!m_document->canRedo());
    QCOMPARE(undoChanged.count(), 0);
    QCOMPARE(redoChanged.count(), 0);
    Okular::Document exported(nullptr);
    QCOMPARE(openPdf(exported, output), Okular::Document::OpenSuccess);
    QVERIFY(exported.page(0)->annotations().isEmpty());
    const QImage baked = exported.renderToImage(0, 72, false, &error);
    QVERIFY2(!baked.isNull(), qPrintable(error));
    QVERIFY(baked != background);
    const QString evidenceError = saveEvidence(QString::fromLatin1(QTest::currentDataTag()), *m_document, m_source, output, baked);
    QVERIFY2(evidenceError.isEmpty(), qPrintable(evidenceError));
    const QImage live = m_document->renderToImage(0, 72, true, &error);
    QVERIFY2(!live.isNull(), qPrintable(error));
    const QString difference = appearanceDifference(live, baked, background);
    QVERIFY2(difference.isEmpty(), qPrintable(difference));
    // The source retains the editable LaTeX stamp, including through undo/redo.
    m_document->undo();
    QVERIFY(m_document->page(0)->annotations().isEmpty());
    QVERIFY(m_document->canRedo());
    m_document->redo();
    const auto *restored = m_document->page(0)->annotation(stampName);
    QVERIFY(restored);
    QVERIFY(restored->isOkularLatex());
    QVERIFY(restored->flags() & Okular::Annotation::FixedRotation);
    QCOMPARE(hash(m_source), originalHash);
    exported.closeDocument();
}

void FlattenedPdfExportTest::missingAppearance_data()
{
    QTest::addColumn<QByteArray>("annotation");
    QTest::addColumn<int>("nativeRotation");
    const QByteArray line("<< /Type /Annot /Subtype /Line /Rect [20 20 180 180] /L [20 30 160 170] /F 4 /C [0 0.6 0] /CA 0.5 /BS << /W 4 >> /LE [/None /None] >>");
    const QByteArray highlight("<< /Type /Annot /Subtype /Highlight /Rect [30 110 170 140] /QuadPoints [30 140 170 140 30 110 170 110] /F 4 /C [1 1 0] /CA 0.5 >>");
    QTest::newRow("missing-ap-line-0") << line << 0;
    QTest::newRow("missing-ap-highlight-0") << highlight << 0;
    QTest::newRow("missing-ap-line-90") << line << 90;
    QTest::newRow("missing-ap-highlight-90") << highlight << 90;
}

void FlattenedPdfExportTest::missingAppearance()
{
    QFETCH(QByteArray, annotation);
    QFETCH(int, nativeRotation);
    auto objects = singleAnnotationPdf(annotation);
    // Neither /AP nor annotation /P is present. Native rotation is in the file,
    // so this is an untouched loaded annotation, not a live annotation save.
    const QByteArray rotatedPage = "/Type /Page /Rotate " + QByteArray::number(nativeRotation) + " ";
    objects[2].replace("/Type /Page ", rotatedPage);
    const QString source = m_dir.filePath(QString::fromLatin1(QTest::currentDataTag()) + QStringLiteral(".pdf"));
    const QString output = m_dir.filePath(QString::fromLatin1(QTest::currentDataTag()) + QStringLiteral("-flat.pdf"));
    m_document->closeDocument();
    QVERIFY(writePdf(source, objects));
    const QByteArray sourceHash = hash(source);
    QCOMPARE(openPdf(*m_document, source), Okular::Document::OpenSuccess);
    const auto originalAnnotations = m_document->page(0)->annotations();
    QCOMPARE(originalAnnotations.size(), 1);
    QString error;
    int flattened = -1, preserved = -1;
    // Absolutely no renderToImage()/saveChanges() before this call.
    QVERIFY2(m_document->exportFlattenedPdf(output, &error, &flattened, &preserved), qPrintable(error));
    QCOMPARE(flattened, 1);
    QCOMPARE(preserved, 0);
    QCOMPARE(hash(source), sourceHash);
    QCOMPARE(m_document->page(0)->annotations(), originalAnnotations);
    Okular::Document exported(nullptr);
    QCOMPARE(openPdf(exported, output), Okular::Document::OpenSuccess);
    QVERIFY(exported.page(0)->annotations().isEmpty());
    const QImage baked = exported.renderToImage(0, 72, false, &error);
    QVERIFY2(!baked.isNull(), qPrintable(error));
    const QString evidenceError = saveEvidence(QString::fromLatin1(QTest::currentDataTag()), *m_document, source, output, baked);
    QVERIFY2(evidenceError.isEmpty(), qPrintable(evidenceError));
    const QImage background = m_document->renderToImage(0, 72, false, &error);
    const QImage live = m_document->renderToImage(0, 72, true, &error);
    QVERIFY2(!background.isNull() && !live.isNull(), qPrintable(error));
    QVERIFY(baked != background);
    const QString difference = appearanceDifference(live, baked, background);
    QVERIFY2(difference.isEmpty(), qPrintable(difference));
    exported.closeDocument();
}

QTEST_MAIN(FlattenedPdfExportTest)
#include "flattenedpdfexporttest.moc"
