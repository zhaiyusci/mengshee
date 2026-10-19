// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QImage>
#include <QSize>
#include <QString>
#include <functional>

namespace Okular {
struct PrintPreviewImage {
    QImage image;
    QString error;
};
// Owns an immutable document snapshot. Safe to invoke on a worker after the
// source document closes. No widgets or printer objects may be accessed here.
using PrintPreviewRenderer = std::function<PrintPreviewImage(int page, QSize maximumPixels)>;
class PrintPreviewInterface {
public:
    virtual ~PrintPreviewInterface() = default;
    virtual PrintPreviewRenderer createPrintPreviewRenderer(QString *error) = 0;
};
}
