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

namespace Okular
{
class Annotation;
class Document;
class Page;
class StampAnnotation;
class TextAnnotation;
}

namespace LatexNoteUtils
{
struct RenderResult {
    bool ok = false;
    QString pdfFileName;
    QSizeF pdfSizePoints;
    QString errorMessage;
    QString warningMessage;
    GuiUtils::LatexRenderWarning warning;
};

Okular::TextAnnotation *annotationAsLatexTextAnnotation(Okular::Annotation *annotation);
const Okular::TextAnnotation *annotationAsLatexTextAnnotation(const Okular::Annotation *annotation);
Okular::StampAnnotation *annotationAsLatexStampAnnotation(Okular::Annotation *annotation);
const Okular::StampAnnotation *annotationAsLatexStampAnnotation(const Okular::Annotation *annotation);
bool annotationIsLatex(Okular::Annotation *annotation);
bool annotationIsLatex(const Okular::Annotation *annotation);

QColor colorForLatexAnnotation(const Okular::Annotation *annotation);
QString defaultLatexAppearancePdfFileName();

double pageWidthInPoints(const Okular::Page *page);
double pageHeightInPoints(const Okular::Page *page);
double rectWidthInPoints(const Okular::NormalizedRect &rect, const Okular::Page *page);
double rectHeightInPoints(const Okular::NormalizedRect &rect, const Okular::Page *page);
double annotationWidthInPoints(const Okular::Annotation *annotation, const Okular::Page *page);
double layoutWidthForLatexTextVisibleWidth(double visibleWidthPoints, double padding);
double layoutWidthForLatexFrame(const Okular::NormalizedRect &frame, const Okular::Page *page, double padding);
double paddingForLatexAnnotation(const Okular::Annotation *annotation);
double fontSizeForLatexAnnotation(const Okular::Annotation *annotation);
double layoutWidthForLatexTextAnnotation(const Okular::TextAnnotation *annotation, const Okular::Page *page);
QSizeF visualSizeForLatexTextAnnotation(const QSizeF &contentPdfSizePoints, double layoutWidthPoints, double padding);

RenderResult renderAppearancePdf(const QString &latexInput, const QColor &textColor, double layoutWidthPoints);
RenderResult renderAppearancePdf(const QString &latexInput, const QColor &textColor, double layoutWidthPoints, bool callout);
RenderResult renderAppearancePdf(const QString &latexInput, const QColor &textColor, double layoutWidthPoints, bool callout, double fontSizePoints);
bool updateLatexTextAnnotationAppearance(QWidget *parent,
                                         Okular::Document *document,
                                         int pageNumber,
                                         Okular::TextAnnotation *textAnnotation,
                                         const QColor &textColor,
                                         const QColor &fillColor,
                                         const QColor &borderColor,
                                         double layoutWidthPoints,
                                         bool boxed,
                                         bool prepareModification = true);
bool updateLatexStampAnnotationAppearance(QWidget *parent,
                                          Okular::Document *document,
                                          int pageNumber,
                                          Okular::StampAnnotation *stampAnnotation,
                                          const QColor &textColor,
                                          const QColor &fillColor,
                                          const QColor &borderColor,
                                          double layoutWidthPoints,
                                          bool boxed,
                                          bool prepareModification = true);
void updateLatexTextAnnotationAppearanceAsync(QWidget *parent,
                                              Okular::Document *document,
                                              int pageNumber,
                                              const QString &annotationUniqueName,
                                              const QString &latexInput,
                                              const QColor &textColor,
                                              const QColor &fillColor,
                                              const QColor &borderColor,
                                              double layoutWidthPoints,
                                              bool boxed);
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
