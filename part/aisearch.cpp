// SPDX-License-Identifier: GPL-2.0-or-later
#include "aisearch.h"

#include "core/document.h"
#include "core/page.h"
#include "core/textpage.h"

#include <QHash>
#include <QPair>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <utility>

namespace AiSearch
{
namespace
{
// Passage shape. A passage is what the result list shows and what one
// highlight covers, so it stays close to a paragraph, not a whole page.
constexpr int maxPassageCharacters = 480;
// A hard bound so a pathological document cannot exhaust memory.
constexpr int maxIndexGlyphs = 400000;

// BM25 parameters, the common defaults (Lucene's).
constexpr double k1 = 1.2;
constexpr double b = 0.75;

bool isCjkCharacter(QChar character)
{
    const ushort value = character.unicode();
    return (value >= 0x3040 && value <= 0x30FF)   // Hiragana, Katakana
        || (value >= 0x3400 && value <= 0x4DBF)   // CJK extension A
        || (value >= 0x4E00 && value <= 0x9FFF)   // CJK unified ideographs
        || (value >= 0xAC00 && value <= 0xD7AF)   // Hangul syllables
        || (value >= 0xF900 && value <= 0xFAFF);  // CJK compatibility ideographs
}

bool isWordCharacter(QChar character)
{
    return character.isLetterOrNumber() || character == QLatin1Char('\'') || character == QLatin1Char('-');
}

// One rectangle per visual line, so a hit highlights whole lines of the
// passage instead of a cloud of individual glyph boxes.
QList<Okular::NormalizedRect> mergeLineRectangles(const QList<Okular::NormalizedRect> &rectangles)
{
    QList<Okular::NormalizedRect> sorted = rectangles;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Okular::NormalizedRect &a, const Okular::NormalizedRect &c) {
        if (a.top != c.top) {
            return a.top < c.top;
        }
        return a.left < c.left;
    });

