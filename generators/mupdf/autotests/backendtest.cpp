// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../../../part/closedtemporarypdffile.h"
#include "../../poppler/pdfpagebounds.h"
#include "../generator_mupdf.h"
#include <QSignalSpy>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <future>
#include <core/annotations.h>
#include <core/form.h>
#include <core/latexnotegeometry.h>
#include <core/page.h>

class BackendTest : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_directory;
    QString sample() const
    {
        return m_directory.filePath(QStringLiteral("source.pdf"));
    }
private Q_SLOTS:
    void appearanceBounds()
    {
        const auto output = m_directory.filePath(QStringLiteral("expanded.pdf"));
        const auto result = PdfPageBounds::expandAppearance(sample().toUtf8().toStdString(), output.toUtf8().toStdString());
        QVERIFY2(result.ok, result.message.c_str());
        QVERIFY(result.hasInk);
        MuPDFGenerator original(nullptr, {}), expanded(nullptr, {});
        QList<Okular::Page *> before, after;
        QCOMPARE(original.loadDocumentWithPassword(sample(), before, {}), Okular::Document::OpenSuccess);
        QCOMPARE(expanded.loadDocumentWithPassword(output, after, {}), Okular::Document::OpenSuccess);
        QCOMPARE(expanded.renderToImage(0, 72, false), original.renderToImage(0, 72, false));
        const auto repeated = PdfPageBounds::expandAppearance(sample().toUtf8().toStdString(), output.toUtf8().toStdString());
        QVERIFY(!repeated.ok);
        qDeleteAll(before);
        qDeleteAll(after);
    }
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        fz_context *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
        pdf_document *doc = pdf_create_document(ctx);
        pdf_obj *resources = pdf_new_dict(ctx, doc, 1);
        auto *fonts = pdf_dict_put_dict(ctx, resources, PDF_NAME(Font), 1);
        auto *font = pdf_new_dict(ctx, doc, 3);
        pdf_dict_puts_drop(ctx, fonts, "F0", font);
        pdf_dict_put_name(ctx, font, PDF_NAME(Type), "Font");
        pdf_dict_put_name(ctx, font, PDF_NAME(Subtype), "Type1");
        pdf_dict_put_name(ctx, font, PDF_NAME(BaseFont), "Helvetica");
        const char bytes[] = "BT /F0 20 Tf 30 150 Td (MuPDF migration test) Tj ET\n";
        auto *buffer = fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char *>(bytes), sizeof(bytes) - 1);
        auto *page = pdf_add_page(ctx, doc, fz_make_rect(0, 0, 300, 200), 0, resources, buffer);
        pdf_insert_page(ctx, doc, -1, page);
        const auto path = sample().toUtf8();
        pdf_save_document(ctx, doc, path.constData(), &pdf_default_write_options);
        pdf_drop_obj(ctx, page);
        pdf_drop_obj(ctx, resources);
        fz_drop_buffer(ctx, buffer);
        pdf_drop_document(ctx, doc);
        fz_drop_context(ctx);
    }
    void renderCancellationCallback()
    {
        Okular::PixmapRequest request(nullptr, 0, 300, 200, 1, 0, Okular::PixmapRequest::NoFeature);
        int calls = 0;
        request.setRenderAbortCallback([&] { ++calls; });
        request.abortRender();
        request.abortRender();
        QCOMPARE(calls, 1);
        QVERIFY(request.shouldAbortRender());
        request.setRenderAbortCallback({});
        request.setRenderAbortCallback([&] { ++calls; });
        QCOMPARE(calls, 2); // Cancellation before callback registration is not lost.
        request.setRenderAbortCallback({});

        Okular::PixmapRequest concurrent(nullptr, 0, 300, 200, 1, 0, Okular::PixmapRequest::NoFeature);
        std::promise<void> entered, release;
        auto released = release.get_future().share();
        concurrent.setRenderAbortCallback([&] { entered.set_value(); released.wait(); });
        auto aborting = std::async(std::launch::async, [&] { concurrent.abortRender(); });
        entered.get_future().wait();
        auto detaching = std::async(std::launch::async, [&] { concurrent.setRenderAbortCallback({}); });
        const auto status = detaching.wait_for(std::chrono::milliseconds(10));
        release.set_value();
        aborting.get();
        detaching.get();
        QCOMPARE(status, std::future_status::timeout);
    }
    void concurrentPreviewRendering()
    {
        MuPDFGenerator backend(nullptr, {});
        QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(sample(), pages, {}), Okular::Document::OpenSuccess);
        QString error;
        const auto renderer = backend.createPrintPreviewRenderer(&error);
        QVERIFY2(bool(renderer), qPrintable(error));
        const auto expected = backend.renderToImage(0, 72, true);
        auto worker = std::async(std::launch::async, [renderer] {
            QList<QImage> images;
            for (int i = 0; i < 12; ++i) images.append(renderer(0, QSize(300, 200)).image);
            return images;
        });
        for (int i = 0; i < 12; ++i)
            QCOMPARE(backend.renderToImage(0, 72, true), expected);
        for (const auto &image : worker.get())
            QCOMPARE(image, expected);
        qDeleteAll(pages);
    }
    void tileRendering_data()
    {
        QTest::addColumn<int>("rotation");
        QTest::addColumn<int>("dpr");
        for (int rotation : {0, 90, 180, 270})
            for (int dpr : {1, 2})
                QTest::newRow(qPrintable(QStringLiteral("%1-dpr%2").arg(rotation).arg(dpr))) << rotation << dpr;
    }
    void tileRendering()
    {
        QFETCH(int, rotation);
        QFETCH(int, dpr);
        const auto input = m_directory.filePath(QStringLiteral("tiles.pdf"));
        auto *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
        auto *doc = pdf_open_document(ctx, sample().toUtf8().constData());
        auto *object = pdf_lookup_page_obj(ctx, doc, 0);
        pdf_dict_put_int(ctx, object, PDF_NAME(Rotate), rotation);
        pdf_dict_put_int(ctx, object, PDF_NAME(UserUnit), 2);
        pdf_dict_put_rect(ctx, object, PDF_NAME(CropBox), fz_make_rect(10, 10, 280, 190));
        pdf_save_document(ctx, doc, input.toUtf8().constData(), &pdf_default_write_options);
        pdf_drop_document(ctx, doc);
        fz_drop_context(ctx);
        MuPDFGenerator backend(nullptr, {});
        QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(input, pages, {}), Okular::Document::OpenSuccess);
        QVERIFY(backend.hasFeature(Okular::Generator::TiledRendering));
        Okular::TextAnnotation note;
        note.setTextType(Okular::TextAnnotation::InPlace);
        note.setContents(QStringLiteral("Tile boundary"));
        note.setBoundingRectangle({.2, .2, .8, .8});
        note.style().setColor(Qt::red);
        backend.annotationProxy()->notifyAddition(&note, 0);
        const int width = rotation % 180 ? 360 : 540;
        const int height = rotation % 180 ? 540 : 360;
        const auto checkTiles = [&] {
            Okular::PixmapRequest full(nullptr, 0, width, height, dpr, 0, Okular::PixmapRequest::NoFeature);
            const auto image = backend.image(&full);
            QVERIFY(!image.isNull());
            for (const auto &rect : {Okular::NormalizedRect(0, 0, .5, .5), Okular::NormalizedRect(.5, 0, 1, .5),
                                     Okular::NormalizedRect(0, .5, .5, 1), Okular::NormalizedRect(.5, .5, 1, 1),
                                     Okular::NormalizedRect(.137, .219, .731, .847)}) {
                Okular::PixmapRequest tile(nullptr, 0, width, height, dpr, 0, Okular::PixmapRequest::NoFeature);
                tile.setTile(true);
                tile.setNormalizedRect(rect);
                const auto result = backend.image(&tile);
                const auto pixels = rect.geometry(full.width(), full.height());
                QImage expected(pixels.size(), image.format());
                expected.fill(Qt::white);
                { QPainter painter(&expected); painter.drawImage(-pixels.topLeft(), image); }
                QCOMPARE(result, expected);
            }
        };
        checkTiles();
        note.setBoundingRectangle({.35, .1, .9, .6});
        note.setContents(QStringLiteral("Moved annotation"));
        backend.annotationProxy()->notifyModification(&note, 0, true);
        checkTiles();
        qDeleteAll(pages);
    }
    void hugePageTileAndCacheInvalidation()
    {
        MuPDFGenerator backend(nullptr, {});
        QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(sample(), pages, {}), Okular::Document::OpenSuccess);
        Okular::PixmapRequest full(nullptr, 0, 100000, 100000, 1, 0, Okular::PixmapRequest::NoFeature);
        QVERIFY(backend.image(&full).isNull());
        Okular::PixmapRequest tile(nullptr, 0, 100000, 100000, 1, 0, Okular::PixmapRequest::NoFeature);
        tile.setTile(true);
        tile.setNormalizedRect({.1, .2, .105, .205});
        const auto result = backend.image(&tile);
        QCOMPARE(result.size(), tile.normalizedRect().geometry(tile.width(), tile.height()).size());
        QVERIFY(result.sizeInBytes() < 1024 * 1024);
        const auto before = backend.renderToImage(0, 72, true);
        QString error;
        Okular::Page *blank = nullptr;
        quint64 edit = 0;
        QVERIFY(backend.insertBlankPageInDocument(-1, 300, 200, &blank, &edit, &error));
        pages.prepend(blank);
        const auto after = backend.renderToImage(0, 72, true);
        QVERIFY(after != before);
        QImage white(after.size(), after.format());
        white.fill(Qt::white);
        QCOMPARE(after, white);
        QVERIFY(backend.movePageInDocument(0, 1, &error));
        QCOMPARE(backend.renderToImage(0, 72, true), before);
        qDeleteAll(pages);
    }
    void bundledCjkFallback()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows SDK font configuration");
