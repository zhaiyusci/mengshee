/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "latexbackend.h"
#include "latex/stemtexbackend.h"

namespace GuiUtils
{
std::unique_ptr<LatexBackend> createDefaultLatexBackend()
{
    return std::make_unique<StemTeXBackend>();
}
}