    QList<Okular::NormalizedRect> merged;
    for (const Okular::NormalizedRect &rectangle : sorted) {
        bool joined = false;
        for (Okular::NormalizedRect &target : merged) {
            const double overlap = std::min(target.bottom, rectangle.bottom) - std::max(target.top, rectangle.top);
            const double smallestHeight = std::min(target.bottom - target.top, rectangle.bottom - rectangle.top);
            const double horizontalGap = std::max(target.left, rectangle.left) - std::min(target.right, rectangle.right);
            if (smallestHeight > 0 && overlap > 0.5 * smallestHeight && horizontalGap < 1.5 * smallestHeight) {
                target.left = std::min(target.left, rectangle.left);
                target.top = std::min(target.top, rectangle.top);
                target.right = std::max(target.right, rectangle.right);
                target.bottom = std::max(target.bottom, rectangle.bottom);
                joined = true;
                break;
            }
        }
        if (!joined) {
            merged.append(rectangle);
        }
    }
    return merged;
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

QList<Token> tokensWithPositions(const QString &text)
{
    QList<Token> tokens;
    int index = 0;
    while (index < text.size()) {
        const QChar character = text.at(index);
        if (isCjkCharacter(character)) {
            tokens.append({QString(character), index, 1});
            if (index > 0 && isCjkCharacter(text.at(index - 1))) {
                tokens.append({QString(text.at(index - 1)) + character, index - 1, 2});
            }
            ++index;
        } else if (isWordCharacter(character)) {
            int end = index + 1;
            while (end < text.size() && isWordCharacter(text.at(end))) {
                ++end;
            }
            tokens.append({text.mid(index, end - index).toLower(), index, end - index});
            index = end;
        } else {
            ++index;
        }
    }
    return tokens;
}

QStringList tokenize(const QString &text)
{
    QStringList result;
    for (const Token &token : tokensWithPositions(text)) {
        result.append(token.text);
    }
    return result;
}

bool Index::build(Okular::Document *document, QString *errorText)
{
    if (errorText) {
        errorText->clear();
    }
    m_passages.clear();
    m_pageCount = 0;
    m_wordCount = 0;
    if (!document) {
        if (errorText) {
            *errorText = QStringLiteral("No document is open.");
        }
        return false;
    }

    const int pages = int(document->pages());
    for (int pageNumber = 0; pageNumber < pages; ++pageNumber) {
        const Okular::Page *page = document->page(pageNumber);
        if (!page) {
            continue;
        }
        if (!page->hasTextPage()) {
            // The generator builds the text page synchronously; the same call
            // search already relies on. It includes an OCR text layer when one
            // has been written to the document.
            document->requestTextPage(pageNumber);
        }
        m_pageCount = pageNumber + 1;

        const Okular::TextEntity::List entities = page->words(nullptr, Okular::TextPage::AnyPixelTextAreaInclusionBehaviour);
        Passage passage;
        passage.page = pageNumber;

        const auto flush = [this, &passage, pageNumber] {
            if (!passage.text.isEmpty()) {
                m_passages.append(passage);
            }
            passage = Passage();
            passage.page = pageNumber;
        };
        const auto appendSeparator = [&passage] {
            if (!passage.text.isEmpty() && !passage.text.endsWith(QLatin1Char(' '))) {
                passage.text += QLatin1Char(' ');
                passage.glyphAtOffset.append(-1);
            }
        };

        Okular::NormalizedRect previous;
        bool havePrevious = false;
        double lineHeight = 0;
        for (const Okular::TextEntity &entity : entities) {
            const QString text = entity.text();
            const Okular::NormalizedRect rectangle = entity.area();
            if (text.trimmed().isEmpty() || rectangle.isNull()) {
                continue;
            }
            const double height = rectangle.bottom - rectangle.top;
            if (height > 0) {
                lineHeight = height;
            }

            if (havePrevious) {
                const double overlap = std::min(previous.bottom, rectangle.bottom) - std::max(previous.top, rectangle.top);
                const double smallestHeight = std::min(previous.bottom - previous.top, height);
                const bool sameLine = smallestHeight > 0 && overlap > 0.5 * smallestHeight;
                if (!sameLine && lineHeight > 0 && rectangle.top > previous.bottom + 0.6 * lineHeight) {
                    // A vertical gap larger than a line means a new paragraph
                    // or a new column: never merge them into one passage.
                    flush();
                } else {
                    const double gap = rectangle.left - previous.right;
                    const bool bothCjk = !passage.text.isEmpty() && isCjkCharacter(passage.text.at(passage.text.size() - 1)) && isCjkCharacter(text.at(0));
                    if (!sameLine || (gap > 0.18 * std::max(height, 1e-6) && !bothCjk)) {
                        appendSeparator();
                    }
                }
            }

            const int glyphIndex = passage.glyphs.size();
            passage.glyphs.append({text, rectangle});
            for (const QChar character : text) {
                passage.text += character;
                passage.glyphAtOffset.append(glyphIndex);
            }
            ++m_wordCount;
            previous = rectangle;
            havePrevious = true;

            if (passage.text.size() >= maxPassageCharacters) {
                flush();
                havePrevious = false;
            }
        }
        flush();

        if (m_wordCount >= maxIndexGlyphs) {
            if (errorText) {
                *errorText = QStringLiteral("The document is too large to index completely; only the first pages were searched.");
            }
            break;
        }
    }
    return !m_passages.isEmpty();
}

QList<Hit> Index::search(const QString &query, int maxHits) const
{
    QList<Hit> hits;
    if (m_passages.isEmpty() || maxHits <= 0) {
        return hits;
    }

    QStringList queryTokens = tokenize(query);
    queryTokens.removeDuplicates();
    const QSet<QString> queryTerms(queryTokens.begin(), queryTokens.end());

    QList<Hit> rankedHits;
    if (!queryTerms.isEmpty()) {
        // Term frequencies and lengths of every passage, computed once.
        QList<QHash<QString, int>> frequencies(m_passages.size());
        QList<int> lengths(m_passages.size());
        QHash<QString, int> documentFrequency;
        int totalLength = 0;
        for (int index = 0; index < m_passages.size(); ++index) {
            QHash<QString, int> &frequency = frequencies[index];
            const QList<Token> tokens = tokensWithPositions(m_passages.at(index).text);
            int length = 0;
            for (const Token &token : tokens) {
                ++length;
                if (queryTerms.contains(token.text)) {
                    ++frequency[token.text];
                }
            }
            lengths[index] = length;
            totalLength += length;
            for (const QString &term : frequency.keys()) {
                ++documentFrequency[term];
            }
        }

        const double passageCount = m_passages.size();
        const double averageLength = totalLength > 0 ? double(totalLength) / passageCount : 0.0;

        QList<QPair<double, int>> ranked;
        ranked.reserve(m_passages.size());
        for (int index = 0; index < m_passages.size(); ++index) {
            const QHash<QString, int> &frequency = frequencies.at(index);
            if (frequency.isEmpty()) {
                continue;
            }
            const double lengthFactor = averageLength > 0 ? lengths.at(index) / averageLength : 0.0;
            double score = 0.0;
            for (auto it = frequency.constBegin(); it != frequency.constEnd(); ++it) {
                const int df = documentFrequency.value(it.key());
                const double idf = std::log(1.0 + (passageCount - df + 0.5) / (df + 0.5));
                const double saturation = it.value() * (k1 + 1.0) / (it.value() + k1 * (1.0 - b + b * lengthFactor));
                score += idf * saturation;
            }
            ranked.append({score, index});
        }

        std::stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &c) {
            if (a.first != c.first) {
                return a.first > c.first;
            }
            return a.second < c.second;
        });

