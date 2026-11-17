// SPDX-License-Identifier: AGPL-3.0-or-later
// Test-only engine module used by mengshee_pdf_registry_test. It implements
// just enough of the seam to verify discovery, the C ABI, ownership and the
// selected-engine path without a real PDF backend.

#include "pdfengine.h"
#include "pdfenginemodule.h"

namespace
{

class FakeEngine final : public Mengshee::Pdf::PdfEngine, public Mengshee::Pdf::PdfDocument
{
public:
    // PdfEngine
    Mengshee::Pdf::EngineId id() const override
    {
        return QStringLiteral("fake");
    }

    Mengshee::Pdf::PdfDocument *document() override
    {
        return this;
    }

    // PdfDocument
    Mengshee::Pdf::OpenStatus openFile(const QString &fileName, const QString &) override
    {
        return fileName.isEmpty() ? Mengshee::Pdf::OpenStatus::Error : Mengshee::Pdf::OpenStatus::Success;
    }

    Mengshee::Pdf::OpenStatus openData(const QByteArray &data, const QString &) override
    {
        return data.isEmpty() ? Mengshee::Pdf::OpenStatus::Error : Mengshee::Pdf::OpenStatus::Success;
    }

    void close() override
    {
    }

    int pageCount() const override
    {
        return 1;
    }

    Mengshee::Pdf::PageGeometry pageGeometry(int page) const override
    {
        if (page != 0) {
            return {};
        }
        Mengshee::Pdf::PageGeometry geometry;
        geometry.mediaBox = QSizeF(100, 50);
        geometry.cropBox = QSizeF(100, 50);
        geometry.label = QStringLiteral("1");
        return geometry;
    }

    Mengshee::Pdf::RenderResult renderPage(const Mengshee::Pdf::RenderRequest &) override
    {
        return {};
    }

    Okular::TextPage *textPage(int) override
    {
        return nullptr;
    }

    void loadAnnotations(int, Okular::Page *) override
    {
    }

    void loadLinks(int, Okular::Page *) override
    {
    }

    void loadForms(int, Okular::Page *) override
    {
    }

    void loadTransition(int, Okular::Page *) override
    {
    }

    void loadPageActions(int, Okular::Page *) override
    {
    }

    Okular::AnnotationProxy *annotationProxy() override
    {
        return nullptr;
    }

    Okular::DocumentInfo documentInfo(const QSet<Okular::DocumentInfo::Key> &) const override
    {
        return {};
    }

    const Okular::DocumentSynopsis *synopsis() override
    {
        return nullptr;
    }

    bool setSynopsis(const Okular::DocumentSynopsis &, QString *) override
    {
        return false;
    }

    Okular::FontInfo::List fontsForPage(int) override
    {
        return {};
    }

    QByteArray fontData(const Okular::FontInfo &) override
    {
        return {};
    }

    const QList<Okular::EmbeddedFile *> *embeddedFiles() const override
    {
        return nullptr;
    }

    bool isAllowed(Okular::Permission) const override
    {
        return true;
    }

    QVariant metaData(const QString &, const QVariant &) const override
    {
        return {};
    }

    Okular::Generator::PageLayout defaultPageLayout() const override
    {
        return Okular::Generator::SinglePage;
    }

    bool defaultPageContinuous() const override
    {
        return true;
    }
};

const Mengshee::Pdf::EngineModuleDescriptor descriptor = {
    Mengshee::Pdf::EngineModuleApiVersion,
    "fake",
    "Fake PDF engine",
    "test-only",
    []() -> void * {
        auto *engine = new FakeEngine;
        return static_cast<Mengshee::Pdf::PdfEngine *>(engine);
    },
    [](void *engine) {
        auto *pdfEngine = static_cast<Mengshee::Pdf::PdfEngine *>(engine);
        delete static_cast<FakeEngine *>(pdfEngine);
    },
};

} // namespace

extern "C" MENGSHEE_PDF_ENGINE_MODULE_EXPORT const Mengshee::Pdf::EngineModuleDescriptor *mengshee_pdf_engine_module()
{
    return &descriptor;
}
