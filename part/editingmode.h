/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef OKULAR_EDITINGMODE_H
#define OKULAR_EDITINGMODE_H

namespace Okular
{
// Organizes editing tools and overlays, not document permissions or rendering.
enum class EditingMode {
    Reading,
    CrossReferences,
    Ocr,
    Pages,
    Views,
};
}

#endif
