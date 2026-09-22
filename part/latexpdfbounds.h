/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef OKULAR_LATEXPDFBOUNDS_H
#define OKULAR_LATEXPDFBOUNDS_H

#include <QString>

namespace GuiUtils::LatexPdfBounds
{
// For a fresh, caller-owned renderer artifact only, never a user document.
// Expand logical page bounds to include painted content, preserving vectors.
// Failure leaves the original artifact untouched.
bool expandInPlace(const QString &pdfFileName, QString &error);
}

#endif
