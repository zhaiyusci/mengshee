// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Runtime discovery and selection of PDF engine modules.
//
// One PDF plugin (okularGenerator_mengshee_pdf) owns this registry. It never
// links an engine library: modules are found on disk, the selected one is
// loaded with QLibrary, validated against the C ABI in pdfenginemodule.h, and
// instantiated on demand. The same registry serves the neutral utilities in
// pdfutils.h, so part/ and gui/ do not link a backend either.
//
// Module file names follow a fixed convention: MengsheePdfEngine<Id>.dll,
// where <Id> lower-cased is the engine id. MengsheePdfEngineMuPdf.dll is
// "mupdf" and MengsheePdfEnginePoppler.dll is "poppler". Discovery enumerates
// the search paths and derives id/display name/path from the file names; it
// never loads a module. Only the selected engine is ever loaded (through
// createEngine(), createSelectedEngine(), createEngineFromModule() or an
// explicit module path in MENGSHEE_PDF_ENGINE), so selecting MuPDF does not
// map Poppler (and its dependencies) into the process.

#include "pdfengine.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

namespace Mengshee
{
namespace Pdf
{

class PdfEngineRegistry
{
public:
    struct EngineInfo {
        QString id;
        QString displayName;
        QString modulePath;
    };

    static PdfEngineRegistry &instance();

    /**
     * Adds a directory that is searched for engine modules. Call before the
     * first discovery. The default search path already contains the plugin
     * directory (plugins/mengshee_pdf) and the application directory; the
     * environment variable in environmentVariable() may add one more.
     */
    void addSearchPath(const QString &directory);
    QStringList searchPaths() const;

    /**
     * Scans the search paths once and caches the result; later calls do not
     * touch the disk. No module DLL is loaded. The displayName is "MuPDF" /
     * "Poppler" for those ids and the id itself for every other engine.
     */
    QList<EngineInfo> availableEngines() const;
    bool hasEngine(const QString &id) const;
    EngineInfo engineInfo(const QString &id) const;

    /**
     * Loads the module that provides id and creates one engine instance. Only
     * that module is loaded; its descriptor id must match id, and a mismatch
     * is rejected with an actionable error (the module is unloaded again).
     * The id comes from the file name, so a module that does not follow
     * MengsheePdfEngine<Id>.dll is not reachable through this method; use
     * createEngineFromModule() for an explicit path.
     * The returned EnginePtr destroys the instance through the module's own
     * destroy() function; never delete it in the front end. errorText may be
     * null.
     */
    EnginePtr createEngine(const QString &id, QString *errorText = nullptr) const;
    EnginePtr createEngineFromModule(const QString &modulePath, QString *errorText = nullptr) const;

    /**
     * Selection precedence:
     *   1. setOverrideEngineId()        (tests and diagnostic tooling)
     *   2. MENGSHEE_PDF_ENGINE          (engine id or explicit module path)
     *   3. defaultEngineId              (user setting, set by the front end)
     *   4. the first available module, in MuPDF, Poppler order
     * Only the engine that wins is loaded. An explicit module path in the
     * environment variable is loaded directly, as before. Returns an empty
     * string when no module is available.
     */
    QString selectedEngineId() const;
    EnginePtr createSelectedEngine(QString *errorText = nullptr) const;

    /** The user-configured default; the plugin sets it before creating a document. */
    void setDefaultEngineId(const QString &id);
    QString defaultEngineId() const;

    /** Process-local override used by tests. Empty clears it. */
    void setOverrideEngineId(const QString &id);
    void clearOverrideEngineId();

    /** "MENGSHEE_PDF_ENGINE"; accepts an engine id or a module path. */
    static QString environmentVariable();

private:
    PdfEngineRegistry();
    ~PdfEngineRegistry();
    PdfEngineRegistry(const PdfEngineRegistry &) = delete;
    PdfEngineRegistry &operator=(const PdfEngineRegistry &) = delete;

    class Private;
    Private *d;
};

} // namespace Pdf
} // namespace Mengshee
