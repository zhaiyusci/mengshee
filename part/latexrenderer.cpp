/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "latexrenderer.h"
#include "latexrenderguards.h"
#include <KLocalizedString>
#include <QDir>
#include <QFile>
#include <QTemporaryFile>
#include <exception>
#include <utility>

namespace GuiUtils
{
LatexRenderer::LatexRenderer()
    : LatexRenderer(createDefaultLatexBackend())
{
}

LatexRenderer::LatexRenderer(std::unique_ptr<LatexBackend> backend)
    : m_backend(std::move(backend))
{
}

LatexRenderer::~LatexRenderer()
{
    for (const QString &file : std::as_const(m_fileList)) {
        QFile::remove(file);
    }
}

QString LatexRenderer::lastBackendName() const
{
    return m_lastBackendName;
}

LatexRenderWarning LatexRenderer::lastWarning() const
{
    return m_lastWarning;
}

QString LatexRenderer::compactErrorMessage(const QString &latexOutput)
{
    const QStringList lines = latexOutput.split(QLatin1Char('\n'));
    QString message;
    QString context;
    bool foundErrorLine = false;

    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        if (!foundErrorLine && trimmed.startsWith(QLatin1Char('!'))) {
            message = trimmed.mid(1).trimmed();
            foundErrorLine = true;
            continue;
        }
        if (foundErrorLine && trimmed.startsWith(QLatin1String("l."))) {
            context = trimmed;
            break;
        }
        if (message.isEmpty()) {
            message = trimmed;
        }
    }

    if (message.isEmpty()) {
        message = i18n("Unknown LaTeX error.");
    }
    if (!context.isEmpty()) {
        message = i18nc("LaTeX error with compiler context", "%1 (%2)", message, context);
    }

    message = message.simplified();
    constexpr int maxLength = 180;
    if (message.size() > maxLength) {
        message = message.left(maxLength - 3) + QStringLiteral("...");
    }
    return message;
}

LatexRenderer::Error LatexRenderer::renderLatexToPdf(const QString &latexFormula, const QColor &textColor, QString &pdfFileName, QString &latexOutput, double maxWidth, double fontSize)
{
    m_lastBackendName.clear();
    m_lastWarning = {};

    pdfFileName.clear();
    latexOutput.clear();
    // Inspect a copy only for emptiness: authored whitespace and terminal
    // comments are part of TeX syntax and must survive rendering unchanged.
    using LatexRenderGuards::InputError;
    const InputError inputError = LatexRenderGuards::validateInput(latexFormula, maxWidth, fontSize);
    switch (inputError) {
    case InputError::None:
        break;
    case InputError::EmptySource:
        latexOutput = i18n("LaTeX source is empty.");
        break;
    case InputError::SourceTooLarge:
        latexOutput = i18n("LaTeX source exceeds the limit of 250,000 characters.");
        break;
    case InputError::EmbeddedNull:
        latexOutput = i18n("LaTeX source contains a null character.");
        break;
    case InputError::InvalidWidth:
        latexOutput = i18n("LaTeX note width must be finite and between 0 and 50,000 points (0 uses the default).");
        break;
    case InputError::InvalidFontSize:
        latexOutput = i18n("LaTeX font size must be finite and between 1 and 200 points (0 uses the default).");
        break;
    }
    if (inputError != InputError::None) {
        return LatexFailed;
    }
    if (!m_backend) {
        latexOutput = i18n("No LaTeX rendering backend is configured.");
        return LatexFailed;
    }
    m_lastBackendName = m_backend->id();
    LatexBackendResult result;
    try {
        result = m_backend->render({latexFormula, textColor, maxWidth, fontSize});
    } catch (const std::exception &exception) {
        latexOutput = i18n("LaTeX rendering failed unexpectedly: %1", QString::fromLocal8Bit(exception.what()));
        return LatexFailed;
    } catch (...) {
        latexOutput = i18n("LaTeX rendering failed unexpectedly.");
        return LatexFailed;
    }
    latexOutput = result.diagnostic;
    if (!result.success) {
        if (latexOutput.isEmpty())
            latexOutput = i18n("The LaTeX backend did not produce a PDF.");
        return LatexFailed;
    }
    if (result.pdf.isEmpty() || result.pdf.size() > LatexRenderGuards::maxPdfBytes || !result.pdf.startsWith("%PDF-")) {
        latexOutput = i18n("The LaTeX backend returned an invalid or oversized PDF (limit: 64 MiB).");
        return LatexFailed;
    }
    QTemporaryFile file(QDir::temp().filePath(QStringLiteral("mengshee-latex-XXXXXX.pdf")));
    if (!file.open() || file.write(result.pdf) != result.pdf.size() || !file.flush()) {
        latexOutput = i18n("Could not save the LaTeX rendering result.");
        return LatexFailed;
    }
    file.setAutoRemove(false);
    pdfFileName = file.fileName();
    m_fileList << pdfFileName;
    m_lastWarning = result.warning;
    return NoError;
}

void LatexRenderer::prewarm()
{
    if (auto backend = createDefaultLatexBackend())
        backend->prewarm();
}

void LatexRenderer::restart()
{
    if (auto backend = createDefaultLatexBackend())
        backend->restart();
}
}
