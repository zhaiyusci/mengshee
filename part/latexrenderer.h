/*
    SPDX-FileCopyrightText: 2004 Duncan Mac-Vicar Prett <duncan@kde.org>
    SPDX-FileCopyrightText: 2004-2005 Olivier Goffart <ogoffart@kde.org>
    SPDX-FileCopyrightText: 2011 Niels Ole Salscheider
    <niels_ole@salscheider-online.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef LATEXRENDERER_H
#define LATEXRENDERER_H

#include "latexbackend.h"
#include <QString>
#include <QStringList>

class QString;
class QColor;

namespace GuiUtils
{
class LatexRenderer
{
public:
    enum Error { NoError, LatexFailed };

    LatexRenderer();
    explicit LatexRenderer(std::unique_ptr<LatexBackend> backend);
    ~LatexRenderer();

    LatexRenderer(const LatexRenderer &) = delete;
    LatexRenderer &operator=(const LatexRenderer &) = delete;

    Error renderLatexToPdf(const QString &latexFormula, const QColor &textColor, QString &pdfFileName, QString &latexOutput, double maxWidth = 0.0, double fontSize = 0.0);
    QString lastBackendName() const;
    LatexRenderWarning lastWarning() const;
    static QString compactErrorMessage(const QString &latexOutput);
    static void prewarm();
    static void restart();

private:
    std::unique_ptr<LatexBackend> m_backend;
    QStringList m_fileList;
    QString m_lastBackendName;
    LatexRenderWarning m_lastWarning;
};

}

#endif // LATEXRENDERER_H