        for (const auto &entry : std::as_const(ranked)) {
            const Passage &passage = m_passages.at(entry.second);
            QList<int> matchedGlyphs;
            int snippetStart = -1;
            for (const Token &token : tokensWithPositions(passage.text)) {
                if (!queryTerms.contains(token.text)) {
                    continue;
                }
                if (snippetStart < 0) {
                    snippetStart = token.start;
                }
                for (int offset = token.start; offset < token.start + token.length; ++offset) {
                    const int glyph = passage.glyphAtOffset.value(offset, -1);
                    if (glyph >= 0 && !matchedGlyphs.contains(glyph)) {
                        matchedGlyphs.append(glyph);
                    }
                }
            }
            if (matchedGlyphs.isEmpty()) {
                continue;
            }
            Hit hit;
            hit.page = passage.page;
            hit.score = entry.first;
            for (const int glyph : std::as_const(matchedGlyphs)) {
                if (glyph < passage.glyphs.size()) {
                    hit.rectangles.append(passage.glyphs.at(glyph).rectangle);
                }
            }
            hit.rectangles = mergeLineRectangles(hit.rectangles);
            const QString &text = passage.text;
            const int from = std::max(0, snippetStart - 40);
            const int to = std::min<int>(text.size(), from + 180);
            hit.snippet = text.mid(from, to - from);
            hit.passageText = text;
            rankedHits.append(hit);
            if (rankedHits.size() >= maxHits) {
                break;
            }
        }
    }

    if (!rankedHits.isEmpty()) {
        return rankedHits;
    }

    // Fallback: exact text. A user who pastes a phrase expects that phrase to
    // come back even when the terms alone score poorly.
    const QString needle = query.simplified().toLower();
    if (needle.isEmpty()) {
        return hits;
    }
    for (const Passage &passage : std::as_const(m_passages)) {
        const QString text = passage.text.toLower();
        const int at = text.indexOf(needle);
        if (at < 0) {
            continue;
        }
        Hit hit;
        hit.page = passage.page;
        QList<Okular::NormalizedRect> rectangles;
        for (int offset = at; offset < at + needle.size(); ++offset) {
            const int glyph = passage.glyphAtOffset.value(offset, -1);
            if (glyph >= 0 && glyph < passage.glyphs.size()) {
                rectangles.append(passage.glyphs.at(glyph).rectangle);
            }
        }
        hit.rectangles = mergeLineRectangles(rectangles);
        const int from = std::max(0, at - 40);
        const int to = std::min<int>(passage.text.size(), from + 180);
        hit.snippet = passage.text.mid(from, to - from);
        hit.passageText = passage.text;
        hits.append(hit);
        if (hits.size() >= maxHits) {
            break;
        }
    }
    return hits;
}

bool Index::isEmpty() const
{
    return m_passages.isEmpty();
}

int Index::pageCount() const
{
    return m_pageCount;
}

int Index::wordCount() const
{
    return m_wordCount;
}

int Index::totalCharacters() const
{
    int total = 0;
    for (const Passage &passage : m_passages) {
        total += passage.text.size();
    }
    return total;
}

QList<Okular::NormalizedRect> Index::widenToLine(int page, const QList<Okular::NormalizedRect> &rectangles) const
{
    QList<Okular::NormalizedRect> widened;
    for (const Okular::NormalizedRect &rectangle : rectangles) {
        Okular::NormalizedRect line = rectangle;
        for (const Passage &passage : m_passages) {
            if (passage.page != page) {
                continue;
            }
            for (const Glyph &glyph : passage.glyphs) {
                const Okular::NormalizedRect &area = glyph.rectangle;
                const double center = (area.top + area.bottom) / 2.0;
                if (center < rectangle.top || center > rectangle.bottom) {
                    continue;
                }
                line.left = std::min(line.left, area.left);
                line.top = std::min(line.top, area.top);
                line.right = std::max(line.right, area.right);
                line.bottom = std::max(line.bottom, area.bottom);
            }
        }
        widened.append(line);
    }
    return widened;
}

