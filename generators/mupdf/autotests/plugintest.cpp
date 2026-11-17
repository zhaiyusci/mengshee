// SPDX-License-Identifier: AGPL-3.0-or-later
// End-to-end phase-1 test: load the single plugin through KPluginFactory, let
// it pick the MuPDF engine module through the registry, and open/render a PDF.

#include <core/generator.h>
#include <core/page.h>

#include <KPluginFactory>
#include <KPluginMetaData>

#include <QDir>
#include <QFileInfo>
#include <QTest>

#include <memory>

class PdfPluginTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        qputenv("MENGSHEE_PDF_ENGINE_PATH", QDir::fromNativeSeparators(QStringLiteral(MENGSHEE_MUPDF_ENGINE_DIR)).toUtf8());
        qunsetenv("MENGSHEE_PDF_ENGINE");
    }

    void loadsThroughFactoryAndRenders()
    {
        const QString pluginPath = QStringLiteral(MENGSHEE_PDF_PLUGIN_PATH);
        QVERIFY2(QFileInfo::exists(pluginPath), qPrintable(pluginPath));

        const KPluginMetaData metadata(pluginPath);
        QVERIFY2(metadata.isValid(), qPrintable(pluginPath));
        QCOMPARE(metadata.pluginId(), QStringLiteral("okularGenerator_mengshee_pdf"));

        const auto result = KPluginFactory::instantiatePlugin<Okular::Generator>(metadata);
        QVERIFY2(result, qPrintable(result.errorText));
        std::unique_ptr<Okular::Generator> generator(result.plugin);
        QVERIFY(generator);

        QList<Okular::Page *> pages;
        QCOMPARE(generator->loadDocumentWithPassword(QStringLiteral(MENGSHEE_TEST_PDF), pages, QString()), Okular::Document::OpenSuccess);
        QVERIFY(!pages.isEmpty());
        QVERIFY(!generator->renderToImage(0, 72, false).isNull());

        qDeleteAll(pages);
        generator->closeDocument();
    }
};

QTEST_GUILESS_MAIN(PdfPluginTest)

#include "plugintest.moc"
