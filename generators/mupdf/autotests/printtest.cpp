// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../generator_mupdf.h"
#include <core/page.h>
#include <core/printjob.h>
#include <QPdfWriter>
#include <QPainter>
#include <QFile>
#include <QPrinter>
#include <QTemporaryDir>
#include <QTest>

class PrintTest : public QObject
{
    Q_OBJECT
    static void fixture(const QString &file, int rotation, bool composite, bool image)
    {
        const QByteArray content = "q 10 10 180 130 re W n 0 0 1 RG 2 w [4 2] 0 d 0 20 m 210 130 l S Q\n"
            "BT /F1 18 Tf 20 90 Td (Vector print 123) Tj ET\n"
            "0 0.7 0 rg 25 25 60 30 re f\n" + QByteArray(image ? "q 40 0 0 30 125 20 cm /Im Do Q\n" : "") + QByteArray(composite ? "/GS gs 1 0 0 rg 55 40 40 30 re f\n" : "");
        QList<QByteArray> objects{
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 150] /Rotate " + QByteArray::number(rotation)
                + " /Resources << /XObject << /Im 7 0 R >> /Font << /F1 5 0 R >> /ExtGState << /GS << /ca 0.5 /BM /Multiply >> >> >> /Contents 4 0 R /Annots [6 0 R 8 0 R] >>",
            "<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content + "endstream",
            "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
            "<< /Type /Annot /Subtype /Link /Rect [10 5 190 18] /F 4 /Border [0 0 2] /C [1 0 0] /A << /S /URI /URI (https://example.com) >> >>",
            "<< /Type /XObject /Subtype /Image /Width 2 /Height 2 /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /ASCIIHexDecode /Length 25 >>\nstream\nFF000000FF000000FFFFFFFF>\nendstream",
            "<< /Type /Annot /Subtype /FreeText /Rect [100 45 180 70] /F 4 /DA (/Helvetica 10 Tf 0 g) /Contents (Annotation) >>"
        };
        QByteArray data="%PDF-1.7\n";
        QList<int> offsets{0};
        for(int i=0;i<objects.size();++i) { offsets<<data.size(); data+=QByteArray::number(i+1)+" 0 obj\n"+objects[i]+"\nendobj\n"; }
        const int xref=data.size();
        data+="xref\n0 "+QByteArray::number(offsets.size())+"\n0000000000 65535 f \n";
        for(int i=1;i<offsets.size();++i) data+=QByteArray::number(offsets[i]).rightJustified(10,'0')+" 00000 n \n";
        data+="trailer\n<< /Size "+QByteArray::number(offsets.size())+" /Root 1 0 R >>\nstartxref\n"+QByteArray::number(xref)+"\n%%EOF\n";
        QFile output(file); QVERIFY(output.open(QIODevice::WriteOnly)); QCOMPARE(output.write(data),data.size());
    }
