/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef MENGSHEE_POPPLERCOREBRIDGE_H
#define MENGSHEE_POPPLERCOREBRIDGE_H

#include "external/poppler/qt6/src/poppler-private.h"
#include <PDFDoc.h>
#include <cstring>

// App-private bridge for the pinned Poppler Qt6 wrapper. This consolidates the
// bridge already used by the outline/page editor; no Annotation layout is read.
// Document has one DocumentData* member in that wrapper. Read its representation
// rather than aliasing a Document object as an unrelated shim struct.
inline PDFDoc *popplerCoreDocument(Poppler::Document *document)
{
    static_assert(sizeof(Poppler::Document) == sizeof(Poppler::DocumentData *), "Recheck the pinned Poppler Document bridge after a wrapper layout change");
    Poppler::DocumentData *data = nullptr;
    if (document) {
        std::memcpy(&data, document, sizeof(data));
    }
    return data ? data->doc.get() : nullptr;
}

#endif
