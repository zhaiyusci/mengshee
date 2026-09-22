/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "part/latexpdfbounds.h"
#include <PdfPageBounds.h>
#include <poppler-qt6.h>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace
{
QByteArray pdf(const QByteArray &commands, const QByteArray &pageExtras = {})
{
    const QList<QByteArray> objects = {
        QByteArrayLiteral("<< /Type /Catalog /Pages 2 0 R >>"),
        QByteArrayLiteral("<< /Type /Pages /Count 1 /Kids [3 0 R] >>"),
        QByteArray(QByteArrayLiteral("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R ") + pageExtras + QByteArrayLiteral(" >>")),
        QByteArray(QByteArrayLiteral("<< /Length ") + QByteArray::number(commands.size()) + QByteArrayLiteral(" >>\nstream\n") + commands + QByteArrayLiteral("\nendstream")),
        QByteArrayLiteral("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"),
    };
    QByteArray data = QByteArrayLiteral("%PDF-1.4\n");
    QList<qsizetype> offsets;
    for (qsizetype i = 0; i < objects.size(); ++i) {
        offsets.append(data.size());
        data += QByteArray::number(i + 1) + QByteArrayLiteral(" 0 obj\n") + objects[i] + QByteArrayLiteral("\nendobj\n");
    }
    const qsizetype xref = data.size();
    data += QByteArrayLiteral("xref\n0 6\n0000000000 65535 f \n");
    for (const auto offset : offsets) {
        data += QByteArray::number(offset).rightJustified(10, '0') + QByteArrayLiteral(" 00000 n \n");
    }
    data += QByteArrayLiteral("trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n") + QByteArray::number(xref) + QByteArrayLiteral("\n%%EOF\n");
    return data;
}
bool save(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
}

class LatexPdfBoundsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void vectorBounds_data();
    void vectorBounds();
    void textOutsidePage();
    void noOverwrite();
    void rejectedInputUnchanged();
};

void LatexPdfBoundsTest::vectorBounds_data()
{
    QTest::addColumn<QByteArray>("commands");
    QTest::addColumn<QSizeF>("expected");
    QTest::newRow("empty-keeps-logical-frame") << QByteArray() << QSizeF(100, 100);
    QTest::newRow("small-ink-keeps-logical-frame") << QByteArrayLiteral("10 10 5 5 re f\n") << QSizeF(100, 100);
    QTest::newRow("left-overhang") << QByteArrayLiteral("-10 20 5 5 re f\n") << QSizeF(111, 100);
    QTest::newRow("right-overhang") << QByteArrayLiteral("98 20 5 5 re f\n") << QSizeF(104, 100);
    QTest::newRow("bottom-overhang") << QByteArrayLiteral("20 -10 5 5 re f\n") << QSizeF(100, 111);
    QTest::newRow("top-overhang") << QByteArrayLiteral("20 98 5 5 re f\n") << QSizeF(100, 104);
    QTest::newRow("pdf-literal-sized-rectangle") << QByteArrayLiteral("-20 -20 300 60 re f\n") << QSizeF(302, 121);
    QTest::newRow("author-clip-is-respected") << QByteArrayLiteral("q 0 0 100 100 re W n -20 10 5 5 re f Q\n") << QSizeF(100, 100);
}

void LatexPdfBoundsTest::vectorBounds()
{
    QFETCH(QByteArray, commands);
    QFETCH(QSizeF, expected);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString input = dir.filePath(QStringLiteral("input.pdf"));
    const QString output = dir.filePath(QStringLiteral("output.pdf"));
    const QByteArray original = pdf(commands);
    QVERIFY(save(input, original));
    const auto result = PdfPageBounds::expandAppearance(input.toUtf8().toStdString(), output.toUtf8().toStdString(), 1.0);
    QVERIFY2(result.ok, result.message.c_str());
    QCOMPARE(load(input), original);
    auto doc = Poppler::Document::load(output);
    QVERIFY(doc);
    QCOMPARE(doc->numPages(), 1);
    auto page = doc->page(0);
    QVERIFY(page);
    const QSizeF size = page->pageSizeF();
    QVERIFY(qAbs(size.width() - expected.width()) < 0.05);
    QVERIFY(qAbs(size.height() - expected.height()) < 0.05);
    QVERIFY(!page->renderToImage().isNull());
    // Poppler holds a Windows input-file handle; close it before asking for an
    // atomic replacement, just as the SDK converter does before returning.
    page.reset();
    doc.reset();
    // A second pass must not keep adding padding or change its logical frame.
    QString error;
    QVERIFY2(GuiUtils::LatexPdfBounds::expandInPlace(output, error), qPrintable(error));
    auto again = Poppler::Document::load(output);
    QVERIFY(again);
    auto againPage = again->page(0);
    QVERIFY(againPage);
    QVERIFY(qAbs(againPage->pageSizeF().width() - size.width()) < 0.05);
    QVERIFY(qAbs(againPage->pageSizeF().height() - size.height()) < 0.05);
}

void LatexPdfBoundsTest::textOutsidePage()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("text.pdf"));
    QVERIFY(save(path, pdf(QByteArrayLiteral("BT /F1 10 Tf -15 50 Td (outside) Tj ET\n"))));
    QString error;
    QVERIFY2(GuiUtils::LatexPdfBounds::expandInPlace(path, error), qPrintable(error));
    auto doc = Poppler::Document::load(path);
    QVERIFY(doc);
    auto page = doc->page(0);
    QVERIFY(page);
    QVERIFY(page->pageSizeF().width() > 110);
    QVERIFY(page->text(QRectF()).contains(QStringLiteral("outside")));
    QVERIFY(!page->renderToImage().isNull());
}

void LatexPdfBoundsTest::noOverwrite()
{
    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("input.pdf"));
    const QString output = dir.filePath(QStringLiteral("exists.pdf"));
    const QByteArray original = pdf(QByteArrayLiteral("-10 10 5 5 re f\n"));
    const QByteArray sentinel = QByteArrayLiteral("do not replace me");
    QVERIFY(save(input, original));
    QVERIFY(save(output, sentinel));
    QVERIFY(!PdfPageBounds::expandAppearance(input.toUtf8().toStdString(), output.toUtf8().toStdString()).ok);
    QCOMPARE(load(input), original);
    QCOMPARE(load(output), sentinel);
    QVERIFY(!PdfPageBounds::expandAppearance(input.toUtf8().toStdString(), input.toUtf8().toStdString()).ok);
    QCOMPARE(load(input), original);
}

void LatexPdfBoundsTest::rejectedInputUnchanged()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("invalid.pdf"));
    const QList<QByteArray> invalid = {
        QByteArrayLiteral("not a PDF"),
        pdf(QByteArrayLiteral("1 1 1 1 re f\n"), QByteArrayLiteral("/Rotate 90")),
        pdf(QByteArrayLiteral("BT /F1 10 Tf (H) Tj ET\n"), QByteArrayLiteral("/UserUnit 2")),
        pdf(QByteArrayLiteral("q 5000 0 0 1 0 0 cm BT /F1 10 Tf (H) Tj ET Q\n")),
        pdf(QByteArrayLiteral("q 1 0 0 0.00001 0 0 cm BT /F1 10 Tf (H) Tj ET Q\n")),
    };
    for (const QByteArray &bytes : invalid) {
        QVERIFY(save(path, bytes));
        QString error;
        QVERIFY(!GuiUtils::LatexPdfBounds::expandInPlace(path, error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(load(path), bytes);
    }
}

QTEST_GUILESS_MAIN(LatexPdfBoundsTest)
#include "latexpdfboundstest.moc"
