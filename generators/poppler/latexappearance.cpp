/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "latexappearance.h"
#include "core/numberedcalloutgeometry_p.h"
#include "popplercorebridge.h"
#include "external/poppler/qt6/src/poppler-annotation-private.h"

#include <Annot.h>
#include <Array.h>
#include <Dict.h>
#include <Object.h>
#include <Page.h>
#include <Stream.h>
#include <XRef.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include <map>
#include <set>
#include <QPainterPath>

namespace MengsheeLatexAppearance
{
struct RawSourceData {
    Poppler::Document *owner = nullptr;
    PDFDoc *core = nullptr;
    Object form;
    QRectF box; // PDF coordinates, not Qt screen coordinates
};

using Okular::NumberedCalloutGeometry::BadgeLabel;
using Okular::NumberedCalloutGeometry::badgeLabel;

QRectF orderedCalloutBadgeRect(const QRectF &frame, int orderedNumber, const QString &formattedLabel)
{
    if (orderedNumber <= 0 || !frame.isValid()) return {};
    const QSizeF size = Okular::NumberedCalloutGeometry::badgeSize(orderedNumber, formattedLabel);
    if (size.isEmpty()) return {};
    return QRectF(QPointF(frame.left(), frame.bottom()), size);
}

namespace
{
// AutoFreeMemStream is not exported by the pinned Core DLL. Use the exported
// MemStream and keep immutable bytes alive in every copy/substream instead.
// These newly generated AP bytes have no filters, so no filter-removal hack is
// needed when PDFDoc writes them back to the document.
class AppearanceStream final : public MemStream
{
public:
    using Storage = std::shared_ptr<const std::vector<char>>;

    AppearanceStream(Storage bytes, Goffset start, Goffset count, Object &&dictionary)
        : MemStream(bytes->data(), start, count, std::move(dictionary))
        , storage(std::move(bytes))
    {
    }

    std::unique_ptr<BaseStream> copy() override
    {
        return std::make_unique<AppearanceStream>(storage, getStart(), getLength(), dict.copy());
    }

    std::unique_ptr<Stream> makeSubStream(Goffset start, bool limited, Goffset count, Object &&dictionary) override
    {
        const Goffset end = getStart() + getLength();
        start = std::clamp(start, getStart(), end);
        const Goffset available = end - start;
        count = limited ? std::clamp(count, Goffset(0), available) : available;
        return std::make_unique<AppearanceStream>(storage, start, count, std::move(dictionary));
    }

