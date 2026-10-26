#include <core/annotationframe.h>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "../generator_mupdf.h"
#include "../../poppler/pdfpagebounds.h"
#include "../runtimehelpers.h"
#include "settings_core.h"
#include <QMimeDatabase>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QTest>
#include <core/annotations.h>
#include <core/document.h>
#include <core/page.h>
class LatexRotationTest : public QObject
{
    Q_OBJECT
    static void verifyFreeTextStorage(const QString &path, bool hasAppearance, int rotation)
    {
        auto *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
        QVERIFY(ctx);
        pdf_document *doc = nullptr;
        fz_var(doc);
        bool found = false, matches = true;
        fz_var(found);
        fz_var(matches);
        fz_try(ctx) {
            doc = pdf_open_document(ctx, path.toUtf8().constData());
            auto *annots = pdf_dict_get(ctx, pdf_lookup_page_obj(ctx, doc, 0), PDF_NAME(Annots));
            for (int i = 0; i < pdf_array_len(ctx, annots); ++i) {
                auto *annot = pdf_array_get(ctx, annots, i);
                if (!pdf_name_eq(ctx, pdf_dict_get(ctx, annot, PDF_NAME(Subtype)), PDF_NAME(FreeText)))
                    continue;
                found = true;
                matches = matches && bool(pdf_dict_get(ctx, annot, PDF_NAME(AP))) == hasAppearance
                    && pdf_dict_get_int(ctx, annot, PDF_NAME(Rotate)) == rotation;
            }
        }
        fz_always(ctx) { pdf_drop_document(ctx, doc); }
        fz_catch(ctx) { matches = false; }
        fz_drop_context(ctx);
        QVERIFY(found);
        QVERIFY(matches);
    }
    static bool samePixels(const QImage &actual, const QImage &expected)
    {
        if (actual.size() != expected.size())
            return false;
        quint64 total = 0;
        for (int y = 0; y < actual.height(); ++y)
            for (int x = 0; x < actual.width(); ++x) {
                auto a = actual.pixelColor(x, y), b = expected.pixelColor(x, y);
                int d = qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) + qAbs(a.blue() - b.blue());
                total += d;
            }
        // MuPDF's antialiasing can differ slightly when rasterizing rotated
        // diagonal leaders; keep the mean RGB error below one level per pixel.
        return total <= quint64(actual.width() * actual.height());
    }
