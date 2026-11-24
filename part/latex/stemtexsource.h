/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MENGSHEE_STEMTEXSOURCE_H
#define MENGSHEE_STEMTEXSOURCE_H
#include <QString>
class QColor;
namespace GuiUtils::StemTeXSource
{
// Executed in the worker before the unchanged source reaches its layout box.
QString requestSetup(const QColor &textColor);

// Drops empty trailing lines and explicit trailing line-break commands.
// The annotation keeps the author's source; only the layout copy is trimmed.
QString sourceForLayout(const QString &source);
}
#endif
