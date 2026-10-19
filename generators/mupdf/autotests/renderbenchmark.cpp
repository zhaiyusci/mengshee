// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../generator_mupdf.h"
#include <core/page.h>
#include <core/annotations.h>
#include <future>
#include <thread>
#include <vector>
#include <QApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif
static QJsonObject memory()
{
    QJsonObject result;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof(counters))) {
        result[QStringLiteral("working_set_mib")] = double(counters.WorkingSetSize) / 1048576;
        result[QStringLiteral("peak_working_set_mib")] = double(counters.PeakWorkingSetSize) / 1048576;
        result[QStringLiteral("private_mib")] = double(counters.PrivateUsage) / 1048576;
    }
#endif
    return result;
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (argc != 4 && argc != 5) return 2; // PDF, mode (full/tile/zoom/uncached), full-page pixel width
    const QString mode = QString::fromLocal8Bit(argv[2]);
    const int base = QString::fromLocal8Bit(argv[3]).toInt();
    if (base < 1024 || (mode != QStringLiteral("full") && mode != QStringLiteral("tile") && mode != QStringLiteral("zoom") && mode != QStringLiteral("uncached") && mode != QStringLiteral("annot-uncached") && mode != QStringLiteral("cancel"))) return 2;
    MuPDFGenerator backend(nullptr, {});
    QList<Okular::Page *> pages;
    QElapsedTimer timer;
    timer.start();
    if (backend.loadDocumentWithPassword(QString::fromLocal8Bit(argv[1]), pages, {}) != Okular::Document::OpenSuccess) return 3;
    QJsonObject output{{QStringLiteral("mode"), mode}, {QStringLiteral("base_pixels"), base}, {QStringLiteral("open_ms"), timer.nsecsElapsed()/1e6}, {QStringLiteral("after_open"), memory()}};
    std::vector<std::unique_ptr<Okular::TextAnnotation>> annotations;
    const int annotationCount = argc == 5 ? QString::fromLocal8Bit(argv[4]).toInt() : 0;
    if (annotationCount < 0 || annotationCount > 4096) return 2;
    for (int i = 0; i < annotationCount; ++i) {
        auto note = std::make_unique<Okular::TextAnnotation>();
        note->setTextType(Okular::TextAnnotation::InPlace);
        note->setContents(QStringLiteral("Cached annotation %1 - ABC 012345").arg(i));
        const double x = (i % 32) / 32.0, y = (i / 32) / 32.0;
        note->setBoundingRectangle({x, y, x + .03, y + .025});
        backend.annotationProxy()->notifyAddition(note.get(), 0);
        annotations.push_back(std::move(note));
    }
    output[QStringLiteral("annotation_count")] = annotationCount;
    if (mode == QStringLiteral("cancel")) {
        Okular::PixmapRequest request(nullptr, 0, base, base, 1, 0, Okular::PixmapRequest::NoFeature);
        auto worker = std::async(std::launch::async, [&] { return backend.image(&request); });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        timer.restart();
        request.abortRender();
        const auto cancelled = worker.get();
        output[QStringLiteral("cancel_latency_ms")] = timer.nsecsElapsed()/1e6;
        output[QStringLiteral("cancelled_image_null")] = cancelled.isNull();
        // A cancelled recording must not be reused as a complete Display List.
        const auto recovered = backend.renderToImage(0, 4, false);
        MuPDFGenerator fresh(nullptr, {});
        QList<Okular::Page *> freshPages;
        if (fresh.loadDocumentWithPassword(QString::fromLocal8Bit(argv[1]), freshPages, {}) != Okular::Document::OpenSuccess) return 3;
        output[QStringLiteral("recovered_matches_fresh")] = recovered == fresh.renderToImage(0, 4, false);
        qDeleteAll(freshPages);
        std::puts(QJsonDocument(output).toJson(QJsonDocument::Compact).constData());
        qDeleteAll(pages);
        return 0;
    }
    QJsonArray frames;
    for (int i = 0; i < 13; ++i) {
        if (mode == QStringLiteral("uncached") && i > 0) {
            qDeleteAll(pages);
            pages.clear();
            if (backend.loadDocumentWithPassword(QString::fromLocal8Bit(argv[1]), pages, {}) != Okular::Document::OpenSuccess) return 3;
        }
        if (mode == QStringLiteral("annot-uncached") && !annotations.empty() && i > 0)
            backend.annotationProxy()->notifyModification(annotations.front().get(), 0, false);
        int width = base;
        if (mode == QStringLiteral("zoom")) width = base * (1 << (i % 4));
        const int height = qRound(width * pages[0]->height()/pages[0]->width());
        Okular::PixmapRequest request(nullptr, 0, width, height, 1, 0, Okular::PixmapRequest::NoFeature);
        if (mode != QStringLiteral("full")) {
            const int x = qRound((width - 1024) * ((i * 7) % 13) / 13.0);
            const int y = qRound((height - 768) * ((i * 5) % 13) / 13.0);
            request.setTile(true);
            request.setNormalizedRect({double(x)/width, double(y)/height, double(x+1023)/width, double(y+767)/height});
        }
        timer.restart();
        const auto image = backend.image(&request);
        const double elapsed = timer.nsecsElapsed()/1e6;
        if (image.isNull()) return 4;
        frames.append(QJsonObject{{QStringLiteral("frame"), i}, {QStringLiteral("page_width"), width}, {QStringLiteral("ms"), elapsed},
                                  {QStringLiteral("image_bytes"), double(image.sizeInBytes())}, {QStringLiteral("memory"), memory()}});
    }
    output[QStringLiteral("frames")] = frames;
    if (!annotations.empty()) {
        const auto plain = backend.renderToImage(0, 12, false);
        const auto withNotes = backend.renderToImage(0, 12, true);
        for (const auto &note : annotations)
            backend.annotationProxy()->notifyRemoval(note.get(), 0);
        output[QStringLiteral("annotations_visible")] = plain != withNotes;
        output[QStringLiteral("removal_matches_plain")] = backend.renderToImage(0, 12, true) == plain;
    }
    const auto json = QJsonDocument(output).toJson(QJsonDocument::Compact);
    std::puts(json.constData());
    qDeleteAll(pages);
    return 0;
}
