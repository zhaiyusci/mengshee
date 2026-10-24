// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <mupdf/fitz.h>
class QPainter;
class QRegion;
class QRect;

// Coordinates received by the device are printer pixels. Unsupported composite
// regions are returned for a final, native MuPDF replay at printer resolution.
fz_device *newVectorPrintDevice(fz_context *, QPainter &, const QRect &, QRegion &);
