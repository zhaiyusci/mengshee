/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef OKULAR_NUMBEREDCALLOUTNUMBERINGINTERFACE_H
#define OKULAR_NUMBEREDCALLOUTNUMBERINGINTERFACE_H

#include "okularcore_export.h"
#include <QString>

namespace Okular
{
// Optional backend capability for document-wide numbered-callout metadata.
class OKULARCORE_EXPORT NumberedCalloutNumberingInterface
{
public:
    virtual ~NumberedCalloutNumberingInterface() = default;
    virtual bool canEditNumberedCalloutNumbering() const = 0;
    // Non-mutating preflight of an expanded display label using AP font shaping.
    virtual bool validateNumberedCalloutLabel(const QString &label, QString *errorText) const = 0;
    // Return the original stored JSON, or an empty string when absent.
    virtual QString numberedCalloutNumberingJson() const = 0;
    // Empty JSON removes the metadata, including when undoing its first write.
    virtual bool setNumberedCalloutNumberingJson(const QString &json, QString *errorText) = 0;
};
}

#endif
