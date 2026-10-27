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
};
}
