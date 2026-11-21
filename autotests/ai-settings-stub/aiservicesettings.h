// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
// Controlled settings seam. No real configuration or credentials are read.
namespace Repro { struct Configuration { QString base, model, key, effort; }; inline Configuration configuration; }
namespace Okular {
class AiServiceSettings {
    Repro::Configuration value;
public:
    void load() { value = Repro::configuration; }
    QString baseUrl() const { return value.base; }
    QString modelId() const { return value.model; }
    QString apiKey() const { return value.key; }
    QString reasoningEffort() const { return value.effort; }
};
}
