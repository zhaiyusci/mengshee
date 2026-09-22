/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef OKULAR_LATEXRENDERGUARDS_H
#define OKULAR_LATEXRENDERGUARDS_H

#include <QString>

namespace GuiUtils
{
namespace LatexRenderGuards
{
enum class InputError {
    None,
    EmptySource,
    SourceTooLarge,
    EmbeddedNull,
    InvalidWidth,
    InvalidFontSize,
};

constexpr qint64 maxPdfBytes = 64 * 1024 * 1024;

// Zero width/font size selects the renderer default. Source is never rewritten.
InputError validateInput(const QString &source, double width, double fontSize);

// A bounded, readable regular file with a PDF signature, not full PDF validation
// or a security boundary. Symbolic links are not accepted as render artifacts.
bool isUsablePdfArtifact(const QString &fileName);
}
}

#endif
