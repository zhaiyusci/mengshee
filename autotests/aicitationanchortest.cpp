// SPDX-License-Identifier: GPL-2.0-or-later
#include "part/aisearch.h"
#include "core/document.h"
#include "settings_core.h"
#include <QApplication>
#include <QDir>
#include <QMimeDatabase>
#include <QPainter>
#include <QPdfWriter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QDebug>
#include <iostream>

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    Okular::SettingsCore::instance(QStringLiteral("ai-citation-anchor-test"));
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/anchor-XXXXXX"));
    if (!dir.isValid()) { std::cerr << "temporary directory: " << dir.errorString().toStdString() << std::endl; return 1; }
    const QString file = dir.filePath(QStringLiteral("page.pdf"));
    {
        QPdfWriter writer(file);
        writer.setResolution(72);
        writer.setPageSize(QPageSize(QSizeF(600, 800), QPageSize::Point));
        writer.setPageMargins(QMarginsF());
        QPainter painter(&writer);
        painter.drawText(120, 130, QStringLiteral("Page 2"));
        painter.drawText(295, 710, QStringLiteral("2"));
        writer.newPage();
        painter.drawText(80, 180, QStringLiteral("The counterpoise correction is"));
        painter.drawText(80, 200, QStringLiteral("evaluated for each geometry."));
        painter.drawText(295, 710, QStringLiteral("3"));
    }
    Okular::Document document(nullptr);
    if (document.openDocument(file, QUrl::fromLocalFile(file), QMimeDatabase().mimeTypeForFile(file)) != Okular::Document::OpenSuccess) { std::cerr << "document: " << document.openError().toStdString() << std::endl; return 1; }
    AiSearch::Index index;
    if (!index.build(&document)) { std::cerr << "index empty" << std::endl; return 1; }
    bool failed = false;
    const auto check = [&](const QString &quote, int page, bool shouldLocate) {
        QString located;
        const auto rectangles = index.locate(page, quote, &located);
        double bottom = 0;
        for (const auto &rect : rectangles) bottom = qMax(bottom, rect.bottom);
        const bool valid = shouldLocate ? (!rectangles.isEmpty() && bottom < .5) : rectangles.isEmpty();
        std::cerr << (valid ? "PASS " : "FAIL ") << quote.toStdString() << " matched=" << located.toStdString() << " rectangles=" << rectangles.size() << " bottom=" << bottom << std::endl;
        failed |= !valid;
    };
    check(QStringLiteral("Page 2"), 0, true);
    check(QStringLiteral("Page 2 describes this result"), 0, false);
    check(QStringLiteral("Page 2 2"), 0, false);
    check(QStringLiteral("The counterpoise correction is evaluated for each geometry."), 1, true);
    check(QStringLiteral("The counterpoise correction is entirely different"), 1, false);
    return failed ? 1 : 0;
}
