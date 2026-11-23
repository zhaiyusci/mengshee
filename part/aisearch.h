// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/area.h"

#include <QList>
#include <QString>

namespace Okular
{
class Page;
}

/** Text-layer blocks and native coordinates for source-range citations. */
namespace AiSearch
{
struct TextBlock
{
    QString text;
    Okular::NormalizedRect rectangle;
};

/** Visual text lines in reading order. The caller loads the text page first. */
QList<TextBlock> textBlocks(const Okular::Page *page);
}