QString Index::pageText(int page) const
{
    QString text;
    for (const Passage &passage : m_passages) {
        if (passage.page != page) {
            continue;
        }
        if (!text.isEmpty()) {
            text += QLatin1Char(' ');
        }
        text += passage.text;
    }
    return text;
}

QStringList Index::pageSegments(int page) const
{
    QStringList segments;
    QString current;
    const auto flush = [&segments, &current] {
        const QString trimmed = current.trimmed();
        if (trimmed.size() >= 8) {
            segments.append(trimmed);
        }
        current.clear();
    };
    const QString text = pageText(page);
    for (const QChar character : text) {
        if (character == QLatin1Char('.') || character == QLatin1Char('!') || character == QLatin1Char('?') || character == QLatin1Char(';') || character == QChar(0x3002)
            || character == QChar(0xFF01) || character == QChar(0xFF1F) || character == QChar(0xFF1B) || character == QLatin1Char('\n')) {
            flush();
            continue;
        }
        current += character;
        // Long unpunctuated runs are split so a segment stays quotable.
        if (current.size() >= 300) {
            flush();
        }
    }
    flush();
    return segments;
}

QList<Okular::NormalizedRect> Index::locate(int page, const QString &quote, QString *locatedText) const
{
    // Flatten the page into one glyph sequence. Matching then tolerates the
    // differences that survive between a model's quote and a PDF text layer:
    // dash variants and whitespace. Keep the word separators inserted by the
    // index: concatenating glyph text turns "Page 2" into "Page2".
    if (locatedText) {
        locatedText->clear();
    }
    QList<Glyph> glyphs;
    QList<int> glyphAtOffset;
    QString text;
    for (const Passage &passage : m_passages) {
        if (passage.page != page) {
            continue;
        }
        if (!text.isEmpty()) {
            text += QLatin1Char(' ');
            glyphAtOffset.append(-1);
        }
        const int base = glyphs.size();
        glyphs.append(passage.glyphs);
        text += passage.text;
        for (int glyph : passage.glyphAtOffset) {
            glyphAtOffset.append(glyph < 0 ? -1 : base + glyph);
        }
    }
    if (text.isEmpty() || quote.trimmed().isEmpty()) {
        return {};
    }

    const auto fold = [](QChar character) -> QChar {
        const ushort value = character.unicode();
        // Minus signs and hyphen-like dashes, full-width and typographic forms.
        if (value == 0x2212 || (value >= 0x2010 && value <= 0x2015) || value == 0xFF0D || value == 0xFE63) {
            return QChar(0x002D);
        }
        if (value == 0x2018 || value == 0x2019 || value == 0x2032) {
            return QChar(0x0027);
        }
        if (value == 0x201C || value == 0x201D || value == 0x2033) {
            return QChar(0x0022);
        }
        return character;
    };

    // Produce a comparable form of @p input. Offsets map each produced
    // character back to the original string.
    const auto normalize = [&fold](const QString &input, QList<int> *offsets, bool dropSpaces) {
        QString result;
        for (int index = 0; index < input.size(); ++index) {
            const QChar character = fold(input.at(index));
            if (character.isSpace()) {
                if (dropSpaces) {
                    continue;
                }
                if (result.isEmpty() || result.endsWith(QLatin1Char(' '))) {
                    continue;
                }
                result += QLatin1Char(' ');
            } else {
                result += character.toLower();
            }
            if (offsets) {
                offsets->append(index);
            }
        }
        return result;
    };

    const auto rectanglesFor = [&](int originalStart, int originalEnd) {
        QList<Okular::NormalizedRect> rectangles;
        for (int offset = originalStart; offset <= originalEnd; ++offset) {
            const int glyph = glyphAtOffset.value(offset, -1);
            if (glyph >= 0 && glyph < glyphs.size()) {
                rectangles.append(glyphs.at(glyph).rectangle);
            }
        }
        return mergeLineRectangles(rectangles);
    };

    const auto spanFrom = [&](const QList<int> &offsets, int at, int length) {
        const int first = offsets.value(at, -1);
        const int last = offsets.value(at + length - 1, -1);
        if (first < 0 || last < 0 || last < first) {
            return QPair<int, int>{-1, -1};
        }
        return QPair<int, int>{first, last};
    };

    const auto collect = [&](const QString &haystack, const QString &needle, const QList<int> &offsets) {
        QList<QPair<int, int>> spans;
        if (needle.isEmpty()) {
            return spans;
        }
        for (int at = haystack.indexOf(needle); at >= 0 && spans.size() < 32; at = haystack.indexOf(needle, at + 1)) {
            const int length = std::min<int>(needle.size(), haystack.size() - at);
            const auto span = spanFrom(offsets, at, length);
            if (span.first >= 0) {
                // A numeric label must not match inside a page number such as
                // 12 or 20; use boundaries from the original spaced text.
                if (needle.front().isLetterOrNumber() && span.first > 0 && text.at(span.first - 1).isLetterOrNumber()) continue;
                if (needle.back().isLetterOrNumber() && span.second + 1 < text.size() && text.at(span.second + 1).isLetterOrNumber()) continue;
                spans.append({span.first, span.second});
            }
        }
        return spans;
    };

    // A short label such as "(23)" occurs several times on a page: once at the
    // end of the displayed equation and more often inside running text ("as
    // shown in Eq. (23)"). Equation and figure numbers sit at the right edge of
    // their line, so prefer the occurrence that ends a line, then the rightmost.
    const auto lineEndingScore = [&](const QPair<int, int> &span) {
        double top = 1.0;
        double bottom = 0.0;
        double right = 0.0;
        for (int offset = span.first; offset <= span.second; ++offset) {
            const int glyph = glyphAtOffset.value(offset, -1);
            if (glyph < 0 || glyph >= glyphs.size()) {
                continue;
            }
            const Okular::NormalizedRect &rectangle = glyphs.at(glyph).rectangle;
            top = std::min(top, rectangle.top);
            bottom = std::max(bottom, rectangle.bottom);
            right = std::max(right, rectangle.right);
        }
        if (bottom <= top) {
            return 0.0;
        }
        bool lineEnd = true;
        for (const Glyph &glyph : std::as_const(glyphs)) {
            const Okular::NormalizedRect &rectangle = glyph.rectangle;
            const double center = (rectangle.top + rectangle.bottom) / 2.0;
            if (center < top || center > bottom) {
                continue;
            }
            if (rectangle.left > right + 0.01) {
                lineEnd = false;
                break;
            }
        }
        return (lineEnd ? 2.0 : 0.0) + right;
    };

    QList<QPair<int, int>> candidates;
    QList<int> spacedOffsets;
    const QString spaced = normalize(text, &spacedOffsets, false);
    const QString spacedNeedle = normalize(quote, nullptr, false).trimmed();
    candidates = collect(spaced, spacedNeedle, spacedOffsets);
    if (candidates.isEmpty()) {
        // Ignore whitespace completely: PDF text layers and model quotes
        // disagree about spaces and line breaks around formulas.
        QList<int> tightOffsets;
        const QString tight = normalize(text, &tightOffsets, true);
        const QString tightNeedle = normalize(quote, nullptr, true);
        if (tightNeedle.size() >= 6) {
            candidates = collect(tight, tightNeedle, tightOffsets);
        }
    }
    if (!candidates.isEmpty()) {
        QPair<int, int> best{-1, -1};
        double bestScore = -1.0;
        for (const auto &span : std::as_const(candidates)) {
            const auto rectangles = rectanglesFor(span.first, span.second);
            bool connected = !rectangles.isEmpty();
            for (int i = 1; i < rectangles.size(); ++i) {
                const auto &previous = rectangles.at(i - 1);
                const auto &current = rectangles.at(i);
                const double lineHeight = std::max(previous.height(), current.height());
                if (current.top - previous.bottom > 3 * lineHeight) {
                    connected = false;
                    break;
                }
            }
            // Page text includes running headers/footers. A quote spanning a
            // large blank region is not one passage, even if its text matches.
            if (!connected) continue;
            const double score = lineEndingScore(span);
            if (score > bestScore) {
                bestScore = score;
                best = span;
            }
        }
        if (best.first < 0) return {};
        if (locatedText) {
            *locatedText = text.mid(best.first, best.second - best.first + 1);
        }
        return rectanglesFor(best.first, best.second);
    }

    // A citation is evidence, not a keyword search. Let the caller repair the
    // quote rather than highlighting an approximate window (including footers).
    return {};
}

bool Index::mostlyCjk() const
{
    int cjk = 0;
    int other = 0;
    for (const Passage &passage : m_passages) {
        for (const QChar character : passage.text) {
            if (!character.isLetter() && !isCjkCharacter(character)) {
                continue;
            }
            if (isCjkCharacter(character)) {
                ++cjk;
            } else {
                ++other;
            }
        }
        if (cjk + other >= 4000) {
            break;
        }
    }
    return cjk > other;
}
}
