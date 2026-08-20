/*
    SPDX-FileCopyrightText: 2013 Peter Grasch <me@bedahr.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QClipboard>
#include <QDomDocument>
#include <QMimeData>
#include <QMimeDatabase>
#include <QTest>

#include "../core/annotations.h"
#include "../core/document.h"
#include "../core/page.h"
#include "../part/annotationpopup.h"
#include "../settings_core.h"

namespace
{
void simulateNativeClipboardSerialization()
{
    const QMimeData *source = QApplication::clipboard()->mimeData();
    QVERIFY(source);

    auto *serialized = new QMimeData();
    const QStringList formats = source->formats();
    for (const QString &format : formats) {
        serialized->setData(format, source->data(format));
    }
    QApplication::clipboard()->setMimeData(serialized);
}
}

class AnnotationClipboardTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void testCopy();
    void testCopyPaste();
    void testCopyPasteWrongVersion();
    void testClipboardMimetype();
    void testStampCopyEligibility();
    void testCopyPasteStamp();
    void testCopyPasteTextCallout();
    void testCopyPasteLatex_data();
    void testCopyPasteLatex();
    void cleanup();
    void cleanupTestCase();

private:
    Okular::Document *m_document;
};

void AnnotationClipboardTest::initTestCase()
{
    Okular::SettingsCore::instance(QStringLiteral("annotationclipboardtest"));
    m_document = new Okular::Document(nullptr);
    const QString testFile = QStringLiteral(KDESRCDIR "data/file1.pdf");
    QMimeDatabase db;
    const QMimeType mime = db.mimeTypeForFile(testFile);
    QCOMPARE(m_document->openDocument(testFile, QUrl(), mime), Okular::Document::OpenSuccess);
}

void AnnotationClipboardTest::cleanup()
{
    const QList<Okular::Annotation *> annotations = m_document->page(0)->annotations();
    for (Okular::Annotation *annotation : annotations) {
        m_document->removePageAnnotation(0, annotation);
    }
}

void AnnotationClipboardTest::cleanupTestCase()
{
    delete m_document;
}

void AnnotationClipboardTest::testCopy()
{
    Okular::TextAnnotation *ta = new Okular::TextAnnotation();
    ta->setFlags(ta->flags() | Okular::Annotation::FixedRotation);
    ta->setTextType(Okular::TextAnnotation::InPlace);
    ta->setInplaceIntent(Okular::TextAnnotation::TypeWriter);
    ta->style().setWidth(0.0);
    ta->style().setColor(QColor(255, 255, 255, 0));
    ta->setBoundingRectangle(Okular::NormalizedRect(0.1, 0.1, 0.5, 0.5));
    ta->setContents(QStringLiteral("annot contents"));

    AnnotationPopup popup(m_document, AnnotationPopup::SingleAnnotationMode);
    popup.addAnnotation(ta, 0);
    popup.doCopyAnnotation({ta, 0});

    const QMimeData *clipData = QApplication::clipboard()->mimeData();
    QVERIFY(clipData && clipData->hasFormat(QLatin1String(AnnotationPopup::annotationClipboardMimeType)));

    QDomDocument document;
    document.setContent(clipData->data(QLatin1String(AnnotationPopup::annotationClipboardMimeType)));
    const QDomElement root = document.documentElement();

    QCOMPARE(root.tagName(), QStringLiteral("annotations"));
    QVERIFY(root.hasAttribute(QStringLiteral("version")));
    QCOMPARE(root.attribute(QStringLiteral("version")).toInt(), AnnotationPopup::annotationClipboardFormatVersion);

    const QDomElement annEl = root.firstChildElement(QStringLiteral("annotation"));
    QVERIFY(!annEl.isNull());
    QCOMPARE(annEl.attribute(QStringLiteral("type")).toInt(), (int)Okular::Annotation::AText);

    delete ta;
}

void AnnotationClipboardTest::testCopyPaste()
{
    // Copy a known annotation to the clipboard
    Okular::TextAnnotation *original = new Okular::TextAnnotation();
    original->setFlags(original->flags() | Okular::Annotation::FixedRotation);
    original->setTextType(Okular::TextAnnotation::InPlace);
    original->setInplaceIntent(Okular::TextAnnotation::TypeWriter);
    original->setBoundingRectangle(Okular::NormalizedRect(0.1, 0.1, 0.5, 0.5));
    original->setContents(QStringLiteral("test contents"));

    AnnotationPopup popup(m_document, AnnotationPopup::SingleAnnotationMode);
    popup.addAnnotation(original, 0);
    popup.doCopyAnnotation({original, 0});
    QVERIFY(AnnotationPopup::clipboardHasAnnotations());

    // Paste onto page 0 and verify the annotation was deserialized correctly
    popup.pasteAnnotationToPage(0);

    const QList<Okular::Annotation *> annotations = m_document->page(0)->annotations();
    QCOMPARE(annotations.size(), 1);

    const auto *loaded = dynamic_cast<const Okular::TextAnnotation *>(annotations.first());
    QVERIFY(loaded != nullptr);
    QCOMPARE(loaded->contents(), QStringLiteral("test contents"));
    QCOMPARE(loaded->textType(), Okular::TextAnnotation::InPlace);
    QCOMPARE(loaded->inplaceIntent(), Okular::TextAnnotation::TypeWriter);

    delete original;
}

void AnnotationClipboardTest::testCopyPasteWrongVersion()
{
    // Build clipboard data with an unsupported version
    Okular::TextAnnotation ta;
    ta.setBoundingRectangle(Okular::NormalizedRect(0.1, 0.1, 0.5, 0.5));
    ta.setContents(QStringLiteral("should not be pasted"));

    QDomDocument document(QStringLiteral("okular-annotations"));
    QDomElement root = document.createElement(QStringLiteral("annotations"));
    root.setAttribute(QStringLiteral("version"), AnnotationPopup::annotationClipboardFormatVersion + 1);
    document.appendChild(root);
    QDomElement annotationElement = document.createElement(QStringLiteral("annotation"));
    Okular::AnnotationUtils::storeAnnotation(&ta, annotationElement, document);
    root.appendChild(annotationElement);

    auto *mimeData = new QMimeData();
    mimeData->setData(QLatin1String(AnnotationPopup::annotationClipboardMimeType), document.toByteArray());
    QApplication::clipboard()->setMimeData(mimeData);

    AnnotationPopup popup(m_document, AnnotationPopup::SingleAnnotationMode);
    popup.pasteAnnotationToPage(0);

    // Version mismatch → nothing should have been pasted
    QCOMPARE(m_document->page(0)->annotations().size(), 0);
}

void AnnotationClipboardTest::testClipboardMimetype()
{
    Okular::TextAnnotation *ta = new Okular::TextAnnotation();
    ta->setTextType(Okular::TextAnnotation::InPlace);
    ta->setBoundingRectangle(Okular::NormalizedRect(0.1, 0.1, 0.5, 0.5));
    ta->setContents(QStringLiteral("annot contents"));

    AnnotationPopup popup(m_document, AnnotationPopup::SingleAnnotationMode);
    popup.addAnnotation(ta, 0);
    popup.doCopyAnnotation({ta, 0});

    const QMimeData *clipData = QApplication::clipboard()->mimeData();
    QVERIFY(clipData && clipData->hasFormat(QLatin1String(AnnotationPopup::annotationClipboardMimeType)));

    delete ta;
}

void AnnotationClipboardTest::testStampCopyEligibility()
{
    Okular::StampAnnotation stamp;
    QVERIFY(AnnotationPopup::annotationSupportsCopy(&stamp));
}

void AnnotationClipboardTest::testCopyPasteStamp()
{
    auto *original = new Okular::StampAnnotation();
    original->setStampIconName(QStringLiteral("Approved"));
    original->setBoundingRectangle(Okular::NormalizedRect(0.1, 0.1, 0.3, 0.2));
    m_document->addPageAnnotation(0, original);
    QVERIFY(m_document->annotationAppearance(original));

    AnnotationPopup popup(m_document, AnnotationPopup::SingleAnnotationMode);
    popup.addAnnotation(original, 0);
    popup.doCopyAnnotation({original, 0});
    simulateNativeClipboardSerialization();
    popup.pasteAnnotationToPage(0);

    const QList<Okular::Annotation *> annotations = m_document->page(0)->annotations();
    QCOMPARE(annotations.size(), 2);
    const auto *loaded = dynamic_cast<const Okular::StampAnnotation *>(annotations.constLast());
    QVERIFY(loaded);
    QVERIFY(loaded != original);
    QCOMPARE(loaded->stampIconName(), original->stampIconName());
    QVERIFY(m_document->annotationAppearance(loaded));
}

void AnnotationClipboardTest::testCopyPasteTextCallout()
{
    auto *original = new Okular::TextAnnotation();
    original->setTextType(Okular::TextAnnotation::InPlace);
    original->setInplaceIntent(Okular::TextAnnotation::Callout);
    original->setBoundingRectangle(Okular::NormalizedRect(0.4, 0.4, 0.6, 0.6));
    original->setInplaceCallout(Okular::NormalizedPoint(0.1, 0.1), 0);
    original->setInplaceCallout(Okular::NormalizedPoint(0.2, 0.2), 1);
    original->setInplaceCallout(Okular::NormalizedPoint(0.5, 0.4), 2);
    m_document->addPageAnnotation(0, original);

    AnnotationPopup popup(m_document, AnnotationPopup::SingleAnnotationMode);
    popup.addAnnotation(original, 0);
    popup.doCopyAnnotation({original, 0});
    popup.pasteAnnotationToPage(0);

    const QList<Okular::Annotation *> annotations = m_document->page(0)->annotations();
    QCOMPARE(annotations.size(), 2);
    const auto *loaded = dynamic_cast<const Okular::TextAnnotation *>(annotations.constLast());
    QVERIFY(loaded);
    QCOMPARE(loaded->boundingRectangle().left, original->boundingRectangle().left + 0.02);
    QCOMPARE(loaded->boundingRectangle().top, original->boundingRectangle().top + 0.02);
    for (int index = 0; index < 3; ++index) {
        QCOMPARE(loaded->inplaceCallout(index).x, original->inplaceCallout(index).x + 0.02);
        QCOMPARE(loaded->inplaceCallout(index).y, original->inplaceCallout(index).y + 0.02);
    }
}

void AnnotationClipboardTest::testCopyPasteLatex_data()
{
    QTest::addColumn<int>("noteType");

    QTest::newRow("plain") << int(Okular::Annotation::LatexNotePlain);
    QTest::newRow("boxed") << int(Okular::Annotation::LatexNoteBoxed);
    QTest::newRow("callout") << int(Okular::Annotation::LatexNoteCallout);
}

void AnnotationClipboardTest::testCopyPasteLatex()
{
    QFETCH(int, noteType);
    const auto latexNoteType = static_cast<Okular::Annotation::LatexNoteType>(noteType);
    const QString appearanceFileName = QStringLiteral(KDESRCDIR "data/file1.pdf");

    auto *original = new Okular::StampAnnotation();
    original->setOkularLatex(true);
    original->setLatexNoteType(latexNoteType);
    original->setStampIconName(QStringLiteral("latex-notes"));
    original->setContents(QStringLiteral("\\frac{a}{b}"));
    original->setBoundingRectangle(Okular::NormalizedRect(0.1, 0.1, 0.4, 0.3));
    original->setLatexLayoutWidth(144.0);
    original->setLatexPadding(3.5);
    original->setLatexFontSize(11.0);
    original->setLatexTextColor(QColor(QStringLiteral("#ff123456")));
    original->setLatexFillColor(QColor(QStringLiteral("#80112233")));
    original->setLatexBorderColor(QColor(QStringLiteral("#ff445566")));
    original->setLatexAppearancePdfFileName(appearanceFileName);
    if (latexNoteType == Okular::Annotation::LatexNoteCallout) {
        original->setLatexCalloutPoint(Okular::NormalizedPoint(0.05, 0.05), 0);
        original->setLatexCalloutPoint(Okular::NormalizedPoint(0.1, 0.1), 1);
        original->setLatexCalloutPoint(Okular::NormalizedPoint(0.2, 0.2), 2);
    }

    // The clipboard must copy the appearance already held by Poppler, not
    // reopen this renderer-side source file.
    m_document->addPageAnnotation(0, original);
    QVERIFY(m_document->annotationAppearance(original));
    original->setLatexAppearancePdfFileName(QString());

    AnnotationPopup popup(m_document, AnnotationPopup::SingleAnnotationMode);
    popup.addAnnotation(original, 0);
    popup.doCopyAnnotation({original, 0});

    const QMimeData *clipData = QApplication::clipboard()->mimeData();
    QVERIFY(clipData);
    QVERIFY(clipData->hasFormat(QLatin1String(AnnotationPopup::annotationClipboardMimeType)));

    simulateNativeClipboardSerialization();
    popup.pasteAnnotationToPage(0);
    const QList<Okular::Annotation *> annotations = m_document->page(0)->annotations();
    QCOMPARE(annotations.size(), 2);
    const auto *loaded = dynamic_cast<const Okular::StampAnnotation *>(annotations.constLast());
    QVERIFY(loaded);
    QVERIFY(loaded->isOkularLatex());
    QCOMPARE(loaded->latexNoteType(), latexNoteType);
    QCOMPARE(loaded->contents(), original->contents());
    QCOMPARE(loaded->latexLayoutWidth(), original->latexLayoutWidth());
    QCOMPARE(loaded->latexPadding(), original->latexPadding());
    QCOMPARE(loaded->latexFontSize(), original->latexFontSize());
    QCOMPARE(loaded->latexTextColor(), original->latexTextColor());
    QCOMPARE(loaded->latexFillColor(), original->latexFillColor());
    QCOMPARE(loaded->latexBorderColor(), original->latexBorderColor());
    QVERIFY(loaded->latexAppearancePdfFileName().isEmpty());
    QVERIFY(m_document->annotationAppearance(loaded));
    if (latexNoteType == Okular::Annotation::LatexNoteCallout) {
        // Copy/paste moves the complete callout, unlike interactively dragging
        // its text box, which intentionally leaves the leader tip anchored.
        for (int index = 0; index < 3; ++index) {
            QCOMPARE(loaded->latexCalloutPoint(index).x, original->latexCalloutPoint(index).x + 0.02);
            QCOMPARE(loaded->latexCalloutPoint(index).y, original->latexCalloutPoint(index).y + 0.02);
        }
    }
}

QTEST_MAIN(AnnotationClipboardTest)
#include "annotationclipboardtest.moc"
