/*
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef OKULAR_LATEXNOTEUTILS_H
#define OKULAR_LATEXNOTEUTILS_H

#include <QColor>
#include <QPoint>
#include <QSizeF>
#include <QString>

class QWidget;

#include "core/area.h"
#include "latexrenderer.h"
#include "okularpart_export.h"

namespace Okular
{
class Annotation;
class Document;
class Page;
class StampAnnotation;
}

namespace LatexNoteUtils
{
struct RenderResult {
    bool ok = false; // A usable AP, including an error AP when errorMessage is nonempty.
    QString pdfFileName;
    QSizeF pdfSizePoints;
    QString errorMessage;
    QString warningMessage;
    GuiUtils::LatexRenderWarning warning;
};

Okular::StampAnnotation *annotationAsLatexStampAnnotation(Okular::Annotation *annotation);
const Okular::StampAnnotation *annotationAsLatexStampAnnotation(const Okular::Annotation *annotation);
bool annotationIsLatex(const Okular::Annotation *annotation);

QColor colorForLatexAnnotation(const Okular::Annotation *annotation);
QColor fillColorForLatexAnnotation(const Okular::Annotation *annotation);
QColor borderColorForLatexAnnotation(const Okular::Annotation *annotation);

double pageWidthInPoints(const Okular::Page *page);
double pageHeightInPoints(const Okular::Page *page);
OKULARPART_EXPORT double annotationWidthInPoints(const Okular::Annotation *annotation, const Okular::Page *page);
double paddingForLatexAnnotation(const Okular::Annotation *annotation);
double fontSizeForLatexAnnotation(const Okular::Annotation *annotation);

double layoutWidthForLatexAnnotation(const Okular::Annotation *annotation, const Okular::Page *page);
Okular::NormalizedRect frameForLatexContent(const Okular::Annotation *annotation,
                                          const Okular::Page *page,
                                          const Okular::NormalizedRect &anchorRect,
                                          const QSizeF &contentPdfSizePoints,
                                          double layoutWidthPoints);
RenderResult renderAppearancePdf(const QString &latexInput, const QColor &textColor, double layoutWidthPoints);
RenderResult renderAppearancePdf(const QString &latexInput, const QColor &textColor, double layoutWidthPoints, bool callout);
OKULARPART_EXPORT RenderResult renderAppearancePdf(const QString &latexInput, const QColor &textColor, double layoutWidthPoints, bool callout, double fontSizePoints);
OKULARPART_EXPORT bool updateLatexStampAnnotationAppearance(QWidget *parent,
                                                             Okular::Document *document,
                                                             int pageNumber,
                                                             Okular::StampAnnotation *stampAnnotation,
                                                             const QColor &textColor,
                                                             const QColor &fillColor,
                                                             const QColor &borderColor,
                                                             double layoutWidthPoints,
                                                             bool boxed,
                                                             bool prepareModification = true);
void updateLatexStampAnnotationAppearanceAsync(QWidget *parent,
                                               Okular::Document *document,
                                               int pageNumber,
                                               const QString &annotationUniqueName,
                                               const QString &latexInput,
                                               const QColor &textColor,
                                               const QColor &fillColor,
                                               const QColor &borderColor,
                                               double layoutWidthPoints,
                                               bool boxed);
QString warningText(const GuiUtils::LatexRenderWarning &warning);
void showRenderWarning(QWidget *parent, const QString &warningMessage);
void showRenderWarning(QWidget *parent, const GuiUtils::LatexRenderWarning &warning);
void showRenderWarning(QWidget *parent, const QString &warningMessage, const QPoint &globalPosition);
void showRenderWarning(QWidget *parent, const GuiUtils::LatexRenderWarning &warning, const QPoint &globalPosition);
}

#endif
