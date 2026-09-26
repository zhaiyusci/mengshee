/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "pdfreadingviews.h"

#include <Array.h>
#include <Dict.h>
#include <GfxState.h>
#include <Object.h>
#include <PDFDoc.h>
#include <Page.h>
#include <XRef.h>
#include <goo/GooString.h>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>

namespace MengsheePdfReadingViews
{
namespace
{
constexpr const char *metadataKey = "MengsheeViews";
constexpr int maxViews = 10000;

bool fail(QString *error, const char *message)
{
    if (error) {
        *error = QString::fromLatin1(message);
    }
    return false;
}

Page *getPage(PDFDoc *document, int index, QString *error)
{
    if (error) {
        error->clear();
    }
    if (!document || index < 0 || index >= document->getNumPages()) {
        fail(error, "View definitions require a valid PDF page.");
        return nullptr;
    }
    Page *page = document->getPage(index + 1);
    if (!page) {
        fail(error, "Could not read the PDF page for View definitions.");
    }
    return page;
}

QString uuidValue(const Object &value)
{
    if (!value.isString() || value.getString()->toStr().size() > 64) {
        return {};
    }
    const auto &bytes = value.getString()->toStr();
    const QString text = QString::fromUtf8(bytes.data(), static_cast<qsizetype>(bytes.size()));
    const QUuid uuid(text);
    return uuid.isNull() ? QString() : uuid.toString(QUuid::WithoutBraces);
}

Object uuidObject(const QString &uuid)
{
    return Object(std::make_unique<GooString>(uuid.toUtf8().toStdString()));
}

bool schema(const Object &metadata, QString *error)
{
    if (metadata.isNull()) {
        return true;
    }
    if (!metadata.isDict()) {
        return fail(error, "Malformed View metadata; it will not be overwritten.");
    }
    const Object version = metadata.dictLookup("Version");
    if (!version.isInt() || version.getInt() != 1) {
        return fail(error, "Unsupported View metadata version; it will not be overwritten.");
    }
    if (uuidValue(metadata.dictLookup("PageId")).isEmpty()) {
        return fail(error, "Malformed View page identity; it will not be overwritten.");
    }
    const Object items = metadata.dictLookup("Items");
    if (!items.isArray() || items.arrayGetLength() > maxViews) {
        return fail(error, "Malformed or oversized View definitions; they will not be overwritten.");
    }
    return true;
}

bool finiteCoordinate(double value)
{
    return std::isfinite(value) && std::abs(value) <= 1.0e12;
}

// Persist raw PDF user-space coordinates, not device pixels. /UserUnit changes
// the physical unit of the page and its content together: no extra multiplier
// belongs in stored Rect values or normalized coordinates. Native /Rotate and
// nonzero/negative CropBox origins are accounted for by this same CTM both ways.
struct Coordinates {
    std::array<double, 6> ctm{};
    double width = 0;
    double height = 0;
    double determinant = 0;

    explicit Coordinates(Page *page)
    {
        GfxState state(72, 72, page->getCropBox(), page->getRotate(), true);
        ctm = state.getCTM();
        width = page->getCropWidth();
        height = page->getCropHeight();
        if (page->getRotate() == 90 || page->getRotate() == 270) {
            std::swap(width, height);
        }
        determinant = ctm[0] * ctm[3] - ctm[1] * ctm[2];
    }

    bool valid() const
    {
        return finiteCoordinate(width) && finiteCoordinate(height) && width > 0 && height > 0 && std::isfinite(determinant) && std::abs(determinant) > 1e-12
            && std::all_of(ctm.begin(), ctm.end(), finiteCoordinate);
    }

    QPointF toPdf(double x, double y) const
    {
        const double dx = x * width - ctm[4];
        const double dy = y * height - ctm[5];
        return {(ctm[3] * dx - ctm[2] * dy) / determinant, (-ctm[1] * dx + ctm[0] * dy) / determinant};
    }

