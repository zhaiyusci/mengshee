// SPDX-License-Identifier: AGPL-3.0-or-later
// Single PDF plugin: chooses an engine at run time and exposes the union of
// the optional PDF interfaces. It links the front end, not an engine library.

#include "pdfengineregistry.h"
#include "pdfgeneratorbase.h"

#include <core/printoptionswidget.h>
#include <core/printpreviewrenderer.h>
#include <interfaces/configinterface.h>
#include <interfaces/printinterface.h>

#include <KConfigGroup>
#include <KPluginFactory>
#include <KSharedConfig>

#include <QDebug>

class MengsheePdfGenerator final : public Mengshee::Pdf::PdfGeneratorBase,
                                   public Okular::ConfigInterface,
                                   public Okular::PrintInterface,
                                   public Okular::PrintPreviewInterface
{
    Q_OBJECT
    Q_INTERFACES(Okular::ConfigInterface Okular::PrintInterface)

public:
    MengsheePdfGenerator(QObject *parent, const QVariantList &args)
        : PdfGeneratorBase(createEngine(), parent, args)
    {
    }

    // ConfigInterface: engines with native settings pages expose them through
    // PdfConfigurable. MuPDF intentionally has no settings page.
    bool reparseConfig() override
    {
        return configurable() ? configurable()->reparseConfig() : false;
    }

    void addPages(KConfigDialog *dialog) override
    {
        if (auto *config = configurable()) {
            config->addPages(dialog);
        }
    }

    // PrintInterface: a default widget keeps the print dialog usable when the
    // engine has no PDF-specific options.
    Okular::PrintOptionsWidget *printConfigurationWidget() const override
    {
        if (auto *configurator = printConfigurator()) {
            if (Okular::PrintOptionsWidget *options = configurator->printConfigurationWidget()) {
                return options;
            }
        }
        return new Okular::DefaultPrintOptionsWidget();
    }

    // PrintPreviewInterface: only engines with a snapshot renderer provide one.
    Okular::PrintPreviewRenderer createPrintPreviewRenderer(QString *error) override
    {
        if (auto *provider = printPreviewProvider()) {
            return provider->createPrintPreviewRenderer(error);
        }
        if (error) {
            *error = QStringLiteral("Print preview is not supported by the active PDF engine.");
        }
        return {};
    }

private:
    static Mengshee::Pdf::EnginePtr createEngine()
    {
        Mengshee::Pdf::PdfEngineRegistry &registry = Mengshee::Pdf::PdfEngineRegistry::instance();

        // User default; the environment variable still wins inside the
        // registry. Read the KConfig key directly: the generator plugin can be
        // instantiated by tests before SettingsCore is initialized.
        const KConfigGroup coreGeneral(KSharedConfig::openConfig(), QStringLiteral("Core General"));
        const QString configured = coreGeneral.readEntry("PdfEngine", QStringLiteral("MuPDF")).trimmed();
        if (configured.compare(QLatin1String("mupdf"), Qt::CaseInsensitive) == 0) {
            registry.setDefaultEngineId(QString::fromLatin1(Mengshee::Pdf::MuPdfEngineId));
        } else if (configured.compare(QLatin1String("poppler"), Qt::CaseInsensitive) == 0) {
            registry.setDefaultEngineId(QString::fromLatin1(Mengshee::Pdf::PopplerEngineId));
        } else {
            registry.setDefaultEngineId(QString());
        }

        QString error;
        Mengshee::Pdf::EnginePtr engine = registry.createSelectedEngine(&error);
        if (!engine) {
            qWarning().noquote() << QStringLiteral("Mengshee PDF plugin: %1").arg(error);
        }
        return engine;
    }
};

K_PLUGIN_CLASS_WITH_JSON(MengsheePdfGenerator, "libokularGenerator_mengshee_pdf.json")

#include "mengsheeplugin.moc"