private Q_SLOTS:
    void mixedPaperOrientation_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("reverse");
        for (int mode : {0,1,2}) for (bool reverse : {false,true})
            QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(mode).arg(reverse))) << mode << reverse;
    }
    void mixedPaperOrientation()
    {
        QFETCH(int, mode); QFETCH(bool, reverse);
        QTemporaryDir dir;
        const auto original = dir.filePath(QStringLiteral("original.pdf"));
        const auto source = dir.filePath(QStringLiteral("source.pdf"));
        const auto printed = dir.filePath(QStringLiteral("printed.pdf"));
        {
            QPdfWriter writer(original);
            writer.setPageSize(QPageSize(QPageSize::A4));
            QPainter painter(&writer);
            painter.drawText(100, 100, QStringLiteral("Portrait"));
            writer.setPageOrientation(QPageLayout::Landscape);
            QVERIFY(writer.newPage());
            painter.drawText(100, 100, QStringLiteral("Landscape"));
            writer.setPageOrientation(QPageLayout::Portrait);
            QVERIFY(writer.newPage());
            painter.drawText(100, 100, QStringLiteral("Rotated portrait"));
        }
        auto *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
        auto *pdf = pdf_open_document(ctx, original.toUtf8().constData());
        pdf_dict_put_int(ctx, pdf_lookup_page_obj(ctx, pdf, 2), PDF_NAME(Rotate), 90);
        pdf_save_document(ctx, pdf, source.toUtf8().constData(), &pdf_default_write_options);
        pdf_drop_document(ctx, pdf); fz_drop_context(ctx);
        MuPDFGenerator input(nullptr, {}); QList<Okular::Page *> pages;
        QCOMPARE(input.loadDocumentWithPassword(source, pages, {}), Okular::Document::OpenSuccess);
        Okular::PrintJobPrinter printer;
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(printed);
        printer.setPageSize(QPageSize(QPageSize::A4));
        printer.setPageOrientation(mode == 2 ? QPageLayout::Landscape : QPageLayout::Portrait);
        printer.automaticPageOrientation = mode == 0;
        printer.setPageOrder(reverse ? QPrinter::LastPageFirst : QPrinter::FirstPageFirst);
        QCOMPARE(input.print(printer), Okular::Document::NoPrintError);
        MuPDFGenerator output(nullptr, {}); QList<Okular::Page *> result;
        QCOMPARE(output.loadDocumentWithPassword(printed, result, {}), Okular::Document::OpenSuccess);
        QCOMPARE(result.size(), 3);
        for (int i=0; i<3; ++i) {
            const int sourcePage = reverse ? 2-i : i;
            const bool landscape = mode == 0 ? sourcePage != 0 : mode == 2;
            QCOMPARE(result[i]->width() > result[i]->height(), landscape);
        }
        qDeleteAll(result); qDeleteAll(pages);
    }
    void vectorOutput_data()
    {
        QTest::addColumn<int>("rotation"); QTest::addColumn<bool>("composite"); QTest::addColumn<bool>("image");
        for(int r:{0,90,180,270}) for(bool c:{false,true}) for(bool im:{false,true})
            QTest::newRow(qPrintable(QStringLiteral("%1-%2-%3").arg(r).arg(c).arg(im)))<<r<<c<<im;
    }
    void vectorOutput()
    {
        QFETCH(int,rotation); QFETCH(bool,composite); QFETCH(bool,image);
        QTemporaryDir dir;
        const auto source=dir.filePath(QStringLiteral("source.pdf")), printed=dir.filePath(QStringLiteral("printed.pdf"));
        fixture(source,rotation,composite,image);
        MuPDFGenerator input(nullptr,{}); QList<Okular::Page *> pages;
        QCOMPARE(input.loadDocumentWithPassword(source,pages,{}),Okular::Document::OpenSuccess);
        const auto expected=input.renderToImage(0,144,true);
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat); printer.setOutputFileName(printed);
        printer.setResolution(600); printer.setFullPage(true);
        printer.setPageSize(QPageSize(rotation%180?QSizeF(150,200):QSizeF(200,150),QPageSize::Point));
        QCOMPARE(input.print(printer),Okular::Document::NoPrintError);
        MuPDFGenerator output(nullptr,{}); QList<Okular::Page *> outputPages;
        QCOMPARE(output.loadDocumentWithPassword(printed,outputPages,{}),Okular::Document::OpenSuccess);
        const auto actual=output.renderToImage(0,144,true);
        QCOMPARE(actual.size(),expected.size());
        quint64 difference=0;
        for(int y=0;y<actual.height();++y) for(int x=0;x<actual.width();++x) {
            const auto a=actual.pixelColor(x,y),b=expected.pixelColor(x,y);
            difference+=qAbs(a.red()-b.red())+qAbs(a.green()-b.green())+qAbs(a.blue()-b.blue());
        }
        const double mean=double(difference)/(actual.width()*actual.height()*3);
        QVERIFY2(mean<3,qPrintable(QStringLiteral("Mean pixel difference %1").arg(mean)));
        QFile pdf(printed); QVERIFY(pdf.open(QIODevice::ReadOnly)); const auto bytes=pdf.readAll();
        if(!composite && !image) QVERIFY2(!bytes.contains("/Subtype /Image"),"Vector-only printing must not create image XObjects");
        // Preserve the most recent artifacts for manual inspection.
        if(rotation==0 && !image && !qEnvironmentVariableIsEmpty("MENGSHEE_PRINT_ARTIFACTS")) {
            const auto prefix=qEnvironmentVariable("MENGSHEE_PRINT_ARTIFACTS")+QStringLiteral("/print-%1").arg(composite?"composite":"vector");
            QFile::remove(prefix+QStringLiteral(".pdf")); QFile::copy(printed,prefix+QStringLiteral(".pdf"));
            expected.save(prefix+QStringLiteral("-expected.png")); actual.save(prefix+QStringLiteral("-actual.png"));
        }
        output.closeDocument(); qDeleteAll(outputPages);
        input.closeDocument(); qDeleteAll(pages);
    }
};
QTEST_MAIN(PrintTest)
#include "printtest.moc"
