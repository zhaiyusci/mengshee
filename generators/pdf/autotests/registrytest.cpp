// SPDX-License-Identifier: AGPL-3.0-or-later
#include "pdfengineregistry.h"
#include "pdfutils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace Mengshee::Pdf;

namespace
{

#ifdef Q_OS_WIN
bool isModuleLoadedInThisProcess(const QString &moduleFileName)
{
    return GetModuleHandleW(reinterpret_cast<LPCWSTR>(moduleFileName.utf16())) != nullptr;
}
#endif

} // namespace

class PdfEngineRegistryTest : public QObject
{
    Q_OBJECT

private:
    static QString fakeEngineDirectory()
    {
        return QDir::fromNativeSeparators(QStringLiteral(FAKE_ENGINE_DIR));
    }

private Q_SLOTS:
    void initTestCase()
    {
        qunsetenv("MENGSHEE_PDF_ENGINE");
        PdfEngineRegistry::instance().clearOverrideEngineId();
        PdfEngineRegistry::instance().setDefaultEngineId({});
        PdfEngineRegistry::instance().addSearchPath(fakeEngineDirectory());
    }

    void cleanup()
    {
        qunsetenv("MENGSHEE_PDF_ENGINE");
        PdfEngineRegistry::instance().clearOverrideEngineId();
        PdfEngineRegistry::instance().setDefaultEngineId({});
    }

    void discoveryDoesNotLoadEngineModules()
    {
#ifdef Q_OS_WIN
        // build/bin also contains the real Poppler module; lazy discovery must
        // leave both files on disk until one of them is actually selected.
        QVERIFY2(!isModuleLoadedInThisProcess(QStringLiteral("MengsheePdfEngineFake.dll")),
                 "The fake module was loaded before discovery started.");
        QVERIFY2(!isModuleLoadedInThisProcess(QStringLiteral("MengsheePdfEnginePoppler.dll")),
                 "The Poppler module was loaded before discovery started.");

        const auto engines = PdfEngineRegistry::instance().availableEngines();
        QVERIFY(!engines.isEmpty());

        QVERIFY2(!isModuleLoadedInThisProcess(QStringLiteral("MengsheePdfEngineFake.dll")),
                 "availableEngines() must enumerate modules without loading them.");
        QVERIFY2(!isModuleLoadedInThisProcess(QStringLiteral("MengsheePdfEnginePoppler.dll")),
                 "Discovery must not load the unselected Poppler module.");
#else
        QSKIP("The in-process module check uses the Win32 loader.");
#endif
    }

    void discoversModule()
    {
        const auto engines = PdfEngineRegistry::instance().availableEngines();
        QVERIFY2(!engines.isEmpty(), qPrintable(PdfEngineRegistry::instance().searchPaths().join(QStringLiteral("; "))));

        bool found = false;
        for (const auto &engine : engines) {
            QVERIFY(!engine.id.isEmpty());
            QVERIFY(!engine.displayName.isEmpty());
            QVERIFY(QFileInfo::exists(engine.modulePath));
            if (engine.id == QLatin1String("fake")) {
                found = true;
                // The display name comes from the file name convention, not
                // from the module descriptor, because discovery never loads it.
                QCOMPARE(engine.displayName, QStringLiteral("fake"));
            }
        }
        QVERIFY(found);
        QVERIFY(PdfEngineRegistry::instance().hasEngine(QStringLiteral("fake")));
    }

    void createsEngineThroughModuleAbi()
    {
        QString error;
        EnginePtr engine = PdfEngineRegistry::instance().createEngine(QStringLiteral("fake"), &error);
        QVERIFY2(engine, qPrintable(error));
        QCOMPARE(engine->id(), QStringLiteral("fake"));

        PdfDocument *document = engine->document();
        QVERIFY(document);
        QCOMPARE(document->openFile(QStringLiteral("dummy.pdf"), {}), OpenStatus::Success);
        QCOMPARE(document->pageCount(), 1);
        const PageGeometry geometry = document->pageGeometry(0);
        QCOMPARE(geometry.cropBox, QSizeF(100, 50));
        QCOMPARE(geometry.rotation, 0);
        document->close();
    }

    void missingEngineReportsError()
    {
        QString error;
        EnginePtr engine = PdfEngineRegistry::instance().createEngine(QStringLiteral("does-not-exist"), &error);
        QVERIFY(!engine);
        QVERIFY(!error.isEmpty());
        QVERIFY(error.contains(QStringLiteral("does-not-exist")));
    }

    void explicitModulePathCreatesEngine()
    {
        const QString modulePath = QDir(fakeEngineDirectory()).filePath(QStringLiteral("MengsheePdfEngineFake.dll"));
        QVERIFY2(QFileInfo::exists(modulePath), qPrintable(modulePath));

        QString error;
        EnginePtr engine = PdfEngineRegistry::instance().createEngineFromModule(modulePath, &error);
        QVERIFY2(engine, qPrintable(error));
        QCOMPARE(engine->id(), QStringLiteral("fake"));
    }