#else
        auto *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
        int size = 0, subfont = 0;
        const auto *data = fz_lookup_cjk_font(ctx, FZ_ADOBE_GB, &size, &subfont);
        QVERIFY(data && size > 0);
        auto *font = fz_new_font_from_memory(ctx, nullptr, data, size, subfont, 1);
        QVERIFY(QByteArray(fz_font_name(ctx, font)).contains("Droid"));
        for (auto c : QStringLiteral("中文繁體あいう한글"))
            QVERIFY(fz_encode_character(ctx, font, c.unicode()) > 0);
        fz_drop_font(ctx, font);
        fz_drop_context(ctx);
#endif
    }
    void unembeddedChineseUsesWindowsFont()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows system font integration");
#else
        const QString systemFont = qEnvironmentVariable("WINDIR") + QStringLiteral("/Fonts/simsun.ttc");
        if (!QFile::exists(systemFont))
            QSKIP("SimSun is not installed on this machine");
        const QString text = QStringLiteral("中文测试汉字繁體");
        const auto unembedded = m_directory.filePath(QStringLiteral("unembedded-chinese.pdf"));
        const auto embedded = m_directory.filePath(QStringLiteral("embedded-chinese.pdf"));
        auto *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
        auto *font = fz_new_font_from_file(ctx, "SimSun", systemFont.toUtf8().constData(), 0, 1);
        for (bool embed : {false, true}) {
            auto *doc = pdf_create_document(ctx);
            auto *resources = pdf_new_dict(ctx, doc, 1);
            auto *fonts = pdf_dict_put_dict(ctx, resources, PDF_NAME(Font), 1);
            if (embed) {
                pdf_dict_puts_drop(ctx, fonts, "F0", pdf_add_cid_font(ctx, doc, font));
            } else {
                // Adobe's standard Chinese font name, deliberately no FontFile stream.
                const char dictionary[] = "<< /Type /Font /Subtype /Type0 /BaseFont /STSong-Light "
                    "/Encoding /UniGB-UCS2-H /DescendantFonts [ << /Type /Font /Subtype /CIDFontType0 "
                    "/BaseFont /STSong-Light /CIDSystemInfo << /Registry (Adobe) /Ordering (GB1) /Supplement 4 >> "
                    "/DW 1000 /FontDescriptor << /Type /FontDescriptor /FontName /STSong-Light /Flags 6 "
                    "/FontBBox [0 -200 1000 900] /Ascent 900 /Descent -200 /CapHeight 700 /ItalicAngle 0 /StemV 80 >> >> ] >>";
                auto *stream = fz_open_memory(ctx, reinterpret_cast<const unsigned char *>(dictionary), sizeof(dictionary) - 1);
                pdf_lexbuf lex;
                pdf_lexbuf_init(ctx, &lex, PDF_LEXBUF_SMALL);
                pdf_lex(ctx, stream, &lex); // consume the opening dictionary token
                pdf_dict_puts_drop(ctx, fonts, "F0", pdf_parse_dict(ctx, doc, stream, &lex));
                pdf_lexbuf_fin(ctx, &lex);
                fz_drop_stream(ctx, stream);
            }
            QByteArray hex;
            for (auto c : text) {
                const int glyph = fz_encode_character(ctx, font, c.unicode());
                QVERIFY(glyph > 0);
                hex += QByteArray::number(embed ? glyph : c.unicode(), 16).rightJustified(4, '0');
            }
            const QByteArray content = QByteArray("BT /F0 24 Tf 30 100 Td <") + hex + "> Tj ET";
            auto *buffer = fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char *>(content.constData()), content.size());
            auto *page = pdf_add_page(ctx, doc, fz_make_rect(0, 0, 300, 160), 0, resources, buffer);
            pdf_insert_page(ctx, doc, -1, page);
            pdf_save_document(ctx, doc, (embed ? embedded : unembedded).toUtf8().constData(), &pdf_default_write_options);
            pdf_drop_obj(ctx, page);
            fz_drop_buffer(ctx, buffer);
            pdf_drop_obj(ctx, resources);
            pdf_drop_document(ctx, doc);
        }
        fz_drop_font(ctx, font);
        fz_drop_context(ctx);
        MuPDFGenerator actual(nullptr, {}), reference(nullptr, {});
        QList<Okular::Page *> pages, referencePages;
        QCOMPARE(actual.loadDocumentWithPassword(unembedded, pages, {}), Okular::Document::OpenSuccess);
        QCOMPARE(reference.loadDocumentWithPassword(embedded, referencePages, {}), Okular::Document::OpenSuccess);
        const auto image = actual.renderToImage(0, 144, false);
        QVERIFY(!image.isNull());
        // The same SimSun glyphs must render identically whether embedded or resolved from Windows.
        QCOMPARE(image, reference.renderToImage(0, 144, false));
        QString error;
        const auto saved = m_directory.filePath(QStringLiteral("unembedded-chinese-saved.pdf"));
        QVERIFY2(actual.save(saved, Okular::SaveInterface::SaveChanges, &error), qPrintable(error));
        MuPDFGenerator reopened(nullptr, {});
        QList<Okular::Page *> reopenedPages;
        QCOMPARE(reopened.loadDocumentWithPassword(saved, reopenedPages, {}), Okular::Document::OpenSuccess);
        QCOMPARE(reopened.renderToImage(0, 144, false), image);
        qDeleteAll(pages);
        qDeleteAll(referencePages);
        qDeleteAll(reopenedPages);
