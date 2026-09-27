/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef MENGSHEE_LATEXAPPEARANCE_H
#define MENGSHEE_LATEXAPPEARANCE_H

#include <poppler-annotation.h>
#include <memory>

namespace Poppler {
class Document;
}

namespace MengsheeLatexAppearance
{
struct RawSourceData;

// Live page-tree edits destroy cached Core Pages. Retie existing Qt wrappers
// by indirect object identity before any wrapper geometry getter/setter runs.
// Wrappers for detached annotations are not rebound to a different page.
void refreshAnnotationPageBindings(Poppler::Document *document, const QList<Poppler::Annotation *> &annotations);

// A short-lived, same-document raw-Form snapshot. Capture before Qt setters
// invalidate the old AP. It is not a cross-document appearance transfer.
class RawSource
{
public:
    RawSource() = default;
    bool isValid() const;

private:
    std::shared_ptr<RawSourceData> data;
    friend RawSource captureRawSource(Poppler::Document *, int, Poppler::StampAnnotation *, QString *);
    friend bool rebuild(Poppler::Document *, int, Poppler::StampAnnotation *, const QString &, const Poppler::StampAnnotation::CustomPdfAppearanceOptions &, const RawSource &, QString *, int, const QString &);
};

// nativePage is zero-based, as in the Qt wrapper. Caller holds the generator's
// document mutex. Uses the real bound Core handle, never NM-based identity;
// unbound annotations and wrong-document/page handles are rejected.
RawSource captureRawSource(Poppler::Document *document, int nativePage, Poppler::StampAnnotation *stamp, QString *error = nullptr);

// PDF (y-up) coordinates. The badge sits above the body's top-left corner;
// use the same geometry when expanding Annot /Rect and composing AP /BBox.
QRectF orderedCalloutBadgeRect(const QRectF &frame, int orderedNumber, const QString &formattedLabel = {});

// Fresh source uses only Poppler's existing plain PDF importer (no frame options).
// Mengshee then owns the entire frame/leader/content AP. Its Resources/Fm0 always
// points at the original, untrimmed source Form, never a previous composed AP.
// Without a file, use preserved/current raw source. Source and destination must
// belong to the same live document; import a PDF file for cross-document data.
// orderedNumber is the Numbered Callout's display order, not its editable ID.
// Empty formattedLabel falls back to decimal order. ASCII uses Base14 metrics;
// Unicode is shaped with Qt font fallback and persisted as vector glyph paths.
// Its badge uses borderColor for box fill/stroke and fillColor for text, each
// with its own color alpha multiplied by annotation opacity exactly once.
bool rebuild(Poppler::Document *document,
             int nativePage,
             Poppler::StampAnnotation *stamp,
             const QString &sourcePdf,
             const Poppler::StampAnnotation::CustomPdfAppearanceOptions &options,
             const RawSource &preserved = {},
             QString *error = nullptr,
             int orderedNumber = 0,
             const QString &formattedLabel = {});
}

#endif