    void selectionPrecedence()
    {
        PdfEngineRegistry &registry = PdfEngineRegistry::instance();
        QVERIFY(!registry.selectedEngineId().isEmpty());

        registry.setDefaultEngineId(QStringLiteral("fake"));
        QCOMPARE(registry.selectedEngineId(), QStringLiteral("fake"));

        registry.setOverrideEngineId(QStringLiteral("override-wins"));
        QCOMPARE(registry.selectedEngineId(), QStringLiteral("override-wins"));

        registry.clearOverrideEngineId();
        qputenv("MENGSHEE_PDF_ENGINE", "fake");
        QCOMPARE(registry.selectedEngineId(), QStringLiteral("fake"));
    }

    void selectedEngineCreatesInstance()
    {
        PdfEngineRegistry::instance().setDefaultEngineId(QStringLiteral("fake"));
        QString error;
        EnginePtr engine = PdfEngineRegistry::instance().createSelectedEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        QCOMPARE(engine->id(), QStringLiteral("fake"));
    }

    void environmentSelectsByPath()
    {
        const QString modulePath = QDir(fakeEngineDirectory()).filePath(QStringLiteral("MengsheePdfEngineFake.dll"));
        qputenv("MENGSHEE_PDF_ENGINE", QDir::toNativeSeparators(modulePath).toUtf8());
        QString error;
        EnginePtr engine = PdfEngineRegistry::instance().createSelectedEngine(&error);
        QVERIFY2(engine, qPrintable(error));
        QCOMPARE(engine->id(), QStringLiteral("fake"));
    }

    void utilityUsesSelectedEngine()
    {
        PdfEngineRegistry::instance().setDefaultEngineId(QStringLiteral("fake"));
        const PageSizeResult result = firstPageLogicalSize(QStringLiteral("dummy.pdf"));
        QVERIFY2(result.ok, qPrintable(result.errorText));
        QCOMPARE(result.sizePoints, QSizeF(100, 50));
    }

    void utilityUsesExplicitEngine()
    {
        const PageSizeResult result = firstPageLogicalSize(QStringLiteral("dummy.pdf"), QStringLiteral("fake"));
        QVERIFY2(result.ok, qPrintable(result.errorText));
        QCOMPARE(result.sizePoints, QSizeF(100, 50));
    }

    void utilityReportsMissingEngine()
    {
        const PageSizeResult result = firstPageLogicalSize(QStringLiteral("dummy.pdf"), QStringLiteral("does-not-exist"));
        QVERIFY(!result.ok);
        QVERIFY(!result.errorText.isEmpty());
    }

    void moduleDescriptorMustMatchFileName()
    {
        // Copy the fake module under a name that claims a different engine id.
        // Discovery trusts the file name, so createEngine() must load it,
        // notice the descriptor id mismatch, unload it and report an
        // actionable error. Use an alias that no real module in the build tree
        // provides, so the test is independent of which engines are present.
        const QString alias = QStringLiteral("mupdfalias");
        QTemporaryDir directory;
        QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));

        const QString fakePath = QDir(fakeEngineDirectory()).filePath(QStringLiteral("MengsheePdfEngineFake.dll"));
        const QString copiedPath = QDir(directory.path()).filePath(QStringLiteral("MengsheePdfEngineMupdfAlias.dll"));
        QVERIFY2(QFile::copy(fakePath, copiedPath), qPrintable(copiedPath));

        PdfEngineRegistry &registry = PdfEngineRegistry::instance();
        registry.addSearchPath(directory.path());

        QString error;
        EnginePtr engine = registry.createEngine(alias, &error);
        QVERIFY(!engine);
        QVERIFY2(error.contains(alias), qPrintable(error));
        QVERIFY2(error.contains(QStringLiteral("fake")), qPrintable(error));
        QVERIFY2(error.contains(QStringLiteral("MengsheePdfEngine<Id>.dll")), qPrintable(error));

        // The rejected module must not poison the registry: the real fake
        // engine still loads, and a second attempt reports the mismatch again.
        QString fakeError;
        EnginePtr fakeEngine = registry.createEngine(QStringLiteral("fake"), &fakeError);
        QVERIFY2(fakeEngine, qPrintable(fakeError));
        QCOMPARE(fakeEngine->id(), QStringLiteral("fake"));

        QString secondError;
        EnginePtr secondAttempt = registry.createEngine(alias, &secondError);
        QVERIFY(!secondAttempt);
        QVERIFY2(secondError.contains(QStringLiteral("fake")), qPrintable(secondError));

        // A rejected module is unloaded again, so its file is no longer locked.
        QVERIFY2(directory.remove(), qPrintable(directory.path()));
    }
};

QTEST_GUILESS_MAIN(PdfEngineRegistryTest)

#include "registrytest.moc"
