// SPDX-License-Identifier: AGPL-3.0-or-later
#include "generator_mupdf.h"
#include <QDateTime>
#include <QSet>

Okular::FontInfo::List MuPDFGenerator::fontsForPage(int index)
{
    Okular::FontInfo::List result;
    operation(
        [&] {
            QSet<pdf_obj *> seenResources, seenFonts;
            std::function<void(pdf_obj *, int)> visit = [&](pdf_obj *resources, int depth) {
                if (depth > 64 || seenResources.contains(pdf_resolve_indirect(m_context, resources)))
                    return;
                seenResources.insert(pdf_resolve_indirect(m_context, resources));
                auto *fonts = pdf_dict_get(m_context, resources, PDF_NAME(Font));
                for (int i = 0; i < pdf_dict_len(m_context, fonts); ++i) {
                    auto *font = pdf_dict_get_val(m_context, fonts, i);
                    if (seenFonts.contains(pdf_resolve_indirect(m_context, font)))
                        continue;
                    seenFonts.insert(pdf_resolve_indirect(m_context, font));
                    Okular::FontInfo info;
                    const auto name = QString::fromUtf8(pdf_to_name(m_context, pdf_dict_get(m_context, font, PDF_NAME(BaseFont))));
                    info.setName(name);
                    const QByteArray type = pdf_to_name(m_context, pdf_dict_get(m_context, font, PDF_NAME(Subtype)));
                    auto *descendant = font;
                    if (type == "Type0")
                        descendant = pdf_array_get(m_context, pdf_dict_get(m_context, font, PDF_NAME(DescendantFonts)), 0);
                    auto *descriptor = pdf_dict_get(m_context, descendant, PDF_NAME(FontDescriptor));
                    pdf_obj *stream = nullptr;
                    for (auto *key : {PDF_NAME(FontFile), PDF_NAME(FontFile2), PDF_NAME(FontFile3)})
                        if (auto *candidate = pdf_dict_get(m_context, descriptor, key))
                            stream = candidate;
                    const QByteArray subtype = pdf_to_name(m_context, pdf_dict_get(m_context, descendant, PDF_NAME(Subtype)));
                    info.setType(type == "Type3"                 ? Okular::FontInfo::Type3
                                     : type == "TrueType"        ? Okular::FontInfo::TrueType
                                     : type == "Type1"           ? Okular::FontInfo::Type1
                                     : subtype == "CIDFontType2" ? Okular::FontInfo::CIDTrueType
                                     : type == "Type0"           ? Okular::FontInfo::CIDType0
                                                                 : Okular::FontInfo::Unknown);
                    info.setEmbedType(stream ? (name.size() > 7 && name[6] == QLatin1Char('+') ? Okular::FontInfo::EmbeddedSubset : Okular::FontInfo::FullyEmbedded) : Okular::FontInfo::NotEmbedded);
                    info.setCanBeExtracted(pdf_is_stream(m_context, stream));
                    info.setNativeId(pdf_to_num(m_context, stream));
                    result.append(info);
                    if (type == "Type3")
                        visit(pdf_dict_get(m_context, font, PDF_NAME(Resources)), depth + 1);
                }
                auto *objects = pdf_dict_get(m_context, resources, PDF_NAME(XObject));
                for (int i = 0; i < pdf_dict_len(m_context, objects); ++i)
                    visit(pdf_dict_get(m_context, pdf_dict_get_val(m_context, objects, i), PDF_NAME(Resources)), depth + 1);
            };
            auto *page = pdf_lookup_page_obj(m_context, m_document, index);
            visit(pdf_dict_get_inheritable(m_context, page, PDF_NAME(Resources)), 0);
            auto *annots = pdf_dict_get(m_context, page, PDF_NAME(Annots));
            for (int i = 0; i < pdf_array_len(m_context, annots); ++i) {
                auto *normal = pdf_dict_getp(m_context, pdf_array_get(m_context, annots, i), "AP/N");
                if (pdf_is_stream(m_context, normal))
                    visit(pdf_dict_get(m_context, normal, PDF_NAME(Resources)), 0);
                else
                    for (int j = 0; j < pdf_dict_len(m_context, normal); ++j)
                        visit(pdf_dict_get(m_context, pdf_dict_get_val(m_context, normal, j), PDF_NAME(Resources)), 0);
            }
        },
        nullptr);
    return result;
}
QByteArray MuPDFGenerator::requestFontData(const Okular::FontInfo &font)
{
    QByteArray result;
    fz_buffer *buffer = nullptr;
    fz_var(buffer);
    if (font.nativeId().toInt() <= 0)
        return result;
    operation(
        [&] {
            fz_try(m_context)
            {
                buffer = pdf_load_stream_number(m_context, m_document, font.nativeId().toInt());
                unsigned char *data = nullptr;
                const size_t size = fz_buffer_storage(m_context, buffer, &data);
                result = QByteArray(reinterpret_cast<const char *>(data), size);
            }
            fz_always(m_context)
            {
                fz_drop_buffer(m_context, buffer);
            }
            fz_catch(m_context)
            {
                fz_rethrow(m_context);
            }
        },
        nullptr);
    return result;
}
class MuPDFEmbeddedFile final : public Okular::EmbeddedFile
{
public:
    MuPDFEmbeddedFile(MuPDFGenerator *generator, pdf_obj *file)
        : generator(generator)
        , file(pdf_keep_obj(generator->m_context, file))
    {
        pdf_filespec_params params;
        pdf_get_filespec_params(generator->m_context, file, &params);
        filename = QString::fromUtf8(params.filename);
        length = params.size;
        descriptionText = QString::fromUtf8(pdf_to_text_string(generator->m_context, pdf_dict_get(generator->m_context, file, PDF_NAME(Desc))));
        if (params.created >= 0)
            created = QDateTime::fromSecsSinceEpoch(params.created, Qt::UTC);
        if (params.modified >= 0)
            modified = QDateTime::fromSecsSinceEpoch(params.modified, Qt::UTC);
    }
    ~MuPDFEmbeddedFile() override
    {
        pdf_drop_obj(generator->m_context, file);
    }
    QString name() const override
    {
        return filename;
    }
    QString description() const override
    {
        return descriptionText;
    }
    int size() const override
    {
        return length;
    }
    QDateTime creationDate() const override
    {
        return created;
    }
    QDateTime modificationDate() const override
    {
        return modified;
    }
    QByteArray data() const override
    {
        QByteArray result;
        fz_buffer *buffer = nullptr;
        fz_var(buffer);
        generator->operation(
            [&] {
                auto *ctx = generator->m_context;
                fz_try(ctx)
                {
                    buffer = pdf_load_embedded_file_contents(ctx, file);
                    unsigned char *data = nullptr;
                    const size_t size = fz_buffer_storage(ctx, buffer, &data);
                    result = QByteArray(reinterpret_cast<const char *>(data), size);
                }
                fz_always(ctx)
                {
                    fz_drop_buffer(ctx, buffer);
                }
                fz_catch(ctx)
                {
                    fz_rethrow(ctx);
                }
            },
            nullptr);
        return result;
    }

private:
    MuPDFGenerator *generator;
    pdf_obj *file;
    QString filename, descriptionText;
    int length = -1;
    QDateTime created, modified;
};
const QList<Okular::EmbeddedFile *> *MuPDFGenerator::embeddedFiles() const
{
    auto *self = const_cast<MuPDFGenerator *>(this);
    if (!m_embeddedLoaded)
        self->operation(
            [&] {
                pdf_obj *tree = nullptr;
                fz_var(tree);
                fz_try(m_context)
                {
                    tree = pdf_load_name_tree(m_context, m_document, PDF_NAME(EmbeddedFiles));
                    for (int i = 0; i < pdf_dict_len(m_context, tree); ++i) {
                        auto *file = pdf_dict_get_val(m_context, tree, i);
                        if (pdf_is_embedded_file(m_context, file))
                            m_embeddedFiles.append(new MuPDFEmbeddedFile(self, file));
                    }
                    m_embeddedLoaded = true;
                }
                fz_always(m_context)
                {
                    pdf_drop_obj(m_context, tree);
                }
                fz_catch(m_context)
                {
                    qDeleteAll(m_embeddedFiles);
                    m_embeddedFiles.clear();
                    fz_rethrow(m_context);
                }
            },
            nullptr);
    return &m_embeddedFiles;
}
Okular::Generator::PageLayout MuPDFGenerator::defaultPageLayout() const
{
    PageLayout result = NoLayout;
    const_cast<MuPDFGenerator *>(this)->operation(
        [&] {
            const QByteArray layout = pdf_to_name(m_context, pdf_dict_getp(m_context, pdf_trailer(m_context, m_document), "Root/PageLayout"));
            if (layout == "SinglePage" || layout == "OneColumn")
                result = SinglePage;
            if (layout == "TwoColumnLeft" || layout == "TwoPageLeft")
                result = TwoPageLeft;
            if (layout == "TwoColumnRight" || layout == "TwoPageRight")
                result = TwoPageRight;
        },
        nullptr);
    return result;
}
bool MuPDFGenerator::defaultPageContinuous() const
{
    bool result = false;
    const_cast<MuPDFGenerator *>(this)->operation(
        [&] {
            const QByteArray layout = pdf_to_name(m_context, pdf_dict_getp(m_context, pdf_trailer(m_context, m_document), "Root/PageLayout"));
            result = layout == "OneColumn" || layout.startsWith("TwoColumn");
        },
        nullptr);
    return result;
}
