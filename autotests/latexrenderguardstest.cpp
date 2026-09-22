/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "part/latexrenderguards.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <limits>

using namespace GuiUtils::LatexRenderGuards;

class LatexRenderGuardsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void input_data();
    void input();
    void sourceIsUnchanged();
    void pdfArtifact();
};

void LatexRenderGuardsTest::input_data()
{
    QTest::addColumn<QString>("source");
    QTest::addColumn<double>("width");
    QTest::addColumn<double>("fontSize");
    QTest::addColumn<int>("expected");

    const QString source = QStringLiteral("\\[x^2\\]");
    const auto row = [&](const char *name, const QString &text, double width, double fontSize, InputError expected) {
        QTest::newRow(name) << text << width << fontSize << static_cast<int>(expected);
    };
    row("defaults", source, 0, 0, InputError::None);
    row("ordinary", source, 360, 10, InputError::None);
    row("maximum-dimensions", source, 50000, 200, InputError::None);
    row("minimum-font", source, 1, 1, InputError::None);
    row("positive-fractional-width", source, 0.001, 10, InputError::None);
    row("null-source", QString(), 0, 0, InputError::EmptySource);
    row("empty-source", QStringLiteral(""), 0, 0, InputError::EmptySource);
    row("whitespace", QStringLiteral(" \t\r\n\u2003"), 0, 0, InputError::EmptySource);
    row("maximum-source", QString(250000, QLatin1Char('x')), 0, 0, InputError::None);
    row("oversized-source", QString(250001, QLatin1Char('x')), 0, 0, InputError::SourceTooLarge);
    row("embedded-null", QStringLiteral("x\0y"), 0, 0, InputError::EmbeddedNull);
    row("only-null", QString(1, QChar(u'\0')), 0, 0, InputError::EmbeddedNull);
    row("negative-width", source, -1, 10, InputError::InvalidWidth);
    row("excessive-width", source, 50000.001, 10, InputError::InvalidWidth);
    row("negative-font", source, 360, -1, InputError::InvalidFontSize);
    row("fractional-font", source, 360, 0.5, InputError::InvalidFontSize);
    row("excessive-font", source, 360, 200.001, InputError::InvalidFontSize);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    row("nan-width", source, nan, 10, InputError::InvalidWidth);
    row("infinite-width", source, infinity, 10, InputError::InvalidWidth);
    row("negative-infinite-width", source, -infinity, 10, InputError::InvalidWidth);
    row("nan-font", source, 360, nan, InputError::InvalidFontSize);
    row("infinite-font", source, 360, infinity, InputError::InvalidFontSize);
    row("negative-infinite-font", source, 360, -infinity, InputError::InvalidFontSize);
}

void LatexRenderGuardsTest::input()
{
    QFETCH(QString, source);
    QFETCH(double, width);
    QFETCH(double, fontSize);
    QFETCH(int, expected);
    const QString original = source;
    QCOMPARE(static_cast<int>(validateInput(source, width, fontSize)), expected);
    QCOMPARE(source, original);
}

void LatexRenderGuardsTest::sourceIsUnchanged()
{
    const QString source = QStringLiteral(" \r\n% comment\r\n\\[x\\]\n\n ");
    const QString original = source;
    QCOMPARE(validateInput(source, 0, 0), InputError::None);
    QCOMPARE(source, original);
}

void LatexRenderGuardsTest::pdfArtifact()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(!isUsablePdfArtifact(directory.path()));
    QVERIFY(!isUsablePdfArtifact(directory.filePath(QStringLiteral("missing.pdf"))));

    const QString path = directory.filePath(QStringLiteral("artifact.pdf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    QVERIFY(!isUsablePdfArtifact(path));

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write("%PDF"), qint64(4));
    file.close();
    QVERIFY(!isUsablePdfArtifact(path));

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write("not a PDF"), qint64(9));
    file.close();
    QVERIFY(!isUsablePdfArtifact(path));

    // The signature alone deliberately suffices: this is not a PDF parser.
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write("%PDF-"), qint64(5));
    file.close();
    QVERIFY(isUsablePdfArtifact(path));

    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.resize(maxPdfBytes));
    file.close();
    QVERIFY(isUsablePdfArtifact(path));

    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.resize(maxPdfBytes + 1));
    file.close();
    QVERIFY(!isUsablePdfArtifact(path));
}

QTEST_GUILESS_MAIN(LatexRenderGuardsTest)

#include "latexrenderguardstest.moc"
