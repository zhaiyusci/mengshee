// SPDX-License-Identifier: GPL-2.0-or-later
#include "aisearch.h"

#include "core/page.h"
#include "core/textpage.h"

#include <algorithm>

namespace AiSearch
{
namespace
{
bool isCjkCharacter(QChar character)
{
    const ushort value = character.unicode();
    return (value >= 0x3040 && value <= 0x30FF)   // Hiragana, Katakana
        || (value >= 0x3400 && value <= 0x4DBF)   // CJK extension A
        || (value >= 0x4E00 && value <= 0x9FFF)   // CJK unified ideographs
        || (value >= 0xAC00 && value <= 0xD7AF)   // Hangul syllables
        || (value >= 0xF900 && value <= 0xFAFF);  // CJK compatibility ideographs
}
}

QList<TextBlock> textBlocks(const Okular::Page *page)
{
    QList<TextBlock> blocks;
    if (!page) {
        return blocks;
    }
    TextBlock block;
    Okular::NormalizedRect previous;
    bool separated = false;
    const auto flush = [&] {
        if (!block.text.trimmed().isEmpty()) {
            block.text = block.text.trimmed();
            blocks.append(block);
        }
        block = TextBlock();
        separated = false;
    };
    const auto entities = page->words(nullptr, Okular::TextPage::AnyPixelTextAreaInclusionBehaviour);
    for (const auto &entity : entities) {
        const QString raw = entity.text();
        const QString text = raw.simplified();
        const auto rectangle = entity.area();
        if (text.isEmpty()) {
            if (raw.contains(QLatin1Char('\n'))) {
                flush();
            } else {
                separated = true;
            }
            continue;
        }
        if (rectangle.isNull()) {
            continue;
        }
        const double height = rectangle.bottom - rectangle.top;
        if (!block.text.isEmpty()) {
            const double previousHeight = previous.bottom - previous.top;
            const double overlap = std::min(previous.bottom, rectangle.bottom) - std::max(previous.top, rectangle.top);
            const double gap = rectangle.left - previous.right;
            // Preserve reading order and never join separate columns, lines or footers.
            if (overlap <= 0.5 * std::min(height, previousHeight)
                || gap > 2.0 * std::max(height, previousHeight)
                || rectangle.right < previous.left) {
                flush();
            } else {
                const bool bothCjk = isCjkCharacter(block.text.back()) && isCjkCharacter(text.front());
                if (separated || raw.front().isSpace() || (gap > 0.18 * height && !bothCjk)) {
                    block.text += QLatin1Char(' ');
                }
            }
        }
        if (block.text.isEmpty()) {
            block.rectangle = rectangle;
        } else {
            block.rectangle.left = std::min(block.rectangle.left, rectangle.left);
            block.rectangle.top = std::min(block.rectangle.top, rectangle.top);
            block.rectangle.right = std::max(block.rectangle.right, rectangle.right);
            block.rectangle.bottom = std::max(block.rectangle.bottom, rectangle.bottom);
        }
        block.text += text;
        previous = rectangle;
        separated = raw.back().isSpace();
        if (raw.endsWith(QLatin1Char('\n'))) {
            flush();
        }
    }
    flush();
    return blocks;
}
}