    QPointF toNormalized(double x, double y) const
    {
        return {(ctm[0] * x + ctm[2] * y + ctm[4]) / width, (ctm[1] * x + ctm[3] * y + ctm[5]) / height};
    }
};

template<typename Mapper>
std::array<double, 4> mapRectangle(const std::array<double, 4> &rect, Mapper map)
{
    const std::array<QPointF, 4> points = {map(rect[0], rect[1]), map(rect[0], rect[3]), map(rect[2], rect[1]), map(rect[2], rect[3])};
    std::array<double, 4> result = {points[0].x(), points[0].y(), points[0].x(), points[0].y()};
    for (const auto &point : points) {
        result[0] = std::min(result[0], point.x());
        result[1] = std::min(result[1], point.y());
        result[2] = std::max(result[2], point.x());
        result[3] = std::max(result[3], point.y());
    }
    return result;
}

bool validRectangle(const std::array<double, 4> &rect)
{
    return std::all_of(rect.begin(), rect.end(), finiteCoordinate) && rect[0] < rect[2] && rect[1] < rect[3];
}

bool parseItems(const Object &metadata, const Coordinates &coordinates, QList<Okular::ReadingView> *views, QString *error)
{
    if (metadata.isNull()) {
        return true;
    }
    const Object items = metadata.dictLookup("Items");
    QSet<QString> ids;
    for (int i = 0; i < items.arrayGetLength(); ++i) {
        const Object item = items.arrayGet(i);
        if (!item.isDict()) {
            return fail(error, "Malformed View definition.");
        }
        const QString id = uuidValue(item.dictLookup("Id"));
        const Object number = item.dictLookup("Number");
        const Object rect = item.dictLookup("Rect");
        if (id.isEmpty() || ids.contains(id) || !number.isInt() || number.getInt() <= 0 || !rect.isArray() || rect.arrayGetLength() != 4) {
            return fail(error, "Invalid View identity, number or rectangle.");
        }
        ids.insert(id);
        std::array<double, 4> values;
        for (int j = 0; j < 4; ++j) {
            const Object value = rect.arrayGet(j);
            if (!value.isNum()) {
                return fail(error, "View rectangle coordinates must be finite numbers.");
            }
            values[j] = value.getNum();
        }
        if (!validRectangle(values)) {
            return fail(error, "View rectangle must have finite positive dimensions.");
        }
        const auto normalized = mapRectangle(values, [&](double x, double y) { return coordinates.toNormalized(x, y); });
        if (!validRectangle(normalized)) {
            return fail(error, "Could not transform the stored View rectangle.");
        }
        // A later CropBox edit may place a previously valid view outside [0,1].
        // Keep its full definition; the editing overlay may clip its display.
        views->append({id, number.getInt(), Okular::NormalizedRect(normalized[0], normalized[1], normalized[2], normalized[3])});
    }
    return true;
}

void replaceMetadata(PDFDoc *document, Page *page, Object &&metadata)
{
    // Shallow-copy the live page object so both the cached Page and XRef see the
    // new field. The metadata dictionary/arrays are freshly owned: never mutate
    // nested objects shared with another page after a permissive PDF import.
    Object pageObject = page->getPageObj().copy();
    pageObject.dictSet(metadataKey, std::move(metadata));
    document->getXRef()->setModifiedObject(&pageObject, page->getRef());
}
}

QList<Okular::ReadingView> read(PDFDoc *document, int index, QString *error)
{
    Page *page = getPage(document, index, error);
    if (!page) {
        return {};
    }
    const Object metadata = page->getPageObj().dictLookup(metadataKey);
    if (!schema(metadata, error)) {
        return {};
    }
    const Coordinates coordinates(page);
    if (!coordinates.valid()) {
        fail(error, "Invalid PDF page geometry for View definitions.");
        return {};
    }
    QList<Okular::ReadingView> views;
    if (!parseItems(metadata, coordinates, &views, error)) {
        return {};
    }
    return views;
}

QString pageToken(PDFDoc *document, int index)
{
    Page *page = getPage(document, index, nullptr);
    if (!page) {
        return {};
    }
    const Object metadata = page->getPageObj().dictLookup(metadataKey);
    return schema(metadata, nullptr) && metadata.isDict() ? uuidValue(metadata.dictLookup("PageId")) : QString();
}

int pageForToken(PDFDoc *document, const QString &token)
{
    if (!document || token.isEmpty()) {
        return -1;
    }
    int found = -1;
    for (int i = 0; i < document->getNumPages(); ++i) {
        if (pageToken(document, i) == token) {
            if (found != -1) {
                return -1; // Foreign duplicate identities must never redirect undo.
            }
            found = i;
        }
    }
    return found;
}

bool write(PDFDoc *document, int index, const QList<Okular::ReadingView> &views, QString *error)
{
    Page *page = getPage(document, index, error);
    if (!page || views.size() > maxViews) {
        return fail(error, "Invalid page or too many View definitions.");
    }
    const Object old = page->getPageObj().dictLookup(metadataKey);
    if (!schema(old, error)) {
        return false;
    }
    const Coordinates coordinates(page);
    QList<Okular::ReadingView> previous;
    if (!coordinates.valid() || !parseItems(old, coordinates, &previous, error)) {
        return fail(error, "Invalid page geometry or stored View data; no changes were made.");
    }
    QString token = old.isDict() ? uuidValue(old.dictLookup("PageId")) : QString();
    if (!token.isEmpty() && pageForToken(document, token) != index) {
        return fail(error, "Ambiguous View page identity; no changes were made.");
    }
    QSet<QString> ids;
    auto items = std::make_unique<Array>(document->getXRef());
    for (const auto &view : views) {
        const QUuid uuid(view.id);
        const QString id = uuid.toString(QUuid::WithoutBraces);
        const auto &r = view.rectangle;
        const std::array<double, 4> normalized = {r.left, r.top, r.right, r.bottom};
        if (uuid.isNull() || ids.contains(id) || view.number <= 0 || !validRectangle(normalized)) {
            return fail(error, "View IDs must be unique UUIDs, numbers positive, and rectangles finite with positive dimensions.");
        }
        ids.insert(id);
        const auto values = mapRectangle(normalized, [&](double x, double y) { return coordinates.toPdf(x, y); });
        if (!validRectangle(values)) {
            return fail(error, "Could not transform the View rectangle into PDF coordinates.");
        }
        auto *rect = new Array(document->getXRef());
        for (double value : values) {
            rect->add(Object(value));
        }
        auto *entry = new Dict(document->getXRef());
        entry->set("Id", uuidObject(id));
        entry->set("Number", Object(view.number));
        entry->set("Rect", Object(rect));
        items->add(Object(entry));
    }
    if (token.isEmpty()) {
        token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    // Copy-on-write even when malformed foreign PDFs share the same metadata Ref.
    auto *metadata = old.isDict() ? old.getDict()->copy(document->getXRef()) : new Dict(document->getXRef());
    metadata->set("Version", Object(1));
    metadata->set("PageId", uuidObject(token));
    metadata->set("Items", Object(items.release()));
    replaceMetadata(document, page, Object(metadata));
    return true;
}

void renewCopiedPageToken(PDFDoc *document, int index)
{
    Page *page = getPage(document, index, nullptr);
    if (!page) {
        return;
    }
    const Object old = page->getPageObj().dictLookup(metadataKey);
    if (!old.isDict() || !schema(old, nullptr)) {
        return;
    }
    auto *metadata = old.getDict()->copy(document->getXRef());
    metadata->set("PageId", uuidObject(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    replaceMetadata(document, page, Object(metadata));
}
}
