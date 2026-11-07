// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QPrinter>
#include <QSizeF>

namespace Okular {
inline QPageLayout::Orientation paperOrientationForPage(QSizeF size)
{
    return size.width() > size.height() ? QPageLayout::Landscape : QPageLayout::Portrait;
}

// Per-job policy, separate from QPrinter's current sheet orientation.
class PrintJobPrinter : public QPrinter
{
public:
    using QPrinter::QPrinter;
    bool automaticPageOrientation = false;
    // Print each page at its original physical size instead of scaling it to
    // the sheet. The page is centred and clipped when it is larger than the
    // sheet.
    bool originalPageSize = false;
};
}
