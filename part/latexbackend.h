/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MENGSHEE_LATEXBACKEND_H
#define MENGSHEE_LATEXBACKEND_H
#include <QByteArray>
#include <QColor>
#include <QString>
#include <memory>

namespace GuiUtils
{
enum class LatexRenderWarningType { None, CompileError, BackendLimitation, CompileFallback };

struct LatexRenderWarning {
    LatexRenderWarningType type = LatexRenderWarningType::None;
    QString message;
    double severity = 0.0;
    bool isValid() const
    {
        return type != LatexRenderWarningType::None && !message.isEmpty();
    }
};

// A LaTeX fragment, not a backend template or a complete document. The
// facade preserves source exactly; backend-specific preparation stays private.
struct LatexRenderRequest {
    QString source;
    QColor textColor;
    double widthPoints = 0.0; // PDF points (1/72 inch); zero selects backend default.
    double fontSizePoints = 0.0; // TeX typographic points (1/72.27 inch); zero selects default.
};

struct LatexBackendResult {
    bool success = false; // A usable PDF may still carry a compile warning.
    QByteArray pdf; // Owned bytes: no SDK path, allocation, or process lifetime leaks out.
    QString diagnostic;
    LatexRenderWarning warning;
};

// Each renderer owns its backend. Implementations may share a synchronized
// service internally; render() is called synchronously by the existing job worker.
class LatexBackend
{
public:
    virtual ~LatexBackend() = default;
    virtual QString id() const = 0;
    virtual LatexBackendResult render(const LatexRenderRequest &request) = 0;
    virtual void prewarm() {}
    virtual void restart() {}
};

// The application composition point. New backends do not change the note layer
// or the rendering facade; explicit backend injection is also supported.
std::unique_ptr<LatexBackend> createDefaultLatexBackend();
}
#endif
