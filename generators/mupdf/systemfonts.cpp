// SPDX-License-Identifier: AGPL-3.0-or-later
#include "systemfonts.h"
#include <QtGlobal>
#ifdef Q_OS_WIN
#include <QString>
#include <dwrite.h>
#include <wrl/client.h>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;
struct FontFile {
    QByteArray path;
    unsigned int index = 0;
};

// DirectWrite resolves both machine-wide and per-user installed font families,
// including the correct face index within a TTC. No GUI-thread Qt font APIs.
FontFile findFont(const QString &name, bool bold, bool italic)
{
    ComPtr<IDWriteFactory> factory;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                  reinterpret_cast<IUnknown **>(factory.GetAddressOf()))))
        return {};
    ComPtr<IDWriteFontCollection> collection;
    if (FAILED(factory->GetSystemFontCollection(&collection)))
        return {};
    UINT32 familyIndex = 0;
    BOOL exists = FALSE;
    if (FAILED(collection->FindFamilyName(reinterpret_cast<const wchar_t *>(name.utf16()), &familyIndex, &exists)) || !exists)
        return {};
    ComPtr<IDWriteFontFamily> family;
    ComPtr<IDWriteFont> font;
    ComPtr<IDWriteFontFace> face;
    if (FAILED(collection->GetFontFamily(familyIndex, &family)) ||
        FAILED(family->GetFirstMatchingFont(bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                           DWRITE_FONT_STRETCH_NORMAL,
                                           italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL, &font)) ||
        FAILED(font->CreateFontFace(&face)))
        return {};
    UINT32 count = 0;
    if (FAILED(face->GetFiles(&count, nullptr)) || count != 1)
        return {};
    ComPtr<IDWriteFontFile> file;
    if (FAILED(face->GetFiles(&count, file.GetAddressOf())))
        return {};
    ComPtr<IDWriteFontFileLoader> loader;
    ComPtr<IDWriteLocalFontFileLoader> localLoader;
    const void *key = nullptr;
    UINT32 keySize = 0, pathLength = 0;
    if (FAILED(file->GetLoader(&loader)) || FAILED(loader.As(&localLoader)) ||
        FAILED(file->GetReferenceKey(&key, &keySize)) ||
        FAILED(localLoader->GetFilePathLengthFromKey(key, keySize, &pathLength)))
        return {};
    std::vector<wchar_t> path(pathLength + 1);
    if (FAILED(localLoader->GetFilePathFromKey(key, keySize, path.data(), pathLength + 1)))
        return {};
    return {QString::fromWCharArray(path.data()).toUtf8(), face->GetIndex()};
}

fz_font *loadFont(fz_context *ctx, const char *name, int bold, int italic, int exactMetrics)
{
    // Preserve MuPDF's metric-compatible Base14 substitutions.
    if (exactMetrics)
        return nullptr;
    const auto file = findFont(QString::fromUtf8(name), bold, italic);
    if (file.path.isEmpty())
        return nullptr;
    fz_font *font = nullptr;
    fz_var(font);
    // Catch locally so MuPDF longjmp never skips C++ object destructors.
    fz_try(ctx) {
        font = fz_new_font_from_file(ctx, name, file.path.constData(), file.index, 1);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    return font;
}

fz_font *loadCjkFont(fz_context *ctx, const char *name, int ordering, int serif)
{
    if (auto *font = loadFont(ctx, name, 0, 0, 0))
        return font;
    const char *family = nullptr;
    switch (ordering) {
    case FZ_ADOBE_GB: family = serif ? "SimSun" : "Microsoft YaHei"; break;
    case FZ_ADOBE_CNS: family = serif ? "MingLiU" : "Microsoft JhengHei"; break;
    case FZ_ADOBE_JAPAN: family = serif ? "MS Mincho" : "Yu Gothic"; break;
    case FZ_ADOBE_KOREA: family = serif ? "Batang" : "Malgun Gothic"; break;
    default: return nullptr;
    }
    // A missing Windows language pack falls back to MuPDF's bundled Droid font.
    return loadFont(ctx, family, 0, 0, 0);
}
}
#endif

void MengsheePdfRuntime::installSystemFonts(fz_context *context)
{
#ifdef Q_OS_WIN
    if (context)
        fz_install_load_system_font_funcs(context, nullptr, loadCjkFont, nullptr);
#else
    Q_UNUSED(context);
#endif
}
