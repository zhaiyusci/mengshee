/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef OKULAR_LATEXNOTEGEOMETRY_H
#define OKULAR_LATEXNOTEGEOMETRY_H

#include <algorithm>
#include <cmath>

#include <QSizeF>

namespace Okular
{
namespace LatexNoteGeometry
{
constexpr double defaultPaddingPoints()
{
    return 3.0;
}

inline double layoutWidthForVisibleWidth(double visibleWidthPoints, double padding)
{
    if (!std::isfinite(visibleWidthPoints) || visibleWidthPoints <= 0.0 || !std::isfinite(padding) || padding < 0.0) {
        return 0.0;
    }
    return std::max(1.0, visibleWidthPoints - 2.0 * padding);
}

inline QSizeF visualSizeForContent(const QSizeF &contentSizePoints, double layoutWidthPoints, double padding = defaultPaddingPoints())
{
    if (!contentSizePoints.isValid() || contentSizePoints.isEmpty() || !std::isfinite(padding) || padding < 0.0) {
        return contentSizePoints;
    }

    return QSizeF(std::max(layoutWidthPoints, contentSizePoints.width()) + 2.0 * padding, contentSizePoints.height() + 2.0 * padding);
}
}
}

#endif