private Q_SLOTS:
    void importedTypewriter()
    {
        const auto path = qEnvironmentVariable("MENGSHEE_TEST_TYPEWRITER_PDF");
        if (path.isEmpty()) QSKIP("Optional external FreeText import regression");
        MuPDFGenerator backend(nullptr, {});
        QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(path, pages, {}), Okular::Document::OpenSuccess);
        int count = 0;
        for (auto *page : pages) {
            for (auto *annotation : page->annotations()) {
                auto *text = dynamic_cast<Okular::TextAnnotation *>(annotation);
                if (!text || text->textType() != Okular::TextAnnotation::InPlace) continue;
                QCOMPARE(text->inplaceIntent(), Okular::TextAnnotation::TypeWriter);
                QCOMPARE(text->textFontName(), QStringLiteral("Helvetica"));
                QCOMPARE(text->textFontPointSize(), 6.0);
                QCOMPARE(text->textColor(), QColor(Qt::red));
                ++count;
            }
        }
        QCOMPARE(count, 3);
        Okular::TextAnnotation added;
        added.setTextType(Okular::TextAnnotation::InPlace);
        added.setInplaceIntent(Okular::TextAnnotation::TypeWriter);
        added.setContents(QStringLiteral("New typewriter regression"));
        added.setBoundingRectangle({.1, .1, .5, .2});
        added.setTextFontName(QStringLiteral("Helvetica"));
        added.setTextFontPointSize(6);
        added.setTextColor(Qt::red);
        backend.annotationProxy()->notifyAddition(&added, 0);
        QTemporaryDir dir;
        const auto saved = dir.filePath(QStringLiteral("saved.pdf"));
        QString error;
        QVERIFY(backend.save(saved, Okular::SaveInterface::SaveChanges, &error));
        verifyFreeTextStorage(saved, false, 0);
        // Verify both the newly authored note and untouched imported notes.
        auto *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
        auto *pdf = pdf_open_document(ctx, saved.toUtf8().constData());
        int checked = 0;
        for (int page = 0; page < pdf_count_pages(ctx, pdf); ++page) {
            auto *annots = pdf_dict_get(ctx, pdf_lookup_page_obj(ctx, pdf, page), PDF_NAME(Annots));
            for (int i = 0; i < pdf_array_len(ctx, annots); ++i) {
                auto *annot = pdf_array_get(ctx, annots, i);
                if (!pdf_name_eq(ctx, pdf_dict_get(ctx, annot, PDF_NAME(Subtype)), PDF_NAME(FreeText))) continue;
                const QByteArray da(pdf_dict_get_text_string(ctx, annot, PDF_NAME(DA)));
                QVERIFY(!da.contains('\n'));
                QVERIFY(!da.contains('\r'));
                QVERIFY(!pdf_dict_get(ctx, annot, PDF_NAME(AP)));
                ++checked;
            }
        }
        pdf_drop_document(ctx, pdf);
        fz_drop_context(ctx);
        QCOMPARE(checked, 4);
        const auto output = qEnvironmentVariable("MENGSHEE_TEST_TYPEWRITER_OUTPUT");
        if (!output.isEmpty()) QVERIFY(QFile::copy(saved, output));
        MuPDFGenerator reopened(nullptr, {});
        QList<Okular::Page *> fresh;
        QCOMPARE(reopened.loadDocumentWithPassword(saved, fresh, {}), Okular::Document::OpenSuccess);
        auto *loaded = dynamic_cast<Okular::TextAnnotation *>(fresh[0]->annotation(added.uniqueName()));
        QVERIFY(loaded);
        QCOMPARE(loaded->inplaceIntent(), Okular::TextAnnotation::TypeWriter);
        QCOMPARE(loaded->textFontName(), QStringLiteral("Helvetica"));
        QCOMPARE(loaded->textFontPointSize(), 6.0);
        QCOMPARE(loaded->textColor(), QColor(Qt::red));
        qDeleteAll(fresh);
        qDeleteAll(pages);
    }
    void latexPadding_data()
    {
        QTest::addColumn<double>("border");
        QTest::addColumn<bool>("expand");
        for(double border:{0.0,1.0,4.0}) for(bool expand:{false,true})
            QTest::newRow(qPrintable(QStringLiteral("%1-expanded-%2").arg(border).arg(expand))) << border << expand;
    }
    void latexPadding()
    {
        QFETCH(double,border);
        QFETCH(bool,expand);
        QTemporaryDir dir;
        const auto pageFile=dir.filePath(QStringLiteral("page.pdf"));
        auto source=dir.filePath(QStringLiteral("content.pdf"));
        for(bool content:{false,true}) {
            QPdfWriter writer(content?source:pageFile); writer.setResolution(72);
            writer.setPageSize(QPageSize(content?QSizeF(108,28):QSizeF(300,300),QPageSize::Point)); writer.setPageMargins(QMarginsF());
            QPainter painter(&writer); painter.fillRect(content?QRect(0,0,108,28):QRect(0,0,300,300),content?Qt::blue:Qt::white);
        }
        if(expand) {
            const auto expanded=dir.filePath(QStringLiteral("expanded.pdf"));
            QVERIFY(PdfPageBounds::expandAppearance(source.toUtf8().toStdString(),expanded.toUtf8().toStdString(),1.0).ok);
            source=dir.filePath(QStringLiteral("expanded-twice.pdf"));
            QVERIFY(PdfPageBounds::expandAppearance(expanded.toUtf8().toStdString(),source.toUtf8().toStdString(),2.0).ok);
            QCOMPARE(MengsheePdfRuntime::firstPageSize(source),QSizeF(108,28));
        }
        MuPDFGenerator backend(nullptr,{}); QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(pageFile,pages,{}),Okular::Document::OpenSuccess);
        Okular::StampAnnotation note;
        note.setOkularLatex(true); note.setLatexNoteType(Okular::Annotation::LatexNoteBoxed);
        note.setBoundingRectangle({.1,.2,.5,1.0/3}); note.setLatexPadding(6); note.setLatexLayoutWidth(108);
        note.style().setWidth(border); note.setLatexBorderColor(Qt::red); note.setLatexFillColor(Qt::yellow);
        note.setLatexAppearancePdfFileName(source);
        backend.annotationProxy()->notifyAddition(&note,0);
        const auto image=backend.renderToImage(0,72,true);
        QRect ink;
        for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) {
            const auto color=image.pixelColor(x,y);
            if(color.blue()>200 && color.red()<50 && color.green()<50) ink=ink.united(QRect(x,y,1,1));
        }
        // A 120x40 frame and 108x28 source leave six points on every side,
        // independently of the border width. No source scaling is allowed.
        QCOMPARE(ink,QRect(36,66,108,28));
        QString error; const auto saved=dir.filePath(QStringLiteral("saved.pdf"));
        QVERIFY(backend.save(saved,Okular::SaveInterface::SaveChanges,&error));
        MuPDFGenerator reopened(nullptr,{}); QList<Okular::Page *> fresh;
        QCOMPARE(reopened.loadDocumentWithPassword(saved,fresh,{}),Okular::Document::OpenSuccess);
        QVERIFY(samePixels(reopened.renderToImage(0,72,true),image));
        qDeleteAll(fresh); qDeleteAll(pages);
    }
    void latexPaddingExample()
    {
        const QString source=qEnvironmentVariable("MENGSHEE_PADDING_SOURCE_PDF");
        const QString output=qEnvironmentVariable("MENGSHEE_PADDING_OUTPUT");
        if(source.isEmpty() || output.isEmpty()) QSKIP("Optional LaTeX padding visual fixture");
        QTemporaryDir dir;
        const auto expanded=dir.filePath(QStringLiteral("content.pdf"));
        const auto expansion=PdfPageBounds::expandAppearance(source.toUtf8().toStdString(),expanded.toUtf8().toStdString(),1.0);
        QVERIFY(expansion.ok);
        MuPDFGenerator contentBackend(nullptr,{}); QList<Okular::Page *> contentPages;
        QCOMPARE(contentBackend.loadDocumentWithPassword(expanded,contentPages,{}),Okular::Document::OpenSuccess);
        const double width=qEnvironmentVariable("MENGSHEE_PADDING_WIDTH",QStringLiteral("300")).toDouble(), height=MengsheePdfRuntime::firstPageSize(expanded).height();
        // Do not fit the frame to the generated source: that hides discrepancies
        // between the requested paragraph width and its physical PDF extent.
        QVERIFY(qAbs(expansion.logicalBounds.x2-expansion.logicalBounds.x1-width)<0.02);
        const auto blank=dir.filePath(QStringLiteral("blank.pdf"));
        const double pageWidth=width+60, pageHeight=3*(height+60);
        {
            QPdfWriter writer(blank); writer.setResolution(72);
            writer.setPageSize(QPageSize(QSizeF(pageWidth,pageHeight),QPageSize::Point)); writer.setPageMargins(QMarginsF());
            QPainter painter(&writer); painter.fillRect(QRectF(0,0,pageWidth,pageHeight),Qt::white);
            for(int i=0;i<3;++i) painter.drawText(QPointF(24,i*(height+60)+18),QStringLiteral("Padding: 6 pt; border: %1 pt").arg(i==2?4:i));
        }
        MuPDFGenerator backend(nullptr,{}); QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(blank,pages,{}),Okular::Document::OpenSuccess);
        for(int i=0;i<3;++i) {
            Okular::StampAnnotation note;
            note.setOkularLatex(true); note.setLatexNoteType(Okular::Annotation::LatexNoteBoxed);
            const double top=i*(height+60)+28;
            note.setBoundingRectangle({24/pageWidth,top/pageHeight,(24+width+12)/pageWidth,(top+height+12)/pageHeight});
            note.setLatexPadding(6); note.setLatexLayoutWidth(width);
            note.setContents(qEnvironmentVariable("MENGSHEE_PADDING_TEXT"));
            note.style().setWidth(i==2?4:i); note.setLatexBorderColor(Qt::red); note.setLatexFillColor(Qt::yellow);
            note.setLatexAppearancePdfFileName(expanded);
            backend.annotationProxy()->notifyAddition(&note,0);
        }
        QString error;
        QVERIFY(backend.save(output+QStringLiteral(".pdf"),Okular::SaveInterface::SaveChanges,&error));
        QVERIFY(backend.renderToImage(0,144,true).save(output+QStringLiteral(".png")));
        qDeleteAll(pages); qDeleteAll(contentPages);
    }
    void linkBorders_data()
    {
        QTest::addColumn<QByteArray>("attributes");
        QTest::addColumn<bool>("visible");
        QTest::newRow("solid") << QByteArray("/Border [0 0 2] /C [0 1 0]") << true;
        QTest::newRow("rounded") << QByteArray("/Border [5 5 2] /C [0 1 0]") << true;
        QTest::newRow("dashed") << QByteArray("/Border [0 0 2 [4 3]] /C [0 1 0]") << true;
        QTest::newRow("bs-precedence") << QByteArray("/Border [0 0 0] /BS << /W 2 /S /D /D [4 3] >> /C [0 1 0]") << true;
        QTest::newRow("underline") << QByteArray("/BS << /W 2 /S /U >> /C [0 1 0]") << true;
        QTest::newRow("zero") << QByteArray("/Border [0 0 0] /C [0 1 0]") << false;
        QTest::newRow("bs-zero") << QByteArray("/Border [0 0 2] /BS << /W 0 >> /C [0 1 0]") << false;
        QTest::newRow("transparent") << QByteArray("/Border [0 0 2] /C []") << false;
        QTest::newRow("hidden") << QByteArray("/Border [0 0 2] /C [0 1 0] /F 2") << false;
        // An explicit blank AP overrides a visible border declaration.
        QTest::newRow("appearance-precedence") << QByteArray("/Border [0 0 2] /C [0 1 0] /AP << /N 5 0 R >>") << false;
    }
    void linkBorders()
    {
        QFETCH(QByteArray, attributes); QFETCH(bool, visible);
        QTemporaryDir dir;
        const auto source=dir.filePath(QStringLiteral("links.pdf"));
        QList<QByteArray> objects {
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] /Resources <<>> /Annots [4 0 R] >>",
            "<< /Type /Annot /Subtype /Link /Rect [30 40 90 80] /A << /S /URI /URI (https://example.com) >> "+attributes+" >>",
            "<< /Type /XObject /Subtype /Form /BBox [0 0 60 40] /Resources <<>> /Length 0 >>\nstream\n\nendstream"
        };
        QByteArray bytes("%PDF-1.7\n"); QList<int> offsets;
        for(int i=0;i<objects.size();++i) { offsets.append(bytes.size()); bytes+=QByteArray::number(i+1)+" 0 obj\n"+objects[i]+"\nendobj\n"; }
        const int xref=bytes.size(); bytes+="xref\n0 6\n0000000000 65535 f \n";
        for(int offset:offsets) bytes+=QByteArray::number(offset).rightJustified(10,'0')+" 00000 n \n";
        bytes+="trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n"+QByteArray::number(xref)+"\n%%EOF\n";
        QFile file(source); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(bytes); file.close();
        MuPDFGenerator backend(nullptr,{}); QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(source,pages,{}),Okular::Document::OpenSuccess);
        const auto original=backend.renderToImage(0,72,true);
        QCOMPARE(original!=backend.renderToImage(0,72,false),visible);
        if(visible) {
            int green=0;
            for(int y=0;y<300;++y) for(int x=0;x<300;++x) { auto c=original.pixelColor(x,y); if(c.green()>200 && c.red()<100 && c.blue()<100) ++green; }
            QVERIFY(green>20);
        }
        for(int angle:{90,180,270,0}) {
            QString error; Okular::Page *next=nullptr;
            QVERIFY(backend.rotatePageInDocument(pages[0],0,angle,&next,&error)); delete pages[0]; pages[0]=next;
            QVERIFY(samePixels(backend.renderToImage(0,72,true),original.transformed(QTransform().rotate(angle))));
        }
        qDeleteAll(pages);
    }
    void inspectExternalLinks()
    {
        const QString source=qEnvironmentVariable("MENGSHEE_LINK_TEST_PDF");
        if(source.isEmpty()) QSKIP("Optional external PDF visual check");
        MuPDFGenerator backend(nullptr,{}); QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(source,pages,{}),Okular::Document::OpenSuccess);
        QVERIFY(pages.size()>1);
        QVERIFY(backend.renderToImage(1,85,true).save(qEnvironmentVariable("MENGSHEE_LINK_TEST_PNG")));
        qDeleteAll(pages);
    }
    void freeTextSurvivesNeighbourEdit()
    {
        QTemporaryDir dir;
        const auto source = dir.filePath(QStringLiteral("page.pdf"));
        {
            QPdfWriter writer(source);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QSizeF(300, 300), QPageSize::Point));
            writer.setPageMargins(QMarginsF());
            QPainter painter(&writer);
            painter.fillRect(0, 0, 300, 300, Qt::white);
        }
        const auto appearance = dir.filePath(QStringLiteral("appearance.pdf"));
        {
            QPdfWriter writer(appearance);
            writer.setResolution(72);
            QPainter painter(&writer);
            painter.drawText(30, 30, QStringLiteral("Latex"));
        }
        MuPDFGenerator backend(nullptr, {});
        QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(source, pages, {}), Okular::Document::OpenSuccess);
        Okular::TextAnnotation text;
        text.setTextType(Okular::TextAnnotation::InPlace);
        text.setContents(QStringLiteral("Keep this text visible"));
        text.setBoundingRectangle({.1, .1, .8, .25});
        text.setTextFontName(QStringLiteral("Helvetica"));
        text.setTextFontPointSize(14);
        text.setTextColor(Qt::black);
        backend.annotationProxy()->notifyAddition(&text, 0);
        // Ignore subpixel border/placement differences between initial synthesis
        // and editing. Measure the painted text, not the frame's antialiasing.
        const auto inkCount = [&backend]() {
            const auto image = backend.renderToImage(0, 72, true).copy(32, 32, 205, 40);
            int count = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    if (qGray(image.pixel(x, y)) < 128) ++count;
            return count;
        };
        const int smallText = inkCount();
        text.setTextFontPointSize(28);
        backend.annotationProxy()->notifyModification(&text, 0, true);
        QVERIFY(inkCount() > smallText * 1.3);
        text.setTextFontPointSize(14);
        backend.annotationProxy()->notifyModification(&text, 0, true);
        QVERIFY(qAbs(inkCount() - smallText) < smallText * .1);
        Okular::TextAnnotation second;
        second.setTextType(Okular::TextAnnotation::InPlace);
        second.setContents(QStringLiteral("Second text must also remain visible"));
        second.setBoundingRectangle({.1, .3, .9, .45});
        second.setTextColor(Qt::black);
        backend.annotationProxy()->notifyAddition(&second, 0);
        const auto before = backend.renderToImage(0, 72, true).copy(0, 0, 300, 150);
        QVERIFY(before != backend.renderToImage(0, 72, false).copy(0, 0, 300, 150));
        Okular::StampAnnotation latex;
        latex.setOkularLatex(true);
        latex.setLatexAppearancePdfFileName(appearance);
        latex.setBoundingRectangle({.1, .5, .6, .8});
        backend.annotationProxy()->notifyAddition(&latex, 0);
        for (int i = 0; i < 3; ++i) {
            latex.setContents(QString::number(i));
            backend.annotationProxy()->notifyModification(&latex, 0, true);
            QCOMPARE(backend.renderToImage(0, 72, true).copy(0, 0, 300, 150), before);
        }
        QString error;
        const auto saved = dir.filePath(QStringLiteral("saved.pdf"));
        QVERIFY(backend.save(saved, Okular::SaveInterface::SaveChanges, &error));
        verifyFreeTextStorage(saved, false, 0);
        MuPDFGenerator reopened(nullptr, {});
        QList<Okular::Page *> fresh;
        QCOMPARE(reopened.loadDocumentWithPassword(saved, fresh, {}), Okular::Document::OpenSuccess);
        QCOMPARE(reopened.renderToImage(0, 72, true).copy(0, 0, 300, 150), before);
        auto *edited = fresh[0]->annotations().last();
        for (int i = 0; i < 3; ++i) {
            edited->setContents(QString::number(i));
            reopened.annotationProxy()->notifyModification(edited, 0, true);
            QCOMPARE(reopened.renderToImage(0, 72, true).copy(0, 0, 300, 150), before);
        }
        qDeleteAll(fresh);
        qDeleteAll(pages);
    }
    void matchingCalloutArrows_data()
    {
        QTest::addColumn<double>("width");
        QTest::addColumn<bool>("shortLeader");
        for (double width : {0.5, 1.0, 3.0})
            for (bool shortLeader : {false, true})
                QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(width).arg(shortLeader))) << width << shortLeader;
    }
    void matchingCalloutArrows()
    {
        QFETCH(double, width);
        QFETCH(bool, shortLeader);
        QTemporaryDir dir;
        const auto source = dir.filePath(QStringLiteral("blank.pdf"));
        { QPdfWriter writer(source); writer.setResolution(72); writer.setPageSize(QPageSize(QSizeF(300,300),QPageSize::Point)); writer.setPageMargins(QMarginsF()); QPainter painter(&writer); painter.fillRect(0,0,300,300,Qt::white); }
        MuPDFGenerator backend(nullptr, {}); QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(source,pages,{}),Okular::Document::OpenSuccess);
        Okular::TextAnnotation text;
        text.setTextType(Okular::TextAnnotation::InPlace); text.setInplaceIntent(Okular::TextAnnotation::Callout);
        text.setBoundingRectangle({.65,.1,.95,.4}); text.style().setWidth(width); text.setTextColor(Qt::black);
        const Okular::NormalizedPoint points[] = {{.2,.7},{shortLeader ? .21 : .5,.7},{.65,.3}};
        for(int i=0;i<3;++i) text.setInplaceCallout(points[i],i);
        backend.annotationProxy()->notifyAddition(&text,0);
        const auto ordinary=backend.renderToImage(0,144,true).copy(20,320,220,200);
        backend.annotationProxy()->notifyRemoval(&text,0);
        Okular::StampAnnotation latex;
        latex.setOkularLatex(true); latex.setLatexNoteType(Okular::Annotation::LatexNoteCallout);
        latex.setLatexAppearancePdfFileName(source); latex.setBoundingRectangle(text.boundingRectangle());
        latex.style().setWidth(width); latex.setLatexBorderColor(Qt::black);
        for(int i=0;i<3;++i) latex.setLatexCalloutPoint(points[i],i);
        backend.annotationProxy()->notifyAddition(&latex,0);
        QVERIFY(samePixels(backend.renderToImage(0,144,true).copy(20,320,220,200),ordinary));
        qDeleteAll(pages);
    }
    void plainCalloutLeader_data()
    {
        QTest::addColumn<int>("angle");
        for (int angle : {0, 90, 180, 270})
            QTest::newRow(qPrintable(QString::number(angle))) << angle;
    }
    void plainCalloutLeader()
    {
        QTemporaryDir dir;
        const auto source = dir.filePath(QStringLiteral("page.pdf"));
        {
            QPdfWriter writer(source);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QSizeF(300, 300), QPageSize::Point));
            writer.setPageMargins(QMarginsF());
            QPainter painter(&writer);
            painter.fillRect(0, 0, 300, 300, Qt::white);
        }
        MuPDFGenerator backend(nullptr, {});
        QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(source, pages, {}), Okular::Document::OpenSuccess);
        QFETCH(int, angle);
        QString rotationError;
        Okular::Page *rotated = nullptr;
        QVERIFY(backend.rotatePageInDocument(pages[0], 0, angle, &rotated, &rotationError));
        delete pages[0];
        pages[0] = rotated;
        Okular::TextAnnotation text;
        text.setTextType(Okular::TextAnnotation::InPlace);
        text.setInplaceIntent(Okular::TextAnnotation::Callout);
        text.setContents(QStringLiteral("Callout"));
        text.setTextColor(Qt::black);
        text.setBoundingRectangle({.5, .2, .9, .4});
        text.setInplaceCallout({.1, .7}, 0);
        text.setInplaceCallout({.3, .3}, 1);
        text.setInplaceCallout({.5, .3}, 2);
        backend.annotationProxy()->notifyAddition(&text, 0);
        const auto image = backend.renderToImage(0, 72, true);
        QVERIFY(image.copy(20, 80, 120, 140) != backend.renderToImage(0, 72, false).copy(20, 80, 120, 140));
        QString error;
        const auto saved = dir.filePath(QStringLiteral("saved.pdf"));
        QVERIFY(backend.save(saved, Okular::SaveInterface::SaveChanges, &error));
        verifyFreeTextStorage(saved, false, angle);
        MuPDFGenerator reopened(nullptr, {});
        QList<Okular::Page *> fresh;
        QCOMPARE(reopened.loadDocumentWithPassword(saved, fresh, {}), Okular::Document::OpenSuccess);
        QVERIFY(samePixels(reopened.renderToImage(0, 72, true), image));
        auto *loaded = dynamic_cast<Okular::TextAnnotation *>(fresh[0]->annotations()[0]);
        QVERIFY(loaded);
        QCOMPARE(loaded->inplaceIntent(), Okular::TextAnnotation::Callout);
        const auto originalRect = text.boundingRectangle(), loadedRect = loaded->boundingRectangle();
        QVERIFY2(qAbs(originalRect.left - loadedRect.left) < .001, qPrintable(QStringLiteral("expected %1 actual %2").arg(originalRect.left).arg(loadedRect.left)));
        QVERIFY(qAbs(originalRect.top - loadedRect.top) < .001);
        auto movedRect = loadedRect;
        movedRect.left += .05;
        movedRect.right += .05;
        movedRect.top += .05;
        movedRect.bottom += .05;
        loaded->setBoundingRectangle(movedRect);
        for (int i = 0; i < 3; ++i) {
            auto point = loaded->inplaceCallout(i);
            point.x += .05;
            point.y += .05;
            loaded->setInplaceCallout(point, i);
        }
        reopened.annotationProxy()->notifyModification(loaded, 0, false);
        QImage expected(image.size(), image.format());
        expected.fill(Qt::white);
        {
            QPainter painter(&expected);
            painter.drawImage(15, 15, image);
        }
        QVERIFY(samePixels(reopened.renderToImage(0, 72, true), expected));
        qDeleteAll(fresh);
        qDeleteAll(pages);
    }
    void standardFreeTextBaseline()
    {
        QTemporaryDir dir;
        const QByteArray stream("BT /F1 12 Tf 1 0 0 1 4 30 Tm (Baseline) Tj ET");
        QList<QByteArray> objects {"<< /Type /Catalog /Pages 2 0 R >>",
                                   "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                                   "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 400] /Resources <<>> /Annots [4 0 R] >>",
                                   "<< /Type /Annot /Subtype /FreeText /Rect [60 80 120 200] /DA (/Helv 12 Tf 0 g) /Contents (Baseline) /F 4 /AP << /N 5 0 R >> >>",
                                   "<< /Type /XObject /Subtype /Form /BBox [0 0 120 60] /Matrix [0 1 -1 0 0 0] /Resources << /Font << /F1 6 0 R >> >> /Length " + QByteArray::number(stream.size()) + " >>\nstream\n" + stream + "\nendstream",
                                   "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"};
        QByteArray pdf("%PDF-1.7\n");
        QList<int> offsets;
        for(int i=0;i<objects.size();++i) {offsets.append(pdf.size());pdf+=QByteArray::number(i+1)+" 0 obj\n"+objects[i]+"\nendobj\n";}
        const int xref=pdf.size();pdf+="xref\n0 7\n0000000000 65535 f \n";
        for(int offset:offsets)pdf+=QByteArray::number(offset).rightJustified(10,'0')+" 00000 n \n";
        pdf+="trailer << /Size 7 /Root 1 0 R >>\nstartxref\n"+QByteArray::number(xref)+"\n%%EOF\n";
        const auto path=dir.filePath(QStringLiteral("standard.pdf"));QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(pdf);file.close();
        MuPDFGenerator backend(nullptr,{});QList<Okular::Page*> pages;QString error;
        QCOMPARE(backend.loadDocumentWithPassword(path,pages,{}),Okular::Document::OpenSuccess);
        QCOMPARE(pages[0]->annotations()[0]->appearanceRotation(),270);
        for(int angle:{90,180,270,0}) {
            Okular::Page *next=nullptr;QVERIFY(backend.rotatePageInDocument(pages[0],0,angle,&next,&error));delete pages[0];pages[0]=next;
            QCOMPARE(next->annotations()[0]->appearanceRotation(),(angle+270)%360);
        }
        const auto saved = dir.filePath(QStringLiteral("existing-ap.pdf"));
        QVERIFY(backend.save(saved, Okular::SaveInterface::SaveChanges, &error));
        verifyFreeTextStorage(saved, true, 0);
        qDeleteAll(pages);
    }
    void layoutAxes()
    {
        const QSizeF page(600, 800);
        for (int angle : {0, 90, 180, 270}) {
            const Okular::NormalizedRect box(.2, .2, .4, .3);
            auto wider = box;
            wider.right += .1;
            auto taller = box;
            taller.bottom += .1;
            const auto original = Okular::AnnotationFrame::localSize(box, page, angle);
            const auto horizontal = Okular::AnnotationFrame::localSize(wider, page, angle) - original;
            const auto vertical = Okular::AnnotationFrame::localSize(taller, page, angle) - original;
            QVERIFY(qAbs(horizontal.width() - (angle % 180 ? 0 : 60)) < 1e-9);
            QVERIFY(qAbs(horizontal.height() - (angle % 180 ? 60 : 0)) < 1e-9);
            QVERIFY(qAbs(vertical.width() - (angle % 180 ? 80 : 0)) < 1e-9);
            QVERIFY(qAbs(vertical.height() - (angle % 180 ? 0 : 80)) < 1e-9);
        }
    }
    void independentRotation_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<int>("pageAngle");
        for (int kind : {-2, -1, 0, 1, 2, 3})
            for (int angle : {0, 90, 180, 270})
                QTest::newRow(qPrintable(QStringLiteral("%1-at-%2").arg(kind).arg(angle))) << kind << angle;
    }
    void independentRotation()
    {
        QFETCH(int, kind);
        QFETCH(int, pageAngle);
        QTemporaryDir dir;
        auto file = dir.filePath(QStringLiteral("page.pdf")), appearance = dir.filePath(QStringLiteral("appearance.pdf")), image = dir.filePath(QStringLiteral("image.png"));
        for (auto path : {file, appearance}) {
            QPdfWriter writer(path);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(path == file ? QSizeF(400, 300) : QSizeF(80, 40), QPageSize::Point));
            writer.setPageMargins(QMarginsF());
            QPainter p(&writer);
            p.fillRect(0, 0, 400, 300, path == file ? Qt::white : Qt::red);
            if (path == appearance)
                p.fillRect(40, 0, 40, 20, Qt::blue);
        }
        QImage bitmap(80, 40, QImage::Format_RGB32);
        bitmap.fill(Qt::red);
        {
            QPainter p(&bitmap);
            p.fillRect(40, 0, 40, 20, Qt::blue);
        }
        QVERIFY(bitmap.save(image));
        MuPDFGenerator backend(nullptr, {});
        QList<Okular::Page *> pages;
        QString error;
        QCOMPARE(backend.loadDocumentWithPassword(file, pages, {}), Okular::Document::OpenSuccess);
        Okular::Page *next = nullptr;
        QVERIFY(backend.rotatePageInDocument(pages[0], 0, pageAngle, &next, &error));
        delete pages[0];
        pages[0] = next;
        std::unique_ptr<Okular::Annotation> note;
        if (kind == -2) {
            auto *text = new Okular::TextAnnotation;
            text->setTextType(Okular::TextAnnotation::InPlace);
            text->setContents(QStringLiteral("Text baseline"));
            note.reset(text);
        } else {
            auto *stamp = new Okular::StampAnnotation;
            note.reset(stamp);
            if (kind == -1)
                stamp->setStampImagePath(image);
            else {
                stamp->setOkularLatex(true);
                stamp->setLatexNoteType(static_cast<Okular::Annotation::LatexNoteType>(kind));
                stamp->setLatexAppearancePdfFileName(appearance);
                stamp->setLatexPadding(0);
            }
            if (stamp->isLatexCallout()) {
                stamp->setLatexCalloutPoint({.2, .6}, 0);
                stamp->setLatexCalloutPoint({.25, .4}, 1);
                stamp->setLatexCalloutPoint({.3, .4}, 2);
            }
        }
        note->setBoundingRectangle({.3, .3, .6, .5});
        backend.annotationProxy()->notifyAddition(note.get(), 0);
        QVERIFY(backend.rotatePageInDocument(pages[0], 0, pageAngle, &next, &error));
        delete pages[0];
        pages[0] = next;
        auto *model = pages[0]->annotations()[0];
        const QSizeF pageSize(pages[0]->width(), pages[0]->height());
        const auto baseline = backend.renderToImage(0, 72, true);
        const auto originalRect = model->boundingRectangle();
        const auto originalSize = Okular::AnnotationFrame::localSize(originalRect, pageSize, model->appearanceRotation());
        // Embedded appearances must be sufficient for independent rotation.
        QVERIFY(QFile::remove(image));
        QVERIFY(QFile::remove(appearance));
        for (int delta : {90, -90}) {
            const auto before = backend.renderToImage(0, 72, true);
            const auto transform = Okular::AnnotationFrame::rotationAbout(model->boundingRectangle(), pageSize, delta);
            if (model->isLatexCallout())
                for (int i = 0; i < 3; ++i)
                    model->setLatexCalloutPoint(Okular::AnnotationFrame::mapPoint(model->latexCalloutPoint(i), pageSize, transform), i);
            model->setBoundingRectangle(Okular::AnnotationFrame::mapRect(model->boundingRectangle(), pageSize, transform));
            model->setAppearanceRotation(model->appearanceRotation() + delta);
            backend.annotationProxy()->notifyModification(model, 0, true);
            QImage expected(before.size(), before.format());
            expected.fill(Qt::white);
            {
                QPainter p(&expected);
                p.setTransform(transform);
                p.drawImage(0, 0, before);
            }
            QVERIFY(samePixels(backend.renderToImage(0, 72, true), expected));
            QCOMPARE(Okular::AnnotationFrame::localSize(model->boundingRectangle(), pageSize, model->appearanceRotation()), originalSize);
            auto saved = dir.filePath(QStringLiteral("rotated.pdf"));
            QVERIFY(backend.save(saved, Okular::SaveInterface::SaveChanges, &error));
            if (kind == -2)
                verifyFreeTextStorage(saved, false, (pageAngle - model->appearanceRotation() + 360) % 360);

            MuPDFGenerator reopened(nullptr, {});
            QList<Okular::Page *> fresh;
            QCOMPARE(reopened.loadDocumentWithPassword(saved, fresh, {}), Okular::Document::OpenSuccess);
            QCOMPARE(fresh[0]->annotations()[0]->appearanceRotation(), model->appearanceRotation());
            QVERIFY(samePixels(reopened.renderToImage(0, 72, true), expected));
            qDeleteAll(fresh);
        }
        QVERIFY(samePixels(backend.renderToImage(0, 72, true), baseline));
        qDeleteAll(pages);
    }
    void freeTextRotation_data()
    {
        QTest::addColumn<int>("initial");
        QTest::addColumn<bool>("fixed");
        QTest::addColumn<bool>("edge");
        QTest::newRow("existing-fixed") << 0 << true << false;
        QTest::newRow("near-edge") << 0 << false << true;
        for (int angle : {0, 90, 180, 270})
            QTest::newRow(qPrintable(QStringLiteral("new-at-%1").arg(angle))) << angle << false << false;
    }
    void freeTextRotation()
    {
        QTemporaryDir dir;
        QFETCH(int, initial);
        QFETCH(bool, fixed);
        QFETCH(bool, edge);
        const auto source = dir.filePath(QStringLiteral("text-page.pdf"));
        {
            QPdfWriter writer(source);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QSizeF(300, 300), QPageSize::Point));
            writer.setPageMargins(QMarginsF());
            QPainter p(&writer);
            p.fillRect(0, 0, 300, 300, Qt::white);
        }
        MuPDFGenerator backend(nullptr, {});
        QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(source, pages, {}), Okular::Document::OpenSuccess);
        QString initialError;
        Okular::Page *initialPage = nullptr;
        QVERIFY(backend.rotatePageInDocument(pages[0], 0, initial, &initialPage, &initialError));
        delete pages[0];
        pages[0] = initialPage;
        Okular::TextAnnotation note;
        note.setTextType(Okular::TextAnnotation::InPlace);
        note.setContents(QStringLiteral("Text box"));
        note.setBoundingRectangle(edge ? Okular::NormalizedRect(.65, .01, .95, .16) : Okular::NormalizedRect(.65, .15, .95, .3));
        if (fixed)
            note.setFlags(Okular::Annotation::FixedRotation); // Existing UI-created FreeText.
        backend.annotationProxy()->notifyAddition(&note, 0);
        const auto original = backend.renderToImage(0, 72, true);
        QVERIFY(original != backend.renderToImage(0, 72, false));
        QString error;
        Okular::Page *loaded = nullptr;
        QVERIFY(backend.rotatePageInDocument(pages[0], 0, initial, &loaded, &error));
        delete pages[0];
        pages[0] = loaded;
        for (int angle : {90, 180, 270, 0}) {
            Okular::Page *next = nullptr;
            QVERIFY(backend.rotatePageWithAnnotationsInDocument(pages[0], 0, angle, true, &next, &error));
            delete pages[0];
            pages[0] = next;
            QVERIFY(samePixels(backend.renderToImage(0, 72, true), original.transformed(QTransform().rotate((angle - initial + 360) % 360))));
            QCOMPARE(next->annotations().size(), 1);
        }
        const auto beforeFixed = backend.renderToImage(0, 72, true);
        auto *moving = pages[0]->annotations()[0];
        auto rect = moving->boundingRectangle();
        rect.left -= .02;
        rect.right -= .02;
        rect.top += .02;
        rect.bottom += .02;
        moving->setBoundingRectangle(rect);
        backend.annotationProxy()->notifyModification(moving, 0, false);
        QImage moved(beforeFixed.size(), beforeFixed.format());
        moved.fill(Qt::white);
        {
            QPainter painter(&moved);
            painter.drawImage(-6, 6, beforeFixed);
        }
        QVERIFY(samePixels(backend.renderToImage(0, 72, true), moved));
        const auto saved = dir.filePath(QStringLiteral("saved.pdf"));
        QVERIFY(backend.save(saved, Okular::SaveInterface::SaveChanges, &error));
        MuPDFGenerator reopened(nullptr, {});
        QList<Okular::Page *> fresh;
        QCOMPARE(reopened.loadDocumentWithPassword(saved, fresh, {}), Okular::Document::OpenSuccess);
        QVERIFY(samePixels(reopened.renderToImage(0, 72, true), moved));
        qDeleteAll(fresh);
        qDeleteAll(pages);
    }
    void noRotateHitGeometry()
    {
        QTemporaryDir dir;
        const auto source = dir.filePath(QStringLiteral("page.pdf"));
        const auto appearance = dir.filePath(QStringLiteral("appearance.pdf"));
        for (const auto &file : {source, appearance}) {
            QPdfWriter writer(file);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(file == source ? QSizeF(300, 300) : QSizeF(60, 30), QPageSize::Point));
            writer.setPageMargins(QMarginsF());
            QPainter painter(&writer);
            painter.fillRect(0, 0, 300, 300, file == source ? Qt::white : Qt::red);
        }
        QImage stampImage(60, 30, QImage::Format_RGB32);
        stampImage.fill(Qt::red);
        const auto imagePath = dir.filePath(QStringLiteral("stamp.png"));
        QVERIFY(stampImage.save(imagePath));
        for (int kind : {-1, 0, 1, 2, 3}) {
            MuPDFGenerator backend(nullptr, {});
            QList<Okular::Page *> pages;
            QCOMPARE(backend.loadDocumentWithPassword(source, pages, {}), Okular::Document::OpenSuccess);
            Okular::StampAnnotation note;
            note.setBoundingRectangle({.4, .4, .6, .5});
            note.setOkularLatex(kind >= 0);
            if (kind < 0)
                note.setStampImagePath(imagePath);
            note.setLatexNoteType(static_cast<Okular::Annotation::LatexNoteType>(kind));
            note.setLatexPadding(0);
            note.style().setWidth(0);
            note.setLatexAppearancePdfFileName(appearance);
            if (note.isLatexCallout()) {
                note.setLatexCalloutPoint({.35, .55}, 0);
                note.setLatexCalloutPoint({.35, .45}, 1);
                note.setLatexCalloutPoint({.4, .45}, 2);
            }
            if (note.isNumberedCallout())
                note.setOrderedCalloutNumber(1);
            note.setFlags(note.flags() | Okular::Annotation::FixedRotation);
            backend.annotationProxy()->notifyAddition(&note, 0);
            QString error;
            for (int angle : {90, 180, 270, 0}) {
                Okular::Page *next = nullptr;
                QVERIFY(backend.rotatePageInDocument(pages[0], 0, angle, &next, &error));
                delete pages[0];
                pages[0] = next;
                const auto rendered = backend.renderToImage(0, 72, true);
                int redPixels = 0, missed = 0;
                for (int y = 0; y < rendered.height(); ++y)
                    for (int x = 0; x < rendered.width(); ++x) {
                        const auto color = rendered.pixelColor(x, y);
                        if (color.red() < 250 || color.green() > 5 || color.blue() > 5)
                            continue;
                        ++redPixels;
                        bool hit = false;
                        for (const auto *rect : next->objectRects())
                            if (rect->objectType() == Okular::ObjectRect::OAnnotation)
                                hit |= rect->contains(double(x) / 300, double(y) / 300, 300, 300);
                        if (!hit)
                            ++missed;
                    }
                QVERIFY(redPixels > 1500);
                QCOMPARE(missed, 0);
                const auto saved = dir.filePath(QStringLiteral("saved.pdf"));
                QVERIFY(backend.save(saved, Okular::SaveInterface::SaveChanges, &error));
                MuPDFGenerator reopened(nullptr, {});
                QList<Okular::Page *> fresh;
                QCOMPARE(reopened.loadDocumentWithPassword(saved, fresh, {}), Okular::Document::OpenSuccess);
                QCOMPARE(reopened.renderToImage(0, 72, true), rendered);
                QCOMPARE(fresh[0]->annotations()[0]->boundingRectangle(), next->annotations()[0]->boundingRectangle());
                qDeleteAll(fresh);
                // Changing the operation policy alone must not move or turn APs.
                for (bool follow : {true, false}) {
                    Okular::Page *converted = nullptr;
                    QVERIFY(backend.rotatePageWithAnnotationsInDocument(pages[0], 0, angle, follow, &converted, &error));
                    delete pages[0];
                    pages[0] = converted;
                    QVERIFY(samePixels(backend.renderToImage(0, 72, true), rendered));
                }
                Okular::Page *turned = nullptr;
                QVERIFY(backend.rotatePageWithAnnotationsInDocument(pages[0], 0, (angle + 90) % 360, true, &turned, &error));
                delete pages[0];
                pages[0] = turned;
                QVERIFY(samePixels(backend.renderToImage(0, 72, true), rendered.transformed(QTransform().rotate(90))));
                QVERIFY(backend.rotatePageWithAnnotationsInDocument(pages[0], 0, angle, true, &turned, &error));
                delete pages[0];
                pages[0] = turned;
                QVERIFY(samePixels(backend.renderToImage(0, 72, true), rendered));
                for (int step = 0; step <= 4; ++step) {
                    QVERIFY(backend.rotatePageWithAnnotationsInDocument(pages[0], 0, (angle + step * 90) % 360, false, &turned, &error));
                    delete pages[0];
                    pages[0] = turned;
                }
                QVERIFY(samePixels(backend.renderToImage(0, 72, true), rendered));
            }
            const auto beforeMove = backend.renderToImage(0, 72, true);
            auto *moving = pages[0]->annotations()[0];
            auto r = moving->boundingRectangle();
            r.left += .02;
            r.right += .02;
            r.top += .02;
            r.bottom += .02;
            moving->setBoundingRectangle(r);
            if (moving->isLatexCallout())
                for (int i = 0; i < 3; ++i) {
                    auto p = moving->latexCalloutPoint(i);
                    p.x += .02;
                    p.y += .02;
                    moving->setLatexCalloutPoint(p, i);
                }
            backend.annotationProxy()->notifyModification(moving, 0, false);
            QImage moved(beforeMove.size(), beforeMove.format());
            moved.fill(Qt::white);
            {
                QPainter painter(&moved);
                painter.drawImage(6, 6, beforeMove);
            }
            QVERIFY(samePixels(backend.renderToImage(0, 72, true), moved));
            qDeleteAll(pages);
        }
    }
    void rotation_data()
    {
        QTest::addColumn<int>("initial");
        QTest::addColumn<int>("kind");
        for (int initial : {0, 90, 180, 270})
            for (int kind : {-1, 0, 1, 2, 3})
                QTest::newRow(qPrintable(QStringLiteral("%1-kind-%2").arg(initial).arg(kind))) << initial << kind;
    }
    void rotation()
    {
        QFETCH(int, initial);
        QFETCH(int, kind);
        const bool image = kind == -1;
        QTemporaryDir dir;
        const auto source = dir.filePath(QStringLiteral("source.pdf"));
        const auto appearance = dir.filePath(QStringLiteral("appearance.pdf"));
        for (const auto &file : {source, appearance}) {
            QPdfWriter writer(file);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QSizeF(file == appearance ? 120 : 300, file == appearance ? 60 : 300), QPageSize::Point));
            writer.setPageMargins(QMarginsF());
            QPainter p(&writer);
            p.fillRect(0, 0, 300, 300, Qt::white);
            if (file == appearance) {
                p.fillRect(0, 0, 60, 30, Qt::red);
                p.fillRect(60, 30, 60, 30, Qt::blue);
            }
        }
        QImage bitmap(300, 150, QImage::Format_RGB32);
        bitmap.fill(Qt::white);
        {
            QPainter p(&bitmap);
            p.fillRect(0, 0, 150, 75, Qt::red);
            p.fillRect(150, 75, 150, 75, Qt::blue);
        }
        const auto imageFile = dir.filePath(QStringLiteral("image.png"));
        QVERIFY(bitmap.save(imageFile));
        MuPDFGenerator backend(nullptr, {});
        QList<Okular::Page *> pages;
        QCOMPARE(backend.loadDocumentWithPassword(source, pages, {}), Okular::Document::OpenSuccess);
        QString error;
        // Mirror the UI's relative left/right actions, which derive their target
        // from Page::orientation(), rather than supplying known absolute angles.
        const auto original = backend.renderToImage(0, 72, true);
        for (int delta : {-90, 90, 90, -90, 90, 90, 90, 90}) {
            const int target = (int(pages[0]->orientation()) * 90 + delta + 360) % 360;
            Okular::Page *next = nullptr;
            QVERIFY(backend.rotatePageInDocument(pages[0], 0, target, &next, &error));
            delete pages[0];
            pages[0] = next;
            QCOMPARE(int(next->orientation()) * 90, target);
        }
        QCOMPARE(int(pages[0]->orientation()), 0);
        QCOMPARE(backend.renderToImage(0, 72, true), original);
        Okular::Page *replacement = nullptr;
        QVERIFY(backend.rotatePageInDocument(pages[0], 0, initial, &replacement, &error));
        delete pages[0];
        pages[0] = replacement;
        QVERIFY2(backend.createInternalLink(pages[0], .7, .05, .9, .15, {}, 1, 0, 0, &error), qPrintable(error));
        Okular::StampAnnotation note;
        note.setBoundingRectangle({.1, .2, .5, .4});
        if (image)
            note.setStampImagePath(imageFile);
        else {
            note.setOkularLatex(true);
            note.setLatexNoteType(static_cast<Okular::Annotation::LatexNoteType>(kind));
            note.setLatexPadding(0);
            note.style().setWidth(0);
            note.setLatexAppearancePdfFileName(appearance);
        }
        if (note.isLatexCallout()) {
            note.setLatexCalloutPoint({.7, .7}, 0);
            note.setLatexCalloutPoint({.6, .5}, 1);
            note.setLatexCalloutPoint({.5, .3}, 2);
        }
        if (note.isNumberedCallout())
            note.setOrderedCalloutNumber(1);
        backend.annotationProxy()->notifyAddition(&note, 0);
        const auto upright = backend.renderToImage(0, 72, true);
        QCOMPARE(upright.pixelColor(50, 70), QColor(Qt::red));
        QCOMPARE(upright.pixelColor(130, 110), QColor(Qt::blue));
        QVERIFY(backend.rotatePageInDocument(pages[0], 0, (initial + 90) % 360, &replacement, &error));
        delete pages[0];
        pages[0] = replacement;
        bool linkHit = false, noteHit = false;
        for (const auto *rect : pages[0]->objectRects()) {
            if (rect->objectType() == Okular::ObjectRect::Action)
                linkHit |= rect->contains(.9, .8, 300, 300);
            if (rect->objectType() == Okular::ObjectRect::OAnnotation)
                noteHit |= rect->contains(230.0 / 300, 50.0 / 300, 300, 300);
        }
        QVERIFY2(noteHit, "Rotated appearance must be selectable where it is painted");
        QVERIFY2(linkHit, "Rotated link must be clickable where it is painted");
        const int rotatedOrientation = int(pages[0]->orientation()) * 90;
        QCOMPARE(rotatedOrientation, (initial + 90) % 360);
        auto expected = upright.transformed(QTransform().rotate(90));
        QVERIFY(samePixels(backend.renderToImage(0, 72, true), expected));
        const int reverseTarget = (int(pages[0]->orientation()) * 90 + 270) % 360;
        QVERIFY(backend.rotatePageInDocument(pages[0], 0, reverseTarget, &replacement, &error));
        delete pages[0];
        pages[0] = replacement;
        QCOMPARE(int(pages[0]->orientation()) * 90, initial);
        QVERIFY(samePixels(backend.renderToImage(0, 72, true), upright));
        QVERIFY(backend.rotatePageInDocument(pages[0], 0, (int(pages[0]->orientation()) * 90 + 90) % 360, &replacement, &error));
        delete pages[0];
        pages[0] = replacement;
        // Moving after rotation must retain the original AP orientation.
        auto *moved = pages[0]->annotations()[0];
        auto rect = moved->boundingRectangle();
        rect.left += .05;
        rect.right += .05;
        rect.top += .05;
        rect.bottom += .05;
        moved->setBoundingRectangle(rect);
        if (moved->isLatexCallout())
            for (int i = 0; i < 3; ++i) {
                auto point = moved->latexCalloutPoint(i);
                point.x += .05;
                point.y += .05;
                moved->setLatexCalloutPoint(point, i);
            }
        backend.annotationProxy()->notifyModification(moved, 0, false);
        QImage translated(expected.size(), expected.format());
        translated.fill(Qt::white);
        {
            QPainter p(&translated);
            p.drawImage(15, 15, expected);
        }
        QVERIFY(samePixels(backend.renderToImage(0, 72, true), translated));
        expected = translated;
        const auto saved = dir.filePath(QStringLiteral("saved.pdf"));
        QVERIFY2(backend.save(saved, Okular::SaveInterface::SaveChanges, &error), qPrintable(error));
        // The reader-only rotation must also define the new note's orientation.
        Okular::SettingsCore::instance(QStringLiteral("mupdf-latex-rotation-test"));
        Okular::Document document(nullptr);
        QCOMPARE(document.openDocument(source, QUrl::fromLocalFile(source), QMimeDatabase().mimeTypeForName(QStringLiteral("application/pdf"))), Okular::Document::OpenSuccess);
        document.setRotation(initial / 90);
        auto *viewNote = new Okular::StampAnnotation;
        viewNote->setBoundingRectangle({.1, .2, .5, .4});
        if (image)
            viewNote->setStampImagePath(imageFile);
        else {
            viewNote->setOkularLatex(true);
            viewNote->setLatexNoteType(static_cast<Okular::Annotation::LatexNoteType>(kind));
            viewNote->setLatexPadding(0);
            viewNote->style().setWidth(0);
            viewNote->setLatexAppearancePdfFileName(appearance);
            if (viewNote->isLatexCallout()) {
                viewNote->setLatexCalloutPoint({.7, .7}, 0);
                viewNote->setLatexCalloutPoint({.6, .5}, 1);
                viewNote->setLatexCalloutPoint({.5, .3}, 2);
            }
        }
        document.addPageAnnotation(0, viewNote);
        const auto viewImage = document.renderToImage(0, 72, true, &error).transformed(QTransform().rotate(initial));
        QCOMPARE(viewImage.pixelColor(50, 70), QColor(Qt::red));
        QCOMPARE(viewImage.pixelColor(130, 110), QColor(Qt::blue));
        document.closeDocument();
        QVERIFY(QFile::remove(appearance));
        QVERIFY(QFile::remove(imageFile));
        MuPDFGenerator reopened(nullptr, {});
        QList<Okular::Page *> fresh;
        QCOMPARE(reopened.loadDocumentWithPassword(saved, fresh, {}), Okular::Document::OpenSuccess);
        QCOMPARE(int(fresh[0]->orientation()) * 90, (initial + 90) % 360);
        QVERIFY(samePixels(reopened.renderToImage(0, 72, true), expected));
        auto *embedded = fresh[0]->annotations()[0];
        auto embeddedRect = embedded->boundingRectangle();
        embeddedRect.left += .05;
        embeddedRect.right += .05;
        embeddedRect.top += .05;
        embeddedRect.bottom += .05;
        embedded->setBoundingRectangle(embeddedRect);
        if (embedded->isLatexCallout())
            for (int i = 0; i < 3; ++i) {
                auto point = embedded->latexCalloutPoint(i);
                point.x += .05;
                point.y += .05;
                embedded->setLatexCalloutPoint(point, i);
            }
        reopened.annotationProxy()->notifyModification(embedded, 0, false);
        QImage embeddedExpected(expected.size(), expected.format());
        embeddedExpected.fill(Qt::white);
        {
            QPainter p(&embeddedExpected);
            p.drawImage(15, 15, expected);
        }
        QVERIFY(samePixels(reopened.renderToImage(0, 72, true), embeddedExpected));
        qDeleteAll(fresh);
        qDeleteAll(pages);
    }
};
QTEST_MAIN(LatexRotationTest)
#include "latexrotationtest.moc"