    void moveStart(Goffset delta) override
    {
        MemStream::moveStart(std::clamp(delta, -getStart(), getLength()));
    }

private:
    Storage storage;
};

bool fail(QString *error, const char *message)
{
    if (error) {
        *error = QString::fromLatin1(message);
    }
    return false;
}

bool coordinate(double value)
{
    return std::isfinite(value) && std::abs(value) <= 1000000.0;
}

bool rectangle(const QRectF &rect)
{
    return coordinate(rect.x()) && coordinate(rect.y()) && coordinate(rect.right()) && coordinate(rect.bottom()) && rect.width() > 0.0 && rect.height() > 0.0
        && rect.width() <= 50000.0 && rect.height() <= 50000.0;
}

// Form a pointer to the existing protected base member in a derived context.
// Applying that Base::* pointer to the real Annotation is standard C++; no
// object is instantiated as/cast to this subclass, and no layout is guessed.
// The pinned private header then provides the actual bound Core handle.
class AnnotationDataAccess : public Poppler::Annotation
{
public:
    static auto member() { return &AnnotationDataAccess::d_ptr; }
};

std::shared_ptr<Annot> findStamp(Poppler::Document *document, int nativePage, Poppler::StampAnnotation *stamp, QString *error)
{
    PDFDoc *core = popplerCoreDocument(document);
    if (!core || !stamp || nativePage < 0 || nativePage >= core->getNumPages()) {
        fail(error, "LaTeX appearance requires an annotation bound to the live document.");
        return {};
    }
    const auto *data = (stamp->*AnnotationDataAccess::member()).constData();
    if (!data || !data->pdfAnnot || !data->parentDoc || data->parentDoc->doc.get() != core || data->pdfAnnot->getDoc() != core || data->pdfAnnot->getType() != Annot::typeStamp
        || data->pdfPage != core->getPage(nativePage + 1) || data->pdfAnnot->getPageNum() != nativePage + 1) {
        fail(error, "The Qt stamp is not bound to the expected native document and page.");
        return {};
    }
    // NM is user metadata, not identity: empty and duplicate names are legal.
    return data->pdfAnnot;
}

QByteArray number(double value, int precision = 6)
{
    return QByteArray::number(value, 'f', precision);
}

void point(QByteArray &out, const QPointF &p, const char *op)
{
    out += number(p.x(), 2) + ' ' + number(p.y(), 2) + ' ' + op + '\n';
}

void color(QByteArray &out, const QColor &c, bool fill)
{
    out += number(c.redF(), 5) + ' ' + number(c.greenF(), 5) + ' ' + number(c.blueF(), 5) + (fill ? " rg\n" : " RG\n");
}

void boxPath(QByteArray &out, const QRectF &r, int precision)
{
    out += number(r.x(), precision) + ' ' + number(r.y(), precision) + ' ' + number(r.width(), precision) + ' ' + number(r.height(), precision) + " re\n";
}

bool validateOptions(const Poppler::StampAnnotation::CustomPdfAppearanceOptions &options, QString *error)
{
    if ((options.outerSize.isValid() && !rectangle(QRectF(QPointF(0, 0), options.outerSize))) || (options.frameRect.isValid() && !rectangle(options.frameRect))
        || !coordinate(options.contentOffset.x()) || !coordinate(options.contentOffset.y()) || !coordinate(options.contentFrameInset) || !std::isfinite(options.borderWidth)
        || options.borderWidth < 0.0 || options.borderWidth > 50000.0) {
        return fail(error, "Invalid LaTeX appearance frame geometry.");
    }
    for (const QPointF &p : options.leaderLine) {
        if (!coordinate(p.x()) || !coordinate(p.y())) {
            return fail(error, "Invalid LaTeX callout geometry.");
        }
    }
    return true;
}

Object buildForm(XRef *xref, const Object &rawForm, const QRectF &sourceBox, const Poppler::StampAnnotation::CustomPdfAppearanceOptions &options, double opacity, int orderedNumber, const QString &formattedLabel)
{
    const QSizeF outer = options.outerSize.isValid() ? options.outerSize : sourceBox.size();
    const bool framed = options.frameRect.isValid();
    const QRectF frame = framed ? options.frameRect : QRectF(QPointF(0, 0), sourceBox.size());
    QPointF offset = options.contentOffset;
    if (framed && options.alignContentToFrameTopLeft) {
        offset = QPointF(frame.x() + options.contentFrameInset, frame.y() + frame.height() - options.contentFrameInset - sourceBox.height());
    }
    const QColor border = options.borderColor.isValid() ? options.borderColor : QColor(Qt::black);
    const bool visibleBorder = border.alpha() != 0;
    const bool stroke = visibleBorder && options.borderWidth > 0.0;
    const bool fill = options.fillColor.isValid() && options.fillColor.alpha() != 0;
    QByteArray out("/GS0 gs\n");

    // Same three-point leader and open-arrow geometry used by existing notes.
    // It is outside the source-only clip, including zero-width frame borders.
    if (options.leaderLine.size() == 3 && visibleBorder) {
        const double width = std::max(1.0, options.borderWidth);
        const QPointF p1 = options.leaderLine[0], p2 = options.leaderLine[1], p3 = options.leaderLine[2];
        out += "q\n";
        color(out, border, false);
        out += number(width, 2) + " w\n";
        point(out, p1, "m");
        point(out, p2, "l");
        point(out, p3, "l");
        out += "S\n";
        const double length = std::hypot(p2.x() - p1.x(), p2.y() - p1.y());
        if (length > 0.0) {
            const double size = std::min(std::max(6.0, 6.0 * width), length / 2.0);
            const double ux = (p2.x() - p1.x()) / length, uy = (p2.y() - p1.y()) / length;
            const double wing = size / std::sqrt(3.0);
            point(out, QPointF(p1.x() + size * ux + wing * uy, p1.y() + size * uy - wing * ux), "m");
            point(out, p1, "l");
            point(out, QPointF(p1.x() + size * ux - wing * uy, p1.y() + size * uy + wing * ux), "l");
            out += "S\n";
        }
        out += "Q\n";
    }
    if (framed && (fill || stroke)) {
        out += "q\n";
        if (stroke) {
            color(out, border, false);
            out += number(options.borderWidth, 2) + " w\n";
        }
        const double half = options.borderWidth / 2.0;
        boxPath(out, frame.adjusted(half, half, -half, -half), 2);
        if (fill) {
            color(out, options.fillColor, true);
        }
        out += fill ? (stroke ? "B\n" : "f\n") : "S\n";
        out += "Q\n";
    }

    const double inset = stroke ? options.borderWidth : 0.0;
    const QRectF inner = frame.adjusted(inset, inset, -inset, -inset);
    if (!framed || (inner.width() > 0.0 && inner.height() > 0.0)) {
        out += "q\n";
        if (framed) {
            boxPath(out, inner, 6);
            out += "W n\n";
        }
        out += "1 0 0 1 " + number(offset.x() - sourceBox.x()) + ' ' + number(offset.y() - sourceBox.y()) + " cm\n/Fm0 Do\nQ\n";
    }

    if (orderedNumber > 0) {
        const BadgeLabel shaped = badgeLabel(orderedNumber, formattedLabel);
        const QRectF badge = orderedCalloutBadgeRect(frame, orderedNumber, formattedLabel);
        const double fontSize = 9.0;
        const double textWidth = shaped.width;
        const double textX = badge.left() + (badge.width() - textWidth) / 2.0;
        const double baseline = badge.top() + (badge.height() - fontSize * 0.718) / 2.0;
        // Deliberately outside the source-only clip; never scale the body to
        // make room for the badge, and never bake it into the raw Fm0 source.
        const QColor textColor = options.fillColor.isValid() ? options.fillColor : QColor(Qt::transparent);
        out += "q\n/BadgeBoxGS gs\n";
        color(out, border, true);
        color(out, border, false);
        out += "0.75 w\n[] 0 d\n0 j\n";
        boxPath(out, badge.adjusted(0.375, 0.375, -0.375, -0.375), 6);
        out += "B\n/BadgeTextGS gs\n";
        color(out, textColor, true);
        if (shaped.ascii) {
            QByteArray literal = shaped.text.toLatin1();
            literal.replace("\\", "\\\\");
            literal.replace("(", "\\(");
            literal.replace(")", "\\)");
            out += "BT\n/OrderBadgeFont 9 Tf\n0 Tc 0 Tw 100 Tz 0 Ts 0 Tr\n1 0 0 1 " + number(textX) + ' ' + number(baseline) + " Tm\n(" + literal + ") Tj\nET\n";
        } else {
            const QRectF ink = shaped.outline.boundingRect();
            const QPainterPath path = QTransform::fromTranslate(badge.center().x() - ink.center().x(), badge.center().y() - ink.center().y()).map(shaped.outline);
            for (int i = 0; i < path.elementCount(); ++i) {
                const auto e = path.elementAt(i);
                if (e.isMoveTo() || e.isLineTo()) {
                    out += number(e.x) + ' ' + number(e.y) + (e.isMoveTo() ? " m\n" : " l\n");
                } else if (e.type == QPainterPath::CurveToElement) {
                    const auto c2 = path.elementAt(++i);
                    const auto end = path.elementAt(++i);
                    out += number(e.x) + ' ' + number(e.y) + ' ' + number(c2.x) + ' ' + number(c2.y) + ' ' + number(end.x) + ' ' + number(end.y) + " c\n";
                }
            }
            out += "f\n";
        }
        out += "Q\n";
    }

    auto *gs = new Dict(xref);
    gs->set("CA", Object(opacity));
    gs->set("ca", Object(opacity));
    auto *states = new Dict(xref);
    states->set("GS0", Object(gs));
    if (orderedNumber > 0) {
        // ExtGState alpha replaces the inherited GS0 value. Include annotation
        // opacity exactly once, independently for background and foreground.
        const auto badgeState = [xref, opacity](double colorAlpha) {
            auto *state = new Dict(xref);
            state->set("CA", Object(opacity * colorAlpha));
            state->set("ca", Object(opacity * colorAlpha));
            return Object(state);
        };
        states->set("BadgeBoxGS", badgeState(border.alphaF()));
        states->set("BadgeTextGS", badgeState(options.fillColor.isValid() ? options.fillColor.alphaF() : 0.0));
    }
    auto *forms = new Dict(xref);
    // Never put the previous outer AP here: it contains a destructive frame clip.
    // Retaining this raw Form even for an empty inner frame makes growth reversible.
    forms->set("Fm0", rawForm.copy());
    auto *resources = new Dict(xref);
    resources->set("ExtGState", Object(states));
    resources->set("XObject", Object(forms));
    if (orderedNumber > 0) {
        auto *font = new Dict(xref);
        font->set("Type", Object(objName, "Font"));
        font->set("Subtype", Object(objName, "Type1"));
        font->set("BaseFont", Object(objName, "Helvetica-Bold"));
        font->set("Encoding", Object(objName, "WinAnsiEncoding"));
        Object fontObject(font);
        auto *fonts = new Dict(xref);
        fonts->set("OrderBadgeFont", Object(xref->addIndirectObject(fontObject)));
        resources->set("Font", Object(fonts));
    }
    auto *bounds = new Array(xref);
    bounds->add(Object(0.0));
    bounds->add(Object(0.0));
    bounds->add(Object(outer.width()));
    bounds->add(Object(outer.height()));
    auto *dict = new Dict(xref);
    dict->set("Type", Object(objName, "XObject"));
    dict->set("Subtype", Object(objName, "Form"));
    dict->set("BBox", Object(bounds));
    dict->set("Resources", Object(resources));
    dict->set("MengsheeLatexAppearanceVersion", Object(1));
    dict->set("Length", Object(static_cast<int>(out.size())));
    auto bytes = std::make_shared<const std::vector<char>>(out.begin(), out.end());
    const Goffset length = static_cast<Goffset>(bytes->size());
    return Object(std::make_unique<AppearanceStream>(std::move(bytes), 0, length, Object(dict)));
}
}

void refreshAnnotationPageBindings(Poppler::Document *document, const QList<Poppler::Annotation *> &annotations)
{
    PDFDoc *core = popplerCoreDocument(document);
    if (!core || annotations.isEmpty()) return;
    struct Binding {
        ::Page *page = nullptr;
        std::shared_ptr<Annot> annotation;
    };
    using Key = std::pair<int, int>;
    std::map<Key, Binding> live;
    for (int pageIndex = 1; pageIndex <= core->getNumPages(); ++pageIndex) {
        ::Page *page = core->getPage(pageIndex);
        Annots *annots = page ? page->getAnnots() : nullptr;
        if (!annots) continue;
        for (const auto &annotation : annots->getAnnots()) {
            const Ref ref = annotation->getRef();
            if (ref.num <= 0) continue;
            const auto [entry, inserted] = live.emplace(Key(ref.num, ref.gen), Binding{page, annotation});
            // Malformed PDFs may share one annotation across pages. Never pick
            // an arbitrary page for such an ambiguous object identity.
            if (!inserted && entry->second.page != page) entry->second = {};
        }
    }
    QList<Poppler::Annotation *> pending = annotations;
    std::set<Poppler::AnnotationPrivate *> visited;
    while (!pending.isEmpty()) {
        Poppler::Annotation *wrapper = pending.takeLast();
        if (!wrapper) continue;
        auto *data = (wrapper->*AnnotationDataAccess::member()).data();
        if (!data || !visited.insert(data).second || !data->pdfAnnot || !data->parentDoc || data->parentDoc->doc.get() != core || data->pdfAnnot->getDoc() != core) continue;
        // Revisions can hold their own Qt wrappers, sharing the same page cache.
        for (const auto &revision : data->revisions) pending.append(revision.get());
        const Ref ref = data->pdfAnnot->getRef();
        const auto found = live.find(Key(ref.num, ref.gen));
        if (found == live.end() || !found->second.annotation || found->second.annotation->getType() != data->pdfAnnot->getType()) continue;
        // Do not dereference the old pdfPage: the page-tree editor destroyed it.
        // Keep the Qt wrapper/data identity (and Core undo targets), but replace
        // BOTH native handles. The fresh Annots constructor assigned pageNum.
        data->pdfAnnot = found->second.annotation;
        data->pdfPage = found->second.page;
    }
}

bool RawSource::isValid() const
{
    return data && data->owner && data->core && !data->form.isNull();
}

RawSource captureRawSource(Poppler::Document *document, int nativePage, Poppler::StampAnnotation *stamp, QString *error)
{
    RawSource result;
    auto native = findStamp(document, nativePage, stamp, error);
    if (!native) {
        return result;
    }
    XRef *xref = native->getDoc()->getXRef();
    Object appearance = native->getAppearance().fetch(xref);
    if (!appearance.isStream()) {
        fail(error, "The annotation has no recoverable appearance stream.");
        return result;
    }
    Object version = appearance.streamGetDict()->lookup("MengsheeLatexAppearanceVersion");
    if (!version.isNull() && (!version.isInt() || version.getInt() != 1)) {
        fail(error, "Unsupported Mengshee LaTeX appearance version.");
        return result;
    }
    Object resources = appearance.streamGetDict()->lookup("Resources");
    Object forms = resources.isDict() ? resources.dictLookup("XObject") : Object::null();
    Object raw = forms.isDict() ? forms.dictLookupNF("Fm0").copy() : Object::null();
    Object stream = raw.fetch(xref);
    if (stream.isStream() && (stream.getStream() == appearance.getStream() || !stream.streamGetDict()->lookup("MengsheeLatexAppearanceVersion").isNull())) {
        fail(error, "The saved source points at a composed appearance, not an untrimmed Form.");
        return result;
    }
    Object bounds = stream.isStream() ? stream.streamGetDict()->lookup("BBox") : Object::null();
    if (!bounds.isArray() || bounds.arrayGetLength() != 4) {
        fail(error, "The old appearance has no recoverable raw source Form; recompile the note.");
        return result;
    }
    double values[4];
    for (int i = 0; i < 4; ++i) {
        Object value = bounds.arrayGet(i);
        if (!value.isNum() || !coordinate(value.getNum())) {
            fail(error, "Invalid raw LaTeX source bounds.");
            return result;
        }
        values[i] = value.getNum();
    }
    const QRectF box(values[0], values[1], values[2] - values[0], values[3] - values[1]);
    if (!rectangle(box)) {
        fail(error, "Invalid raw LaTeX source dimensions.");
        return result;
    }
    if (!raw.isRef()) {
        // PDF resources should be indirect streams. Normalize a permissively
        // parsed direct stream without ever substituting the outer clipped AP.
        raw = Object(xref->addIndirectObject(stream));
    }
    result.data = std::make_shared<RawSourceData>();
    result.data->owner = document;
    result.data->core = native->getDoc();
    result.data->form = std::move(raw);
    result.data->box = box;
    if (error) {
        error->clear();
    }
    return result;
}

bool rebuild(Poppler::Document *document,
             int nativePage,
             Poppler::StampAnnotation *stamp,
             const QString &sourcePdf,
             const Poppler::StampAnnotation::CustomPdfAppearanceOptions &options,
             const RawSource &preserved,
             QString *error,
             int orderedNumber,
             const QString &formattedLabel)
{
    auto native = findStamp(document, nativePage, stamp, error);
    if (!native || !validateOptions(options, error)) {
        return false;
    }
    if (orderedNumber < 0) {
        return fail(error, "Invalid ordered callout number.");
    }
    if (orderedNumber > 0) {
        if (!badgeLabel(orderedNumber, formattedLabel).valid) {
            return fail(error, "The formatted callout label is too long or contains unavailable glyphs.");
        }
        const QRectF outerBounds = QRectF(QPointF(0, 0), options.outerSize).adjusted(-0.000001, -0.000001, 0.000001, 0.000001);
        if (!options.frameRect.isValid() || !outerBounds.contains(orderedCalloutBadgeRect(options.frameRect, orderedNumber, formattedLabel))) {
            return fail(error, "Ordered callout appearance bounds do not include its badge.");
        }
    }
    const double opacity = stamp->style().opacity();
    if (!std::isfinite(opacity) || opacity < 0.0 || opacity > 1.0) {
        return fail(error, "Invalid LaTeX appearance opacity.");
    }
    if (preserved.isValid() && (preserved.data->owner != document || preserved.data->core != native->getDoc())) {
        return fail(error, "Raw appearance resources belong to a different document.");
    }
    Object oldAppearance = native->getAppearance().fetch(native->getDoc()->getXRef());
    RawSource source = preserved;
    if (!sourcePdf.isEmpty()) {
        // Existing generic importer copies fonts/resources into the destination
        // XRef. Poppler is not asked to draw a frame, leader, or content clip.
        if (!stamp->setStampCustomPdf(sourcePdf, 1)) {
            return fail(error, "Could not import the raw LaTeX source PDF.");
        }
        source = captureRawSource(document, nativePage, stamp, error);
    } else if (!source.isValid()) {
        source = captureRawSource(document, nativePage, stamp, error);
    }
    if (!source.isValid()) {
        if (oldAppearance.isStream()) {
            native->setNewAppearance(std::move(oldAppearance));
        }
        return false;
    }
    Object form = buildForm(native->getDoc()->getXRef(), source.data->form, source.data->box, options, opacity, orderedNumber, formattedLabel);
    native->setNewAppearance(std::move(form));
    if (error) {
        error->clear();
    }
    return true;
}
}