#endif
    }
    void pagePhysicalUnits_data()
    {
        QTest::addColumn<QSizeF>("screenDpi");
        QTest::newRow("72-dpi") << QSizeF(72, 72);
        QTest::newRow("96-dpi") << QSizeF(96, 96);
        QTest::newRow("144-dpi") << QSizeF(144, 144);
        QTest::newRow("unequal-axes") << QSizeF(120, 144);
    }
    void pagePhysicalUnits()
    {
        QFETCH(QSizeF, screenDpi);
        MuPDFGenerator backend(nullptr, {});
        backend.setDPI(screenDpi);
        QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(sample(), pages, {}), Okular::Document::OpenSuccess);
        // Page dimensions are logical pixels. Converting them back to PDF points
        // must recover the same physical width at every display DPI.
        QCOMPARE(pages[0]->width() * 72.0 / screenDpi.width(), 300.0);
        QCOMPARE(pages[0]->height() * 72.0 / screenDpi.height(), 200.0);
        const double frameWidthPt = .8 * pages[0]->width() * 72.0 / screenDpi.width();
        QCOMPARE(Okular::LatexNoteGeometry::layoutWidthForVisibleWidth(frameWidthPt, 3), 234.0);
        QCOMPARE(backend.renderToImage(0, 72, false).size(), QSize(300, 200));
        QString error;
        Okular::Page *inserted = nullptr;
        quint64 id = 0;
        QVERIFY2(backend.insertBlankPageInDocument(0, 400, 500, &inserted, &id, &error), qPrintable(error));
        pages.append(inserted);
        QCOMPARE(inserted->width() * 72.0 / screenDpi.width(), 400.0);
        QCOMPARE(inserted->height() * 72.0 / screenDpi.height(), 500.0);
        QCOMPARE(backend.renderToImage(1, 72, false).size(), QSize(400, 500));
        qDeleteAll(pages);
    }
    void ocrWordRectanglesRemainStable()
    {
        MuPDFGenerator backend(nullptr, {});
        QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(sample(), pages, {}), Okular::Document::OpenSuccess);
        QList<Okular::OcrTextWord> words {{QStringLiteral("first"), QRectF(.1, .3, .2, .04)}, {QStringLiteral("untouched"), QRectF(.4, .3, .3, .04)}};
        QString error;
        const auto original = backend.renderToImage(0, 144, false);
        for (int i = 0; i < 5; ++i) {
            words[0].rectangle.setWidth(.2 + i * .01);
            QVERIFY2(backend.replaceOcrTextLayer(0, words, &error), qPrintable(error));
            QList<Okular::OcrTextWord> actual;
            QVERIFY2(backend.readOcrTextLayer(0, &actual, &error), qPrintable(error));
            QCOMPARE(actual, words);
            QCOMPARE(backend.renderToImage(0, 144, false), original);
        }
        const auto saved = m_directory.filePath(QStringLiteral("ocr-edited.pdf"));
        QVERIFY2(backend.save(saved, Okular::SaveInterface::SaveChanges, &error), qPrintable(error));
        MuPDFGenerator reopened(nullptr, {});
        QList<Okular::Page *> fresh;
        QCOMPARE(reopened.loadDocumentWithPassword(saved, fresh, {}), Okular::Document::OpenSuccess);
        QList<Okular::OcrTextWord> actual;
        QVERIFY2(reopened.readOcrTextLayer(0, &actual, &error), qPrintable(error));
        QCOMPARE(actual, words);
        // Undo/redo payloads also round-trip without expanding other words.
        words[0].rectangle.setWidth(.2);
        QVERIFY2(reopened.replaceOcrTextLayer(0, words, &error), qPrintable(error));
        QVERIFY2(reopened.readOcrTextLayer(0, &actual, &error), qPrintable(error));
        QCOMPARE(actual, words);
        qDeleteAll(pages);
        qDeleteAll(fresh);
    }
    void ocrSnapshotTemporaryFiles()
    {
        MuPDFGenerator backend(nullptr, {});
        QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(sample(), pages, {}), Okular::Document::OpenSuccess);
        auto source = ClosedTemporaryPdfFile::create(m_directory.filePath(QStringLiteral("ocr-source-XXXXXX.pdf")));
        auto output = ClosedTemporaryPdfFile::create(m_directory.filePath(QStringLiteral("ocr-output-XXXXXX.pdf")));
        QVERIFY(source);
        QVERIFY(output);
        const auto sourcePath = source->fileName(), outputPath = output->fileName();
        QString error;
        QVERIFY2(backend.save(sourcePath, Okular::SaveInterface::SaveChanges, &error), qPrintable(error));
        QVERIFY2(backend.save(sourcePath, Okular::SaveInterface::SaveChanges, &error), qPrintable(error));
        const auto result = backend.saveWithEnglishOcr(sourcePath, outputPath, {1}, false, {});
        QVERIFY2(result.success, qPrintable(result.errorText));
        QCOMPARE(result.recognizedPages, 1);
        QVERIFY(result.recognizedWords > 0);
        const auto repeated = backend.saveWithEnglishOcr(sourcePath, outputPath, {1}, true, {});
        QVERIFY2(repeated.success, qPrintable(repeated.errorText));
        QCOMPARE(repeated.skippedPages, 1);
        // Restore recognized output after exercising a second allocator session.
        const auto recognized = backend.saveWithEnglishOcr(sourcePath, outputPath, {1}, false, {});
        QVERIFY2(recognized.success, qPrintable(recognized.errorText));
        MuPDFGenerator reopened(nullptr, {});
        QList<Okular::Page *> fresh;
        QCOMPARE(reopened.loadDocumentWithPassword(outputPath, fresh, {}), Okular::Document::OpenSuccess);
        QList<Okular::OcrTextWord> words;
        QVERIFY2(reopened.readOcrTextLayer(0, &words, &error), qPrintable(error));
        QVERIFY(!words.isEmpty());
        QCOMPARE(reopened.renderToImage(0, 144, false), backend.renderToImage(0, 144, false));
        source.reset();
        output.reset();
        QVERIFY(!QFileInfo::exists(sourcePath));
        QVERIFY(!QFileInfo::exists(outputPath));
        qDeleteAll(pages);
        qDeleteAll(fresh);
    }
    void renderAndSave()
    {
        MuPDFGenerator backend(nullptr, {});
        QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(sample(), pages, {}), Okular::Document::OpenSuccess);
        QCOMPARE(pages.size(), 1);
        QString error;
        const QImage before = backend.renderToImage(0, 72, true, &error);
        QVERIFY2(!before.isNull(), qPrintable(error));
        QCOMPARE(before.size(), QSize(300, 200));
        const QString output = m_directory.filePath(QStringLiteral("saved.pdf"));
        QVERIFY2(backend.save(output, Okular::SaveInterface::SaveChanges, &error), qPrintable(error));
        MuPDFGenerator reopened(nullptr, {});
        QList<Okular::Page *> fresh;
        QCOMPARE(reopened.loadDocumentWithPassword(output, fresh, {}), Okular::Document::OpenSuccess);
        QCOMPARE(reopened.renderToImage(0, 72, true, &error), before);
        qDeleteAll(pages);
        qDeleteAll(fresh);
    }
    void annotationRoundTrip()
    {
        MuPDFGenerator backend(nullptr, {});
        QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(sample(), pages, {}), Okular::Document::OpenSuccess);
        Okular::TextAnnotation annotation;
        annotation.setTextType(Okular::TextAnnotation::InPlace);
        annotation.setContents(QStringLiteral("Persisted annotation"));
        annotation.setBoundingRectangle({.1, .4, .9, .7});
        annotation.style().setColor(Qt::red);
        backend.annotationProxy()->notifyAddition(&annotation, 0);
        QString error;
        const auto output = m_directory.filePath(QStringLiteral("annotation.pdf"));
        QVERIFY2(backend.save(output, Okular::SaveInterface::SaveChanges, &error), qPrintable(error));
        MuPDFGenerator reopened(nullptr, {});
        QList<Okular::Page *> fresh;
        QCOMPARE(reopened.loadDocumentWithPassword(output, fresh, {}), Okular::Document::OpenSuccess);
        QCOMPARE(fresh[0]->annotations().size(), 1);
        QCOMPARE(fresh[0]->annotations().first()->contents(), annotation.contents());
        const auto withAnnotations = reopened.renderToImage(0, 72, true, &error);
        QVERIFY(withAnnotations != reopened.renderToImage(0, 72, false, &error));
        qDeleteAll(pages);
        qDeleteAll(fresh);
    }
    void geometryDrivenAnnotations()
    {
        const auto source = m_directory.filePath(QStringLiteral("native-annotations.pdf"));
        auto *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
        const auto input = sample().toUtf8();
        auto *doc = pdf_open_document(ctx, input.constData());
        auto *page = pdf_load_page(ctx, doc, 0);
        const enum pdf_annot_type types[] = {PDF_ANNOT_HIGHLIGHT, PDF_ANNOT_UNDERLINE, PDF_ANNOT_STRIKE_OUT, PDF_ANNOT_SQUIGGLY, PDF_ANNOT_LINE, PDF_ANNOT_INK};
        for (int i = 0; i < 6; ++i) {
            auto *annotation = pdf_create_annot(ctx, page, types[i]);
            const float y = 20 + i * 25;
            if (i < 4) {
                const fz_quad quad = {{30, y}, {180, y}, {30, y + 15}, {180, y + 15}};
                pdf_set_annot_quad_points(ctx, annotation, 1, &quad);
            } else if (i == 4)
                pdf_set_annot_line(ctx, annotation, {30, y}, {180, y + 15});
            else {
                const fz_point points[] = {{30, y}, {80, y + 15}, {180, y}};
                const int count = 3;
                pdf_set_annot_ink_list(ctx, annotation, 1, &count, points);
            }
            pdf_set_annot_contents(ctx, annotation, "Existing annotation");
            pdf_update_annot(ctx, annotation);
            pdf_drop_annot(ctx, annotation);
        }
        const auto output = source.toUtf8();
        pdf_save_document(ctx, doc, output.constData(), &pdf_default_write_options);
        fz_drop_page(ctx, reinterpret_cast<fz_page *>(page));
        pdf_drop_document(ctx, doc);
        fz_drop_context(ctx);
        MuPDFGenerator backend(nullptr, {});
        QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(source, pages, {}), Okular::Document::OpenSuccess);
        QCOMPARE(pages[0]->annotations().size(), 6);
        for (auto *annotation : pages[0]->annotations()) {
            QVERIFY(annotation->boundingRectangle().right > annotation->boundingRectangle().left);
            annotation->setContents(QStringLiteral("Edited annotation"));
            annotation->style().setColor(Qt::red);
            backend.annotationProxy()->notifyModification(annotation, 0, true);
        }
        QString error;
        const auto saved = m_directory.filePath(QStringLiteral("native-annotations-saved.pdf"));
        QVERIFY2(backend.save(saved, Okular::SaveInterface::SaveChanges, &error), qPrintable(error));
        MuPDFGenerator reopened(nullptr, {});
        QList<Okular::Page *> fresh;
        QCOMPARE(reopened.loadDocumentWithPassword(saved, fresh, {}), Okular::Document::OpenSuccess);
        QCOMPARE(fresh[0]->annotations().size(), 6);
        for (auto *annotation : fresh[0]->annotations())
            QCOMPARE(annotation->contents(), QStringLiteral("Edited annotation"));
        QCOMPARE(reopened.renderToImage(0, 72, true), backend.renderToImage(0, 72, true));
        qDeleteAll(pages);
        qDeleteAll(fresh);
    }
    void malformedAnnotationDoesNotRejectDocument()
    {
        const auto path = m_directory.filePath(QStringLiteral("malformed-annotation.pdf"));
        auto *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
        const auto input = sample().toUtf8();
        auto *doc = pdf_open_document(ctx, input.constData());
        auto *page = pdf_lookup_page_obj(ctx, doc, 0);
        auto *annots = pdf_dict_put_array(ctx, page, PDF_NAME(Annots), 2);
        auto *bad = pdf_add_new_dict(ctx, doc, 4);
        pdf_dict_put(ctx, bad, PDF_NAME(Type), PDF_NAME(Annot));
        pdf_dict_put(ctx, bad, PDF_NAME(Subtype), PDF_NAME(Squiggly));
        pdf_dict_put_rect(ctx, bad, PDF_NAME(Rect), {30, 30, 180, 45});
        // Deliberately omit the required QuadPoints. Keep this raw object on save.
        pdf_array_push_drop(ctx, annots, bad);
        auto *good = pdf_add_new_dict(ctx, doc, 4);
        pdf_dict_put(ctx, good, PDF_NAME(Type), PDF_NAME(Annot));
        pdf_dict_put(ctx, good, PDF_NAME(Subtype), PDF_NAME(Text));
        pdf_dict_put_rect(ctx, good, PDF_NAME(Rect), {200, 80, 220, 100});
        pdf_dict_put_text_string(ctx, good, PDF_NAME(Contents), "Unaffected annotation");
        pdf_array_push_drop(ctx, annots, good);
        auto bytes = path.toUtf8();
        pdf_save_document(ctx, doc, bytes.constData(), &pdf_default_write_options);
        pdf_drop_document(ctx, doc);
        fz_drop_context(ctx);
        MuPDFGenerator backend(nullptr, {});
        QSignalSpy warnings(&backend, &Okular::Generator::warning);
        QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(path, pages, {}), Okular::Document::OpenSuccess);
        QCOMPARE(pages.size(), 1);
        QCOMPARE(pages[0]->annotations().size(), 1);
        QCOMPARE(pages[0]->annotations()[0]->contents(), QStringLiteral("Unaffected annotation"));
        QCOMPARE(warnings.size(), 1);
        QVERIFY(!backend.renderToImage(0, 72, true).isNull());
        QString error;
        const auto saved = m_directory.filePath(QStringLiteral("malformed-preserved.pdf"));
        QVERIFY2(backend.save(saved, Okular::SaveInterface::SaveChanges, &error), qPrintable(error));
        ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
        bytes = saved.toUtf8();
        doc = pdf_open_document(ctx, bytes.constData());
        QCOMPARE(pdf_array_len(ctx, pdf_dict_get(ctx, pdf_lookup_page_obj(ctx, doc, 0), PDF_NAME(Annots))), 2);
        pdf_drop_document(ctx, doc);
        fz_drop_context(ctx);
        qDeleteAll(pages);
    }
    void externalPdfRegression()
    {
        const auto source = qEnvironmentVariable("MENGSHEE_TEST_PDF");
        if (source.isEmpty())
            QSKIP("Set MENGSHEE_TEST_PDF to check a local PDF without adding it to the repository.");
        MuPDFGenerator backend(nullptr, {});
        QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(source, pages, {}), Okular::Document::OpenSuccess);
        QVERIFY(!pages.isEmpty());
        for (int i = 0; i < pages.size(); ++i)
            QVERIFY(!backend.renderToImage(i, 36, true).isNull());
        QString error;
        const auto original = backend.renderToImage(0, 144, false);
        const QList<Okular::OcrTextWord> words {{QStringLiteral("OCR regression"), QRectF(.1, .1, .3, .03)}};
        QVERIFY2(backend.replaceOcrTextLayer(0, words, &error), qPrintable(error));
        QCOMPARE(backend.renderToImage(0, 144, false), original);
        QVERIFY2(backend.replaceOcrTextLayer(0, words, &error), qPrintable(error));
        QCOMPARE(backend.renderToImage(0, 144, false), original);
        const auto saved = m_directory.filePath(QStringLiteral("external-copy.pdf"));
        QVERIFY2(backend.save(saved, Okular::SaveInterface::SaveChanges, &error), qPrintable(error));
        MuPDFGenerator reopened(nullptr, {});
        QList<Okular::Page *> fresh;
        QCOMPARE(reopened.loadDocumentWithPassword(saved, fresh, {}), Okular::Document::OpenSuccess);
        QCOMPARE(fresh.size(), pages.size());
        QCOMPARE(reopened.renderToImage(0, 144, false), original);
        for (int i = 0; i < pages.size(); ++i)
            QCOMPARE(fresh[i]->annotations().size(), pages[i]->annotations().size());
        qDeleteAll(pages);
        qDeleteAll(fresh);
    }
    void pageAndMetadataRoundTrip()
    {
        MuPDFGenerator backend(nullptr, {});
        QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(sample(), pages, {}), Okular::Document::OpenSuccess);
        QString error;
        Okular::Page *inserted = nullptr;
        quint64 edit = 0;
        QVERIFY2(backend.insertBlankPageInDocument(0, 400, 500, &inserted, &edit, &error), qPrintable(error));
        pages.append(inserted);
        const QList<Okular::ReadingView> definitions {{QUuid::createUuid().toString(QUuid::WithoutBraces), 1, {.1, .2, .8, .9}}};
        QVERIFY2(backend.setReadingViews(0, definitions, &error), qPrintable(error));
        const QString token = backend.readingViewPageToken(0);
        QVERIFY(!token.isEmpty());
        QVERIFY2(backend.movePageInDocument(0, 1, &error), qPrintable(error));
        QCOMPARE(backend.readingViewPageForToken(token), 1);
        const auto output = m_directory.filePath(QStringLiteral("pages.pdf"));
        QVERIFY2(backend.save(output, Okular::SaveInterface::SaveChanges, &error), qPrintable(error));
        MuPDFGenerator reopened(nullptr, {});
        QList<Okular::Page *> fresh;
        QCOMPARE(reopened.loadDocumentWithPassword(output, fresh, {}), Okular::Document::OpenSuccess);
        QCOMPARE(fresh.size(), 2);
        QCOMPARE(reopened.readingViewPageForToken(token), 1);
        QCOMPARE(reopened.readingViews(1, &error).size(), 1);
        qDeleteAll(pages);
        qDeleteAll(fresh);
    }
    void latexAppearanceRoundTrip_data()
    {
        pagePhysicalUnits_data();
    }
    void latexAppearanceRoundTrip()
    {
        QFETCH(QSizeF, screenDpi);
        MuPDFGenerator backend(nullptr, {});
        backend.setDPI(screenDpi);
        QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(sample(), pages, {}), Okular::Document::OpenSuccess);
        Okular::StampAnnotation annotation;
        annotation.setOkularLatex(true);
        annotation.setLatexNoteType(Okular::Annotation::LatexNoteBoxed);
        annotation.setContents(QStringLiteral("$x^2$"));
        annotation.setLatexAppearancePdfFileName(sample());
        annotation.setBoundingRectangle({.1, .1, .9, .9});
        annotation.setLatexFillColor(Qt::white);
        annotation.setLatexBorderColor(Qt::blue);
        annotation.style().setWidth(1);
        annotation.setLatexPadding(2);
        backend.annotationProxy()->notifyAddition(&annotation, 0);
        QString error;
        const auto output = m_directory.filePath(QStringLiteral("latex.pdf"));
        QVERIFY2(backend.save(output, Okular::SaveInterface::SaveChanges, &error), qPrintable(error));
        MuPDFGenerator reopened(nullptr, {});
        reopened.setDPI(screenDpi);
        QList<Okular::Page *> fresh;
        QCOMPARE(reopened.loadDocumentWithPassword(output, fresh, {}), Okular::Document::OpenSuccess);
        QCOMPARE(fresh[0]->annotations().size(), 1);
        QVERIFY(fresh[0]->annotations()[0]->isOkularLatex());
        QCOMPARE(fresh[0]->annotations()[0]->contents(), annotation.contents());
        // Reopened notes have no temporary TeX PDF. Drag start/end notifications
        // must preserve the embedded formula, including renders between updates.
        auto *moved = static_cast<Okular::StampAnnotation *>(fresh[0]->annotations()[0]);
        QVERIFY(moved->latexAppearancePdfFileName().isEmpty());
        const auto rawFormula = [](const QString &file) {
            auto *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
            const auto path = file.toUtf8();
            auto *doc = pdf_open_document(ctx, path.constData());
            auto *object = pdf_array_get(ctx, pdf_dict_get(ctx, pdf_lookup_page_obj(ctx, doc, 0), PDF_NAME(Annots)), 0);
            auto *raw = pdf_dict_getp(ctx, object, "AP/N/Resources/XObject/Fm0");
            QByteArray result;
            if (pdf_is_stream(ctx, raw)) {
                auto *buffer = pdf_load_stream(ctx, raw);
                unsigned char *data;
                const auto length = fz_buffer_storage(ctx, buffer, &data);
                result = QByteArray(reinterpret_cast<const char *>(data), length);
                fz_drop_buffer(ctx, buffer);
            }
            pdf_drop_document(ctx, doc);
            fz_drop_context(ctx);
            return result;
        };
        const auto formula = rawFormula(output);
        QVERIFY(!formula.isEmpty());
        for (bool appearanceChanged : {false, true}) {
            moved->setFlags(moved->flags() | Okular::Annotation::BeingMoved);
            reopened.annotationProxy()->notifyModification(moved, 0, false);
            QVERIFY(!reopened.renderToImage(0, 72, true, &error).isNull());
            moved->setBoundingRectangle({.15, .15, .95, .95});
            reopened.annotationProxy()->notifyModification(moved, 0, appearanceChanged);
            QVERIFY(!reopened.renderToImage(0, 72, true, &error).isNull());
            moved->setFlags(moved->flags() & ~Okular::Annotation::BeingMoved);
            reopened.annotationProxy()->notifyModification(moved, 0, false);
            const auto rendered = reopened.renderToImage(0, 72, true, &error);
            QVERIFY(!rendered.isNull());
            QVERIFY(rendered != reopened.renderToImage(0, 72, false, &error));
            const auto movedFile = m_directory.filePath(QStringLiteral("latex-moved.pdf"));
            QVERIFY2(reopened.save(movedFile, Okular::SaveInterface::SaveChanges, &error), qPrintable(error));
            QCOMPARE(rawFormula(movedFile), formula);
            MuPDFGenerator check(nullptr, {});
            check.setDPI(screenDpi);
            QList<Okular::Page *> checkPages;
            QCOMPARE(check.loadDocumentWithPassword(movedFile, checkPages, {}), Okular::Document::OpenSuccess);
            QVERIFY(checkPages[0]->annotations()[0]->isOkularLatex());
            QVERIFY(qAbs(checkPages[0]->annotations()[0]->boundingRectangle().left - .15) < .001);
            QCOMPARE(check.renderToImage(0, 72, true, &error), rendered);
            qDeleteAll(checkPages);
        }
        QVERIFY(!reopened.renderToImage(0, 72, true, &error).isNull());
        qDeleteAll(pages);
        qDeleteAll(fresh);
    }
    void linksAndOcr()
    {
        MuPDFGenerator backend(nullptr, {});
        QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(sample(), pages, {}), Okular::Document::OpenSuccess);
        QString error;
        QVERIFY2(backend.setNamedDestination(QStringLiteral("target"), 1, .2, .3, &error), qPrintable(error));
        QVERIFY2(backend.createInternalLink(pages[0], .1, .1, .4, .2, QStringLiteral("target"), 0, .2, .3, &error), qPrintable(error));
        QVERIFY2(backend.renameNamedDestination(QStringLiteral("target"), QStringLiteral("renamed"), &error), qPrintable(error));
        const auto destinations = backend.metaData(QStringLiteral("NamedViewports"), {}).toList();
        QCOMPARE(destinations.size(), 1);
        QCOMPARE(destinations.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("renamed"));
        const auto before = backend.renderToImage(0, 72, false, &error);
        const QList<Okular::OcrTextWord> words {{QStringLiteral("Invisible"), QRectF(.1, .6, .4, .1)}};
        QVERIFY2(backend.replaceOcrTextLayer(0, words, &error), qPrintable(error));
        QCOMPARE(backend.renderToImage(0, 72, false, &error), before);
        QList<Okular::OcrTextWord> extracted;
        QVERIFY2(backend.readOcrTextLayer(0, &extracted, &error), qPrintable(error));
        QVERIFY(!extracted.isEmpty());
        QCOMPARE(extracted[0].text, QStringLiteral("Invisible"));
        const QList<Okular::OcrTextWord> replacement {{QStringLiteral("Replaced—café"), QRectF(.1, .6, .4, .1)}};
        QVERIFY2(backend.replaceOcrTextLayer(0, replacement, &error), qPrintable(error));
        QVERIFY2(backend.readOcrTextLayer(0, &extracted, &error), qPrintable(error));
        QCOMPARE(extracted.size(), 1);
        QCOMPARE(extracted[0].text, QStringLiteral("Replaced—café"));
        const auto path = m_directory.filePath(QStringLiteral("links-ocr.pdf"));
        QVERIFY2(backend.save(path, Okular::SaveInterface::SaveChanges, &error), qPrintable(error));
        MuPDFGenerator reopened(nullptr, {});
        QList<Okular::Page *> fresh;
        QCOMPARE(reopened.loadDocumentWithPassword(path, fresh, {}), Okular::Document::OpenSuccess);
        QVERIFY(!reopened.metaData(QStringLiteral("NamedViewport"), QStringLiteral("renamed")).toString().isEmpty());
        QVERIFY2(reopened.readOcrTextLayer(0, &extracted, &error), qPrintable(error));
        QCOMPARE(extracted.size(), 1);
        qDeleteAll(pages);
        qDeleteAll(fresh);
    }
    void formAndEmbeddedFileRoundTrip()
    {
        const auto path = m_directory.filePath(QStringLiteral("form-source.pdf"));
        auto *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
        auto input = sample().toUtf8();
        auto *doc = pdf_open_document(ctx, input.constData());
        auto *page = pdf_lookup_page_obj(ctx, doc, 0);
        auto *root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
        auto *form = pdf_dict_put_dict(ctx, root, PDF_NAME(AcroForm), 3);
        pdf_dict_put(ctx, form, PDF_NAME(DR), pdf_dict_get(ctx, page, PDF_NAME(Resources)));
        auto *fields = pdf_dict_put_array(ctx, form, PDF_NAME(Fields), 2);
        auto *annots = pdf_dict_put_array(ctx, page, PDF_NAME(Annots), 2);
        for (int i = 0; i < 2; ++i) {
            auto *field = pdf_add_new_dict(ctx, doc, 9);
            pdf_array_push(ctx, fields, field);
            pdf_array_push(ctx, annots, field);
            pdf_dict_put(ctx, field, PDF_NAME(Type), PDF_NAME(Annot));
            pdf_dict_put(ctx, field, PDF_NAME(Subtype), PDF_NAME(Widget));
            pdf_dict_put(ctx, field, PDF_NAME(FT), i ? PDF_NAME(Ch) : PDF_NAME(Tx));
            pdf_dict_put_text_string(ctx, field, PDF_NAME(T), i ? "choice" : "text");
            pdf_dict_put_text_string(ctx, field, PDF_NAME(DA), "/F1 12 Tf 0 g");
            pdf_dict_put_rect(ctx, field, PDF_NAME(Rect), fz_make_rect(30, 30 + i * 40, 200, 60 + i * 40));
            if (i) {
                auto *options = pdf_dict_put_array(ctx, field, PDF_NAME(Opt), 2);
                pdf_array_push_text_string(ctx, options, "First");
                pdf_array_push_text_string(ctx, options, "Second");
                pdf_dict_put_int(ctx, field, PDF_NAME(Ff), PDF_CH_FIELD_IS_COMBO);
            }
            pdf_drop_obj(ctx, field);
        }
        const char payload[] = "attachment contents";
        auto *buffer = fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char *>(payload), sizeof(payload) - 1);
        auto *file = pdf_add_embedded_file(ctx, doc, "test.txt", "text/plain", buffer, 0, 1, 1);
        auto *names = pdf_dict_put_dict(ctx, root, PDF_NAME(Names), 1);
        auto *tree = pdf_dict_put_dict(ctx, names, PDF_NAME(EmbeddedFiles), 1);
        auto *entries = pdf_dict_put_array(ctx, tree, PDF_NAME(Names), 2);
        pdf_array_push_text_string(ctx, entries, "test.txt");
        pdf_array_push(ctx, entries, file);
        pdf_drop_obj(ctx, file);
        fz_drop_buffer(ctx, buffer);
        auto output = path.toUtf8();
        pdf_save_document(ctx, doc, output.constData(), &pdf_default_write_options);
        pdf_drop_document(ctx, doc);
        fz_drop_context(ctx);
        MuPDFGenerator backend(nullptr, {});
        QList<Okular::Page *> pages;
        QString error;
        QCOMPARE(backend.loadDocumentWithPassword(path, pages, {}), Okular::Document::OpenSuccess);
        QCOMPARE(pages[0]->formFields().size(), 2);
        auto *text = dynamic_cast<Okular::FormFieldText *>(pages[0]->formFields()[0]);
        auto *choice = dynamic_cast<Okular::FormFieldChoice *>(pages[0]->formFields()[1]);
        QVERIFY(text);
        QVERIFY(choice);
        QCOMPARE(text->fullyQualifiedName(), QStringLiteral("text"));
        text->setText(QStringLiteral("Hello form"));
        text->setAppearanceText(QStringLiteral("Formatted value"));
        choice->setCurrentChoices({1});
        QCOMPARE(text->text(), QStringLiteral("Hello form"));
        QCOMPARE(choice->currentChoices(), QList<int> {1});
        const auto *files = backend.embeddedFiles();
        QCOMPARE(files->size(), 1);
        QCOMPARE(files->first()->data(), QByteArray(payload));
        QVERIFY(!backend.fontsForPage(0).isEmpty());
        Okular::Page *duplicate = nullptr;
        quint64 editId;
        QVERIFY2(backend.duplicatePageInDocument(0, true, &duplicate, &editId, &error), qPrintable(error));
        pages.append(duplicate);
        QCOMPARE(duplicate->formFields().size(), 2);
        auto *copiedText = dynamic_cast<Okular::FormFieldText *>(duplicate->formFields()[0]);
        QVERIFY(copiedText);
        QVERIFY(copiedText->fullyQualifiedName() != text->fullyQualifiedName());
        copiedText->setText(QStringLiteral("Independent copy"));
        QCOMPARE(text->text(), QStringLiteral("Hello form"));
        const auto saved = m_directory.filePath(QStringLiteral("form-saved.pdf"));
        QVERIFY2(backend.save(saved, Okular::SaveInterface::SaveChanges, &error), qPrintable(error));
        MuPDFGenerator reopened(nullptr, {});
        QList<Okular::Page *> fresh;
        QCOMPARE(reopened.loadDocumentWithPassword(saved, fresh, {}), Okular::Document::OpenSuccess);
        QCOMPARE(dynamic_cast<Okular::FormFieldText *>(fresh[0]->formFields()[0])->text(), QStringLiteral("Hello form"));
        QCOMPARE(dynamic_cast<Okular::FormFieldChoice *>(fresh[0]->formFields()[1])->currentChoices(), QList<int> {1});
        QCOMPARE(fresh.size(), 2);
        QCOMPARE(dynamic_cast<Okular::FormFieldText *>(fresh[1]->formFields()[0])->text(), QStringLiteral("Independent copy"));
        QCOMPARE(reopened.embeddedFiles()->first()->data(), QByteArray(payload));
        qDeleteAll(pages);
        qDeleteAll(fresh);
    }
    void contentsAndNoteLink()
    {
        MuPDFGenerator backend(nullptr, {});
        QList<Okular::Page *> pages;
        QString error;
        QCOMPARE(backend.loadDocumentWithPassword(sample(), pages, {}), Okular::Document::OpenSuccess);
        Okular::DocumentSynopsis synopsis;
        auto root = synopsis.createElement(QStringLiteral("章节一"));
        synopsis.appendChild(root);
        Okular::DocumentViewport target(0);
        target.rePos.enabled = true;
        target.rePos.normalizedX = .2;
        target.rePos.normalizedY = .3;
        root.setAttribute(QStringLiteral("Viewport"), target.toString());
        auto child = synopsis.createElement(QStringLiteral("Note"));
        root.appendChild(child);
        child.setAttribute(QStringLiteral("Viewport"), target.toString());
        child.setAttribute(QStringLiteral("ViewportName"), QStringLiteral("mnote-1-1"));
        child.setAttribute(QStringLiteral("CreateViewportName"), QStringLiteral("true"));
        QVERIFY2(backend.setDocumentSynopsis(synopsis, &error), qPrintable(error));
        QCOMPARE(backend.generateDocumentSynopsis()->firstChildElement().tagName(), QStringLiteral("章节一"));
        const auto plain = backend.renderToImage(0, 72, true);
        Okular::PdfLinkAppearance appearance;
        appearance.kind = Okular::PdfLinkAppearance::NoteLabel;
        appearance.label = QStringLiteral("see note 1.");
        QVERIFY2(backend.createInternalLink(pages[0], .1, .6, .4, .7, QStringLiteral("mnote-1-1"), 0, 0, 0, &error, appearance), qPrintable(error));
        const auto labelled = backend.renderToImage(0, 72, true);
        QVERIFY(labelled != plain);
        QCOMPARE(backend.renderToImage(0, 72, false), plain);
        const auto path = m_directory.filePath(QStringLiteral("contents-label.pdf"));
        QVERIFY2(backend.save(path, Okular::SaveInterface::SaveChanges, &error), qPrintable(error));
        MuPDFGenerator reopened(nullptr, {});
        QList<Okular::Page *> fresh;
        QCOMPARE(reopened.loadDocumentWithPassword(path, fresh, {}), Okular::Document::OpenSuccess);
        QCOMPARE(reopened.renderToImage(0, 72, true), labelled);
        QCOMPARE(reopened.generateDocumentSynopsis()->firstChildElement().firstChildElement().tagName(), QStringLiteral("Note"));
        QCOMPARE(reopened.generateDocumentSynopsis()->firstChildElement().firstChildElement().attribute(QStringLiteral("ViewportName")), QStringLiteral("mnote-1-1"));
        qDeleteAll(pages);
        qDeleteAll(fresh);
    }
    void printPreviewSnapshotSurvivesSourceClose()
    {
        Okular::PrintPreviewRenderer renderer;
        QImage expected;
        {
            MuPDFGenerator backend(nullptr, {});
            QList<Okular::Page *> pages;
            QCOMPARE(backend.loadDocumentWithPassword(sample(), pages, {}), Okular::Document::OpenSuccess);
            Okular::StampAnnotation note;
            note.setOkularLatex(true);
            note.setLatexNoteType(Okular::Annotation::LatexNoteBoxed);
            note.setLatexAppearancePdfFileName(sample());
            note.setBoundingRectangle({.1, .3, .9, .9});
            backend.annotationProxy()->notifyAddition(&note, 0);
            expected = backend.renderToImage(0, 72, true);
            QString error;
            renderer = backend.createPrintPreviewRenderer(&error);
            QVERIFY2(bool(renderer), qPrintable(error));
            qDeleteAll(pages);
        }
        const auto full = std::async(std::launch::async, [renderer] { return renderer(0, QSize(2048,2048)); }).get();
        QVERIFY2(full.error.isEmpty(), qPrintable(full.error));
        QCOMPARE(full.image, expected);
        const auto reduced = renderer(0, QSize(150,100));
        QCOMPARE(reduced.image.size(), QSize(150,100));
    }
    void rotatedPageResize_data()
    {
        QTest::addColumn<int>("rotation");
        QTest::addColumn<int>("edge");
        QTest::addColumn<int>("unit");
        for (int rotation : {0, 90, 180, 270})
            for (int edge = 0; edge < 4; ++edge)
                for (int unit : {1, 2})
                    QTest::newRow(qPrintable(QStringLiteral("rotation-%1-edge-%2-unit-%3").arg(rotation).arg(edge).arg(unit))) << rotation << edge << unit;
    }
    void rotatedPageResize()
    {
        QFETCH(int, rotation);
        QFETCH(int, edge);
        QFETCH(int, unit);
        const auto input = m_directory.filePath(QStringLiteral("resize-input.pdf"));
        auto *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
        auto *doc = pdf_open_document(ctx, sample().toUtf8().constData());
        auto *object = pdf_lookup_page_obj(ctx, doc, 0);
        pdf_dict_put_int(ctx, object, PDF_NAME(UserUnit), unit);
        pdf_dict_put_rect(ctx, object, PDF_NAME(CropBox), fz_make_rect(10, 10, 280, 190));
        pdf_save_document(ctx, doc, input.toUtf8().constData(), &pdf_default_write_options);
        pdf_drop_document(ctx, doc);
        fz_drop_context(ctx);
        MuPDFGenerator backend(nullptr, {});
        QList<Okular::Page *> pages;
        QString error;
        QCOMPARE(backend.loadDocumentWithPassword(input, pages, {}), Okular::Document::OpenSuccess);
        Okular::Page *rotated = nullptr;
        QVERIFY2(backend.rotatePageInDocument(pages[0], 0, rotation, &rotated, &error), qPrintable(error));
        delete pages[0];
        pages[0] = rotated;
        const auto before = backend.renderToImage(0, 72, true);
        const double x0 = edge == 0 ? -20 : 0, y0 = edge == 3 ? -20 : 0;
        const double x1 = edge == 2 ? 20 : 0, y1 = edge == 1 ? 20 : 0;
        Okular::Page *resized = nullptr;
        QVERIFY2(backend.adjustPageBoxInDocument(pages[0], 0, x0, y0, x1, y1, &resized, &error), qPrintable(error));
        delete pages[0];
        pages[0] = resized;
        const auto after = backend.renderToImage(0, 72, true);
        QCOMPARE(after.size(), before.size() + QSize(edge % 2 == 0 ? 20 : 0, edge % 2 == 1 ? 20 : 0));
        QCOMPARE(after.copy(edge == 0 ? 20 : 0, edge == 1 ? 20 : 0, before.width(), before.height()), before);
        const auto output = m_directory.filePath(QStringLiteral("resize-output.pdf"));
        QVERIFY2(backend.save(output, Okular::SaveInterface::SaveChanges, &error), qPrintable(error));
        MuPDFGenerator reopened(nullptr, {});
        QList<Okular::Page *> fresh;
        QCOMPARE(reopened.loadDocumentWithPassword(output, fresh, {}), Okular::Document::OpenSuccess);
        QCOMPARE(reopened.renderToImage(0, 72, true), after);
        Okular::Page *restored = nullptr;
        QVERIFY2(backend.adjustPageBoxInDocument(pages[0], 0, -x0, -y0, -x1, -y1, &restored, &error), qPrintable(error));
        QCOMPARE(backend.renderToImage(0, 72, true), before);
        delete restored;
        qDeleteAll(pages);
        qDeleteAll(fresh);
    }
    void flattenedExportPreservesAppearanceAndSource_data()
    {
        QTest::addColumn<bool>("linkFirst");
        QTest::addColumn<bool>("overlap");
        QTest::newRow("separate-link") << false << false;
        QTest::newRow("overlapping-link-before-note") << true << true;
        QTest::newRow("overlapping-link-after-note") << false << true;
    }
    void flattenedExportPreservesAppearanceAndSource()
    {
        QFETCH(bool, linkFirst);
        QFETCH(bool, overlap);
        MuPDFGenerator backend(nullptr, {});
        QList<Okular::Page *> pages;
        QString error;
        QCOMPARE(backend.loadDocumentWithPassword(sample(), pages, {}), Okular::Document::OpenSuccess);
        if (linkFirst)
            QVERIFY2(backend.createExternalLink(pages[0], .1, overlap ? .4 : .1, .3, overlap ? .5 : .2, QStringLiteral("https://example.com"), &error), qPrintable(error));
        Okular::StampAnnotation annotation;
        annotation.setOkularLatex(true);
        annotation.setLatexNoteType(Okular::Annotation::LatexNoteBoxed);
        annotation.setContents(QStringLiteral("$x$"));
        annotation.setLatexAppearancePdfFileName(sample());
        annotation.setBoundingRectangle({.1, .3, .9, .9});
        annotation.setLatexFillColor(Qt::white);
        annotation.setLatexBorderColor(Qt::blue);
        annotation.style().setWidth(1);
        backend.annotationProxy()->notifyAddition(&annotation, 0);
        if (!linkFirst)
            QVERIFY2(backend.createExternalLink(pages[0], .1, overlap ? .4 : .1, .3, overlap ? .5 : .2, QStringLiteral("https://example.com"), &error), qPrintable(error));
        const auto before = backend.renderToImage(0, 144, true);
        int baked = -1, retained = -1;
        const auto path = m_directory.filePath(QStringLiteral("flat.pdf"));
        QVERIFY2(backend.exportFlattenedPdf(path, &error, &baked, &retained), qPrintable(error));
        QCOMPARE(baked, 1);
        QCOMPARE(retained, 1);
        QCOMPARE(backend.renderToImage(0, 144, true), before);
        MuPDFGenerator reopened(nullptr, {});
        QList<Okular::Page *> fresh;
        QCOMPARE(reopened.loadDocumentWithPassword(path, fresh, {}), Okular::Document::OpenSuccess);
        QVERIFY(fresh[0]->annotations().isEmpty());
        QCOMPARE(reopened.renderToImage(0, 144, false), before);
        auto *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
        auto *doc = pdf_open_document(ctx, path.toUtf8().constData());
        auto *links = pdf_dict_get(ctx, pdf_lookup_page_obj(ctx, doc, 0), PDF_NAME(Annots));
        QCOMPARE(pdf_array_len(ctx, links), 1);
        auto *link = pdf_array_get(ctx, links, 0);
        QVERIFY(pdf_name_eq(ctx, pdf_dict_get(ctx, link, PDF_NAME(Subtype)), PDF_NAME(Link)));
        QCOMPARE(QByteArray(pdf_to_text_string(ctx, pdf_dict_getp(ctx, link, "A/URI"))), QByteArray("https://example.com"));
        pdf_drop_document(ctx, doc);
        fz_drop_context(ctx);
        qDeleteAll(pages);
        qDeleteAll(fresh);
    }
    void clipboardAppearanceSurvivesSourceClose()
    {
        std::shared_ptr<void> transfer;
        QImage expected;
        {
            MuPDFGenerator source(nullptr, {});
            QList<Okular::Page *> pages;
            QCOMPARE(source.loadDocumentWithPassword(sample(), pages, {}), Okular::Document::OpenSuccess);
            Okular::StampAnnotation annotation;
            annotation.setOkularLatex(true);
            annotation.setLatexNoteType(Okular::Annotation::LatexNoteBoxed);
            annotation.setContents(QStringLiteral("$x$"));
            annotation.setLatexAppearancePdfFileName(sample());
            annotation.setBoundingRectangle({.1, .3, .9, .9});
            source.annotationProxy()->notifyAddition(&annotation, 0);
            transfer = source.annotationProxy()->annotationAppearance(&annotation);
            QVERIFY(transfer);
            expected = source.renderToImage(0, 72, true);
            qDeleteAll(pages);
        }
        MuPDFGenerator destination(nullptr, {});
        QList<Okular::Page *> pages;
        QCOMPARE(destination.loadDocumentWithPassword(sample(), pages, {}), Okular::Document::OpenSuccess);
        Okular::StampAnnotation pasted;
        pasted.setOkularLatex(true);
        pasted.setLatexNoteType(Okular::Annotation::LatexNoteBoxed);
        pasted.setContents(QStringLiteral("$x$"));
        pasted.setBoundingRectangle({.1, .3, .9, .9});
        pasted.setNativeData(transfer);
        destination.annotationProxy()->notifyAddition(&pasted, 0);
        QString error;
        QVERIFY2(destination.save(m_directory.filePath(QStringLiteral("clipboard.pdf")), Okular::SaveInterface::SaveChanges, &error), qPrintable(error));
        QCOMPARE(destination.renderToImage(0, 72, true), expected);
        qDeleteAll(pages);
    }
    void importDestinationsAndSwap()
    {
        MuPDFGenerator backend(nullptr, {});
        QList<Okular::Page *> pages;
        QString error;
        QCOMPARE(backend.loadDocumentWithPassword(sample(), pages, {}), Okular::Document::OpenSuccess);
        QVERIFY(backend.setNamedDestination(QStringLiteral("target"), 1, .1, .2, &error));
        QVERIFY(backend.createInternalLink(pages[0], .1, .1, .2, .2, QStringLiteral("target"), 0, .1, .2, &error));
        const auto source = m_directory.filePath(QStringLiteral("import-source.pdf"));
        QVERIFY2(backend.save(source, Okular::SaveInterface::SaveChanges, &error), qPrintable(error));
        const auto combined = m_directory.filePath(QStringLiteral("combined.pdf"));
        QVERIFY2(backend.combinePdfFiles({source, source}, combined, true, &error), qPrintable(error));
        auto *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
        const auto name = combined.toUtf8();
        auto *doc = pdf_open_document(ctx, name.constData());
        QCOMPARE(pdf_count_pages(ctx, doc), 2);
        auto *destinations = pdf_dict_getp(ctx, pdf_trailer(ctx, doc), "Root/Dests");
        for (int i = 0; i < 2; ++i) {
            const char *target = i ? "target_2" : "target";
            auto *dest = pdf_dict_gets(ctx, destinations, target);
            QCOMPARE(pdf_to_num(ctx, pdf_array_get(ctx, dest, 0)), pdf_to_num(ctx, pdf_lookup_page_obj(ctx, doc, i)));
            auto *link = pdf_array_get(ctx, pdf_dict_get(ctx, pdf_lookup_page_obj(ctx, doc, i), PDF_NAME(Annots)), 0);
            QCOMPARE(QByteArray(pdf_to_text_string(ctx, pdf_dict_getp(ctx, link, "A/D"))), QByteArray(target));
        }
        pdf_drop_document(ctx, doc);
        fz_drop_context(ctx);
        Okular::Page *copy = nullptr;
        quint64 id;
        QVERIFY2(backend.duplicatePageInDocument(0, true, &copy, &id, &error), qPrintable(error));
        pages.append(copy);
        QVERIFY(backend.detachPageInDocument(copy, 1, id, &error));
        const auto saved = m_directory.filePath(QStringLiteral("swap.pdf"));
        QVERIFY(backend.save(saved, Okular::SaveInterface::SaveChanges, &error));
        QList<Okular::Page *> replacement;
        QCOMPARE(backend.swapBackingFile(saved, replacement), Okular::Generator::SwapBackingFileReloadInternalData);
        qDeleteAll(pages);
        pages = replacement;
        QVERIFY2(backend.attachPageInDocument(0, id, &copy, &error), qPrintable(error));
        pages.append(copy);
        QCOMPARE(Okular::DocumentViewport(backend.metaData(QStringLiteral("NamedViewport"), QStringLiteral("target_2")).toString()).pageNumber, 1);
        QCOMPARE(backend.pdfPageCount(saved, &error), 1);
        QVERIFY(backend.save(saved, Okular::SaveInterface::SaveChanges, &error));
        QCOMPARE(backend.pdfPageCount(saved, &error), 2);
        QVERIFY(!backend.duplicatePageInDocument(999, true, &copy, &id, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!backend.combinePdfFiles({QStringLiteral("missing.pdf")}, combined, true, &error));
        QVERIFY(!error.isEmpty());
        const auto before = backend.renderToImage(0, 72, true);
        replacement.clear();
        QCOMPARE(backend.swapBackingFile(QStringLiteral("missing.pdf"), replacement), Okular::Generator::SwapBackingFileError);
        QCOMPARE(backend.renderToImage(0, 72, true), before);
        qDeleteAll(pages);
    }
    void duplicateAnnotationsAreIndependent()
    {
        MuPDFGenerator backend(nullptr, {});
        QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(sample(), pages, {}), Okular::Document::OpenSuccess);
        Okular::TextAnnotation original;
        original.setContents(QStringLiteral("Original"));
        original.setBoundingRectangle({.1, .1, .2, .2});
        backend.annotationProxy()->notifyAddition(&original, 0);
        Okular::Page *duplicate = nullptr;
        quint64 id;
        QString error;
        QVERIFY2(backend.duplicatePageInDocument(0, true, &duplicate, &id, &error), qPrintable(error));
        pages.append(duplicate);
        QCOMPARE(duplicate->annotations().size(), 1);
        auto *annotation = duplicate->annotations()[0];
        annotation->setContents(QStringLiteral("Copy"));
        backend.annotationProxy()->notifyModification(annotation, 1, true);
        const auto path = m_directory.filePath(QStringLiteral("duplicate.pdf"));
        QVERIFY2(backend.save(path, Okular::SaveInterface::SaveChanges, &error), qPrintable(error));
        MuPDFGenerator reopened(nullptr, {});
        QList<Okular::Page *> fresh;
        QCOMPARE(reopened.loadDocumentWithPassword(path, fresh, {}), Okular::Document::OpenSuccess);
        QCOMPARE(fresh.size(), 2);
        QCOMPARE(fresh[0]->annotations().size(), 1);
        QCOMPARE(fresh[1]->annotations().size(), 1);
        QCOMPARE(fresh[0]->annotations()[0]->contents(), QStringLiteral("Original"));
        QCOMPARE(fresh[1]->annotations()[0]->contents(), QStringLiteral("Copy"));
        qDeleteAll(pages);
        qDeleteAll(fresh);
    }
};
QTEST_MAIN(BackendTest)
#include "backendtest.moc"
