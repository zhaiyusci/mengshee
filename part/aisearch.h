// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/area.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace Okular
{
class Document;
class Page;
}

/**
 * Ranked passage search over the document text layer.
 *
 * Everything here works on the document model's own text abstraction
 * (Okular::Page::words()), so it never talks to a PDF backend directly and
 * works with whichever generator produced the text page, including an OCR
 * text layer. Nothing is read from or written to the file: hits are display
 * state only.
 *
 * Some generators describe the text page word by word, others glyph by glyph.
 * The index therefore keeps the glyphs for highlighting and a joined text
 * string for matching; it never assumes which granularity it is given.
 */
namespace AiSearch
{
struct TextBlock
{
    QString text;
    Okular::NormalizedRect rectangle;
};

/** Visual text lines for deterministic citations. The caller loads the text page first. */
QList<TextBlock> textBlocks(const Okular::Page *page);

struct Glyph
{
    QString text;
    Okular::NormalizedRect rectangle;
};

struct Passage
{
    int page = -1;
    QList<Glyph> glyphs;
    QString text;
    // For every character of text, the glyph it came from, or -1 for an
    // inserted separator. This maps a match back to page geometry.
    QList<int> glyphAtOffset;
};

struct Hit
{
    int page = -1;
    QList<Okular::NormalizedRect> rectangles;
    QString snippet;
    // The whole passage, used as model context and to validate citations.
    QString passageText;
    double score = 0.0;
};

struct Token
{
    QString text;
    int start = 0;
    int length = 0;
};

class Index
{
public:
    /** Builds the index from the live document. Returns false if it is empty. */
    bool build(Okular::Document *document, QString *errorText = nullptr);

    /** Returns at most @p maxHits passages, best first. */
    QList<Hit> search(const QString &query, int maxHits = 20) const;

    bool isEmpty() const;
    int pageCount() const;
    int wordCount() const;
    int totalCharacters() const;
    /** Text of one page, passages joined, for model context and evidence lookup. */
    QString pageText(int page) const;
    /** Sentence-like segments of one page, used when the model names a page but
     *  gives no exact quote. */
    QStringList pageSegments(int page) const;
    /** Page geometry of a verbatim quote, empty when the quote is not on that page.
     *  @p locatedText receives the text actually found on the page, which may
     *  differ from @p quote in spacing, dashes or Unicode form. */
    QList<Okular::NormalizedRect> locate(int page, const QString &quote, QString *locatedText = nullptr) const;
    /** Widens each rectangle to the full text line it sits on. */
    QList<Okular::NormalizedRect> widenToLine(int page, const QList<Okular::NormalizedRect> &rectangles) const;
    /** True when the indexed text is mostly CJK, used to tell the model which
     *  language the document is written in. */
    bool mostlyCjk() const;

private:
    QList<Passage> m_passages;
    int m_pageCount = 0;
    int m_wordCount = 0;
};

/** Lowercased words, plus single characters and bigrams for CJK text. */
QStringList tokenize(const QString &text);
QList<Token> tokensWithPositions(const QString &text);
}
