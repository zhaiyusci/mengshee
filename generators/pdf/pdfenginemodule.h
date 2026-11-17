// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Draft: the C ABI of a dynamically loaded PDF engine module.
//
// The front end never links an engine. It locates a module DLL, resolves the
// single exported entry point below with QLibrary, validates apiVersion, and
// uses create()/destroy() to obtain the C++ PdfEngine object. Keeping the
// entry point in C linkage and passing the engine as void* avoids C++ symbol
// name and allocator coupling across the module boundary.
//
// This header is not part of the build yet. The modules are
// MengsheePdfEngineMuPdf.dll and MengsheePdfEnginePoppler.dll; see
// docs/pdf-backend-architecture.md.

#include <cstdint>

#if defined(_WIN32)
#if defined(MENGSHEE_PDF_ENGINE_MODULE_BUILD)
#define MENGSHEE_PDF_ENGINE_MODULE_EXPORT __declspec(dllexport)
#else
#define MENGSHEE_PDF_ENGINE_MODULE_EXPORT __declspec(dllimport)
#endif
#else
#define MENGSHEE_PDF_ENGINE_MODULE_EXPORT __attribute__((visibility("default")))
#endif

namespace Mengshee
{
namespace Pdf
{

/**
 * Increment whenever the descriptor layout or the PdfEngine ABI changes.
 *
 * Version 2 removed the phase-1 legacy bridge (PdfEngine::legacyGenerator()
 * and the legacy* wrappers) and added hasFeature()/setWrapperDocument().
 * Version 3 changed RenderRequest (pageModel/applyViewRotation) and the
 * Poppler worker-render path. A module built against an older version must not
 * be loaded: its PdfEngine vtable / RenderRequest layout does not match, so the
 * registry rejects it by version instead of crashing at the first call.
 */
inline constexpr std::uint32_t EngineModuleApiVersion = 4;

/**
 * A module exports exactly one descriptor through mengshee_pdf_engine_module().
 *
 * The descriptor and all strings it points to must have static lifetime in the
 * module. create() must return the address of the PdfEngine subobject (not the
 * address of a derived complete object when PdfEngine is not the first base)
 * as void*. destroy() receives that same address and must convert it back to
 * the complete type before deleting. The front end must destroy the engine
 * only with the matching destroy() function, never with `delete`. Both must
 * run on the same thread unless the engine interface documents otherwise.
 */
struct EngineModuleDescriptor {
    std::uint32_t apiVersion = 0;
    /** Stable id, e.g. "mupdf" or "poppler"; matches PdfEngine::id(). */
    const char *id = nullptr;
    /** User-visible name, UTF-8. */
    const char *displayName = nullptr;
    /** Diagnostic string: compiler, Qt version, build type. May be empty. */
    const char *buildInfo = nullptr;
    void *(*create)() = nullptr;
    void (*destroy)(void *) = nullptr;
};

} // namespace Pdf
} // namespace Mengshee

extern "C" MENGSHEE_PDF_ENGINE_MODULE_EXPORT const Mengshee::Pdf::EngineModuleDescriptor *mengshee_pdf_engine_module();
