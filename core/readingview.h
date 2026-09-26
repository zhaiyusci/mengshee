/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef OKULAR_READINGVIEW_H
#define OKULAR_READINGVIEW_H

#include "area.h"
#include <QString>

namespace Okular
{
// A user-defined rectangle and number, not a navigation/zoom/reading-order rule.
// Rectangle coordinates are normalized in the PDF page's native /Rotate
// orientation (top-left origin); temporary viewer rotation is not included.
struct ReadingView {
    QString id; // UUID, unique within its page; independent of the displayed number
    int number = 1; // positive, not necessarily unique or contiguous
    NormalizedRect rectangle;

    bool operator==(const ReadingView &other) const
    {
        return id == other.id && number == other.number && rectangle == other.rectangle;
    }
};
}

#endif
