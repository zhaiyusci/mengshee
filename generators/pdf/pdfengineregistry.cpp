// SPDX-License-Identifier: AGPL-3.0-or-later
#include "pdfengineregistry.h"

#include "pdfenginemodule.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace Mengshee
{
namespace Pdf
{
namespace
{

constexpr auto engineFileNamePrefix = "MengsheePdfEngine";
constexpr auto engineSearchSubdirectory = "mengshee_pdf";
constexpr auto enginePathVariable = "MENGSHEE_PDF_ENGINE_PATH";
constexpr auto engineIdVariable = "MENGSHEE_PDF_ENGINE";

bool looksLikeModulePath(const QString &value)
{
    return value.contains(QLatin1Char('/')) || value.contains(QLatin1Char('\\')) || value.endsWith(QLatin1String(".dll"), Qt::CaseInsensitive) || value.endsWith(QLatin1String(".so"), Qt::CaseInsensitive);
}

int engineRank(const QString &id)
{
    if (id == QLatin1String(MuPdfEngineId)) {
        return 0;
    }
    if (id == QLatin1String(PopplerEngineId)) {
        return 1;
    }
    return 2;
}

QStringList applicationDefaultSearchPaths()
{
    if (!QCoreApplication::instance()) {
        return {};
    }
    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    return {
        QDir::cleanPath(applicationDirectory.filePath(QStringLiteral("plugins/%1").arg(QLatin1String(engineSearchSubdirectory)))),
        QDir::cleanPath(applicationDirectory.filePath(QLatin1String(engineSearchSubdirectory))),
    };
}

/**
 * Derives the engine id from a module file name without loading the module.
 *
 * The convention is MengsheePdfEngine<Id>.<library suffix>, where <Id> written
 * in lower case is the engine id: MengsheePdfEngineMuPdf.dll is "mupdf" and
 * MengsheePdfEnginePoppler.dll is "poppler". Returns an empty string when the
 * name does not follow the convention.
 */
QString engineIdFromFileName(const QString &fileName)
{
    const QString prefix = QLatin1String(engineFileNamePrefix);
    const QString stem = QFileInfo(fileName).completeBaseName();
    if (!stem.startsWith(prefix, Qt::CaseInsensitive)) {
        return {};
    }
    return stem.mid(prefix.size()).toLower();
}

QString displayNameForEngineId(const QString &id)
{
    if (id == QLatin1String(MuPdfEngineId)) {
        return QStringLiteral("MuPDF");
    }
    if (id == QLatin1String(PopplerEngineId)) {
        return QStringLiteral("Poppler");
    }
    return id;
}

void writeIdMismatchError(QString *errorText, const QString &modulePath, const QString &descriptorId, const QString &requestedId)
{
    if (!errorText) {
        return;
    }
    *errorText = QStringLiteral("PDF engine module \"%1\" declares engine id \"%2\", but \"%3\" was requested. "
                                "The module file name must be MengsheePdfEngine<Id>.dll with <Id> matching the descriptor id; "
                                "rename the module or rebuild it with the expected id.")
                     .arg(modulePath, descriptorId, requestedId);
}

} // namespace

class PdfEngineRegistry::Private
{
public:
    /** A module found on disk; this does not mean the module is loaded. */
    struct DiscoveredModule {
        QString id;
        QString displayName;
        QString path;
    };

    /** A module whose QLibrary has been loaded and whose descriptor was validated. */
    struct LoadedModule {
        QString id;
        QString path;
        const EngineModuleDescriptor *descriptor = nullptr;
    };

    mutable QMutex mutex;
    QStringList extraSearchPaths;
    QString defaultEngineId;
    QString overrideEngineId;

    mutable bool scanned = false;
    mutable QList<EngineInfo> engines;
    mutable QHash<QString, QString> modulePathsById;
    mutable QHash<QString, LoadedModule> loadedModulesByPath;
    mutable std::vector<std::unique_ptr<QLibrary>> libraries;

    QStringList searchPathsLocked() const
    {
        QStringList result;
        QSet<QString> seen;

        const auto append = [&result, &seen](const QString &path) {
            if (path.isEmpty()) {
                return;
            }
            const QString cleaned = QDir::cleanPath(path);
            const QString key = QDir::fromNativeSeparators(cleaned).toCaseFolded();
            if (!seen.contains(key)) {
                seen.insert(key);
                result.append(cleaned);
            }
        };

        for (const QString &path : extraSearchPaths) {
            append(path);
        }

        const QStringList environmentPaths = qEnvironmentVariable(enginePathVariable).split(QDir::listSeparator(), Qt::SkipEmptyParts);
        for (const QString &path : environmentPaths) {
            append(path.trimmed());
        }

        for (const QString &path : applicationDefaultSearchPaths()) {
            append(path);
        }

        return result;
    }

    /**
     * Enumerates the search paths and records id/display name/path from the
     * file names only. No module is loaded here, so selecting one engine never
     * pulls the other engine's DLL (and its dependencies) into the process.
     */
    void scanLocked() const
    {
        if (scanned) {
            return;
        }
        scanned = true;
        engines.clear();
        modulePathsById.clear();

        QSet<QString> seenIds;
        for (const QString &directoryPath : searchPathsLocked()) {
            const QDir directory(directoryPath);
            if (!directory.exists()) {
                continue;
            }
            const QFileInfoList entries = directory.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
            for (const QFileInfo &entry : entries) {
                if (!QLibrary::isLibrary(entry.absoluteFilePath())) {
                    continue;
                }
                const QString id = engineIdFromFileName(entry.fileName());
                if (id.isEmpty()) {
                    continue;
                }
                if (seenIds.contains(id)) {
                    qWarning().noquote() << QStringLiteral("PDF engine id \"%1\" is provided by more than one module; keeping the first").arg(id);
                    continue;
                }
                seenIds.insert(id);

                DiscoveredModule module;
                module.id = id;
                module.displayName = displayNameForEngineId(id);
                module.path = QDir::cleanPath(entry.absoluteFilePath());

                modulePathsById.insert(module.id, module.path);
                engines.append({module.id, module.displayName, module.path});
            }
        }

        std::stable_sort(engines.begin(), engines.end(), [](const EngineInfo &left, const EngineInfo &right) {
            const int leftRank = engineRank(left.id);
            const int rightRank = engineRank(right.id);
            if (leftRank != rightRank) {
                return leftRank < rightRank;
            }
            return left.id < right.id;
        });
    }

    /**
     * Loads exactly one module and validates its descriptor. expectedId is the
     * id the caller selected; when it is not empty the descriptor id must match
     * it, otherwise the load is rejected and the library is unloaded again.
     * createEngineFromModule() passes an empty expectedId and keeps the old
     * "load whatever this path contains" behavior.
     */
    std::optional<LoadedModule> loadModuleLocked(const QString &modulePath, const QString &expectedId, QString *errorText) const
    {
        const QString absolutePath = QFileInfo(modulePath).absoluteFilePath();
        const auto existing = loadedModulesByPath.constFind(absolutePath);
        if (existing != loadedModulesByPath.constEnd()) {
            if (!expectedId.isEmpty() && existing->id != expectedId) {
                writeIdMismatchError(errorText, absolutePath, existing->id, expectedId);
                return std::nullopt;
            }
            return *existing;
        }

        auto library = std::make_unique<QLibrary>(absolutePath);
        if (!library->load()) {
            if (errorText) {
                *errorText = QStringLiteral("Could not load PDF engine module \"%1\": %2").arg(absolutePath, library->errorString());
            }
            return std::nullopt;
        }

        // QLibrary keeps a loaded library mapped until process exit unless
        // unload() is called explicitly, so every rejection below unloads it.
        const auto reject = [&library]() {
            library->unload();
        };

        using DescriptorFunction = const EngineModuleDescriptor *(*)();
        const auto resolveError = [&absolutePath](QString *target, const QString &detail) {
            if (target) {
                *target = QStringLiteral("PDF engine module \"%1\" is invalid: %2").arg(absolutePath, detail);
            }
        };
        const auto symbol = library->resolve("mengshee_pdf_engine_module");
        if (!symbol) {
            resolveError(errorText, QStringLiteral("missing mengshee_pdf_engine_module entry point"));
            reject();
            return std::nullopt;
        }

        const auto descriptorFunction = reinterpret_cast<DescriptorFunction>(symbol);
        const EngineModuleDescriptor *descriptor = descriptorFunction();
        if (!descriptor) {
            resolveError(errorText, QStringLiteral("the entry point returned null"));
            reject();
            return std::nullopt;
        }
        if (descriptor->apiVersion != EngineModuleApiVersion) {
            resolveError(errorText,
                         QStringLiteral("API version %1, expected %2").arg(descriptor->apiVersion).arg(EngineModuleApiVersion));
            reject();
            return std::nullopt;
        }
        if (!descriptor->id || !*descriptor->id || !descriptor->displayName || !descriptor->create || !descriptor->destroy) {
            resolveError(errorText, QStringLiteral("descriptor has an empty id, display name, create or destroy"));
            reject();
            return std::nullopt;
        }

        LoadedModule module;
        module.id = QString::fromUtf8(descriptor->id);
        module.path = absolutePath;
        module.descriptor = descriptor;

        if (!expectedId.isEmpty() && module.id != expectedId) {
            writeIdMismatchError(errorText, absolutePath, module.id, expectedId);
            reject();
            return std::nullopt;
        }

        // Keep the QLibrary object (and therefore the module) alive for as
        // long as the registry exists; engine instances call destroy() from it.
        qInfo().noquote() << QStringLiteral("PDF engine module loaded: id=%1 path=%2 build=%3")
                                 .arg(module.id, module.path, QString::fromUtf8(descriptor->buildInfo ? descriptor->buildInfo : ""));
        libraries.push_back(std::move(library));
        loadedModulesByPath.insert(absolutePath, module);
        return module;
    }

    EnginePtr createFromModuleLocked(const LoadedModule &module, QString *errorText) const
    {
        void *raw = nullptr;
        try {
            raw = module.descriptor->create();
        } catch (...) {
            if (errorText) {
                *errorText = QStringLiteral("PDF engine \"%1\" threw while creating an instance").arg(module.id);
            }
            return {};
        }
        if (!raw) {
            if (errorText) {
                *errorText = QStringLiteral("PDF engine \"%1\" could not create an instance").arg(module.id);
            }
            return {};
        }
        return EnginePtr(static_cast<PdfEngine *>(raw), EngineDeleter{module.descriptor->destroy});
    }

    /** Loads and creates exactly the requested engine; everything else stays on disk. */
    EnginePtr createEngineLocked(const QString &id, QString *errorText) const
    {
        scanLocked();
        const auto modulePath = modulePathsById.constFind(id);
        if (modulePath == modulePathsById.constEnd()) {
            if (errorText) {
                QStringList available;
                for (const EngineInfo &engine : engines) {
                    available.append(engine.id);
                }
                *errorText = QStringLiteral("PDF engine \"%1\" is not available. Available engines: %2")
                                 .arg(id, available.isEmpty() ? QStringLiteral("none") : available.join(QStringLiteral(", ")));
            }
            return {};
        }
        const auto module = loadModuleLocked(*modulePath, id, errorText);
        if (!module) {
            return {};
        }
        return createFromModuleLocked(*module, errorText);
    }
};

PdfEngineRegistry::PdfEngineRegistry()
    : d(new Private)
{
}

PdfEngineRegistry::~PdfEngineRegistry()
{
    delete d;
}

PdfEngineRegistry &PdfEngineRegistry::instance()
{
    static PdfEngineRegistry registry;
    return registry;
}

void PdfEngineRegistry::addSearchPath(const QString &directory)
{
    if (directory.trimmed().isEmpty()) {
        return;
    }
    QMutexLocker locker(&d->mutex);
    const QString cleaned = QDir::cleanPath(directory);
    if (!d->extraSearchPaths.contains(cleaned)) {
        d->extraSearchPaths.append(cleaned);
        d->scanned = false;
    }
}

QStringList PdfEngineRegistry::searchPaths() const
{
    QMutexLocker locker(&d->mutex);
    return d->searchPathsLocked();
}

QList<PdfEngineRegistry::EngineInfo> PdfEngineRegistry::availableEngines() const
{
    QMutexLocker locker(&d->mutex);
    d->scanLocked();
    return d->engines;
}

bool PdfEngineRegistry::hasEngine(const QString &id) const
{
    return !engineInfo(id).id.isEmpty();
}

PdfEngineRegistry::EngineInfo PdfEngineRegistry::engineInfo(const QString &id) const
{
    QMutexLocker locker(&d->mutex);
    d->scanLocked();
    for (const EngineInfo &engine : d->engines) {
        if (engine.id == id) {
            return engine;
        }
    }
    return {};
}

EnginePtr PdfEngineRegistry::createEngine(const QString &id, QString *errorText) const
{
    QMutexLocker locker(&d->mutex);
    return d->createEngineLocked(id, errorText);
}

EnginePtr PdfEngineRegistry::createEngineFromModule(const QString &modulePath, QString *errorText) const
{
    QMutexLocker locker(&d->mutex);
    const auto module = d->loadModuleLocked(modulePath, QString(), errorText);
    if (!module) {
        return {};
    }
    return d->createFromModuleLocked(*module, errorText);
}

QString PdfEngineRegistry::selectedEngineId() const
{
    QMutexLocker locker(&d->mutex);

    if (!d->overrideEngineId.isEmpty()) {
        return d->overrideEngineId;
    }

    const QString environment = qEnvironmentVariable(engineIdVariable).trimmed();
    if (!environment.isEmpty()) {
        if (looksLikeModulePath(environment)) {
            QString error;
            const auto module = d->loadModuleLocked(environment, QString(), &error);
            if (module) {
                return module->id;
            }
            qWarning().noquote() << error;
        } else {
            return environment;
        }
    }

    d->scanLocked();
    if (!d->defaultEngineId.isEmpty() && d->modulePathsById.contains(d->defaultEngineId)) {
        return d->defaultEngineId;
    }
    return d->engines.isEmpty() ? QString() : d->engines.first().id;
}

EnginePtr PdfEngineRegistry::createSelectedEngine(QString *errorText) const
{
    QMutexLocker locker(&d->mutex);

    if (!d->overrideEngineId.isEmpty()) {
        return d->createEngineLocked(d->overrideEngineId, errorText);
    }

    const QString environment = qEnvironmentVariable(engineIdVariable).trimmed();
    if (!environment.isEmpty() && looksLikeModulePath(environment)) {
        const auto module = d->loadModuleLocked(environment, QString(), errorText);
        return module ? d->createFromModuleLocked(*module, errorText) : EnginePtr();
    }

    if (!environment.isEmpty()) {
        return d->createEngineLocked(environment, errorText);
    }

    d->scanLocked();
    if (!d->defaultEngineId.isEmpty() && d->modulePathsById.contains(d->defaultEngineId)) {
        return d->createEngineLocked(d->defaultEngineId, errorText);
    }

    if (d->engines.isEmpty()) {
        if (errorText) {
            *errorText = QStringLiteral("No PDF engine module was found. Searched: %1").arg(d->searchPathsLocked().join(QStringLiteral(", ")));
        }
        return {};
    }

    return d->createEngineLocked(d->engines.first().id, errorText);
}

void PdfEngineRegistry::setDefaultEngineId(const QString &id)
{
    QMutexLocker locker(&d->mutex);
    d->defaultEngineId = id;
}

QString PdfEngineRegistry::defaultEngineId() const
{
    QMutexLocker locker(&d->mutex);
    return d->defaultEngineId;
}

void PdfEngineRegistry::setOverrideEngineId(const QString &id)
{
    QMutexLocker locker(&d->mutex);
    d->overrideEngineId = id;
}

void PdfEngineRegistry::clearOverrideEngineId()
{
    QMutexLocker locker(&d->mutex);
    d->overrideEngineId.clear();
}

QString PdfEngineRegistry::environmentVariable()
{
    return QString::fromLatin1(engineIdVariable);
}

} // namespace Pdf
} // namespace Mengshee
