// SPDX-License-Identifier: AGPL-3.0-or-later
// End-to-end phase-1 test: load the single plugin, force the Poppler engine,
// and open/render a PDF through the runtime module copy.

#include <core/generator.h>
#include <core/page.h>

#include <KPluginFactory>
#include <KPluginMetaData>

#include <QDir>
#include <QFileInfo>
#include <QTest>

#include <memory>

class PopplerPluginTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        const QFileInfo moduleInfo(QStringLiteral(MENGSHEE_POPPLER_ENGINE_PATH));
        qputenv("MENGSHEE_PDF_ENGINE_PATH", QDir::fromNativeSeparators(moduleInfo.absolutePath()).toUtf8());
        qputenv("MENGSHEE_PDF_ENGINE", "poppler");
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
        QVERIFY2(!pages.isEmpty(), "the test PDF should contain pages");
        QString renderError;
        const QImage image = generator->renderToImage(0, 72, false, &renderError);
        QVERIFY2(!image.isNull(), qPrintable(renderError));

        qDeleteAll(pages);
        generator->closeDocument();
    }
};

QTEST_MAIN(PopplerPluginTest)

#include "plugintest.moc"
