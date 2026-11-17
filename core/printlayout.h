// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "printoptionswidget.h"

#include <QList>
#include <QPageLayout>
#include <QWidget>
#include <algorithm>

namespace Okular
{

/**
 * Plain print-layout request shared by the print dialog and the PDF engines.
 *
 * This is deliberately independent of QPrinter and of a renderer: the caller
 * can first build the sheet/page grid, then render only the pixels each cell
 * needs.
 */
struct PrintLayoutOptions {
    int mode = PrintLayout::Size;
    int pagesPerSheet = 2;
    int pageOrder = PrintLayout::Horizontal;
    bool pageBorder = false;
    qreal posterScale = 1.0;
    qreal posterOverlapMm = 0.0;
    bool posterCutMarks = false;
    int bookletSubset = PrintLayout::BothSides;
    int bookletBinding = PrintLayout::BindLeft;
};

/** One physical sheet, in logical page numbers. -1 denotes a blank page. */
struct PrintSheet {
    QList<int> pages;
    int posterPage = -1;
    int tileColumn = 0;
    int tileRow = 0;
    int tileColumns = 1;
    int tileRows = 1;
};

inline int printLayoutColumns(int pagesPerSheet)
{
    return pagesPerSheet >= 16 ? 4 : pagesPerSheet >= 9 ? 3 : pagesPerSheet >= 6 ? 3 : pagesPerSheet >= 2 ? 2 : 1;
}

inline int printLayoutRows(int pagesPerSheet)
{
    return qMax(1, (pagesPerSheet + printLayoutColumns(pagesPerSheet) - 1) / printLayoutColumns(pagesPerSheet));
}

/**
 * Build the page sequence for a layout. Poster tiles are expanded by the
 * engine after it knows the physical page size; here a poster page is one
 * sheet with posterPage set.
 */
inline QList<PrintSheet> buildPrintSheets(const QList<int> &pages, const PrintLayoutOptions &options)
{
    QList<PrintSheet> sheets;
    if (options.mode == PrintLayout::Multiple) {
        const int perSheet = qMax(1, options.pagesPerSheet);
        for (int first = 0; first < pages.size(); first += perSheet) {
            PrintSheet sheet;
            sheet.pages = pages.mid(first, qMin(perSheet, int(pages.size()) - first));
            sheets.append(sheet);
        }
    } else if (options.mode == PrintLayout::Booklet) {
        QList<int> booklet = pages;
        while (booklet.size() % 4 != 0)
            booklet.append(-1);
        for (int i = 0; i < booklet.size() / 4; ++i) {
            PrintSheet front;
            front.pages = {booklet.at(booklet.size() - 1 - 2 * i), booklet.at(2 * i)};
            PrintSheet back;
            back.pages = {booklet.at(2 * i + 1), booklet.at(booklet.size() - 2 - 2 * i)};
            if (options.bookletBinding == PrintLayout::BindRight) {
                std::reverse(front.pages.begin(), front.pages.end());
                std::reverse(back.pages.begin(), back.pages.end());
            }
            if (options.bookletSubset != PrintLayout::BackSides)
                sheets.append(front);
            if (options.bookletSubset != PrintLayout::FrontSides)
                sheets.append(back);
        }
    } else {
        for (int page : pages) {
            PrintSheet sheet;
            if (options.mode == PrintLayout::Poster)
                sheet.posterPage = page;
            else
                sheet.pages = {page};
            sheets.append(sheet);
        }
    }
    return sheets;
}

/** Orientation of a sheet, independent of the preview image. */
inline QPageLayout::Orientation printLayoutOrientation(const PrintLayoutOptions &options, QPageLayout::Orientation documentOrientation)
{
    if (options.mode == PrintLayout::Multiple) {
        const int columns = printLayoutColumns(options.pagesPerSheet);
        const int rows = printLayoutRows(options.pagesPerSheet);
        if (columns > rows)
            return QPageLayout::Landscape;
        if (rows > columns)
            return QPageLayout::Portrait;
        return documentOrientation;
    }
    if (options.mode == PrintLayout::Booklet)
        return QPageLayout::Landscape;
    return documentOrientation;
}

inline PrintLayoutOptions printLayoutOptionsFromWidget(const QWidget *widget)
{
    PrintLayoutOptions options;
    if (!widget)
        return options;
    options.mode = widget->property(PrintLayout::modeProperty).toInt();
    options.pagesPerSheet = qMax(1, widget->property(PrintLayout::pagesPerSheetProperty).toInt());
    options.pageOrder = widget->property(PrintLayout::pageOrderProperty).toInt();
    options.pageBorder = widget->property(PrintLayout::pageBorderProperty).toBool();
    options.posterScale = widget->property(PrintLayout::posterScaleProperty).toDouble() / 100.0;
    if (options.posterScale <= 0)
        options.posterScale = 1.0;
    options.posterOverlapMm = widget->property(PrintLayout::posterOverlapProperty).toDouble();
    options.posterCutMarks = widget->property(PrintLayout::posterCutMarksProperty).toBool();
    options.bookletSubset = widget->property(PrintLayout::bookletSubsetProperty).toInt();
    options.bookletBinding = widget->property(PrintLayout::bookletBindingProperty).toInt();
    return options;
}

} // namespace Okular
