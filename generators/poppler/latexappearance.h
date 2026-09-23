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
    friend bool rebuild(Poppler::Document *, int, Poppler::StampAnnotation *, const QString &, const Poppler::StampAnnotation::CustomPdfAppearanceOptions &, const RawSource &, QString *);
};

// nativePage is zero-based, as in the Qt wrapper. Caller holds the generator's
// document mutex. Uses the real bound Core handle, never NM-based identity;
// unbound annotations and wrong-document/page handles are rejected.
RawSource captureRawSource(Poppler::Document *document, int nativePage, Poppler::StampAnnotation *stamp, QString *error = nullptr);

// Fresh source uses only Poppler's existing plain PDF importer (no frame options).
// Mengshee then owns the entire frame/leader/content AP. Its Resources/Fm0 always
// points at the original, untrimmed source Form, never a previous composed AP.
// Without a file, use preserved/current raw source. Source and destination must
// belong to the same live document; import a PDF file for cross-document data.
bool rebuild(Poppler::Document *document,
             int nativePage,
             Poppler::StampAnnotation *stamp,
             const QString &sourcePdf,
             const Poppler::StampAnnotation::CustomPdfAppearanceOptions &options,
             const RawSource &preserved = {},
             QString *error = nullptr);
}

#endif
