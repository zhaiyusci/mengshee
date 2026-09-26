/*
    SPDX-FileCopyrightText: 2013 Albert Astals Cid <aacid@kde.org>

    Work sponsored by the LiMux project of the city of Munich:
    SPDX-FileCopyrightText: 2017 Klarälvdalens Datakonsult AB a KDAB Group company <info@kdab.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// clazy:excludeall=qstring-allocations

#include <QSignalSpy>
#include <QInputDialog>
#include <QCheckBox>
#include <QElapsedTimer>
#include "../gui/toolbarbuttonheight.h"
#include <QUuid>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <atomic>
#include <cmath>
#include <QTableWidget>
#include <QDialogButtonBox>
#include "PdfPageSequenceEditor.h"
#include <poppler-qt6.h>
#include "../core/generator.h"
#include <QTest>

#include "../core/action.h"
#include "../core/annotations.h"
#include "../core/document_p.h"
#include "../core/fileprinter.h"
#include "../core/form.h"
#include "../core/misc.h"
#include "../core/page.h"
#include "../gui/tocmodel.h"
#include "../part/documentworkspace.h"
#include "../part/annotationpopup.h"
#include "../part/findbar.h"
#include "../part/pageview.h"
#include "../part/ocrtextlayout.h"
#include "../part/part.h"
#include "../part/editingmode.h"
#include <KSelectAction>
#include "../part/presentationwidget.h"
#include "../part/sidebar.h"
#include "../part/toc.h"
#include "../settings.h"
#include "closedialoghelper.h"

#include <KActionCollection>
#include <KActionMenu>
#include <KConfigDialog>
#include <KParts/OpenUrlArguments>

#include <QAbstractItemModelTester>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QHelpEvent>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QMimeData>
#include <QPageRanges>
#include <QPainter>
#include <QPdfWriter>
#include <QPrinter>
#include <QPushButton>
#include <QScrollBar>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QToolTip>
#include <QTreeView>
#include <QUrl>

#include <limits>

namespace Okular
{
class PartTest : public QObject
{
    Q_OBJECT

    static bool openDocument(Okular::Part *part, const QString &filePath);

Q_SIGNALS:
    void urlHandler(const QUrl &url); // NOLINT(readability-inconsistent-declaration-parameter-name)

private Q_SLOTS:
    void testOcrTextLayerEditing();
    void testOcrTextLayout();
    void testOcrPdfGeometry();
    void init();

    void testZoomWithCrop();
    void testReload();
    void testCanceledReload();
    void testTOCReload();
    void testForwardPDF();
    void testForwardPDF_data();
    void testGeneratorPreferences();
    void testSelectText();
    void testSelectTextMultiline();
    void testCopyTextSelectionModes();
    void testCopyTextWithoutLineBreaksMultiline();
    void testRemoveLineBreaks_data();
    void testRemoveLineBreaks();
    void testClickInternalLink();
    void testNamedDestinationOverlay();
    void testContentsEntryWithFitWidthNamedDestination();
    void testContentsDoesNotExposeInferredCurrentItem();
    void testEditableContentsTree();
    void testAddNamedDestinationToEmptyContents();
    void testLiveNamedDestinationEditing();
    void testNamedDestinationDragDoesNotStartTextSelection();
    void testLivePdfLinkEditing();
    void testLiveLinkSurvivesNamedDestinationUpdate();
    void testEditPdfNamedDestinationAndLink();
    void testEditExternalPdfLink();
    void testOpenAuxiliaryViewWithoutLink();
    void testAuxiliaryDocumentWorkspace();
    void testAuxiliaryTopAlignedDestinationMargin();
    void testFindBarDoesNotConsumeWorkspaceHeight();
    void testScrollBarAndMouseWheel();
    void testOpenUrlArguments();
    void test388288();
    void testSaveAs();
    void testSaveAs_data();
    void testFailedBackingFileSwapKeepsDocumentUsable();
    void testSaveAsToNonExistingPath();
    void testSaveAsToSymlink();
    void testSaveIsSymlink();
    void testSidebarItemAfterSaving();
    void testViewModeSavingPerFile();
    void testSaveAsUndoStackAnnotations();
    void testSaveAsUndoStackAnnotations_data();
    void testSaveAsUndoStackForms();
    void testSaveAsUndoStackForms_data();
    void testRotateSinglePageBackend();
    void testRotateSinglePage();
    void testLatexNoteOnRotatedPage();
    void testLatexAppearanceResizeHistory_data();
    void testLatexAppearanceResizeHistory();
    void testReadingViewsMetadata_data();
    void testReadingViewsMetadata();
    void testReadingViewsHistoryAndPageIdentity();
    void testReadingViewsMouseEditing();
    void testReadingViewsUnsupportedMetadata();
    void testReadingViewModeProjection_data();
    void testReadingViewModeProjection();
    void testReadingViewModeNavigation();
    void testReadingViewModeContinuousRendering();
    void testReadingViewModeIndependentFrames();
    void testReadingViewModeExternalNavigation();
    void testReadingViewModeTextSelection();
    void testReadingViewModeAnnotations_data();
    void testReadingViewModeAnnotations();
    void testReadingViewModeFormReplicas();
    void testReadingViewTemplateApply();
    void testReadingViewTemplateRejectsUnsupported();
    void testUnifiedEditingModes();
    void testToolbarButtonHeights_data();
    void testToolbarButtonHeights();
    void testReadingViewNativeRaster();
    void testReadingViewHighlightInteraction_data();
    void testReadingViewHighlightInteraction();
    void testDeletePagePreservesInternalLinks();
    void testDuplicatePagePreservesInternalLinks();
    void testInsertPdfPagePreservesInternalLinks();
    void testStandaloneCombineBackend();
    void testCombinePdfAvailableWithoutDocument();
    void testCombinePdfFilesPreservesSourceLinkNamespaces();
    void testMouseMoveOverLinkWhileInSelectionMode();
    void testClickUrlLinkWhileInSelectionMode();
    void testClickBlankAfterHoveringLinkDoesNotFollowStaleLink();
    void testeTextSelectionOverAndAcrossLinks_data();
    void testeTextSelectionOverAndAcrossLinks();
    void testClickUrlLinkWhileLinkTextIsSelected();
    void testTextSelectionBlankContextMenu_data();
    void testTextSelectionBlankContextMenu();
    void testRClickWhileLinkTextIsSelected();
    void testRClickOverLinkWhileLinkTextIsSelected();
    void testRClickOnSelectionModeShoulShowFollowTheLinkMenu();
    void testClickAnywhereAfterSelectionShouldUnselect();
    void testeRectSelectionStartingOnLinks();
    void testCheckBoxReadOnly();
    void testCrashTextEditDestroy();
    void testAnnotWindowAppearance();
    void testAnnotWindow();
    void testAnnotWindowInTextSelectionMode();
    void testAdditionalActionTriggers();
    void testTypewriterAnnotTool();
    void testJumpToPage();
    void testOpenAtPage();
    void testForwardBackwardNavigation();
    void testWorkspaceMainViewRetainsPositionWhenDemoted();
    void testTabletProximityBehavior();
    void testOpenPrintPreview();
    void testDisjointPrintPageRanges();
    void testMouseModeMenu();
    void testFullScreenRequest();
    void testZoomInFacingPages();
    void testLinkWithCrop();
    void testFieldFormatting();

private:
    void simulateMouseSelection(double startX, double startY, double endX, double endY, QWidget *target);
};

class PartThatHijacksQueryClose : public Okular::Part
{
    Q_OBJECT
public:
    PartThatHijacksQueryClose(QObject *parent, const QVariantList &args)
        : Okular::Part(parent, args)
        , behavior(PassThru)
    {
    }

    enum Behavior { PassThru, ReturnTrue, ReturnFalse };

    void setQueryCloseBehavior(Behavior new_behavior)
    {
        behavior = new_behavior;
    }

    bool queryClose() override
    {
        if (behavior == PassThru) {
            return Okular::Part::queryClose();
        } else { // ReturnTrue or ReturnFalse
            return (behavior == ReturnTrue);
        }
    }

private:
    Behavior behavior;
};

bool PartTest::openDocument(Okular::Part *part, const QString &filePath)
{
    part->openDocument(filePath);
    return part->m_document->isOpened();
}

static QString linkText(const Okular::Page *page, const Okular::ObjectRect *rect)
{
    const QRectF bounds = rect->region().boundingRect();
    Okular::RegularAreaRect area;
    area.append(Okular::NormalizedRect(bounds.left(), bounds.top(), bounds.right(), bounds.bottom()));
    QString title = page->text(&area, Okular::TextPage::AnyPixelTextAreaInclusionBehaviour).simplified();
    constexpr int maximumTitleLength = 80;
    if (title.size() > maximumTitleLength) {
        title = title.left(maximumTitleLength - 1) + QChar(0x2026);
    }
    return title;
}

static bool
findVisibleInternalGotoLink(PageView *view, Okular::Document *document, int sourcePageNumber, int targetPageNumber, const QString &preferredTitle, QPoint *viewportPosition, Okular::DocumentViewport *targetViewport, QString *title)
{
    if (!view || !document || !viewportPosition || !targetViewport || !title) {
        return false;
    }

    const Okular::Page *sourcePage = document->page(sourcePageNumber);
    if (!sourcePage) {
        return false;
    }

    const Okular::ObjectRect *selectedLink = nullptr;
    Okular::DocumentViewport selectedTarget;
    QString selectedTitle;
    double selectedLeft = 2.0;
    for (const Okular::ObjectRect *rect : sourcePage->objectRects()) {
        if (!rect || rect->objectType() != Okular::ObjectRect::Action || !rect->object()) {
            continue;
        }
        const auto *action = static_cast<const Okular::Action *>(rect->object());
        if (action->actionType() != Okular::Action::Goto) {
            continue;
        }
        const auto *gotoAction = static_cast<const Okular::GotoAction *>(action);
        if (gotoAction->isExternal()) {
            continue;
        }

        Okular::DocumentViewport candidateTarget = gotoAction->destViewport();
        if (!candidateTarget.isValid() && !gotoAction->destinationName().isEmpty()) {
            candidateTarget = Okular::DocumentViewport(document->metaData(QStringLiteral("NamedViewport"), gotoAction->destinationName()).toString());
        }
        if (!candidateTarget.isValid() || candidateTarget.pageNumber != targetPageNumber) {
            continue;
        }

        const QString candidateTitle = linkText(sourcePage, rect);
        const double candidateLeft = rect->region().boundingRect().left();
        if (selectedLink) {
            const bool candidateIsPreferred = candidateTitle == preferredTitle;
            const bool selectedIsPreferred = selectedTitle == preferredTitle;
            if ((selectedIsPreferred && !candidateIsPreferred) || (selectedIsPreferred == candidateIsPreferred && candidateLeft >= selectedLeft)) {
                continue;
            }
        }
        selectedLink = rect;
        selectedTarget = candidateTarget;
        selectedTitle = candidateTitle;
        selectedLeft = candidateLeft;
    }
    if (!selectedLink) {
        return false;
    }

    const QPointF normalizedCenter = selectedLink->region().boundingRect().center();
    const QRect scanRect = view->viewport()->rect();
    const int coarseStep = qMax(1, qMin(scanRect.width(), scanRect.height()) / 80);
    QPoint closestPosition;
    double closestDistance = 3.0;
    bool foundPagePoint = false;
    auto considerPosition = [&](const QPoint &position) {
        int mappedPage = -1;
        Okular::NormalizedPoint mappedPoint;
        if (!view->mapGlobalPosToPagePoint(view->viewport()->mapToGlobal(position), &mappedPage, &mappedPoint) || mappedPage != sourcePageNumber) {
            return;
        }
        const double dx = mappedPoint.x - normalizedCenter.x();
        const double dy = mappedPoint.y - normalizedCenter.y();
        const double distance = dx * dx + dy * dy;
        if (!foundPagePoint || distance < closestDistance) {
            foundPagePoint = true;
            closestDistance = distance;
            closestPosition = position;
        }
    };

    for (int y = scanRect.top(); y <= scanRect.bottom(); y += coarseStep) {
        for (int x = scanRect.left(); x <= scanRect.right(); x += coarseStep) {
            considerPosition(QPoint(x, y));
        }
    }
    if (!foundPagePoint) {
        return false;
    }

    const QRect refineRect(closestPosition.x() - coarseStep, closestPosition.y() - coarseStep, coarseStep * 2 + 1, coarseStep * 2 + 1);
    const QRect visibleRefineRect = refineRect.intersected(scanRect);
    for (int y = visibleRefineRect.top(); y <= visibleRefineRect.bottom(); ++y) {
        for (int x = visibleRefineRect.left(); x <= visibleRefineRect.right(); ++x) {
            considerPosition(QPoint(x, y));
        }
    }

    int mappedPage = -1;
    Okular::NormalizedPoint mappedPoint;
    if (!view->mapGlobalPosToPagePoint(view->viewport()->mapToGlobal(closestPosition), &mappedPage, &mappedPoint) || mappedPage != sourcePageNumber || !selectedLink->contains(mappedPoint.x, mappedPoint.y, 1.0, 1.0)) {
        return false;
    }

    *viewportPosition = closestPosition;
    *targetViewport = selectedTarget;
    *title = selectedTitle;
    return true;
}

static bool linkEditorOpensForView(PageView *view, const Okular::DocumentViewport &target)
{
    if (!view || !target.isValid()) {
        return false;
    }

    bool editorShown = false;
    QTimer dialogCloser;
    dialogCloser.setSingleShot(true);
    QObject::connect(&dialogCloser, &QTimer::timeout, view, [&editorShown]() {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (dialog && dialog->objectName() == QLatin1String("EditLinkDestinationDialog")) {
            auto *search = dialog->findChild<QLineEdit *>(QStringLiteral("NamedDestinationSearch"));
            auto *destinationList = dialog->findChild<QListWidget *>(QStringLiteral("NamedDestinationList"));
            editorShown = search && destinationList && destinationList->count() > 0;
            if (editorShown) {
                search->setText(QStringLiteral("subsection.2.1"));
                for (int row = 0; row < destinationList->count(); ++row) {
                    const QListWidgetItem *item = destinationList->item(row);
                    if (!item->isHidden() && !item->text().contains(search->text(), Qt::CaseInsensitive)) {
                        editorShown = false;
                        break;
                    }
                }
            }
            dialog->reject();
        }
    });
    dialogCloser.start(0);
    const bool invoked = QMetaObject::invokeMethod(view,
                                                   "editPdfLinkRequested",
                                                   Qt::DirectConnection,
                                                   Q_ARG(int, 0),
                                                   Q_ARG(QRectF, QRectF(0.1, 0.1, 0.1, 0.1)),
                                                   Q_ARG(QString, QStringLiteral("subsection.2.1")),
                                                   Q_ARG(Okular::DocumentViewport, target),
                                                   Q_ARG(QUrl, QUrl()));
    dialogCloser.stop();
    return invoked && editorShown;
}

static bool hasInternalGotoLinkToPage(Okular::Document *document, int sourcePageNumber, int targetPageNumber)
{
    if (!document) {
        return false;
    }

    const Okular::Page *sourcePage = document->page(sourcePageNumber);
    if (!sourcePage) {
        return false;
    }

    for (const Okular::ObjectRect *rect : sourcePage->objectRects()) {
        if (!rect || rect->objectType() != Okular::ObjectRect::Action || !rect->object()) {
            continue;
        }
        const auto *action = static_cast<const Okular::Action *>(rect->object());
        if (action->actionType() != Okular::Action::Goto) {
            continue;
        }
        const auto *gotoAction = static_cast<const Okular::GotoAction *>(action);
        if (gotoAction->isExternal()) {
            continue;
        }

        Okular::DocumentViewport candidateTarget = gotoAction->destViewport();
        if (!candidateTarget.isValid() && !gotoAction->destinationName().isEmpty()) {
            candidateTarget = Okular::DocumentViewport(document->metaData(QStringLiteral("NamedViewport"), gotoAction->destinationName()).toString());
        }
        if (candidateTarget.isValid() && candidateTarget.pageNumber == targetPageNumber) {
            return true;
        }
    }
    return false;
}

void PartTest::init()
{
    // Default settings for every test
    Okular::Settings::self()->setDefaults();

    // Clean docdatas
    const QList<QUrl> urls = {QUrl::fromUserInput(QStringLiteral("file://" KDESRCDIR "data/file1.pdf")),
                              QUrl::fromUserInput(QStringLiteral("file://" KDESRCDIR "data/file2.pdf")),
                              QUrl::fromUserInput(QStringLiteral("file://" KDESRCDIR "data/simple-multipage.pdf")),
                              QUrl::fromUserInput(QStringLiteral("file://" KDESRCDIR "data/tocreload.pdf")),
                              QUrl::fromUserInput(QStringLiteral("file://" KDESRCDIR "data/pdf_with_links.pdf")),
                              QUrl::fromUserInput(QStringLiteral("file://" KDESRCDIR "data/pdf_with_internal_links.pdf")),
                              QUrl::fromUserInput(QStringLiteral("file://" KDESRCDIR "data/RequestFullScreen.pdf"))};

    for (const QUrl &url : urls) {
        QFileInfo fileReadTest(url.toLocalFile());
        const QString docDataPath = Okular::DocumentPrivate::docDataFileName(url, fileReadTest.size());
        QFile::remove(docDataPath);
    }
}

// Test that Okular doesn't crash after a successful reload
void PartTest::testReload()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/file1.pdf")));
    part.reload();
    qApp->processEvents();
}

// Test that Okular doesn't crash after a canceled reload
void PartTest::testCanceledReload()
{
    QVariantList dummyArgs;
    PartThatHijacksQueryClose part(nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/file1.pdf")));

    // When queryClose() returns false, the reload operation is canceled (as if
    // the user had chosen Cancel in the "Save changes?" message box)
    part.setQueryCloseBehavior(PartThatHijacksQueryClose::ReturnFalse);

    part.reload();

    qApp->processEvents();
}

void PartTest::testTOCReload()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/tocreload.pdf")));
    QCOMPARE(part.m_toc->expandedNodes().count(), 0);
    part.m_toc->m_treeView->expandAll();
    QCOMPARE(part.m_toc->expandedNodes().count(), 3);
    part.reload();
    qApp->processEvents();
    QCOMPARE(part.m_toc->expandedNodes().count(), 3);
}

void PartTest::testForwardPDF()
{
    QFETCH(QString, dir);

    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);

    // Create temp dir named like this: ${system temp dir}/${random string}/${dir}
    const QTemporaryDir tempDir;
    const QDir workDir(QDir(tempDir.path()).filePath(dir));
    workDir.mkpath(QStringLiteral("."));

    const QString pdfResult = workDir.path() + QStringLiteral("/synctextest.pdf");
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/synctextest.pdf"), pdfResult));
    const QString gzDestination = workDir.path() + QStringLiteral("/synctextest.synctex.gz");
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/synctextest.synctex.gz"), gzDestination));

    QVERIFY(openDocument(&part, pdfResult));
    part.m_document->setViewportPage(0);
    QCOMPARE(part.m_document->currentPage(), 0u);
    part.closeUrl();

    QUrl u(QUrl::fromLocalFile(pdfResult));
    // Update this if you regenerate the synctextest.pdf somewhere else
    u.setFragment(QStringLiteral("src:100/home/tsdgeos/devel/kde/okular/autotests/data/synctextest.tex"));
    part.openUrl(u);
    QCOMPARE(part.m_document->currentPage(), 1u);
}

void PartTest::testForwardPDF_data()
{
    QTest::addColumn<QString>("dir");

    QTest::newRow("non-utf8") << QStringLiteral("synctextest");
    // QStringliteral is broken on windows with non ascii chars so using QString::fromUtf8
    QTest::newRow("utf8") << QString::fromUtf8("ßðđđŋßðđŋ");
}

void PartTest::testGeneratorPreferences()
{
    KConfigDialog *dialog;
    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);

    // Test that we don't crash while opening the dialog
    dialog = part.slotGeneratorPreferences();
    qApp->processEvents();
    delete dialog; // closes the dialog and recursively destroys all widgets

    // Test that we don't crash while opening a new instance of the dialog
    // This catches attempts to reuse widgets that have been destroyed
    dialog = part.slotGeneratorPreferences();
    qApp->processEvents();
    delete dialog;
}

void PartTest::testSelectText()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/file2.pdf")));
    part.widget()->show();

    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    part.m_document->setViewportPage(0);

    // wait for pixmap
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));

    const int width = part.m_pageView->horizontalScrollBar()->maximum() + part.m_pageView->viewport()->width();
    const int height = part.m_pageView->verticalScrollBar()->maximum() + part.m_pageView->viewport()->height();

    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotSetMouseTextSelect"));

    const int mouseY = height * 0.052;
    const int mouseStartX = width * 0.12;
    const int mouseEndX = width * 0.7;

    simulateMouseSelection(mouseStartX, mouseY, mouseEndX, mouseY, part.m_pageView->viewport());

    QApplication::clipboard()->clear();
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "copyTextSelection", Q_ARG(PageView::TextCopyMode, PageView::TextCopyMode::AsProvided)));

    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("Hola que tal"));
}

void PartTest::testSelectTextMultiline()
{
    // This test tests a specific variation of multiline selection
    // Select from middle to end of line, then continue to select next line
    // then move selection back on next line past the point of the first line
    // https://bugs.kde.org/show_bug.cgi?id=482249 has a nice animation.
    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/file2.pdf")));
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    part.m_document->setViewportPage(1);

    // wait for pixmap
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));

    const int width = part.m_pageView->horizontalScrollBar()->maximum() + part.m_pageView->viewport()->width();
    const int height = part.m_pageView->verticalScrollBar()->maximum() + part.m_pageView->viewport()->height();

    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotSetMouseTextSelect"));

    const int startY = height * 0.052;
    const int startX = width * 0.22;
    const int endY = height * 0.072;
    const int endX = width * 0.6;

    const int steps = 5;
    const double diffX = endX - startX;
    const double diffXStep = diffX / steps;

    QTestEventList events;
    events.addMouseMove(QPoint(startX, startY));
    events.addMousePress(Qt::LeftButton, Qt::NoModifier, QPoint(startX, startY));
    for (int i = 0; i < steps - 1; ++i) {
        events.addMouseMove(QPoint(startX + i * diffXStep, startY));
        events.addDelay(100);
    }
    events.addMouseMove(QPoint(endX, startY));
    events.addDelay(100);
    events.addMouseMove(QPoint(endX, endY));
    events.addDelay(100);
    for (int i = 0; i < (steps); i++) {
        events.addMouseMove(QPoint(endX - (i * diffXStep), endY));
        events.addDelay(100);
    }
    events.addMouseMove(QPoint(endX - (diffXStep * (steps)), endY));
    events.addDelay(100);
    events.addMouseMove(QPoint(endX - (diffXStep * (steps + 0.5)), endY));
    events.addDelay(100);

    events.addMouseRelease(Qt::LeftButton, Qt::NoModifier, QPoint(endX - (diffXStep * (steps + 0.5)), endY));

    events.simulate(part.m_pageView->viewport());

    QApplication::clipboard()->clear();
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "copyTextSelection", Q_ARG(PageView::TextCopyMode, PageView::TextCopyMode::AsProvided)));

    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("cks!\nOf c"));
}

void PartTest::testCopyTextSelectionModes()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/file2.pdf")));

    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    part.m_document->setViewportPage(0);

    // wait for pixmap
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));

    const int width = part.m_pageView->horizontalScrollBar()->maximum() + part.m_pageView->viewport()->width();
    const int height = part.m_pageView->verticalScrollBar()->maximum() + part.m_pageView->viewport()->height();

    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotSetMouseTextSelect"));

    const int mouseY = height * 0.052;
    const int mouseStartX = width * 0.12;
    const int mouseEndX = width * 0.7;

    simulateMouseSelection(mouseStartX, mouseY, mouseEndX, mouseY, part.m_pageView->viewport());

    QApplication::clipboard()->clear();

    // Test AsProvided mode (default behavior)
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "copyTextSelection", Q_ARG(PageView::TextCopyMode, PageView::TextCopyMode::AsProvided)));
    QString rawText = QApplication::clipboard()->text();
    QCOMPARE(rawText, QStringLiteral("Hola que tal"));

    // Test WithoutLineBreaks mode
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "copyTextSelection", Q_ARG(PageView::TextCopyMode, PageView::TextCopyMode::WithoutLineBreaks)));
    QString cleanText = QApplication::clipboard()->text();

    // Verify clean text is not changing this text, since it's single-line
    QCOMPARE(cleanText, QStringLiteral("Hola que tal"));
    QVERIFY(!cleanText.isEmpty());
}

void PartTest::testCopyTextWithoutLineBreaksMultiline()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);

    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/file2.pdf")));

    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    part.m_document->setViewportPage(1);

    // wait for pixmap
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));

    const int width = part.m_pageView->horizontalScrollBar()->maximum() + part.m_pageView->viewport()->width();
    const int height = part.m_pageView->verticalScrollBar()->maximum() + part.m_pageView->viewport()->height();

    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotSetMouseTextSelect"));

    const int startY = height * 0.052;
    const int startX = width * 0.22;
    const int endY = height * 0.072;
    const int endX = width * 0.6;

    simulateMouseSelection(startX, startY, endX, endY, part.m_pageView->viewport());

    QApplication::clipboard()->clear();

    // Test AsProvided mode - text should contain line breaks
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "copyTextSelection", Q_ARG(PageView::TextCopyMode, PageView::TextCopyMode::AsProvided)));
    QString rawText = QApplication::clipboard()->text();

    qDebug() << "Selected text:" << rawText;
    QVERIFY(!rawText.isEmpty());

    // Clear and test WithoutLineBreaks mode
    QApplication::clipboard()->clear();
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "copyTextSelection", Q_ARG(PageView::TextCopyMode, PageView::TextCopyMode::WithoutLineBreaks)));
    QString cleanText = QApplication::clipboard()->text();

    QVERIFY(!cleanText.isEmpty());

    // If original had newlines...
    if (rawText.contains(QLatin1Char('\n'))) {
        int rawNewlineCount = rawText.count(QLatin1Char('\n'));
        int cleanNewlineCount = cleanText.count(QLatin1Char('\n'));

        // ...The clean version should have fewer newlines!
        QVERIFY(cleanNewlineCount <= rawNewlineCount);

        // Verify no hyphen-newline combinations remain
        QVERIFY(!cleanText.contains(QStringLiteral("-\n")));
        QVERIFY(!cleanText.contains(QStringLiteral("- \n")));
    }

    QCOMPARE(cleanText, QStringLiteral("cks! Of course it does"));
}

void PartTest::testRemoveLineBreaks_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("simple newline") << QStringLiteral("Hello\nWorld") << QStringLiteral("Hello World");

    QTest::newRow("hyphen-newline") << QStringLiteral("hyphen-\nated") << QStringLiteral("hyphenated");

    QTest::newRow("hyphen-space-newline") << QStringLiteral("hyphen- \nated") << QStringLiteral("hyphenated");

    QTest::newRow("double newline preserved") << QStringLiteral("Paragraph1\n\nParagraph2") << QStringLiteral("Paragraph1\n\nParagraph2");

    QTest::newRow("multiple spaces") << QStringLiteral("Hello   \n   World") << QStringLiteral("Hello World");

    QTest::newRow("empty string") << QString() << QString();
}

void PartTest::testRemoveLineBreaks()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);

    // Directly test the string-cleanup function!
    QString result = Okular::removeLineBreaks(input);

    QCOMPARE(result, expected);
}

void PartTest::testClickInternalLink()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/pdf_with_internal_links.pdf")));
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    part.m_document->setViewportPage(0);

    // wait for pixmap
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));
    part.m_document->requestTextPage(0);
    QTRY_VERIFY(part.m_document->page(0)->hasTextPage());

    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotSetMouseNormal"));

    QPoint internalLinkPosition;
    DocumentViewport internalLinkTarget;
    QString internalLinkTitle;
    const QString expectedLinkTitle = QStringLiteral("2.1 Example for list (itemize)");
    QVERIFY(findVisibleInternalGotoLink(part.m_pageView, part.m_document, 0, 1, expectedLinkTitle, &internalLinkPosition, &internalLinkTarget, &internalLinkTitle));
    QCOMPARE(internalLinkTitle, expectedLinkTitle);
    QCOMPARE(part.m_document->currentPage(), 0u);
    QHelpEvent tooltipEvent(QEvent::ToolTip, internalLinkPosition, part.m_pageView->viewport()->mapToGlobal(internalLinkPosition));
    QApplication::sendEvent(part.m_pageView->viewport(), &tooltipEvent);
    QTRY_VERIFY(QToolTip::text().contains(QString::number(internalLinkTarget.pageNumber + 1)));
    QTRY_VERIFY(QToolTip::text().contains(QStringLiteral("subsection.2.1")));
    QToolTip::hideText();

    QAction *advancedMode = part.actionCollection()->action(QStringLiteral("view_toggle_named_destinations"));
    QVERIFY(advancedMode);
    advancedMode->setChecked(true);
    QVERIFY(linkEditorOpensForView(part.m_pageView, internalLinkTarget));

    QTest::mouseMove(part.m_pageView->viewport(), internalLinkPosition);
    QTest::mouseClick(part.m_pageView->viewport(), Qt::LeftButton, Qt::NoModifier, internalLinkPosition);
    QTRY_COMPARE(part.m_document->currentPage(), static_cast<uint>(internalLinkTarget.pageNumber));

    // make sure cursor goes back to being an open hand again.  Bug 421437
    QTRY_COMPARE_WITH_TIMEOUT(part.m_pageView->cursor().shape(), Qt::OpenHandCursor, 1000);
}

void PartTest::testNamedDestinationOverlay()
{
    Okular::Part part(nullptr, {});
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/pdf_with_internal_links.pdf")));

    const QVariantList destinations = part.m_document->metaData(QStringLiteral("NamedViewports")).toList();
    QVERIFY(!destinations.isEmpty());

    bool foundSection = false;
    bool foundSubsection = false;
    for (const QVariant &destinationValue : destinations) {
        const QVariantMap destination = destinationValue.toMap();
        const QString name = destination.value(QStringLiteral("name")).toString();
        const DocumentViewport viewport(destination.value(QStringLiteral("viewport")).toString());
        QVERIFY(viewport.isValid());
        foundSection |= name == QLatin1String("section.1");
        foundSubsection |= name == QLatin1String("subsection.2.1");
    }
    QVERIFY(foundSection);
    QVERIFY(foundSubsection);

    QAction *toggle = part.actionCollection()->action(QStringLiteral("view_toggle_named_destinations"));
    QVERIFY(toggle);
    QVERIFY(toggle->isCheckable());
    QVERIFY(!toggle->isVisible()); // compatibility alias, not a second user-facing mode switch
    QCOMPARE(toggle->text(), i18n("Cross-reference Mode"));
    QAction *insertPage = part.actionCollection()->action(QStringLiteral("tools_insert_page"));
    QVERIFY(insertPage);
    QAction *batchNamedDestinations = part.actionCollection()->action(QStringLiteral("advanced_add_named_destinations_from_template"));
    QVERIFY(batchNamedDestinations);
    QVERIFY(!insertPage->isVisible());
    QVERIFY(!batchNamedDestinations->isVisible());
    QVERIFY(!part.m_pageView->advancedModeEnabled());
    QVERIFY(!part.m_pageView->namedDestinationsVisible());
    toggle->setChecked(true);
    QVERIFY(toggle->isChecked());
    QVERIFY(!insertPage->isVisible()); // page editing is a separate mode
    QVERIFY(batchNamedDestinations->isVisible());
    QVERIFY(batchNamedDestinations->isEnabled());
    QVERIFY(part.m_pageView->advancedModeEnabled());
    QVERIFY(part.m_pageView->namedDestinationsVisible());
    toggle->setChecked(false);
    QVERIFY(!insertPage->isVisible());
    QVERIFY(!batchNamedDestinations->isVisible());
    QVERIFY(!part.m_pageView->advancedModeEnabled());
    QApplication::processEvents();
}

void PartTest::testOpenAuxiliaryViewWithoutLink()
{
    Okular::Part part(nullptr, {});
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/file1.pdf")));
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    DocumentWorkspace *workspace = part.m_documentWorkspace;
    PageView *mainView = part.m_pageView;
    QVERIFY(workspace);
    QCOMPARE(workspace->auxiliaryViewCount(), 0);

    // The auxiliary workspace must be usable without a link. The action
    // clones the active frame's current viewport into a new independent tab.
    QAction *openAuxiliaryView = part.actionCollection()->action(QStringLiteral("open_auxiliary_view"));
    QVERIFY(openAuxiliaryView);
    QVERIFY(openAuxiliaryView->isEnabled());
    const DocumentViewport target = mainView->documentViewport();
    openAuxiliaryView->trigger();

    QTRY_COMPARE(workspace->auxiliaryViewCount(), 1);
    QPointer<PageView> auxiliaryView = workspace->auxiliaryViews().constFirst();
    QVERIFY(auxiliaryView);
    QVERIFY(auxiliaryView->documentViewport() == target);
    QVERIFY(!workspace->viewTitle(auxiliaryView).isEmpty());
    QTRY_COMPARE(workspace->activeView(), auxiliaryView.data());

    workspace->closeAuxiliaryTab(0);
    QTRY_COMPARE(workspace->auxiliaryViewCount(), 0);
    QTRY_VERIFY(auxiliaryView.isNull());
    QTRY_COMPARE(workspace->activeView(), mainView);
}

void PartTest::testAuxiliaryTopAlignedDestinationMargin()
{
    Okular::Part part(nullptr, {});
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/pdf_with_internal_links.pdf")));
    part.widget()->resize(1200, 600);
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    PageView *mainView = part.m_pageView;
    part.m_document->setViewportPage(0);
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(mainView));
    const int mainPage = mainView->documentViewport().pageNumber;

    DocumentViewport target(1);
    target.rePos.enabled = true;
    target.rePos.pos = DocumentViewport::TopLeft;
    target.rePos.normalizedX = 0.0;
    target.rePos.normalizedY = 0.5;
    const QString originalTarget = target.toString();
    part.openAuxiliaryView(mainView, target, QStringLiteral("Top-aligned destination"));
    QTRY_COMPARE(part.m_documentWorkspace->auxiliaryViewCount(), 1);
    PageView *auxiliaryView = part.m_documentWorkspace->auxiliaryViews().constFirst();
    QVERIFY(auxiliaryView);
    QTRY_VERIFY(auxiliaryView->isVisible());

    QTimer *mainResizeTimer = mainView->findChild<QTimer *>(QStringLiteral("delayResizeEventTimer"));
    QTimer *auxiliaryResizeTimer = auxiliaryView->findChild<QTimer *>(QStringLiteral("delayResizeEventTimer"));
    QVERIFY(mainResizeTimer);
    QVERIFY(auxiliaryResizeTimer);
    QTRY_VERIFY_WITH_TIMEOUT(!mainResizeTimer->isActive(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!auxiliaryResizeTimer->isActive(), 5000);
    QCOMPARE(mainView->documentViewport().pageNumber, mainPage);
    const QString mainViewport = mainView->documentViewport().toString();
    const int mainScroll = mainView->verticalScrollBar()->value();

    // Use the real zoom slots (not unexported QTest-only PageView methods).
    // A large fixed zoom keeps the mid-page destination away from scroll limits.
    QVERIFY(QMetaObject::invokeMethod(auxiliaryView, "slotZoomActual"));
    for (int i = 0; i < 3; ++i) {
        QVERIFY(QMetaObject::invokeMethod(auxiliaryView, "slotZoomIn"));
    }
    auxiliaryView->goToDocumentViewport(target, false);
    // Large zoom levels use tiles, so a full-page hasPixmap() check with
    // default dimensions cannot indicate readiness. Wait for layout instead.
    QTRY_VERIFY(auxiliaryView->verticalScrollBar()->maximum() > 0);
    QTRY_VERIFY(auxiliaryView->viewport()->height() >= 192);

    // Infer the destination's logical screen Y from two points on the page.
    // This tests rendered geometry, independently of the stored viewport and
    // without assuming a PDF DPI, page size, border width or device pixel ratio.
    const auto targetScreenY = [auxiliaryView, &target]() -> double {
        QWidget *viewport = auxiliaryView->viewport();
        const int x = viewport->width() / 2;
        const int y = viewport->height() / 2;
        int firstPage = -1;
        int secondPage = -1;
        NormalizedPoint first;
        NormalizedPoint second;
        if (!auxiliaryView->mapGlobalPosToPagePoint(viewport->mapToGlobal(QPoint(x, y)), &firstPage, &first)
            || !auxiliaryView->mapGlobalPosToPagePoint(viewport->mapToGlobal(QPoint(x, y + 10)), &secondPage, &second)
            || firstPage != target.pageNumber || secondPage != target.pageNumber || second.y <= first.y) {
            return -10000.0;
        }
        return y + (target.rePos.normalizedY - first.y) * 10.0 / (second.y - first.y);
    };
    QTRY_VERIFY(qAbs(targetScreenY() - 48.0) <= 2.0);
    QCOMPARE(auxiliaryView->documentViewport().toString(), originalTarget);
    QVERIFY(auxiliaryView->verticalScrollBar()->value() > 0);
    QVERIFY(auxiliaryView->verticalScrollBar()->value() < auxiliaryView->verticalScrollBar()->maximum());

    // Repeated navigation must not accumulate the presentation-only inset.
    const int auxiliaryScroll = auxiliaryView->verticalScrollBar()->value();
    for (int i = 0; i < 3; ++i) {
        DocumentViewport away = target;
        away.rePos.normalizedY = 0.6;
        auxiliaryView->goToDocumentViewport(away, false);
        auxiliaryView->goToDocumentViewport(target, false);
        QTRY_VERIFY(qAbs(targetScreenY() - 48.0) <= 2.0);
        QCOMPARE(auxiliaryView->verticalScrollBar()->value(), auxiliaryScroll);
        QCOMPARE(auxiliaryView->documentViewport().toString(), originalTarget);
    }

    DocumentViewport centered = target;
    centered.rePos.pos = DocumentViewport::Center;
    centered.rePos.normalizedX = 0.5;
    auxiliaryView->goToDocumentViewport(centered, false);
    QTRY_VERIFY(qAbs(targetScreenY() - auxiliaryView->viewport()->height() / 2.0) <= 2.0);
    QCOMPARE(auxiliaryView->documentViewport().toString(), centered.toString());
    QCOMPARE(target.toString(), originalTarget);
    QCOMPARE(mainView->documentViewport().toString(), mainViewport);
    QCOMPARE(mainView->verticalScrollBar()->value(), mainScroll);
    QCOMPARE(part.m_document->currentPage(), static_cast<uint>(mainPage));
}

void PartTest::testAuxiliaryDocumentWorkspace()
{
    Okular::Part part(nullptr, {});
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/pdf_with_internal_links.pdf")));
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    DocumentWorkspace *workspace = part.m_documentWorkspace;
    PageView *originalMainView = part.m_pageView;
    QVERIFY(workspace);
    QCOMPARE(workspace->mainView(), originalMainView);
    QCOMPARE(workspace->activeView(), originalMainView);
    QCOMPARE(workspace->auxiliaryViewCount(), 0);

    part.m_document->setViewportPage(0);
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(originalMainView));
    part.m_document->requestTextPage(0);
    QTRY_VERIFY(part.m_document->page(0)->hasTextPage());
    QVERIFY(QMetaObject::invokeMethod(originalMainView, "slotSetMouseNormal"));

    const int originalMainPage = originalMainView->documentViewport().pageNumber;
    QPoint internalLinkPosition;
    DocumentViewport modifiedClickTarget;
    QString modifiedClickTitle;
    const QString expectedLinkTitle = QStringLiteral("2.1 Example for list (itemize)");
    QVERIFY(findVisibleInternalGotoLink(originalMainView, part.m_document, 0, 1, expectedLinkTitle, &internalLinkPosition, &modifiedClickTarget, &modifiedClickTitle));
    QCOMPARE(modifiedClickTitle, expectedLinkTitle);
    const int unsplitMainViewWidth = originalMainView->width();

    QAction *advancedMode = part.actionCollection()->action(QStringLiteral("view_toggle_named_destinations"));
    QVERIFY(advancedMode);
    advancedMode->setChecked(true);
    QVERIFY(originalMainView->advancedModeEnabled());

    // Exercise the real default gesture once. The remaining lifecycle checks
    // use the public request signal so they do not depend on rendered geometry.
    QTest::mouseMove(originalMainView->viewport(), internalLinkPosition);
    QTest::mouseClick(originalMainView->viewport(), Qt::MiddleButton, Qt::NoModifier, internalLinkPosition);

    QTRY_COMPARE(workspace->auxiliaryViewCount(), 1);
    QCOMPARE(workspace->auxiliaryPaneCount(), 1);
    QCOMPARE(workspace->mainView(), originalMainView);
    QCOMPARE(originalMainView->documentViewport().pageNumber, originalMainPage);

    PageView *firstAuxiliaryView = workspace->auxiliaryViews().constFirst();
    QVERIFY(firstAuxiliaryView);
    QVERIFY(firstAuxiliaryView->advancedModeEnabled());
    QTRY_COMPARE(workspace->activeView(), firstAuxiliaryView);
    QTRY_COMPARE(part.m_workspaceActionView.data(), firstAuxiliaryView);
    QAction *auxiliaryAdvancedMode = part.actionCollection()->action(QStringLiteral("view_toggle_named_destinations"));
    QVERIFY(auxiliaryAdvancedMode);
    auxiliaryAdvancedMode->setChecked(false);
    QVERIFY(!firstAuxiliaryView->advancedModeEnabled());
    QVERIFY(!originalMainView->advancedModeEnabled());
    auxiliaryAdvancedMode->setChecked(true);
    QVERIFY(firstAuxiliaryView->advancedModeEnabled());
    QVERIFY(originalMainView->advancedModeEnabled());
    DocumentViewport firstAuxiliaryTarget = firstAuxiliaryView->documentViewport();
    QVERIFY(firstAuxiliaryTarget == modifiedClickTarget);
    QCOMPARE(workspace->viewTitle(firstAuxiliaryView), modifiedClickTitle);
    QVERIFY(!firstAuxiliaryView->viewportHistoryAtBegin());
    QCOMPARE(workspace->activeView(), firstAuxiliaryView);
    QCOMPARE(part.workspaceActivePageView(), firstAuxiliaryView);
    QCOMPARE(part.m_workspaceActionView.data(), firstAuxiliaryView);
    QVERIFY(linkEditorOpensForView(firstAuxiliaryView, modifiedClickTarget));

    // The other advertised gesture must take the same path. The splitter has
    // resized the main view, so derive the link position again after relayout.
    QTRY_VERIFY(originalMainView->width() < unsplitMainViewWidth);
    QTimer *mainResizeTimer = originalMainView->findChild<QTimer *>(QStringLiteral("delayResizeEventTimer"));
    QTimer *firstAuxiliaryResizeTimer = firstAuxiliaryView->findChild<QTimer *>(QStringLiteral("delayResizeEventTimer"));
    QVERIFY(mainResizeTimer);
    QVERIFY(firstAuxiliaryResizeTimer);
    QTRY_VERIFY_WITH_TIMEOUT(!mainResizeTimer->isActive(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!firstAuxiliaryResizeTimer->isActive(), 5000);
    QPoint controlClickPosition;
    DocumentViewport controlClickTarget;
    QString controlClickTitle;
    QVERIFY(findVisibleInternalGotoLink(originalMainView, part.m_document, 0, 1, expectedLinkTitle, &controlClickPosition, &controlClickTarget, &controlClickTitle));
    QVERIFY(controlClickTarget == modifiedClickTarget);
    QCOMPARE(controlClickTitle, modifiedClickTitle);
    QTest::mouseMove(originalMainView->viewport(), controlClickPosition);
    QTest::mouseClick(originalMainView->viewport(), Qt::LeftButton, Qt::ControlModifier, controlClickPosition);
    QTRY_COMPARE(workspace->auxiliaryViewCount(), 2);
    QPointer<PageView> controlClickAuxiliaryView = workspace->auxiliaryViews().constLast();
    QVERIFY(controlClickAuxiliaryView);
    QVERIFY(controlClickAuxiliaryView->documentViewport() == controlClickTarget);
    QCOMPARE(workspace->viewTitle(controlClickAuxiliaryView.data()), controlClickTitle);
    QCOMPARE(originalMainView->documentViewport().pageNumber, originalMainPage);

    // Dragging a tab to the bottom edge is backed by the same split operation:
    // it creates a separately resizable pane without changing either view.
    QVERIFY(workspace->splitAuxiliaryView(controlClickAuxiliaryView.data(), firstAuxiliaryView, Qt::Vertical, true));
    QCOMPARE(workspace->auxiliaryPaneCount(), 2);
    QVERIFY(controlClickAuxiliaryView->parentWidget() != firstAuxiliaryView->parentWidget());
    QTRY_VERIFY(controlClickAuxiliaryView->isVisibleTo(workspace));
    QTRY_VERIFY(firstAuxiliaryView->isVisibleTo(workspace));

    const int controlClickAuxiliaryIndex = workspace->auxiliaryViews().indexOf(controlClickAuxiliaryView.data());
    QVERIFY(controlClickAuxiliaryIndex >= 0);
    workspace->closeAuxiliaryTab(controlClickAuxiliaryIndex);
    QTRY_COMPARE(workspace->auxiliaryViewCount(), 1);
    QTRY_COMPARE(workspace->auxiliaryPaneCount(), 1);
    QTRY_VERIFY(controlClickAuxiliaryView.isNull());
    QCOMPARE(workspace->activeView(), firstAuxiliaryView);
    // A splitter relayout can change which page is nearest the center in
    // continuous mode. Seed the next tab from the source frame's durable
    // post-relayout viewport; Back must return here, wherever that is.
    QCoreApplication::sendPostedEvents(firstAuxiliaryView, QEvent::MetaCall);
    QCoreApplication::processEvents();
    QTRY_VERIFY_WITH_TIMEOUT(!firstAuxiliaryResizeTimer->isActive(), 5000);
    firstAuxiliaryTarget = firstAuxiliaryView->documentViewport();

    const QString secondTitle = QStringLiteral("Second auxiliary link");
    const DocumentViewport secondTarget(0);
    QVERIFY(QMetaObject::invokeMethod(firstAuxiliaryView, "openInternalLinkInAuxiliaryFrame", Qt::DirectConnection, Q_ARG(Okular::DocumentViewport, secondTarget), Q_ARG(QString, secondTitle)));

    QCOMPARE(workspace->auxiliaryViewCount(), 2);
    PageView *secondAuxiliaryView = workspace->auxiliaryViews().constLast();
    QVERIFY(secondAuxiliaryView);
    QVERIFY(secondAuxiliaryView != firstAuxiliaryView);
    QVERIFY(secondAuxiliaryView->documentViewport() == secondTarget);
    QCOMPARE(workspace->viewTitle(secondAuxiliaryView), secondTitle);
    QVERIFY(firstAuxiliaryView->documentViewport() == firstAuxiliaryTarget);
    QCOMPARE(originalMainView->documentViewport().pageNumber, originalMainPage);
    QTRY_COMPARE(workspace->activeView(), secondAuxiliaryView);
    QCOMPARE(part.workspaceActivePageView(), secondAuxiliaryView);
    QCOMPARE(part.m_workspaceActionView.data(), secondAuxiliaryView);
    QCOMPARE(workspace->auxiliaryPaneCount(), 1);
    QCOMPARE(secondAuxiliaryView->parentWidget(), firstAuxiliaryView->parentWidget());

    // A horizontal edge drop creates a second auxiliary pane.  Promotion and
    // reload below must preserve this topology and all existing view routing.
    QVERIFY(workspace->splitAuxiliaryView(secondAuxiliaryView, firstAuxiliaryView, Qt::Horizontal, true));
    QCOMPARE(workspace->auxiliaryPaneCount(), 2);
    QVERIFY(secondAuxiliaryView->parentWidget() != firstAuxiliaryView->parentWidget());
    QTRY_VERIFY(secondAuxiliaryView->isVisibleTo(workspace));
    QTRY_VERIFY(firstAuxiliaryView->isVisibleTo(workspace));

    const QString thirdTitle = QStringLiteral("Nested auxiliary link");
    const DocumentViewport thirdTarget(2);
    QVERIFY(QMetaObject::invokeMethod(firstAuxiliaryView, "openInternalLinkInAuxiliaryFrame", Qt::DirectConnection, Q_ARG(Okular::DocumentViewport, thirdTarget), Q_ARG(QString, thirdTitle)));
    QCOMPARE(workspace->auxiliaryViewCount(), 3);
    QCOMPARE(workspace->auxiliaryPaneCount(), 2);
    PageView *thirdAuxiliaryView = workspace->auxiliaryViews().at(1);
    QVERIFY(thirdAuxiliaryView != firstAuxiliaryView);
    QVERIFY(thirdAuxiliaryView != secondAuxiliaryView);
    QCOMPARE(workspace->viewTitle(thirdAuxiliaryView), thirdTitle);
    QCOMPARE(thirdAuxiliaryView->parentWidget(), firstAuxiliaryView->parentWidget());
    QVERIFY(workspace->splitAuxiliaryView(thirdAuxiliaryView, secondAuxiliaryView, Qt::Vertical, true));
    QCOMPARE(workspace->auxiliaryPaneCount(), 3);
    QPointer<PageView> closingNestedView = thirdAuxiliaryView;
    workspace->closeAuxiliaryTab(workspace->auxiliaryViews().indexOf(thirdAuxiliaryView));
    QTRY_VERIFY(closingNestedView.isNull());
    QCOMPARE(workspace->auxiliaryViewCount(), 2);
    QTRY_COMPARE(workspace->auxiliaryPaneCount(), 2);

    // Part-level navigation must be routed to the active auxiliary tab. Its
    // independent Back entry is the viewport of the frame that spawned it.
    part.slotHistoryBack();
    QVERIFY(secondAuxiliaryView->documentViewport() == firstAuxiliaryTarget);
    QVERIFY(firstAuxiliaryView->documentViewport() == firstAuxiliaryTarget);
    QCOMPARE(originalMainView->documentViewport().pageNumber, originalMainPage);
    part.slotHistoryNext();
    QVERIFY(secondAuxiliaryView->documentViewport() == secondTarget);
    part.slotHistoryBack();
    QVERIFY(secondAuxiliaryView->documentViewport() == firstAuxiliaryTarget);

    workspace->promoteView(secondAuxiliaryView);
    QCOMPARE(workspace->mainView(), secondAuxiliaryView);
    QCOMPARE(workspace->activeView(), secondAuxiliaryView);
    QCOMPARE(part.m_pageView.data(), secondAuxiliaryView);
    QCOMPARE(part.workspaceActivePageView(), secondAuxiliaryView);
    QCOMPARE(part.m_workspaceActionView.data(), secondAuxiliaryView);
    QCOMPARE(workspace->mainViewTitle(), secondTitle);
    QWidget *mainHost = workspace->findChild<QWidget *>(QStringLiteral("documentWorkspaceMainHost"));
    QVERIFY(mainHost);
    QCOMPARE(secondAuxiliaryView->parentWidget(), mainHost);
    QTRY_VERIFY(secondAuxiliaryView->isVisibleTo(workspace));
    QTRY_COMPARE(secondAuxiliaryView->geometry().left(), mainHost->contentsRect().left());
    QTRY_COMPARE(secondAuxiliaryView->geometry().width(), mainHost->contentsRect().width());
    QTRY_COMPARE(secondAuxiliaryView->geometry().bottom(), mainHost->contentsRect().bottom());
    QTRY_VERIFY(secondAuxiliaryView->height() > mainHost->height() / 2);

    // The promoted view keeps its complete history and Part continues to
    // route navigation to it through the default document channel.
    part.slotHistoryNext();
    QVERIFY(secondAuxiliaryView->documentViewport() == secondTarget);
    QCOMPARE(originalMainView->documentViewport().pageNumber, originalMainPage);
    part.slotHistoryBack();
    QVERIFY(secondAuxiliaryView->documentViewport() == firstAuxiliaryTarget);

    const QPointer<PageView> reloadedMainView = secondAuxiliaryView;
    const QPointer<PageView> reloadedFirstAuxiliaryView = firstAuxiliaryView;
    const QPointer<PageView> reloadedOriginalMainView = originalMainView;
    const QString firstAuxiliaryTitle = workspace->viewTitle(firstAuxiliaryView);
    const QString originalMainTitle = workspace->viewTitle(originalMainView);
    const int mainPageBeforeReload = secondAuxiliaryView->documentViewport().pageNumber;
    const int firstAuxiliaryPageBeforeReload = firstAuxiliaryView->documentViewport().pageNumber;
    const int originalMainPageBeforeReload = originalMainView->documentViewport().pageNumber;

    QVERIFY(part.slotAttemptReload(true));
    // Opening and viewport notifications schedule relayouts for every frame.
    // Drain their queued viewport work, then wait on each view's actual resize
    // timer instead of assuming a particular CI machine can finish in 250 ms.
    const QList<QPointer<PageView>> reloadedViews = {reloadedMainView, reloadedFirstAuxiliaryView, reloadedOriginalMainView};
    for (const QPointer<PageView> &view : reloadedViews) {
        QVERIFY(view);
        QCoreApplication::sendPostedEvents(view.data(), QEvent::MetaCall);
        QCoreApplication::processEvents();
        QTimer *resizeTimer = view->findChild<QTimer *>(QStringLiteral("delayResizeEventTimer"));
        QVERIFY(resizeTimer);
        QTRY_VERIFY_WITH_TIMEOUT(!resizeTimer->isActive(), 5000);
    }
    QCOMPARE(workspace->auxiliaryViewCount(), 2);
    QCOMPARE(workspace->auxiliaryPaneCount(), 2);
    QCOMPARE(workspace->mainView(), reloadedMainView.data());
    QCOMPARE(workspace->activeView(), reloadedMainView.data());
    QCOMPARE(part.m_pageView.data(), reloadedMainView.data());
    QCOMPARE(part.workspaceActivePageView(), reloadedMainView.data());
    QCOMPARE(part.m_workspaceActionView.data(), reloadedMainView.data());
    QCOMPARE(workspace->mainViewTitle(), secondTitle);
    QCOMPARE(workspace->viewTitle(reloadedFirstAuxiliaryView.data()), firstAuxiliaryTitle);
    QCOMPARE(workspace->viewTitle(reloadedOriginalMainView.data()), originalMainTitle);
    QCOMPARE(reloadedMainView->documentViewport().pageNumber, mainPageBeforeReload);
    QCOMPARE(reloadedFirstAuxiliaryView->documentViewport().pageNumber, firstAuxiliaryPageBeforeReload);
    QCOMPARE(reloadedOriginalMainView->documentViewport().pageNumber, originalMainPageBeforeReload);

    const int firstAuxiliaryIndex = workspace->auxiliaryViews().indexOf(reloadedFirstAuxiliaryView.data());
    QVERIFY(firstAuxiliaryIndex >= 0);
    workspace->closeAuxiliaryTab(firstAuxiliaryIndex);
    QCOMPARE(workspace->auxiliaryViewCount(), 1);
    QTRY_COMPARE(workspace->auxiliaryPaneCount(), 1);
    QTRY_VERIFY(reloadedFirstAuxiliaryView.isNull());
    QCOMPARE(workspace->mainView(), reloadedMainView.data());
    QCOMPARE(workspace->activeView(), reloadedMainView.data());
    QCOMPARE(part.workspaceActivePageView(), reloadedMainView.data());
}

void PartTest::testFindBarDoesNotConsumeWorkspaceHeight()
{
    Okular::Part part(nullptr, {});
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/file1.pdf")));
    part.widget()->resize(900, 700);
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    DocumentWorkspace *workspace = part.m_documentWorkspace;
    FindBar *findBar = part.m_findBar;
    QVERIFY(workspace);
    QVERIFY(findBar);
    QWidget *rightContainer = workspace->parentWidget();
    QVERIFY(rightContainer);
    QVERIFY(!findBar->isVisible());

    part.slotShowFindBar();
    QTRY_VERIFY(findBar->isVisible());

    // A visible find bar must retain its natural, single-row height.  If the
    // document workspace has no stretch in the surrounding QVBoxLayout, Qt
    // distributes the unused vertical space to the find bar as well.
    QTRY_VERIFY(findBar->height() <= findBar->sizeHint().height() + 1);

    // The workspace must receive all space not occupied by visible sibling
    // widgets.  Compute this from actual geometry so the assertion remains
    // independent of style, font metrics, and optional message widgets.
    QLayout *rightLayout = rightContainer->layout();
    QVERIFY(rightLayout);
    const auto expectedWorkspaceHeight = [rightLayout, workspace]() {
        int reservedHeight = 0;
        int visibleWidgetCount = 0;
        for (int i = 0; i < rightLayout->count(); ++i) {
            QWidget *widget = rightLayout->itemAt(i)->widget();
            if (!widget || widget->isHidden()) {
                continue;
            }
            ++visibleWidgetCount;
            if (widget != workspace) {
                reservedHeight += widget->height();
            }
        }
        const int spacingHeight = qMax(0, visibleWidgetCount - 1) * rightLayout->spacing();
        return rightLayout->contentsRect().height() - reservedHeight - spacingHeight;
    };
    QTRY_COMPARE(workspace->height(), expectedWorkspaceHeight());
}

// Test for bug 421159, which is: When scrolling down with the scroll bar
// followed by scrolling down with the mouse wheel, the mouse wheel scrolling
// will make the viewport jump back to the first page.
void PartTest::testScrollBarAndMouseWheel()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/simple-multipage.pdf")));
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    part.m_document->setViewportPage(0);

    // wait for pixmap
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));

    // Make sure we are on the first page
    QCOMPARE(part.m_document->currentPage(), 0u);

    // Two clicks on the vertical scrollbar
    auto scrollBar = part.m_pageView->verticalScrollBar();

    QTest::mouseClick(scrollBar, Qt::LeftButton);
    QTest::qWait(QApplication::doubleClickInterval() * 2); // Wait a tiny bit
    QTest::mouseClick(scrollBar, Qt::LeftButton);

    // We have scrolled enough to be on the second page now
    QCOMPARE(part.m_document->currentPage(), 1u);

    // Scroll further down using the mouse wheel
    auto wheelDown = new QWheelEvent({}, {}, {}, {0, -150}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::postEvent(part.m_pageView->viewport(), wheelDown);

    // Wait a little for the scrolling to actually happen.
    // We should still be on the second page after that.
    QTest::qWait(1000);

    QCOMPARE(part.m_document->currentPage(), 1u);
}

// cursor switches to Hand when hovering over link in TextSelect mode.
void PartTest::testMouseMoveOverLinkWhileInSelectionMode()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/pdf_with_links.pdf")));
    // resize window to avoid problem with selection areas
    part.widget()->resize(800, 600);
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    part.m_document->setViewportPage(0);

    // wait for pixmap
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));

    const int width = part.m_pageView->horizontalScrollBar()->maximum() + part.m_pageView->viewport()->width();
    const int height = part.m_pageView->verticalScrollBar()->maximum() + part.m_pageView->viewport()->height();

    // enter text-selection mode
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotSetMouseTextSelect"));

    // move mouse over link
    QTest::mouseMove(part.m_pageView->viewport(), QPoint(width * 0.250, height * 0.127));

    // check if mouse icon changed to proper icon
    QTRY_COMPARE(part.m_pageView->cursor().shape(), Qt::PointingHandCursor);
}

// clicking on hyperlink jumps to destination in TextSelect mode.
void PartTest::testClickUrlLinkWhileInSelectionMode()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/pdf_with_links.pdf")));
    // resize window to avoid problem with selection areas
    part.widget()->resize(800, 600);
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    part.m_document->setViewportPage(0);

    // wait for pixmap
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));

    const int width = part.m_pageView->horizontalScrollBar()->maximum() + part.m_pageView->viewport()->width();
    const int height = part.m_pageView->verticalScrollBar()->maximum() + part.m_pageView->viewport()->height();

    // enter text-selection mode
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotSetMouseTextSelect"));

    // overwrite urlHandler for 'mailto' urls
    QDesktopServices::setUrlHandler(QStringLiteral("mailto"), this, "urlHandler");
    QSignalSpy openUrlSignalSpy(this, &PartTest::urlHandler);

    // click on url
    QTest::mouseMove(part.m_pageView->viewport(), QPoint(width * 0.250, height * 0.127));
    QTest::mouseClick(part.m_pageView->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(width * 0.250, height * 0.127));

    // expect that the urlHandler signal was called
    QTRY_COMPARE(openUrlSignalSpy.count(), 1);
    QList<QVariant> arguments = openUrlSignalSpy.takeFirst();
    QCOMPARE(arguments.at(0).value<QUrl>(), QUrl(QStringLiteral("mailto:foo@foo.bar")));
}

void PartTest::testeTextSelectionOverAndAcrossLinks_data()
{
    QTest::addColumn<double>("mouseStartX");
    QTest::addColumn<double>("mouseEndX");
    QTest::addColumn<QString>("expectedResult");

    // can text-select "over and across" hyperlink.
    QTest::newRow("start selection before link") << 0.1564 << 0.2943 << QStringLiteral(" a link: foo@foo.b");
    // can text-select starting at text and ending selection in middle of hyperlink.
    QTest::newRow("start selection in the middle of the link") << 0.28 << 0.382 << QStringLiteral("o.bar");
    // text selection works when selecting left to right or right to left
    QTest::newRow("start selection after link") << 0.40 << 0.05 << QStringLiteral("This is a link: foo@foo.bar");
}

// can text-select "over and across" hyperlink.
void PartTest::testeTextSelectionOverAndAcrossLinks()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/pdf_with_links.pdf")));
    // resize window to avoid problem with selection areas
    part.widget()->resize(800, 600);
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    part.m_document->setViewportPage(0);

    // wait for pixmap
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));

    const int width = part.m_pageView->horizontalScrollBar()->maximum() + part.m_pageView->viewport()->width();
    const int height = part.m_pageView->verticalScrollBar()->maximum() + part.m_pageView->viewport()->height();

    // enter text-selection mode
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotSetMouseTextSelect"));

    const double mouseY = height * 0.127;
    QFETCH(double, mouseStartX);
    QFETCH(double, mouseEndX);

    mouseStartX = width * mouseStartX;
    mouseEndX = width * mouseEndX;

    simulateMouseSelection(mouseStartX, mouseY, mouseEndX, mouseY, part.m_pageView->viewport());

    QApplication::clipboard()->clear();
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "copyTextSelection"));

    QFETCH(QString, expectedResult);
    QCOMPARE(QApplication::clipboard()->text(), expectedResult);
}

// can jump to link while there's an active selection of text.
void PartTest::testClickUrlLinkWhileLinkTextIsSelected()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/pdf_with_links.pdf")));
    // resize window to avoid problem with selection areas
    part.widget()->resize(800, 600);
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    part.m_document->setViewportPage(0);

    // wait for pixmap
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));

    const int width = part.m_pageView->horizontalScrollBar()->maximum() + part.m_pageView->viewport()->width();
    const int height = part.m_pageView->verticalScrollBar()->maximum() + part.m_pageView->viewport()->height();

    // enter text-selection mode
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotSetMouseTextSelect"));

    const double mouseY = height * 0.127;
    const double mouseStartX = width * 0.13;
    const double mouseEndX = width * 0.40;

    simulateMouseSelection(mouseStartX, mouseY, mouseEndX, mouseY, part.m_pageView->viewport());

    // overwrite urlHandler for 'mailto' urls
    QDesktopServices::setUrlHandler(QStringLiteral("mailto"), this, "urlHandler");
    QSignalSpy openUrlSignalSpy(this, &PartTest::urlHandler);

    // click on url
    const double mouseClickX = width * 0.2997;
    const double mouseClickY = height * 0.1293;

    QTest::mouseMove(part.m_pageView->viewport(), QPoint(mouseClickX, mouseClickY));
    QTest::mouseClick(part.m_pageView->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(mouseClickX, mouseClickY), 1000);

    // expect that the urlHandler signal was called
    QTRY_COMPARE(openUrlSignalSpy.count(), 1);
    QList<QVariant> arguments = openUrlSignalSpy.takeFirst();
    QCOMPARE(arguments.at(0).value<QUrl>(), QUrl(QStringLiteral("mailto:foo@foo.bar")));
}

// r-click on the selected text gives the "Go To:" content menu option
void PartTest::testTextSelectionBlankContextMenu_data()
{
    QTest::addColumn<bool>("insidePage");
    QTest::addColumn<bool>("dragBlank");
    QTest::newRow("page-blank") << true << false;
    QTest::newRow("outside-page") << false << false;
    QTest::newRow("page-blank-after-empty-drag") << true << true;
}

void PartTest::testTextSelectionBlankContextMenu()
{
    QFETCH(bool, insidePage);
    QFETCH(bool, dragBlank);
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    // A generated blank page avoids relying on text/annotations in shared fixtures.
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString source = temporary.filePath(QStringLiteral("blank-context-menu.pdf"));
    {
        QPdfWriter writer(source);
        QPainter painter(&writer);
        QVERIFY(painter.isActive());
        painter.fillRect(QRect(0, 0, writer.width(), writer.height()), Qt::white);
        QVERIFY(painter.end());
    }
    Okular::Part part(nullptr, QVariantList());
    QVERIFY(openDocument(&part, source));
    part.widget()->resize(800, 600);
    part.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));
    part.m_document->setViewportPage(0);
    QAction *fitPage = part.actionCollection()->action(QStringLiteral("view_fit_to_page"));
    QVERIFY(fitPage);
    fitPage->trigger();
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));

    QAction *textSelection = part.actionCollection()->action(QStringLiteral("mouse_textselect"));
    QVERIFY(textSelection);
    textSelection->trigger();
    QVERIFY(textSelection->isChecked());

    PageView *view = part.m_pageView;
    QWidget *viewport = view->viewport();
    // Use actual page mapping rather than assuming a particular zoom, margin or scroll position.
    const auto findClickPosition = [&]() {
        for (int y = 2; y < viewport->height() - 2; y += 4) {
            for (int x = 2; x < viewport->width() - 2; x += 4) {
                const QPoint position(x, y);
                int pageNumber = -1;
                Okular::NormalizedPoint point;
                const bool onPage = view->mapGlobalPosToPagePoint(viewport->mapToGlobal(position), &pageNumber, &point);
                // Pick the blank page interior, or the surrounding viewport margin.
                if ((insidePage && onPage && pageNumber == 0 && point.x > 0.4 && point.x < 0.6 && point.y > 0.4 && point.y < 0.6) || (!insidePage && !onPage)) {
                    return position;
                }
            }
        }
        return QPoint(-1, -1);
    };
    QTRY_VERIFY(findClickPosition().x() >= 0);
    const QPoint position = findClickPosition();
    const QPoint globalPosition = viewport->mapToGlobal(position);
    if (dragBlank) {
        const QPoint end = position + QPoint(40, 20);
        int pageNumber = -1;
        Okular::NormalizedPoint point;
        QVERIFY(view->mapGlobalPosToPagePoint(viewport->mapToGlobal(end), &pageNumber, &point));
        QCOMPARE(pageNumber, 0);
        simulateMouseSelection(position.x(), position.y(), end.x(), end.y(), viewport);
        const Okular::Page *page = part.m_document->page(0);
        QVERIFY(!page->textSelection());
        QVERIFY(!view->hasTextSelection());
    }
    // PageView's metaobject/signal symbols are not exported on Windows.
    qRegisterMetaType<const Okular::Page *>();
    QSignalSpy rightClicks(view, SIGNAL(rightClick(const Okular::Page *, QPoint)));
    QVERIFY(rightClicks.isValid());

    bool pageMenuShown = false;
    // exec() starts a nested event loop. Always dismiss every visible menu, even an unexpected
    // one, before asserting anything. A local single-shot also cancels the callback if no menu
    // is opened (the original regression), so neither case leaves a pending reference capture.
    QTimer menuCloser;
    menuCloser.setSingleShot(true);
    connect(&menuCloser, &QTimer::timeout, this, [&]() {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *menu = qobject_cast<QMenu *>(widget); menu && menu->isVisible()) {
                // Shared action identity identifies Part's page menu without translated labels.
                pageMenuShown |= menu->actions().contains(part.m_prevBookmark) && menu->actions().contains(part.m_nextBookmark);
                menu->close();
            }
        }
    });
    QTest::mouseMove(viewport, position);
    menuCloser.start(100);
    QTest::mouseClick(viewport, Qt::RightButton, Qt::NoModifier, position);
    menuCloser.stop();

    QCOMPARE(rightClicks.count(), 1);
    QCOMPARE(qvariant_cast<const Okular::Page *>(rightClicks.at(0).at(0)), insidePage ? part.m_document->page(0) : nullptr);
    QCOMPARE(rightClicks.at(0).at(1).toPoint(), globalPosition);
    if (insidePage) {
        QVERIFY(pageMenuShown);
    } else {
        QVERIFY(!pageMenuShown);
    }
}

void PartTest::testRClickWhileLinkTextIsSelected()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/pdf_with_links.pdf")));
    // resize window to avoid problem with selection areas
    part.widget()->resize(800, 600);
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    part.m_document->setViewportPage(0);

    // wait for pixmap
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));

    const int width = part.m_pageView->horizontalScrollBar()->maximum() + part.m_pageView->viewport()->width();
    const int height = part.m_pageView->verticalScrollBar()->maximum() + part.m_pageView->viewport()->height();

    // enter text-selection mode
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotSetMouseTextSelect"));

    const double mouseY = height * 0.162;
    const double mouseStartX = width * 0.42;
    const double mouseEndX = width * 0.60;

    simulateMouseSelection(mouseStartX, mouseY, mouseEndX, mouseY, part.m_pageView->viewport());
    QVERIFY(part.m_pageView->hasTextSelection());
    QVERIFY(part.m_document->page(0)->textSelection());
    QVERIFY(!part.m_document->page(0)->text(part.m_document->page(0)->textSelection()).trimmed().isEmpty());

    // Need to do this because the pop-menu will have his own mainloop and will block tests until
    // the menu disappear
    PageView *view = part.m_pageView;
    bool menuClosed = false;
    QTimer::singleShot(2000, view, [view, &menuClosed]() {
        // check if popup menu is active and visible
        QMenu *menu = qobject_cast<QMenu *>(view->findChild<QMenu *>(QStringLiteral("PopupMenu")));
        QVERIFY(menu);
        QVERIFY(menu->isVisible());

        // check if the menu contains go-to link action
        QAction *goToAction = qobject_cast<QAction *>(menu->findChild<QAction *>(QStringLiteral("GoToAction")));
        QVERIFY(goToAction);

        // check if the "follow this link" action is not visible
        QAction *processLinkAction = qobject_cast<QAction *>(menu->findChild<QAction *>(QStringLiteral("ProcessLinkAction")));
        QVERIFY(!processLinkAction);

        // check if the "copy link address" action is not visible
        QAction *copyLinkLocation = qobject_cast<QAction *>(menu->findChild<QAction *>(QStringLiteral("CopyLinkLocationAction")));
        QVERIFY(!copyLinkLocation);

        // close menu to continue test
        menu->close();
        menuClosed = true;
    });

    // click on url
    const double mouseClickX = width * 0.425;
    const double mouseClickY = height * 0.162;

    QTest::mouseMove(part.m_pageView->viewport(), QPoint(mouseClickX, mouseClickY));
    QTest::mouseClick(part.m_pageView->viewport(), Qt::RightButton, Qt::NoModifier, QPoint(mouseClickX, mouseClickY), 1000);

    // will continue after pop-menu get closed
    QTRY_VERIFY(menuClosed);
}

// r-click on the link gives the "follow this link" content menu option
void PartTest::testRClickOverLinkWhileLinkTextIsSelected()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/pdf_with_links.pdf")));
    // resize window to avoid problem with selection areas
    part.widget()->resize(800, 600);
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    part.m_document->setViewportPage(0);

    // wait for pixmap
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));

    const int width = part.m_pageView->horizontalScrollBar()->maximum() + part.m_pageView->viewport()->width();
    const int height = part.m_pageView->verticalScrollBar()->maximum() + part.m_pageView->viewport()->height();

    // enter text-selection mode
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotSetMouseTextSelect"));

    const double mouseY = height * 0.162;
    const double mouseStartX = width * 0.42;
    const double mouseEndX = width * 0.60;

    simulateMouseSelection(mouseStartX, mouseY, mouseEndX, mouseY, part.m_pageView->viewport());
    QVERIFY(part.m_pageView->hasTextSelection());
    QVERIFY(part.m_document->page(0)->textSelection());
    QVERIFY(!part.m_document->page(0)->text(part.m_document->page(0)->textSelection()).trimmed().isEmpty());

    // Need to do this because the pop-menu will have his own mainloop and will block tests until
    // the menu disappear
    PageView *view = part.m_pageView;
    bool menuClosed = false;
    QTimer::singleShot(2000, view, [view, &menuClosed]() {
        // check if popup menu is active and visible
        QMenu *menu = qobject_cast<QMenu *>(view->findChild<QMenu *>(QStringLiteral("PopupMenu")));
        QVERIFY(menu);
        QVERIFY(menu->isVisible());

        // check if the menu contains "follow this link" action
        QAction *processLinkAction = qobject_cast<QAction *>(menu->findChild<QAction *>(QStringLiteral("ProcessLinkAction")));
        QVERIFY(processLinkAction);

        // check if the menu contains "copy link address" action
        QAction *copyLinkLocation = qobject_cast<QAction *>(menu->findChild<QAction *>(QStringLiteral("CopyLinkLocationAction")));
        QVERIFY(copyLinkLocation);

        // close menu to continue test
        menu->close();
        menuClosed = true;
    });

    // click on url
    const double mouseClickX = width * 0.593;
    const double mouseClickY = height * 0.162;

    QTest::mouseMove(part.m_pageView->viewport(), QPoint(mouseClickX, mouseClickY));
    QTest::mouseClick(part.m_pageView->viewport(), Qt::RightButton, Qt::NoModifier, QPoint(mouseClickX, mouseClickY), 1000);

    // will continue after pop-menu get closed
    QTRY_VERIFY(menuClosed);
}

void PartTest::testRClickOnSelectionModeShoulShowFollowTheLinkMenu()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/pdf_with_links.pdf")));
    // resize window to avoid problem with selection areas
    part.widget()->resize(800, 600);
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    part.m_document->setViewportPage(0);

    // wait for pixmap
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));

    const int width = part.m_pageView->horizontalScrollBar()->maximum() + part.m_pageView->viewport()->width();
    const int height = part.m_pageView->verticalScrollBar()->maximum() + part.m_pageView->viewport()->height();

    // enter text-selection mode
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotSetMouseTextSelect"));

    // Need to do this because the pop-menu will have his own mainloop and will block tests until
    // the menu disappear
    PageView *view = part.m_pageView;
    bool menuClosed = false;
    QTimer::singleShot(2000, view, [view, &menuClosed]() {
        // check if popup menu is active and visible
        QMenu *menu = qobject_cast<QMenu *>(view->findChild<QMenu *>(QStringLiteral("PopupMenu")));
        QVERIFY(menu);
        QVERIFY(menu->isVisible());

        // check if the menu contains "Follow this link" action
        QAction *processLink = qobject_cast<QAction *>(menu->findChild<QAction *>(QStringLiteral("ProcessLinkAction")));
        QVERIFY(processLink);

        // chek if the menu contains  "Copy Link Address" action
        QAction *actCopyLinkLocation = qobject_cast<QAction *>(menu->findChild<QAction *>(QStringLiteral("CopyLinkLocationAction")));
        QVERIFY(actCopyLinkLocation);

        // close menu to continue test
        menu->close();
        menuClosed = true;
    });

    // r-click on url
    const double mouseClickX = width * 0.604;
    const double mouseClickY = height * 0.162;

    QTest::mouseMove(part.m_pageView->viewport(), QPoint(mouseClickX, mouseClickY));
    QTest::mouseClick(part.m_pageView->viewport(), Qt::RightButton, Qt::NoModifier, QPoint(mouseClickX, mouseClickY), 1000);

    // will continue after pop-menu get closed
    QTRY_VERIFY(menuClosed);
}

void PartTest::testClickAnywhereAfterSelectionShouldUnselect()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/pdf_with_links.pdf")));
    // resize window to avoid problem with selection areas
    part.widget()->resize(800, 600);
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    part.m_document->setViewportPage(0);

    // wait for pixmap
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));

    const int width = part.m_pageView->horizontalScrollBar()->maximum() + part.m_pageView->viewport()->width();
    const int height = part.m_pageView->verticalScrollBar()->maximum() + part.m_pageView->viewport()->height();

    // enter text-selection mode
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotSetMouseTextSelect"));

    const double mouseY = height * 0.162;
    const double mouseStartX = width * 0.42;
    const double mouseEndX = width * 0.60;

    simulateMouseSelection(mouseStartX, mouseY, mouseEndX, mouseY, part.m_pageView->viewport());

    // click on url
    const double mouseClickX = width * 0.10;

    QTest::mouseMove(part.m_pageView->viewport(), QPoint(mouseClickX, mouseY));
    QTest::mouseClick(part.m_pageView->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(mouseClickX, mouseY), 1000);

    QApplication::clipboard()->clear();
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "copyTextSelection"));

    // check if copied text is empty what means no text selected
    QVERIFY(QApplication::clipboard()->text().isEmpty());
}

void PartTest::testeRectSelectionStartingOnLinks()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/pdf_with_links.pdf")));
    // hide info messages as they interfere with selection area
    Okular::Settings::self()->setShowEmbeddedContentMessages(false);
    Okular::Settings::self()->setShowOSD(false);

    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    part.m_document->setViewportPage(0);

    // wait for pixmap
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));

    const int width = part.m_pageView->horizontalScrollBar()->maximum() + part.m_pageView->viewport()->width();
    const int height = part.m_pageView->verticalScrollBar()->maximum() + part.m_pageView->viewport()->height();

    // enter text-selection mode
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotSetMouseSelect"));

    const double mouseStartY = height * 0.127;
    const double mouseEndY = height * 0.127;
    const double mouseStartX = width * 0.28;
    const double mouseEndX = width * 0.382;

    // Need to do this because the pop-menu will have his own mainloop and will block tests until
    // the menu disappear
    PageView *view = part.m_pageView;
    bool menuClosed = false;
    QTimer::singleShot(2000, view, [view, &menuClosed]() {
        QApplication::clipboard()->clear();

        // check if popup menu is active and visible
        QMenu *menu = qobject_cast<QMenu *>(view->findChild<QMenu *>(QStringLiteral("PopupMenu")));
        QVERIFY(menu);
        QVERIFY(menu->isVisible());

        // check if the copy selected text to clipboard is present
        QAction *copyAct = qobject_cast<QAction *>(menu->findChild<QAction *>(QStringLiteral("CopyTextToClipboard")));
        QVERIFY(copyAct);

        menu->close();
        menuClosed = true;
    });

    simulateMouseSelection(mouseStartX, mouseStartY, mouseEndX, mouseEndY, part.m_pageView->viewport());

    // wait menu get closed
    QTRY_VERIFY(menuClosed);
}

void PartTest::simulateMouseSelection(double startX, double startY, double endX, double endY, QWidget *target)
{
    const int steps = 5;
    const double diffX = endX - startX;
    const double diffY = endY - startY;
    const double diffXStep = diffX / steps;
    const double diffYStep = diffY / steps;

    QTestEventList events;
    events.addMouseMove(QPoint(startX, startY));
    events.addMousePress(Qt::LeftButton, Qt::NoModifier, QPoint(startX, startY));
    for (int i = 0; i < steps - 1; ++i) {
        events.addMouseMove(QPoint(startX + i * diffXStep, startY + i * diffYStep));
        events.addDelay(100);
    }
    events.addMouseMove(QPoint(endX, endY));
    events.addDelay(100);
    events.addMouseRelease(Qt::LeftButton, Qt::NoModifier, QPoint(endX, endY));

    events.simulate(target);
}

void PartTest::testSaveAsToNonExistingPath()
{
    Okular::Part part(nullptr, {});
    part.openDocument(QStringLiteral(KDESRCDIR "data/file1.pdf"));

    QString saveFilePath;
    {
        QTemporaryFile saveFile(QStringLiteral("%1/okrXXXXXX.pdf").arg(QDir::tempPath()));
        bool success = saveFile.open();
        QVERIFY(success);
        saveFilePath = saveFile.fileName();
        // QTemporaryFile is destroyed and the file it created is gone, this is a TOCTOU but who cares
    }

    QVERIFY(!QFileInfo::exists(saveFilePath));

    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFilePath), Part::NoSaveAsFlags));

    QFile::remove(saveFilePath);
}

void PartTest::testSaveAsToSymlink()
{
#ifdef Q_OS_UNIX
    Okular::Part part(nullptr, {});
    part.openDocument(QStringLiteral(KDESRCDIR "data/file1.pdf"));

    QTemporaryFile newFile(QStringLiteral("%1/okrXXXXXX.pdf").arg(QDir::tempPath()));
    bool success = newFile.open();
    QVERIFY(success);

    QString linkFilePath;
    {
        QTemporaryFile linkFile(QStringLiteral("%1/okrXXXXXX.pdf").arg(QDir::tempPath()));
        success = linkFile.open();
        QVERIFY(success);
        linkFilePath = linkFile.fileName();
        // QTemporaryFile is destroyed and the file it created is gone, this is a TOCTOU but who cares
    }

    QFile::link(newFile.fileName(), linkFilePath);

    QVERIFY(QFileInfo(linkFilePath).isSymLink());

    QVERIFY(part.saveAs(QUrl::fromLocalFile(linkFilePath), Part::NoSaveAsFlags));

    QVERIFY(QFileInfo(linkFilePath).isSymLink());

    QFile::remove(linkFilePath);
#endif
}

void PartTest::testSaveIsSymlink()
{
#ifdef Q_OS_UNIX
    Okular::Part part(nullptr, {});

    QString newFilePath;
    {
        QTemporaryFile newFile(QStringLiteral("%1/okrXXXXXX.pdf").arg(QDir::tempPath()));
        bool success = newFile.open();
        QVERIFY(success);
        newFilePath = newFile.fileName();
        // QTemporaryFile is destroyed and the file it created is gone, this is a TOCTOU but who cares
    }

    QFile::copy(QStringLiteral(KDESRCDIR "data/file1.pdf"), newFilePath);

    QString linkFilePath;
    {
        QTemporaryFile linkFile(QStringLiteral("%1/okrXXXXXX.pdf").arg(QDir::tempPath()));
        bool success = linkFile.open();
        QVERIFY(success);
        linkFilePath = linkFile.fileName();
        // QTemporaryFile is destroyed and the file it created is gone, this is a TOCTOU but who cares
    }

    QFile::link(newFilePath, linkFilePath);

    QVERIFY(QFileInfo(linkFilePath).isSymLink());

    part.openDocument(linkFilePath);
    QVERIFY(part.saveAs(QUrl::fromLocalFile(linkFilePath), Part::NoSaveAsFlags));

    QVERIFY(QFileInfo(linkFilePath).isSymLink());

    QFile::remove(newFilePath);
    QFile::remove(linkFilePath);
#endif
}

void PartTest::testSaveAs()
{
    QFETCH(QString, file);
    QFETCH(QString, extension);
    QFETCH(bool, nativelySupportsAnnotations);
    QFETCH(bool, canSwapBackingFile);

    QScopedPointer<TestingUtils::CloseDialogHelper> closeDialogHelper;

    QString annotName;
    QTemporaryFile archiveSave(QStringLiteral("%1/okrXXXXXX.okular").arg(QDir::tempPath()));
    QTemporaryFile nativeDirectSave(QStringLiteral("%1/okrXXXXXX.%2").arg(QDir::tempPath(), extension));
    QTemporaryFile nativeFromArchiveFile(QStringLiteral("%1/okrXXXXXX.%2").arg(QDir::tempPath(), extension));
    QVERIFY(archiveSave.open());
    archiveSave.close();
    QVERIFY(nativeDirectSave.open());
    nativeDirectSave.close();
    QVERIFY(nativeFromArchiveFile.open());
    nativeFromArchiveFile.close();

    qDebug() << "Open file, add annotation and save both natively and to .okular";
    {
        Okular::Part part(nullptr, {});
        new QAbstractItemModelTester(part.annotationsModel(), &part);
        part.openDocument(file);
        part.m_document->documentInfo();

        QCOMPARE(part.m_document->canSwapBackingFile(), canSwapBackingFile);

        Okular::Annotation *annot = new Okular::TextAnnotation();
        annot->setBoundingRectangle(Okular::NormalizedRect(0.1, 0.1, 0.15, 0.15));
        annot->setContents(QStringLiteral("annot contents"));
        part.m_document->addPageAnnotation(0, annot);
        annotName = annot->uniqueName();

        if (canSwapBackingFile) {
            if (!nativelySupportsAnnotations) {
                closeDialogHelper.reset(new TestingUtils::CloseDialogHelper(&part, QDialogButtonBox::No)); // this is the "you're going to lose the annotations" dialog
            }
            QVERIFY(part.saveAs(QUrl::fromLocalFile(nativeDirectSave.fileName()), Part::NoSaveAsFlags));
            // For backends that don't support annotations natively we mark the part as still modified
            // after a save because we keep the annotation around but it will get lost if the user closes the app
            // so we want to give her a last chance to save on close with the "you have changes dialog"
            QCOMPARE(part.isModified(), !nativelySupportsAnnotations);
            QVERIFY(part.saveAs(QUrl::fromLocalFile(archiveSave.fileName()), Part::SaveAsOkularArchive));
        } else {
            // We need to save to archive first otherwise we lose the annotation

            closeDialogHelper.reset(new TestingUtils::CloseDialogHelper(&part, QDialogButtonBox::Yes)); // this is the "you're going to lose the undo/redo stack" dialog
            QVERIFY(part.saveAs(QUrl::fromLocalFile(archiveSave.fileName()), Part::SaveAsOkularArchive));

            if (!nativelySupportsAnnotations) {
                closeDialogHelper.reset(new TestingUtils::CloseDialogHelper(&part, QDialogButtonBox::No)); // this is the "you're going to lose the annotations" dialog
            }
            QVERIFY(part.saveAs(QUrl::fromLocalFile(nativeDirectSave.fileName()), Part::NoSaveAsFlags));
        }

        QCOMPARE(part.m_document->documentInfo().get(Okular::DocumentInfo::FilePath), part.m_document->currentDocument().toDisplayString());
        part.closeUrl();
    }

    qDebug() << "Open the .okular, check that the annotation is present and save to native";
    {
        Okular::Part part(nullptr, {});
        new QAbstractItemModelTester(part.annotationsModel(), &part);
        part.openDocument(archiveSave.fileName());
        part.m_document->documentInfo();

        QCOMPARE(part.m_document->page(0)->annotations().size(), 1);
        QCOMPARE(part.m_document->page(0)->annotations().constFirst()->uniqueName(), annotName);

        if (!nativelySupportsAnnotations) {
            closeDialogHelper.reset(new TestingUtils::CloseDialogHelper(&part, QDialogButtonBox::No)); // this is the "you're going to lose the annotations" dialog
        }
        QVERIFY(part.saveAs(QUrl::fromLocalFile(nativeFromArchiveFile.fileName()), Part::NoSaveAsFlags));

        if (canSwapBackingFile && !nativelySupportsAnnotations) {
            // For backends that don't support annotations natively we mark the part as still modified
            // after a save because we keep the annotation around but it will get lost if the user closes the app
            // so we want to give her a last chance to save on close with the "you have changes dialog"
            closeDialogHelper.reset(new TestingUtils::CloseDialogHelper(&part, QDialogButtonBox::No)); // this is the "do you want to save or discard" dialog
        }

        QCOMPARE(part.m_document->documentInfo().get(Okular::DocumentInfo::FilePath), part.m_document->currentDocument().toDisplayString());
        part.closeUrl();
    }

    qDebug() << "Open the native file saved directly, and check that the annot"
             << "is there iff we expect it";
    {
        Okular::Part part(nullptr, {});
        new QAbstractItemModelTester(part.annotationsModel(), &part);
        part.openDocument(nativeDirectSave.fileName());

        QCOMPARE(part.m_document->page(0)->annotations().size(), nativelySupportsAnnotations ? 1 : 0);
        if (nativelySupportsAnnotations) {
            QCOMPARE(part.m_document->page(0)->annotations().constFirst()->uniqueName(), annotName);
        }

        part.closeUrl();
    }

    qDebug() << "Open the native file saved from the .okular, and check that the annot"
             << "is there iff we expect it";
    {
        Okular::Part part(nullptr, {});
        part.openDocument(nativeFromArchiveFile.fileName());

        QCOMPARE(part.m_document->page(0)->annotations().size(), nativelySupportsAnnotations ? 1 : 0);
        if (nativelySupportsAnnotations) {
            QCOMPARE(part.m_document->page(0)->annotations().constFirst()->uniqueName(), annotName);
        }

        part.closeUrl();
    }
}

void PartTest::testFailedBackingFileSwapKeepsDocumentUsable()
{
    Part part(nullptr, {});
    const QString sourceFile = QStringLiteral(KDESRCDIR "data/file1.pdf");
    QVERIFY(openDocument(&part, sourceFile));

    const QUrl originalUrl = part.m_document->currentDocument();
    const int originalPageCount = part.m_document->pages();
    const Page *const originalFirstPage = part.m_document->page(0);
    QVERIFY(originalFirstPage);

    QTemporaryFile invalidReplacement(QStringLiteral("%1/okrXXXXXX.okular").arg(QDir::tempPath()));
    QVERIFY(invalidReplacement.open());
    invalidReplacement.write(
        "%PDF-1.4\n"
        "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n"
        "2 0 obj << /Type /Pages /Count 1 /Kids [3 0 R] >> endobj\n"
        "trailer << /Root 1 0 R >>\n"
        "%%EOF\n");
    invalidReplacement.close();

    QVERIFY(!part.m_document->swapBackingFile(invalidReplacement.fileName(), QUrl::fromLocalFile(invalidReplacement.fileName())));
    QVERIFY(part.m_document->isOpened());
    QCOMPARE(part.m_document->currentDocument(), originalUrl);
    QCOMPARE(part.m_document->pages(), originalPageCount);
    QCOMPARE(part.m_document->page(0), originalFirstPage);
    QVERIFY(part.m_document->page(0)->width() > 0.0);
    QVERIFY(part.m_document->page(0)->height() > 0.0);

    part.closeUrl();
}

void PartTest::testSaveAs_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<QString>("extension");
    QTest::addColumn<bool>("nativelySupportsAnnotations");
    QTest::addColumn<bool>("canSwapBackingFile");

    QTest::newRow("pdf") << KDESRCDIR "data/file1.pdf" << "pdf" << true << true;
    QTest::newRow("pdf.gz") << KDESRCDIR "data/file1.pdf.gz" << "pdf" << true << true;
    QTest::newRow("epub") << KDESRCDIR "data/contents.epub" << "epub" << false << false;
    QTest::newRow("jpg") << KDESRCDIR "data/potato.jpg" << "jpg" << false << true;
}

void PartTest::testSidebarItemAfterSaving()
{
    Okular::Part part(nullptr, {});
    QWidget *currentSidebarItem = part.m_sidebar->currentItem(); // thumbnails
    openDocument(&part, QStringLiteral(KDESRCDIR "data/tocreload.pdf"));
    // since it has TOC it changes to TOC
    QVERIFY(currentSidebarItem != part.m_sidebar->currentItem());
    // now change back to thumbnails
    part.m_sidebar->setCurrentItem(currentSidebarItem);

    part.saveAs(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/tocreload.pdf")));

    // Check it is still thumbnails after saving
    QCOMPARE(currentSidebarItem, part.m_sidebar->currentItem());
}

void PartTest::testViewModeSavingPerFile()
{
    Okular::Part part(nullptr, {});

    // Open some file
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/file1.pdf")));

    // Switch to 'continuous' view mode
    part.m_pageView->setCapability(Okular::View::ViewCapability::Continuous, QVariant(true));

    // Close document
    part.closeUrl();

    // Open another file
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/file2.pdf")));

    // Switch to 'non-continuous' mode
    part.m_pageView->setCapability(Okular::View::ViewCapability::Continuous, QVariant(false));

    // Close that document, too
    part.closeUrl();

    // Open first document again
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/file1.pdf")));

    // If per-file view mode saving works, the view mode should be 'continuous' again.
    QVERIFY(part.m_pageView->capability(Okular::View::ViewCapability::Continuous).toBool());
}

void PartTest::testSaveAsUndoStackAnnotations()
{
    QFETCH(QString, file);
    QFETCH(QString, extension);
    QFETCH(bool, nativelySupportsAnnotations);
    QFETCH(bool, canSwapBackingFile);
    QFETCH(bool, saveToArchive);

    const Part::SaveAsFlag saveFlags = saveToArchive ? Part::SaveAsOkularArchive : Part::NoSaveAsFlags;

    QScopedPointer<TestingUtils::CloseDialogHelper> closeDialogHelper;

    // closeDialogHelper relies on the availability of the "Continue" button to drop changes
    // when saving to a file format not supporting those. However, this button is only sensible
    // and available for "Save As", but not for "Save". By alternately saving to saveFile1 and
    // saveFile2 we always force "Save As", so closeDialogHelper keeps working.
    QTemporaryFile saveFile1(QStringLiteral("%1/okrXXXXXX_1.%2").arg(QDir::tempPath(), extension));
    QVERIFY(saveFile1.open());
    saveFile1.close();
    QTemporaryFile saveFile2(QStringLiteral("%1/okrXXXXXX_2.%2").arg(QDir::tempPath(), extension));
    QVERIFY(saveFile2.open());
    saveFile2.close();

    Okular::Part part(nullptr, {});
    part.openDocument(file);
    new QAbstractItemModelTester(part.annotationsModel(), &part);

    QCOMPARE(part.m_document->canSwapBackingFile(), canSwapBackingFile);

    Okular::Annotation *annot = new Okular::TextAnnotation();
    annot->setBoundingRectangle(Okular::NormalizedRect(0.1, 0.1, 0.15, 0.15));
    annot->setContents(QStringLiteral("annot contents"));
    part.m_document->addPageAnnotation(0, annot);
    QString annotName = annot->uniqueName();

    if (!nativelySupportsAnnotations && !saveToArchive) {
        closeDialogHelper.reset(new TestingUtils::CloseDialogHelper(&part, QDialogButtonBox::No)); // this is the "you're going to lose the annotations" dialog
    }

    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFile1.fileName()), saveFlags));

    if (!canSwapBackingFile) {
        // The undo/redo stack gets lost if you can not swap the backing file
        QVERIFY(!part.m_document->canUndo());
        QVERIFY(!part.m_document->canRedo());
        return;
    }

    // Check we can still undo the annot add after save
    QVERIFY(part.m_document->canUndo());
    part.m_document->undo();
    QVERIFY(!part.m_document->canUndo());

    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFile1.fileName()), saveFlags));
    QVERIFY(part.m_document->page(0)->annotations().isEmpty());

    // Check we can redo the annot add after save
    QVERIFY(part.m_document->canRedo());
    part.m_document->redo();
    QVERIFY(!part.m_document->canRedo());

    if (nativelySupportsAnnotations) {
        // If the annots are provided by the backend we need to refetch the pointer after save
        annot = part.m_document->page(0)->annotation(annotName);
        QVERIFY(annot);
    }

    // Remove the annotation, creates another undo command
    QVERIFY(part.m_document->canRemovePageAnnotation(annot));
    part.m_document->removePageAnnotation(0, annot);
    QVERIFY(part.m_document->page(0)->annotations().isEmpty());

    // Check we can still undo the annot remove after save
    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFile1.fileName()), saveFlags));
    QVERIFY(part.m_document->canUndo());
    part.m_document->undo();
    QVERIFY(part.m_document->canUndo());
    QCOMPARE(part.m_document->page(0)->annotations().count(), 1);

    // Check we can still undo the annot add after save
    if (!nativelySupportsAnnotations && !saveToArchive) {
        closeDialogHelper.reset(new TestingUtils::CloseDialogHelper(&part, QDialogButtonBox::No)); // this is the "you're going to lose the annotations" dialog
    }
    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFile2.fileName()), saveFlags));
    QVERIFY(part.m_document->canUndo());
    part.m_document->undo();
    QVERIFY(!part.m_document->canUndo());
    QVERIFY(part.m_document->page(0)->annotations().isEmpty());

    // Redo the add annotation
    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFile1.fileName()), saveFlags));
    QVERIFY(part.m_document->canRedo());
    part.m_document->redo();
    QVERIFY(part.m_document->canUndo());
    QVERIFY(part.m_document->canRedo());

    if (nativelySupportsAnnotations) {
        // If the annots are provided by the backend we need to refetch the pointer after save
        annot = part.m_document->page(0)->annotation(annotName);
        QVERIFY(annot);
    }

    // Add translate, adjust and modify commands
    part.m_document->translatePageAnnotation(0, annot, Okular::NormalizedPoint(0.1, 0.1));
    part.m_document->adjustPageAnnotation(0, annot, Okular::NormalizedPoint(0.1, 0.1), Okular::NormalizedPoint(0.1, 0.1));
    part.m_document->prepareToModifyAnnotationProperties(annot);
    part.m_document->modifyPageAnnotationProperties(0, annot);

    // Now check we can still undo/redo/save at all the intermediate states and things still work
    if (!nativelySupportsAnnotations && !saveToArchive) {
        closeDialogHelper.reset(new TestingUtils::CloseDialogHelper(&part, QDialogButtonBox::No)); // this is the "you're going to lose the annotations" dialog
    }
    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFile2.fileName()), saveFlags));
    QVERIFY(part.m_document->canUndo());
    part.m_document->undo();
    QVERIFY(part.m_document->canUndo());

    if (!nativelySupportsAnnotations && !saveToArchive) {
        closeDialogHelper.reset(new TestingUtils::CloseDialogHelper(&part, QDialogButtonBox::No)); // this is the "you're going to lose the annotations" dialog
    }
    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFile1.fileName()), saveFlags));
    QVERIFY(part.m_document->canUndo());
    part.m_document->undo();
    QVERIFY(part.m_document->canUndo());

    if (!nativelySupportsAnnotations && !saveToArchive) {
        closeDialogHelper.reset(new TestingUtils::CloseDialogHelper(&part, QDialogButtonBox::No)); // this is the "you're going to lose the annotations" dialog
    }
    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFile2.fileName()), saveFlags));
    QVERIFY(part.m_document->canUndo());
    part.m_document->undo();
    QVERIFY(part.m_document->canUndo());

    if (!nativelySupportsAnnotations && !saveToArchive) {
        closeDialogHelper.reset(new TestingUtils::CloseDialogHelper(&part, QDialogButtonBox::No)); // this is the "you're going to lose the annotations" dialog
    }
    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFile1.fileName()), saveFlags));
    QVERIFY(part.m_document->canUndo());
    part.m_document->undo();
    QVERIFY(!part.m_document->canUndo());
    QVERIFY(part.m_document->canRedo());
    QVERIFY(part.m_document->page(0)->annotations().isEmpty());

    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFile1.fileName()), saveFlags));
    QVERIFY(part.m_document->canRedo());
    part.m_document->redo();
    QVERIFY(part.m_document->canRedo());

    if (!nativelySupportsAnnotations && !saveToArchive) {
        closeDialogHelper.reset(new TestingUtils::CloseDialogHelper(&part, QDialogButtonBox::No)); // this is the "you're going to lose the annotations" dialog
    }
    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFile2.fileName()), saveFlags));
    QVERIFY(part.m_document->canRedo());
    part.m_document->redo();
    QVERIFY(part.m_document->canRedo());

    if (!nativelySupportsAnnotations && !saveToArchive) {
        closeDialogHelper.reset(new TestingUtils::CloseDialogHelper(&part, QDialogButtonBox::No)); // this is the "you're going to lose the annotations" dialog
    }
    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFile1.fileName()), saveFlags));
    QVERIFY(part.m_document->canRedo());
    part.m_document->redo();
    QVERIFY(part.m_document->canRedo());

    if (!nativelySupportsAnnotations && !saveToArchive) {
        closeDialogHelper.reset(new TestingUtils::CloseDialogHelper(&part, QDialogButtonBox::No)); // this is the "you're going to lose the annotations" dialog
    }
    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFile2.fileName()), saveFlags));
    QVERIFY(part.m_document->canRedo());
    part.m_document->redo();
    QVERIFY(!part.m_document->canRedo());

    closeDialogHelper.reset(new TestingUtils::CloseDialogHelper(&part, QDialogButtonBox::No)); // this is the "do you want to save or discard" dialog
    part.closeUrl();
}

void PartTest::testSaveAsUndoStackAnnotations_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<QString>("extension");
    QTest::addColumn<bool>("nativelySupportsAnnotations");
    QTest::addColumn<bool>("canSwapBackingFile");
    QTest::addColumn<bool>("saveToArchive");

    QTest::newRow("pdf") << KDESRCDIR "data/file1.pdf" << "pdf" << true << true << false;
    QTest::newRow("epub") << KDESRCDIR "data/contents.epub" << "epub" << false << false << false;
    QTest::newRow("jpg") << KDESRCDIR "data/potato.jpg" << "jpg" << false << true << false;
    QTest::newRow("pdfarchive") << KDESRCDIR "data/file1.pdf" << "okular" << true << true << true;
    QTest::newRow("jpgarchive") << KDESRCDIR "data/potato.jpg" << "okular" << false << true << true;
}

void PartTest::testSaveAsUndoStackForms()
{
    QFETCH(QString, file);
    QFETCH(QString, extension);
    QFETCH(bool, saveToArchive);

    const Part::SaveAsFlag saveFlags = saveToArchive ? Part::SaveAsOkularArchive : Part::NoSaveAsFlags;

    QTemporaryFile saveFile(QStringLiteral("%1/okrXXXXXX.%2").arg(QDir::tempPath(), extension));
    QVERIFY(saveFile.open());
    saveFile.close();

    Okular::Part part(nullptr, {});
    part.openDocument(file);

    const QList<Okular::FormField *> pageFormFields = part.m_document->page(0)->formFields();
    for (FormField *ff : pageFormFields) {
        if (ff->id() == 65537) {
            QCOMPARE(ff->type(), FormField::FormText);
            FormFieldText *fft = static_cast<FormFieldText *>(ff);
            part.m_document->editFormText(0, fft, QStringLiteral("BlaBla"), 6, 0, 0, QString());
        } else if (ff->id() == 65538) {
            QCOMPARE(ff->type(), FormField::FormButton);
            FormFieldButton *ffb = static_cast<FormFieldButton *>(ff);
            QCOMPARE(ffb->buttonType(), FormFieldButton::Radio);
            part.m_document->editFormButtons(0, QList<FormFieldButton *>() << ffb, QList<bool>() << true);
        } else if (ff->id() == 65542) {
            QCOMPARE(ff->type(), FormField::FormChoice);
            FormFieldChoice *ffc = static_cast<FormFieldChoice *>(ff);
            QCOMPARE(ffc->choiceType(), FormFieldChoice::ListBox);
            part.m_document->editFormList(0, ffc, QList<int>() << 1);
        } else if (ff->id() == 65543) {
            QCOMPARE(ff->type(), FormField::FormChoice);
            FormFieldChoice *ffc = static_cast<FormFieldChoice *>(ff);
            QCOMPARE(ffc->choiceType(), FormFieldChoice::ComboBox);
            part.m_document->editFormCombo(0, ffc, QStringLiteral("combo2"), 3, 0, 0);
        }
    }

    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFile.fileName()), saveFlags));

    QVERIFY(part.m_document->canUndo());
    part.m_document->undo();
    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFile.fileName()), saveFlags));

    QVERIFY(part.m_document->canUndo());
    part.m_document->undo();
    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFile.fileName()), saveFlags));

    QVERIFY(part.m_document->canUndo());
    part.m_document->undo();
    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFile.fileName()), saveFlags));

    QVERIFY(part.m_document->canUndo());
    part.m_document->undo();
    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFile.fileName()), saveFlags));
    QVERIFY(!part.m_document->canUndo());

    QVERIFY(part.m_document->canRedo());
    part.m_document->redo();
    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFile.fileName()), saveFlags));

    QVERIFY(part.m_document->canRedo());
    part.m_document->redo();
    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFile.fileName()), saveFlags));

    QVERIFY(part.m_document->canRedo());
    part.m_document->redo();
    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFile.fileName()), saveFlags));

    QVERIFY(part.m_document->canRedo());
    part.m_document->redo();
    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFile.fileName()), saveFlags));
}

void PartTest::testSaveAsUndoStackForms_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<QString>("extension");
    QTest::addColumn<bool>("saveToArchive");

    QTest::newRow("pdf") << KDESRCDIR "data/formSamples.pdf" << "pdf" << false;
    QTest::newRow("pdfarchive") << KDESRCDIR "data/formSamples.pdf" << "okular" << true;
}

void PartTest::testRotateSinglePageBackend()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString workingFile = tempDir.filePath(QStringLiteral("rotate-page-backend-source.pdf"));
    const QString rotatedFile = tempDir.filePath(QStringLiteral("rotate-page-backend-output.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/simple-multipage.pdf"), workingFile));

    Okular::Part part(nullptr, {});
    QVERIFY(openDocument(&part, workingFile));
    QVERIFY(part.m_document->canRotatePage());
    QVERIFY(part.m_document->pages() > 2);

    constexpr int targetPage = 1;
    const Okular::Rotation previousPageOrientation = part.m_document->page(targetPage - 1)->orientation();
    const Okular::Rotation originalOrientation = part.m_document->page(targetPage)->orientation();
    const Okular::Rotation nextPageOrientation = part.m_document->page(targetPage + 1)->orientation();
    const auto rotatedOrientation = static_cast<Okular::Rotation>((static_cast<int>(originalOrientation) + 1) % 4);

    QString errorText;
    QVERIFY2(part.m_document->rotatePage(targetPage, static_cast<int>(rotatedOrientation) * 90, &errorText), qPrintable(errorText));
    QVERIFY2(part.m_document->saveChanges(rotatedFile, &errorText), qPrintable(errorText));

    Okular::Part reopenedPart(nullptr, {});
    QVERIFY(openDocument(&reopenedPart, rotatedFile));
    QCOMPARE(reopenedPart.m_document->page(targetPage - 1)->orientation(), previousPageOrientation);
    QCOMPARE(reopenedPart.m_document->page(targetPage)->orientation(), rotatedOrientation);
    QCOMPARE(reopenedPart.m_document->page(targetPage + 1)->orientation(), nextPageOrientation);
}

void PartTest::testRotateSinglePage()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString workingFile = tempDir.filePath(QStringLiteral("rotate-page-source.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/simple-multipage.pdf"), workingFile));

    Okular::Part part(nullptr, {});
    QVERIFY(openDocument(&part, workingFile));
    QVERIFY(part.m_document->canRotatePage());
    QVERIFY(part.m_document->pages() > 2);

    constexpr int targetPage = 1;
    const Okular::Rotation previousPageOrientation = part.m_document->page(targetPage - 1)->orientation();
    const Okular::Rotation originalOrientation = part.m_document->page(targetPage)->orientation();
    const Okular::Rotation nextPageOrientation = part.m_document->page(targetPage + 1)->orientation();
    const Okular::Page *originalPageObject = part.m_document->page(targetPage);
    const auto rotatedOrientation = static_cast<Okular::Rotation>((static_cast<int>(originalOrientation) + 1) % 4);

    part.setPageRotation(targetPage, static_cast<int>(rotatedOrientation) * 90);
    QVERIFY(part.m_document->page(targetPage) != originalPageObject);
    QCOMPARE(part.m_document->page(targetPage - 1)->orientation(), previousPageOrientation);
    QCOMPARE(part.m_document->page(targetPage)->orientation(), rotatedOrientation);
    QCOMPARE(part.m_document->page(targetPage + 1)->orientation(), nextPageOrientation);
    QVERIFY(part.m_document->canUndo());

    const Okular::Page *rotatedPageObject = part.m_document->page(targetPage);
    part.m_document->undo();
    QVERIFY(part.m_document->page(targetPage) != rotatedPageObject);
    QCOMPARE(part.m_document->page(targetPage)->orientation(), originalOrientation);
    QVERIFY(part.m_document->canRedo());

    part.m_document->redo();
    QCOMPARE(part.m_document->page(targetPage)->orientation(), rotatedOrientation);
    QVERIFY(part.m_document->canUndo());

    part.m_document->undo();
    QCOMPARE(part.m_document->page(targetPage)->orientation(), originalOrientation);
    QVERIFY(!part.m_document->canUndo());
}

void PartTest::testLatexNoteOnRotatedPage()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString workingFile = tempDir.filePath(QStringLiteral("latex-rotated-page-source.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/simple-multipage.pdf"), workingFile));

    Okular::Part part(nullptr, {});
    QVERIFY(openDocument(&part, workingFile));

    constexpr int targetPage = 1;
    part.setPageRotation(targetPage, 90);
    const Okular::Page *page = part.m_document->page(targetPage);
    QCOMPARE(page->orientation(), Okular::Rotation90);

    auto *annotation = new Okular::StampAnnotation;
    annotation->setBoundingRectangle(Okular::NormalizedRect(0.2, 0.2, 0.5, 0.3));
    annotation->setContents(QStringLiteral("Rotated LaTeX note"));
    annotation->setOkularLatex(true);
    QVERIFY(annotation->flags() & Okular::Annotation::FixedRotation);
    annotation->setLatexNoteType(Okular::Annotation::LatexNoteBoxed);
    const QString appearanceFile = tempDir.filePath(QStringLiteral("latex-default-note.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(":/mengshee/data/latex-default-note.pdf"), appearanceFile));
    annotation->setLatexAppearancePdfFileName(appearanceFile);
    annotation->setLatexLayoutWidth(120.0);
    annotation->setLatexPadding(3.0);
    annotation->setLatexTextColor(Qt::black);
    annotation->setLatexFillColor(QColor(QStringLiteral("#ffff00")));
    annotation->setLatexBorderColor(Qt::red);
    annotation->style().setWidth(2.0);
    part.m_document->addPageAnnotation(targetPage, annotation);

    const QString annotationName = annotation->uniqueName();
    const QString outputFile = tempDir.filePath(QStringLiteral("latex-note-rotated.pdf"));
    QString errorText;
    QVERIFY2(part.m_document->saveChanges(outputFile, &errorText), qPrintable(errorText));
    QVERIFY(QFileInfo::exists(outputFile));

    Okular::Part reopenedPart(nullptr, {});
    QVERIFY(openDocument(&reopenedPart, outputFile));
    const Okular::Page *reopenedPage = reopenedPart.m_document->page(targetPage);
    QCOMPARE(reopenedPage->orientation(), Okular::Rotation90);
    const Okular::Annotation *reopenedAnnotation = reopenedPage->annotation(annotationName);
    QVERIFY(reopenedAnnotation);
    QVERIFY(reopenedAnnotation->isOkularLatex());
    QVERIFY(reopenedAnnotation->flags() & Okular::Annotation::FixedRotation);
}

void PartTest::testLatexAppearanceResizeHistory_data()
{
    QTest::addColumn<bool>("callout");
    QTest::addColumn<int>("rotation");
    QTest::newRow("boxed") << false << 0;
    QTest::newRow("callout") << true << 0;
    QTest::newRow("boxed-rotated") << false << 90;
    QTest::newRow("callout-rotated") << true << 90;
}

void PartTest::testLatexAppearanceResizeHistory()
{
    QFETCH(bool, callout);
    QFETCH(int, rotation);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString working = dir.filePath(QStringLiteral("document.pdf"));
    const QString source = dir.filePath(QStringLiteral("source.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/simple-multipage.pdf"), working));
    QVERIFY(QFile::copy(QStringLiteral(":/mengshee/data/latex-default-note.pdf"), source));
    // Resource copies inherit read-only permissions on Windows.
    QVERIFY(QFile::setPermissions(source, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    Okular::Part part(nullptr, {});
    QVERIFY(openDocument(&part, working));
    if (rotation) {
        part.setPageRotation(0, rotation);
    }
    auto *note = new Okular::StampAnnotation;
    const Okular::NormalizedRect large(.2, .2, .5, .4);
    note->setBoundingRectangle(large);
    note->setContents(QStringLiteral("Appearance history regression"));
    note->setOkularLatex(true);
    note->setLatexNoteType(callout ? Okular::Annotation::LatexNoteCallout : Okular::Annotation::LatexNoteBoxed);
    note->setLatexAppearancePdfFileName(source);
    note->setLatexPadding(3);
    note->setLatexFillColor(Qt::yellow);
    note->setLatexBorderColor(Qt::black);
    note->style().setWidth(2);
    note->style().setOpacity(.5);
    if (callout) {
        note->setLatexCalloutPoint(Okular::NormalizedPoint(.1, .5), 0);
        note->setLatexCalloutPoint(Okular::NormalizedPoint(.15, .3), 1);
        note->setLatexCalloutPoint(Okular::NormalizedPoint(.2, .3), 2);
    }
    part.m_document->addPageAnnotation(0, note);
    const QString name = note->uniqueName();
    auto saveImage = [&](Okular::Part &editor, const QString &file) -> QImage {
        QString error;
        if (!editor.m_document->saveChanges(file, &error)) {
            qWarning() << error;
            return {};
        }
        auto pdf = Poppler::Document::load(file);
        if (!pdf) {
            return {};
        }
        pdf->setRenderHint(Poppler::Document::Antialiasing, true);
        pdf->setRenderHint(Poppler::Document::TextAntialiasing, true);
        auto page = pdf->page(0);
        return page ? page->renderToImage(144, 144) : QImage();
    };
    const QImage full = saveImage(part, dir.filePath(QStringLiteral("full.pdf")));
    QVERIFY(!full.isNull());
    QVERIFY(QFile::remove(source));
    part.m_document->prepareToModifyAnnotationProperties(note);
    note->setBoundingRectangle(Okular::NormalizedRect(.2, .2, .5, .202));
    part.m_document->modifyPageAnnotationProperties(0, note);
    const QString smallFile = dir.filePath(QStringLiteral("small.pdf"));
    const QImage small = saveImage(part, smallFile);
    QVERIFY(!small.isNull());
    QVERIFY(small != full);
    part.m_document->undo();
    QCOMPARE(saveImage(part, dir.filePath(QStringLiteral("undo.pdf"))), full);
    part.m_document->redo();
    QCOMPARE(saveImage(part, dir.filePath(QStringLiteral("redo.pdf"))), small);
    Okular::Part reopened(nullptr, {});
    QVERIFY(openDocument(&reopened, smallFile));
    auto *restored = static_cast<Okular::StampAnnotation *>(reopened.m_document->page(0)->annotation(name));
    QVERIFY(restored);
    reopened.m_document->prepareToModifyAnnotationProperties(restored);
    restored->setBoundingRectangle(large);
    reopened.m_document->modifyPageAnnotationProperties(0, restored);
    QCOMPARE(saveImage(reopened, dir.filePath(QStringLiteral("grown.pdf"))), full);
}

namespace {
// Count actual generator submissions, not UI request attempts or repaints.
std::atomic<int> readingRenderSubmissions{0};
std::atomic<qint64> readingLargestRender{0};
QtMessageHandler previousReadingMessageHandler = nullptr;
QLoggingCategory::CategoryFilter previousReadingCategoryFilter = nullptr;

void readingRenderCategoryFilter(QLoggingCategory *category)
{
    if (previousReadingCategoryFilter) previousReadingCategoryFilter(category);
    if (QByteArray(category->categoryName()) == "org.jairy.mengshee.core") category->setEnabled(QtDebugMsg, true);
}

void readingRenderMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (message.startsWith(QStringLiteral("sending request observer="))) {
        ++readingRenderSubmissions;
        static const QRegularExpression dimensions(QStringLiteral(" ([0-9]+)x([0-9]+)@"));
        const auto match = dimensions.match(message);
        if (match.hasMatch()) {
            const qint64 pixels = match.captured(1).toLongLong() * match.captured(2).toLongLong();
            qint64 previous = readingLargestRender.load();
            while (pixels > previous && !readingLargestRender.compare_exchange_weak(previous, pixels)) {}
        }
        return;
    }
    if (type == QtDebugMsg && QByteArray(context.category) == "org.jairy.mengshee.core") return;
    if (previousReadingMessageHandler) previousReadingMessageHandler(type, context, message);
}

struct ReadingRenderTrace {
    ReadingRenderTrace()
    {
        readingRenderSubmissions = 0;
        readingLargestRender = 0;
        previousReadingCategoryFilter = QLoggingCategory::installFilter(readingRenderCategoryFilter);
        previousReadingMessageHandler = qInstallMessageHandler(readingRenderMessageHandler);
    }
    ~ReadingRenderTrace()
    {
        qInstallMessageHandler(previousReadingMessageHandler);
        QLoggingCategory::installFilter(previousReadingCategoryFilter);
        previousReadingCategoryFilter = nullptr;
    }
};

bool writeReadingViewFixture(const QString &path, int rotation = 0, const QByteArray &metadata = {}, bool withText = false)
{
    QByteArray paint("0 0 1 rg 30 40 60 50 re f\n");
    if (withText) {
        paint += "0 0 0 rg BT /F1 12 Tf 25 125 Td (LEFT) Tj 105 0 Td (RIGHT) Tj ET\nBT /F1 12 Tf 25 60 Td (HIDDEN) Tj ET\n";
    }
    const QByteArray resources = withText ? QByteArray("<< /Font << /F1 5 0 R >> >>") : QByteArray("<< >>");
    QList<QByteArray> objects{
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 220 180] /CropBox [10 20 210 160] /UserUnit 2 /Rotate ") + QByteArray::number(rotation)
            + " /Resources " + resources + " /Contents 4 0 R " + metadata + " >>",
        QByteArray("<< /Length ") + QByteArray::number(paint.size()) + ">>\nstream\n" + paint + "endstream"};
    if (withText) {
        objects.append("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    }
    QByteArray pdf("%PDF-1.7\n");
    QList<qsizetype> offsets;
    for (int i = 0; i < objects.size(); ++i) {
        offsets.append(pdf.size());
        pdf += QByteArray::number(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const auto xref = pdf.size();
    pdf += "xref\n0 " + QByteArray::number(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (auto offset : offsets) {
        pdf += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
    }
    pdf += "trailer\n<< /Size " + QByteArray::number(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(pdf) == pdf.size();
}

QImage readingViewPdfPixels(const QString &path)
{
    auto pdf = Poppler::Document::load(path);
    if (!pdf) {
        return {};
    }
    auto page = pdf->page(0);
    return page ? page->renderToImage(96, 96) : QImage();
}

bool readingViewRectClose(const Okular::NormalizedRect &a, const Okular::NormalizedRect &b)
{
    return qAbs(a.left - b.left) < 1e-9 && qAbs(a.top - b.top) < 1e-9 && qAbs(a.right - b.right) < 1e-9 && qAbs(a.bottom - b.bottom) < 1e-9;
}
}

void PartTest::testReadingViewsMetadata_data()
{
    QTest::addColumn<int>("rotation");
    for (int angle : {0, 90, 180, 270}) {
        QTest::newRow(qPrintable(QString::number(angle))) << angle;
    }
}

void PartTest::testReadingViewsMetadata()
{
    QFETCH(int, rotation);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString input = dir.filePath(QStringLiteral("source.pdf"));
    const QString saved = dir.filePath(QStringLiteral("saved.pdf"));
    QVERIFY(writeReadingViewFixture(input, rotation));
    const QImage original = readingViewPdfPixels(input);
    QVERIFY(!original.isNull());
    Part part(nullptr, {});
    QVERIFY(openDocument(&part, input));
    QVERIFY(part.m_document->canEditReadingViews());
    QString error;
    QVERIFY(part.m_document->readingViews(0, &error).isEmpty());
    QVERIFY(error.isEmpty());
    QVERIFY(part.m_document->readingViewPageToken(0).isEmpty());
    const QList<ReadingView> views{{QStringLiteral("b99f36a5-bb53-4cf0-9a8e-069b84c4df33"), 7, NormalizedRect(.1, .12, .43, .83)},
                                  {QStringLiteral("488284dd-125e-4c0b-8e72-3169a11ef171"), 7, NormalizedRect(.05, .05, .96, .18)}};
    QVERIFY2(part.m_document->setReadingViews(0, views, &error), qPrintable(error));
    const QString token = part.m_document->readingViewPageToken(0);
    QVERIFY(!token.isEmpty());
    QCOMPARE(part.m_document->readingViewPageForToken(token), 0);
    const auto actual = part.m_document->readingViews(0, &error);
    QCOMPARE(actual.size(), 2);
    for (int i = 0; i < views.size(); ++i) {
        QCOMPARE(actual[i].id, views[i].id);
        QCOMPARE(actual[i].number, views[i].number); // duplicate/non-contiguous labels are legal
        QVERIFY(readingViewRectClose(actual[i].rectangle, views[i].rectangle));
    }
    auto invalid = views;
    invalid[0].number = 0;
    QVERIFY(!part.m_document->setReadingViews(0, invalid, &error));
    invalid = views;
    invalid[1].id = invalid[0].id;
    QVERIFY(!part.m_document->setReadingViews(0, invalid, &error));
    invalid = views;
    invalid[0].rectangle.left = invalid[0].rectangle.right;
    QVERIFY(!part.m_document->setReadingViews(0, invalid, &error));
    QCOMPARE(part.m_document->readingViews(0).size(), 2);
    QVERIFY2(part.m_document->saveChanges(saved, &error), qPrintable(error));
    QCOMPARE(readingViewPdfPixels(saved), original); // not annotations or painted PDF content
    Part reopened(nullptr, {});
    QVERIFY(openDocument(&reopened, saved));
    QCOMPARE(reopened.m_document->readingViewPageToken(0), token);
    const auto restored = reopened.m_document->readingViews(0, &error);
    QCOMPARE(restored.size(), 2);
    for (int i = 0; i < views.size(); ++i) {
        QCOMPARE(restored[i].id, views[i].id);
        QCOMPARE(restored[i].number, views[i].number);
        QVERIFY(readingViewRectClose(restored[i].rectangle, views[i].rectangle));
    }
    QVERIFY(reopened.m_document->setReadingViews(0, {}, &error));
    QVERIFY(reopened.m_document->readingViews(0).isEmpty());
    QCOMPARE(reopened.m_document->readingViewPageToken(0), token);
    if (rotation == 0) {
        const QString combined = dir.filePath(QStringLiteral("combined.pdf"));
        QVERIFY2(reopened.m_document->combinePdfFiles({saved, saved}, combined, true, &error), qPrintable(error));
        Part merged(nullptr, {});
        QVERIFY(openDocument(&merged, combined));
        QCOMPARE(merged.m_document->pages(), 2u);
        const QString firstToken = merged.m_document->readingViewPageToken(0);
        const QString secondToken = merged.m_document->readingViewPageToken(1);
        QVERIFY(!firstToken.isEmpty() && !secondToken.isEmpty() && firstToken != secondToken);
        QCOMPARE(merged.m_document->readingViewPageForToken(firstToken), 0);
        QCOMPARE(merged.m_document->readingViewPageForToken(secondToken), 1);
        QVERIFY(merged.m_document->setReadingViews(0, {}, &error));
        QCOMPARE(merged.m_document->readingViews(1).size(), 2);
    }
}

void PartTest::testReadingViewsHistoryAndPageIdentity()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString input = dir.filePath(QStringLiteral("source.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/simple-multipage.pdf"), input));
    Part part(nullptr, {});
    QVERIFY(openDocument(&part, input));
    part.setEditingMode(EditingMode::Views);
    part.m_document->setRotation(Okular::Rotation0);
    QCOMPARE(part.m_document->page(0)->rotation(), Okular::Rotation0);
    const auto viewport = part.m_document->viewport().toString();
    QVERIFY(part.addReadingViewWithNumber(0, QRectF(.1, .1, .35, .7), 9));
    QCOMPARE(part.m_document->viewport().toString(), viewport); // defining a View is not navigation
    auto views = part.m_document->readingViews(0);
    QCOMPARE(views.size(), 1);
    const QString id = views[0].id;
    const QString token = part.m_document->readingViewPageToken(0);
    QVERIFY(part.isModified());
    QTimer::singleShot(50, [] {
        if (auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
            dialog->setIntValue(23);
            dialog->accept();
        }
    });
    part.editReadingViewNumber(0, id);
    QCOMPARE(part.m_document->readingViews(0)[0].number, 23);
    QCOMPARE(part.m_document->readingViews(0)[0].id, id);
    part.m_document->undo();
    QCOMPARE(part.m_document->readingViews(0)[0].number, 9);
    QVERIFY(!part.m_advancedModeEnabled);
    QVERIFY(part.m_pageView->readingViewEditingEnabled());
    QVERIFY(part.canUsePageLevelEditing());
    QCOMPARE(part.m_document->page(0)->rotation(), Okular::Rotation0);
    part.changeReadingViewRectangle(0, id, QRectF(.12, .15, .3, .6));
    QVERIFY(readingViewRectClose(part.m_document->readingViews(0)[0].rectangle, NormalizedRect(.12, .15, .42, .75)));
    part.m_document->undo();
    QVERIFY(readingViewRectClose(part.m_document->readingViews(0)[0].rectangle, views[0].rectangle));
    part.m_document->redo();
    const QString savedHistory = dir.filePath(QStringLiteral("saved-history.pdf"));
    QVERIFY(part.saveAs(QUrl::fromLocalFile(savedHistory), Part::NoSaveAsFlags));
    QVERIFY(!part.isModified());
    QVERIFY(part.m_pageView->readingViewEditingEnabled());
    QCOMPARE(part.m_document->readingViewPageToken(0), token);
    QVERIFY(part.m_document->canUndo());
    part.m_document->undo();
    QVERIFY(part.isModified());
    QVERIFY(readingViewRectClose(part.m_document->readingViews(0)[0].rectangle, views[0].rectangle));
    part.m_document->redo();
    QVERIFY(!part.isModified());
    part.deleteReadingView(0, id);
    QVERIFY(part.m_document->readingViews(0).isEmpty());
    part.m_document->undo();
    QCOMPARE(part.m_document->readingViews(0)[0].id, id);
    QString error;
    // A page move outside the view command cannot redirect its undo to another page.
    QVERIFY2(part.m_document->movePage(0, 1, &error), qPrintable(error));
    QCOMPARE(part.m_document->readingViewPageForToken(token), 1);
    part.m_document->undo();
    QVERIFY(readingViewRectClose(part.m_document->readingViews(1)[0].rectangle, views[0].rectangle));
    QVERIFY(part.m_document->readingViews(0).isEmpty());
    quint64 duplicateToken = 0;
    QVERIFY2(part.m_document->duplicatePage(1, true, &duplicateToken, &error), qPrintable(error));
    QCOMPARE(part.m_document->readingViews(2).size(), 1);
    QVERIFY(part.m_document->readingViewPageToken(2) != token);
    QCOMPARE(part.m_document->readingViewPageForToken(token), 1);
    const auto copy = part.m_document->readingViews(2);
    QVERIFY(part.m_document->setReadingViews(1, {}, &error));
    QCOMPARE(part.m_document->readingViews(2)[0].id, copy[0].id);
    quint64 removed = 0;
    QVERIFY2(part.m_document->detachPage(2, &removed, &error), qPrintable(error));
    QCOMPARE(part.m_document->readingViewPageForToken(part.m_document->readingViewPageToken(1)), 1);
    QVERIFY2(part.m_document->attachPage(1, removed, &error), qPrintable(error));
    QCOMPARE(part.m_document->readingViews(2)[0].id, copy[0].id);
    part.m_document->setRotation(Okular::Rotation90);
    QVERIFY(part.addReadingViewWithNumber(0, QRectF(.1, .2, .3, .4), 11));
    QVERIFY(readingViewRectClose(part.m_document->readingViews(0)[0].rectangle, NormalizedRect(.2, .6, .6, .9)));
    part.m_document->undo();
    QVERIFY(part.m_document->readingViews(0).isEmpty());
    part.m_document->setRotation(Okular::Rotation0);
}

void PartTest::testReadingViewsMouseEditing()
{
    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("mouse.pdf"));
    QVERIFY(writeReadingViewFixture(input));
    Part part(nullptr, {});
    QVERIFY(openDocument(&part, input));
    part.widget()->resize(1100, 800);
    part.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));
    auto *modeSelector = qobject_cast<KSelectAction *>(part.actionCollection()->action(QStringLiteral("editing_mode_selector")));
    QVERIFY(modeSelector && modeSelector->isEnabled());
    modeSelector->actions().at(int(EditingMode::Views))->trigger();
    QVERIFY(!part.m_advancedModeEnabled);
    QVERIFY(part.m_pageView->readingViewEditingEnabled());
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));
    QWidget *canvas = part.m_pageView->viewport();
    const auto pointAt = [&](double nx, double ny) {
        QPoint best(-1, -1);
        double distance = 1e20;
        for (int y = 4; y < canvas->height() - 4; y += 4) {
            for (int x = 4; x < canvas->width() - 4; x += 4) {
                int page = -1;
                NormalizedPoint point;
                if (!part.m_pageView->mapGlobalPosToPagePoint(canvas->mapToGlobal(QPoint(x, y)), &page, &point) || page != 0) {
                    continue;
                }
                const double d = (point.x - nx) * (point.x - nx) + (point.y - ny) * (point.y - ny);
                if (d < distance) {
                    distance = d;
                    best = QPoint(x, y);
                }
            }
        }
        return distance < .002 ? best : QPoint(-1, -1);
    };
    const QPoint start = pointAt(.15, .2), end = pointAt(.7, .75);
    QVERIFY(start.x() >= 0 && end.x() >= 0);
    QSignalSpy created(part.m_pageView, SIGNAL(createReadingViewRequested(int,QRectF)));
    QVERIFY(created.isValid());
    QAction *add = part.actionCollection()->action(QStringLiteral("advanced_add_reading_view"));
    QVERIFY(add && add->isEnabled() && add->isVisible());
    // Entering the editor only exposes existing ranges; it must not arm drawing.
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(canvas, end);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, end);
    QCOMPARE(created.count(), 0);
    QVERIFY(part.m_document->readingViews(0).isEmpty());
    add->trigger();
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(canvas, end);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, end);
    QCOMPARE(created.count(), 1);
    auto views = part.m_document->readingViews(0);
    QCOMPARE(views.size(), 1);
    QCOMPARE(views[0].number, 1);
    QVERIFY(!QApplication::activeModalWidget());
    views[0].number = 17;
    QString error;
    QVERIFY(part.m_document->setReadingViews(0, views, &error));
    // Draw again without reactivating the tool, including after metadata refresh.
    const QPoint secondStart = pointAt(.8, .2), secondEnd = pointAt(.95, .75);
    QVERIFY(secondStart.x() >= 0 && secondEnd.x() >= 0);
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, secondStart);
    QTest::mouseMove(canvas, secondEnd);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, secondEnd);
    QCOMPARE(created.count(), 2);
    QCOMPARE(part.m_document->readingViews(0).size(), 2);
    QCOMPARE(part.m_document->readingViews(0)[1].number, 18);
    QVERIFY(!QApplication::activeModalWidget());
    part.m_document->undo();
    QCOMPARE(part.m_document->readingViews(0).size(), 1);
    const auto before = views[0];
    const QPoint middle = pointAt((before.rectangle.left + before.rectangle.right) / 2, (before.rectangle.top + before.rectangle.bottom) / 2);
    QVERIFY(part.m_pageView->readingViewsAtGlobalPos(canvas->mapToGlobal(middle)).isEmpty()); // preserve ordinary text/link interaction
    const QPoint corner = pointAt(before.rectangle.left, before.rectangle.top);
    QVERIFY(part.m_pageView->readingViewsAtGlobalPos(canvas->mapToGlobal(corner)).contains(before.id));
    QSignalSpy changed(part.m_pageView, SIGNAL(changeReadingViewRectangleRequested(int,QString,QRectF)));
    QVERIFY(changed.isValid());
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, corner);
    QTest::mouseMove(canvas, corner + QPoint(28, 20));
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, corner + QPoint(28, 20));
    QCOMPARE(changed.count(), 1);
    QVERIFY(!readingViewRectClose(part.m_document->readingViews(0)[0].rectangle, before.rectangle));
    part.m_document->undo();
    QVERIFY(readingViewRectClose(part.m_document->readingViews(0)[0].rectangle, before.rectangle));
    // With the View selected, drag a handle rather than moving the entire range.
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, corner);
    const QPoint bottomRight = pointAt(before.rectangle.right, before.rectangle.bottom);
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, bottomRight);
    QTest::mouseMove(canvas, bottomRight - QPoint(24, 20));
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, bottomRight - QPoint(24, 20));
    QCOMPARE(changed.count(), 2);
    const auto resized = part.m_document->readingViews(0)[0].rectangle;
    QVERIFY(qAbs(resized.left - before.rectangle.left) < 1e-9);
    QVERIFY(qAbs(resized.top - before.rectangle.top) < 1e-9);
    QVERIFY(resized.right < before.rectangle.right && resized.bottom < before.rectangle.bottom);
    part.m_document->undo();
    QVERIFY(readingViewRectClose(part.m_document->readingViews(0)[0].rectangle, before.rectangle));
    add->trigger();
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, middle);
    QTest::mouseMove(canvas, middle + QPoint(20, 20));
    QTest::keyClick(part.m_pageView, Qt::Key_Escape);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, middle + QPoint(20, 20));
    QCOMPARE(created.count(), 2);
    QCOMPARE(part.m_document->readingViews(0).size(), 1);
    part.setEditingMode(EditingMode::CrossReferences);
    QVERIFY(!part.m_pageView->readingViewEditingEnabled());
    part.setEditingMode(EditingMode::Views);
    QVERIFY(part.m_pageView->readingViewEditingEnabled());
    QVERIFY(part.m_pageView->readingViewsAtGlobalPos(canvas->mapToGlobal(corner)).contains(before.id));
    modeSelector->actions().at(int(EditingMode::Reading))->trigger();
    QVERIFY(!part.m_pageView->readingViewEditingEnabled());
    QVERIFY(!add->isEnabled() || !add->isVisible());
    QVERIFY(part.m_pageView->readingViewsAtGlobalPos(canvas->mapToGlobal(corner)).isEmpty());
    QCOMPARE(part.m_document->readingViews(0)[0].id, before.id);
}

void PartTest::testReadingViewModeProjection_data()
{
    QTest::addColumn<int>("nativeRotation");
    QTest::addColumn<int>("viewerRotation");
    for (int native : {0, 90, 270}) {
        for (int viewer : {0, 1}) {
            QTest::newRow(qPrintable(QStringLiteral("native-%1-viewer-%2").arg(native).arg(viewer))) << native << viewer;
        }
    }
}

void PartTest::testReadingViewModeProjection()
{
    QFETCH(int, nativeRotation);
    QFETCH(int, viewerRotation);
    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("projection.pdf"));
    QVERIFY(writeReadingViewFixture(input, nativeRotation));
    Part part(nullptr, {});
    QVERIFY(openDocument(&part, input));
    const QList<ReadingView> definitions{{QStringLiteral("b99f36a5-bb53-4cf0-9a8e-069b84c4df33"), 9, NormalizedRect(0, .1, .45, .9)},
                                        {QStringLiteral("488284dd-125e-4c0b-8e72-3169a11ef171"), 2, NormalizedRect(.55, .2, 1, .8)},
                                        {QStringLiteral("90b59bba-c120-4057-ad74-95b0d64e2016"), 2, NormalizedRect(.55, .2, 1, .8)}};
    QString error;
    QVERIFY2(part.m_document->setReadingViews(0, definitions, &error), qPrintable(error));
    const auto before = part.m_document->readingViews(0);
    const bool wasModified = part.isModified();
    part.m_document->setRotation(viewerRotation);
    part.widget()->resize(1100, 800);
    part.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));
    PageView *view = part.m_pageView;
    Okular::View *capabilities = view;
    capabilities->setCapability(Okular::View::Continuous, false);
    capabilities->setCapability(Okular::View::ZoomModality, int(PageView::ZoomFitPage));
    capabilities->setCapability(Okular::View::ViewModeModality, int(Okular::Settings::EnumViewMode::Single));
    const QVariant zoomMode = capabilities->capability(Okular::View::ZoomModality);
    const QVariant continuous = capabilities->capability(Okular::View::Continuous);
    view->setReadingViewMode(true);
    QVERIFY(view->readingViewMode());
    QCOMPARE(part.m_document->pages(), 1u);
    QCOMPARE(view->displayedPageCount(), 3);
    QVERIFY(view->displayedPageLabel(0).contains(QStringLiteral("2")));
    QVERIFY(view->displayedPageLabel(2).contains(QStringLiteral("9")));
    QCOMPARE(capabilities->capability(Okular::View::ZoomModality), zoomMode);
    QCOMPARE(capabilities->capability(Okular::View::Continuous), continuous);
    view->goToDisplayedPage(0);
    QTRY_COMPARE(view->displayedPageNumber(), 0);
    view->goToDisplayedPage(1);
    QTRY_COMPARE(view->displayedPageNumber(), 1);
    QCOMPARE(view->documentViewport().pageNumber, 0);
    QVERIFY(!view->viewportHistoryAtBegin());
    view->goToPreviousViewport();
    QTRY_COMPARE(view->displayedPageNumber(), 0);
    view->goToNextViewport();
    QTRY_COMPARE(view->displayedPageNumber(), 1);
    // Two distinct Views with exactly the same crop must remain separately navigable.
    view->goToDisplayedPage(2);
    QTRY_COMPARE(view->displayedPageNumber(), 2);
    int sourcePage = -1;
    NormalizedPoint point;
    QTRY_VERIFY(view->mapGlobalPosToPagePoint(view->viewport()->mapToGlobal(view->viewport()->rect().center()), &sourcePage, &point));
    QCOMPARE(sourcePage, 0);
    if (viewerRotation == 0) {
        QVERIFY(point.x >= 0 && point.x <= .45 && point.y >= .1 && point.y <= .9);
    } else {
        QVERIFY(point.x >= .1 && point.x <= .9 && point.y >= 0 && point.y <= .45);
    }
    QCOMPARE(part.m_document->readingViews(0), before);
    QCOMPARE(part.isModified(), wasModified);
    auto renumbered = before;
    renumbered[0].number = 1;
    QVERIFY(part.m_document->setReadingViews(0, renumbered, &error));
    QTRY_COMPARE(view->displayedPageNumber(), 0); // same UUID, new presentation order
    view->goToPreviousViewport();
    QTRY_COMPARE(view->displayedPageNumber(), 2); // old #2b, not the old integer index
    view->goToNextViewport();
    QTRY_COMPARE(view->displayedPageNumber(), 0);
    QVERIFY(part.m_document->setReadingViews(0, before, &error));
    QTRY_COMPARE(view->displayedPageNumber(), 2);
    view->setReadingViewMode(false);
    QVERIFY(!view->readingViewMode());
    QCOMPARE(view->displayedPageCount(), 1);
    QCOMPARE(view->displayedPageNumber(), 0);
    QCOMPARE(capabilities->capability(Okular::View::ZoomModality), zoomMode);
    QCOMPARE(capabilities->capability(Okular::View::Continuous), continuous);
    QCOMPARE(part.m_document->readingViews(0), before);
    part.m_document->setRotation(Okular::Rotation0);
}

void PartTest::testReadingViewModeNavigation()
{
    Part part(nullptr, {});
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/simple-multipage.pdf")));
    part.m_document->setRotation(Okular::Rotation0);
    QString error;
    const QList<ReadingView> definitions{{QStringLiteral("b99f36a5-bb53-4cf0-9a8e-069b84c4df33"), 9, NormalizedRect(.05, .05, .45, .95)},
                                        {QStringLiteral("488284dd-125e-4c0b-8e72-3169a11ef171"), 2, NormalizedRect(.55, .05, .95, .95)}};
    QVERIFY2(part.m_document->setReadingViews(0, definitions, &error), qPrintable(error));
    const int sourceCount = int(part.m_document->pages());
    QVERIFY(sourceCount >= 2);
    part.widget()->resize(1100, 800);
    part.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));
    PageView *view = part.m_pageView;
    static_cast<Okular::View *>(view)->setCapability(Okular::View::Continuous, false);
    static_cast<Okular::View *>(view)->setCapability(Okular::View::ZoomModality, int(PageView::ZoomFitPage));
    static_cast<Okular::View *>(view)->setCapability(Okular::View::ViewModeModality, int(Okular::Settings::EnumViewMode::Single));
    QAction *mode = part.actionCollection()->action(QStringLiteral("view_read_by_views"));
    QVERIFY(mode && mode->isCheckable() && mode->isEnabled());
    mode->setChecked(true);
    QVERIFY(view->readingViewMode());
    QAction *paste = part.actionCollection()->action(QStringLiteral("annotation_paste"));
    QVERIFY(paste);
    // Reading Views must not disable annotation tools or paste merely because
    // the displayed page is a projection of a source PDF page.
    QCOMPARE(view->displayedPageCount(), sourceCount + 1);
    view->goToDisplayedPage(0);
    QTRY_VERIFY(part.m_nextPage->isEnabled());
    part.m_nextPage->trigger();
    QTRY_COMPARE(view->displayedPageNumber(), 1);
    QCOMPARE(view->documentViewport().pageNumber, 0);
    part.m_nextPage->trigger();
    QTRY_COMPARE(view->displayedPageNumber(), 2);
    QCOMPARE(view->documentViewport().pageNumber, 1); // whole-page fallback, not omitted
    part.m_prevPage->trigger();
    QTRY_COMPARE(view->displayedPageNumber(), 1);
    part.m_endOfDocument->trigger();
    QTRY_COMPARE(view->displayedPageNumber(), sourceCount);
    QTRY_VERIFY(!part.m_nextPage->isEnabled());
    part.m_beginningOfDocument->trigger();
    QTRY_COMPARE(view->displayedPageNumber(), 0);
    QTRY_VERIFY(!part.m_prevPage->isEnabled());
    const auto editors = part.widget()->findChildren<QLineEdit *>(QStringLiteral("readingPageNumber"));
    QVERIFY(!editors.isEmpty());
    QLineEdit *editor = editors.constFirst();
    editor->setText(QStringLiteral("3"));
    QTest::keyClick(editor, Qt::Key_Return);
    QTRY_COMPARE(view->displayedPageNumber(), 2);
    view->goToDisplayedPage(0);
    QTest::keyClick(view, Qt::Key_Right);
    QTRY_COMPARE(view->displayedPageNumber(), 1);
    mode->setChecked(false);
    QCOMPARE(view->displayedPageCount(), sourceCount);
    QCOMPARE(view->displayedPageNumber(), view->documentViewport().pageNumber);
    QCOMPARE(part.m_document->readingViews(0).size(), 2);
}

void PartTest::testReadingViewModeContinuousRendering()
{
    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("scales.pdf"));
    QVERIFY(writeReadingViewFixture(input));
    Part part(nullptr, {});
    QVERIFY(openDocument(&part, input));
    part.m_document->setRotation(Okular::Rotation0);
    QString error;
    const QList<ReadingView> definitions{{QStringLiteral("b99f36a5-bb53-4cf0-9a8e-069b84c4df33"), 1, NormalizedRect(.12, .55, .15, .65)},
                                        {QStringLiteral("488284dd-125e-4c0b-8e72-3169a11ef171"), 2, NormalizedRect(0, 0, 1, 1)}};
    QVERIFY2(part.m_document->setReadingViews(0, definitions, &error), qPrintable(error));
    part.widget()->resize(1200, 850);
    part.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));
    PageView *view = part.m_pageView;
    Okular::View *capabilities = view;
    capabilities->setCapability(Okular::View::ViewModeModality, int(Okular::Settings::EnumViewMode::Facing));
    capabilities->setCapability(Okular::View::Continuous, true);
    capabilities->setCapability(Okular::View::ZoomModality, int(PageView::ZoomFitPage));
    view->setReadingViewMode(true);
    QCOMPARE(view->displayedPageCount(), 2);
    view->goToDisplayedPage(0);
    // Both virtual pages use the same source page but radically different scales.
    // Test actual pixels, not merely the existence of a pixmap for one scale.
    const auto bothPagesPainted = [&] {
        const QImage image = view->viewport()->grab().toImage();
        bool left = false, right = false;
        for (int y = 8; y < image.height() - 8; y += 8) {
            for (int x = 8; x < image.width() - 8; x += 8) {
                const QColor color = image.pixelColor(x, y);
                if (color.blue() > 220 && color.red() < 35 && color.green() < 35) {
                    (x < image.width() / 2 ? left : right) = true;
                }
            }
        }
        return left && right;
    };
    QTRY_VERIFY_WITH_TIMEOUT(bothPagesPainted(), 15000);
    view->viewport()->update();
    QCoreApplication::processEvents();
    QVERIFY(bothPagesPainted());
    const QString evidence = qEnvironmentVariable("MENGSHEE_VIEW_READING_EVIDENCE");
    if (!evidence.isEmpty()) {
        QVERIFY(view->viewport()->grab().save(evidence));
    }
    capabilities->setCapability(Okular::View::ViewModeModality, int(Okular::Settings::EnumViewMode::Single));
    capabilities->setCapability(Okular::View::Continuous, true);
    view->goToDisplayedPage(1);
    QTRY_COMPARE(view->displayedPageNumber(), 1);
    QCOMPARE(view->documentViewport().pageNumber, 0);
    view->setReadingViewMode(false);
    QCOMPARE(view->displayedPageCount(), 1);
}

void PartTest::testReadingViewModeIndependentFrames()
{
    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("frames.pdf"));
    QVERIFY(writeReadingViewFixture(input));
    Part part(nullptr, {});
    QVERIFY(openDocument(&part, input));
    QString error;
    const QList<ReadingView> definitions{{QStringLiteral("b99f36a5-bb53-4cf0-9a8e-069b84c4df33"), 1, NormalizedRect(0, 0, .5, 1)},
                                        {QStringLiteral("488284dd-125e-4c0b-8e72-3169a11ef171"), 2, NormalizedRect(.5, 0, 1, 1)}};
    QVERIFY(part.m_document->setReadingViews(0, definitions, &error));
    part.widget()->resize(1200, 850);
    part.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));
    PageView *main = part.m_pageView;
    main->setReadingViewMode(true);
    main->goToDisplayedPage(1);
    part.openAuxiliaryView(main, DocumentViewport(0), QStringLiteral("Independent frame"));
    QTRY_COMPARE(part.m_documentWorkspace->auxiliaryViewCount(), 1);
    PageView *auxiliary = part.m_documentWorkspace->auxiliaryViews().constFirst();
    auxiliary->setReadingViewMode(false);
    QVERIFY(main->readingViewMode());
    QCOMPARE(main->displayedPageCount(), 2);
    QCOMPARE(main->displayedPageNumber(), 1);
    QCOMPARE(auxiliary->displayedPageCount(), 1);
    QTRY_COMPARE(part.workspaceActivePageView(), auxiliary);
    QAction *mode = part.actionCollection()->action(QStringLiteral("view_read_by_views"));
    QVERIFY(mode);
    QTRY_VERIFY(!mode->isChecked());
    mode->setChecked(true);
    QVERIFY(auxiliary->readingViewMode());
    auxiliary->goToDisplayedPage(0);
    QCOMPARE(main->displayedPageNumber(), 1);
    QCOMPARE(auxiliary->displayedPageNumber(), 0);
    part.m_documentWorkspace->promoteView(auxiliary);
    QCOMPARE(part.m_documentWorkspace->mainView(), auxiliary);
    QVERIFY(auxiliary->readingViewMode() && main->readingViewMode());
    QCOMPARE(auxiliary->displayedPageNumber(), 0);
    QCOMPARE(main->displayedPageNumber(), 1);
    part.m_documentWorkspace->closeAllAuxiliaryViews();
    QCOMPARE(part.m_documentWorkspace->auxiliaryViewCount(), 0);
    QCOMPARE(auxiliary->displayedPageCount(), 2);
}

void PartTest::testReadingViewModeExternalNavigation()
{
    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("external.pdf"));
    QVERIFY(writeReadingViewFixture(input));
    Part part(nullptr, {});
    QVERIFY(openDocument(&part, input));
    part.m_document->setRotation(Okular::Rotation0);
    QString error;
    const QList<ReadingView> definitions{{QStringLiteral("b99f36a5-bb53-4cf0-9a8e-069b84c4df33"), 1, NormalizedRect(.1, .1, .4, .8)},
                                        {QStringLiteral("488284dd-125e-4c0b-8e72-3169a11ef171"), 2, NormalizedRect(.6, .1, .9, .8)}};
    QVERIFY(part.m_document->setReadingViews(0, definitions, &error));
    part.widget()->resize(1100, 800);
    part.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));
    PageView *view = part.m_pageView;
    view->setReadingViewMode(true);
    DocumentViewport target(0);
    target.rePos.enabled = true;
    target.rePos.pos = DocumentViewport::Center;
    target.rePos.normalizedX = .75;
    target.rePos.normalizedY = .4;
    view->goToDocumentViewport(target, false);
    QTRY_COMPARE(view->displayedPageNumber(), 1);
    QVERIFY(view->readingViewMode());
    target.rePos.normalizedX = .5;
    target.rePos.normalizedY = .95; // outside every View: the user's switch still governs display mode
    view->goToDocumentViewport(target, false);
    QVERIFY(view->readingViewMode());
    QCOMPARE(view->documentViewport().pageNumber, 0);
    QAction *mode = part.actionCollection()->action(QStringLiteral("view_read_by_views"));
    QVERIFY(mode);
    QTRY_VERIFY(mode->isChecked());
    QCOMPARE(part.m_document->readingViews(0).size(), 2);
}

void PartTest::testReadingViewModeTextSelection()
{
    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("selection.pdf"));
    QVERIFY(writeReadingViewFixture(input, 0, {}, true));
    Part part(nullptr, {});
    QVERIFY(openDocument(&part, input));
    part.m_document->setRotation(Okular::Rotation0);
    QString error;
    const QList<ReadingView> definitions{{QStringLiteral("b99f36a5-bb53-4cf0-9a8e-069b84c4df33"), 2, NormalizedRect(.05, .1, .45, .4)},
                                        {QStringLiteral("488284dd-125e-4c0b-8e72-3169a11ef171"), 1, NormalizedRect(.55, .1, .95, .4)}};
    QVERIFY(part.m_document->setReadingViews(0, definitions, &error));
    part.widget()->resize(1100, 800);
    part.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));
    PageView *view = part.m_pageView;
    view->setReadingViewMode(true);
    part.m_document->requestTextPage(0);
    QTRY_VERIFY(part.m_document->page(0)->hasTextPage());
    QVERIFY(QMetaObject::invokeMethod(view, "selectAll"));
    QVERIFY(view->hasTextSelection());
    QVERIFY(QMetaObject::invokeMethod(view, "copyTextSelection"));
    const QString selected = QApplication::clipboard()->text();
    QVERIFY2(selected.contains(QStringLiteral("RIGHT")) && selected.contains(QStringLiteral("LEFT")), qPrintable(selected));
    QVERIFY2(!selected.contains(QStringLiteral("HIDDEN")), qPrintable(selected));
    QVERIFY2(selected.indexOf(QStringLiteral("RIGHT")) < selected.indexOf(QStringLiteral("LEFT")), qPrintable(selected));
    QCOMPARE(part.m_document->pages(), 1u);
    QVERIFY(part.m_document->page(0)->annotations().isEmpty());
    view->setReadingViewMode(false);
}

void PartTest::testReadingViewModeAnnotations_data()
{
    QTest::addColumn<int>("nativeRotation");
    QTest::addColumn<int>("viewerRotation");
    QTest::newRow("plain") << 0 << 0;
    QTest::newRow("native-90") << 90 << 0;
    QTest::newRow("viewer-90") << 0 << 1;
    QTest::newRow("native-90-viewer-270") << 90 << 3;
}

void PartTest::testReadingViewModeAnnotations()
{
    QFETCH(int, nativeRotation);
    QFETCH(int, viewerRotation);
    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("annotate.pdf"));
    const QString saved = dir.filePath(QStringLiteral("annotated.pdf"));
    QVERIFY(writeReadingViewFixture(input, nativeRotation));
    Part part(nullptr, {});
    QVERIFY(openDocument(&part, input));
    QString error;
    const QList<ReadingView> definitions{{QStringLiteral("b99f36a5-bb53-4cf0-9a8e-069b84c4df33"), 1, NormalizedRect(0, 0, 1, 1)},
                                        {QStringLiteral("488284dd-125e-4c0b-8e72-3169a11ef171"), 2, NormalizedRect(.2, .2, .8, .8)}};
    QVERIFY(part.m_document->setReadingViews(0, definitions, &error));
    part.m_document->setRotation(viewerRotation);
    part.widget()->resize(1100, 800);
    part.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));
    PageView *view = part.m_pageView;
    Okular::View *capabilities = view;
    capabilities->setCapability(Okular::View::Continuous, false);
    capabilities->setCapability(Okular::View::ViewModeModality, int(Okular::Settings::EnumViewMode::Single));
    capabilities->setCapability(Okular::View::ZoomModality, int(PageView::ZoomFitPage));
    view->setReadingViewMode(true);
    view->goToDisplayedPage(1);
    QTRY_COMPARE(view->displayedPageNumber(), 1);
    QWidget *canvas = view->viewport();
    const QPoint a = canvas->rect().center() - QPoint(35, 25);
    const QPoint b = canvas->rect().center() + QPoint(35, 25);
    NormalizedPoint pa, pb;
    int pageA = -1, pageB = -1;
    QTRY_VERIFY(view->mapGlobalPosToPagePoint(canvas->mapToGlobal(a), &pageA, &pa));
    QVERIFY(view->mapGlobalPosToPagePoint(canvas->mapToGlobal(b), &pageB, &pb));
    QCOMPARE(pageA, 0);
    QCOMPARE(pageB, 0);
    QAction *rectangle = part.actionCollection()->action(QStringLiteral("annotation_rectangle"));
    QVERIFY(rectangle && rectangle->isEnabled());
    rectangle->trigger();
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, a);
    QTest::mouseMove(canvas, b);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, b);
    QTRY_COMPARE(part.m_document->page(0)->annotations().size(), 1);
    Annotation *annotation = part.m_document->page(0)->annotations().constFirst();
    const NormalizedRect displayed = annotation->transformedBoundingRectangle();
    QVERIFY(qAbs(displayed.left - qMin(pa.x, pb.x)) < .01);
    QVERIFY(qAbs(displayed.top - qMin(pa.y, pb.y)) < .01);
    QVERIFY(qAbs(displayed.right - qMax(pa.x, pb.x)) < .01);
    QVERIFY(qAbs(displayed.bottom - qMax(pa.y, pb.y)) < .01);
    const auto originalRectangle = annotation->boundingRectangle();
    QVERIFY(part.isModified());
    QVERIFY(view->readingViewMode());
    part.m_document->undo();
    QVERIFY(part.m_document->page(0)->annotations().isEmpty());
    part.m_document->redo();
    QCOMPARE(part.m_document->page(0)->annotations().size(), 1);
    annotation = part.m_document->page(0)->annotations().constFirst();
    QVERIFY(readingViewRectClose(annotation->boundingRectangle(), originalRectangle));
    QVERIFY(QMetaObject::invokeMethod(view, "slotSetMouseNormal"));
    AnnotationPopup clipboard(part.m_document, AnnotationPopup::SingleAnnotationMode, part.widget());
    clipboard.addAnnotation(annotation, 0);
    clipboard.doCopyAnnotation({annotation, 0});
    QVERIFY(AnnotationPopup::clipboardHasAnnotations());
    QAction *paste = part.actionCollection()->action(QStringLiteral("annotation_paste"));
    QVERIFY(paste);
    QTRY_VERIFY(paste->isEnabled());
    QTest::mouseMove(canvas, canvas->rect().center());
    paste->trigger();
    QTRY_COMPARE(part.m_document->page(0)->annotations().size(), 2);
    QCOMPARE(part.m_document->pages(), 1u); // virtual page 1 must never become source page 1
    part.m_document->undo();
    QCOMPARE(part.m_document->page(0)->annotations().size(), 1);
    part.m_document->redo();
    QCOMPARE(part.m_document->page(0)->annotations().size(), 2);
    view->goToDisplayedPage(0);
    QCOMPARE(part.m_document->page(0)->annotations().size(), 2);
    view->setReadingViewMode(false);
    QCOMPARE(part.m_document->page(0)->annotations().size(), 2);
    QVERIFY(readingViewRectClose(part.m_document->page(0)->annotations().constFirst()->boundingRectangle(), originalRectangle));
    QVERIFY2(part.m_document->saveChanges(saved, &error), qPrintable(error));
    Part reopened(nullptr, {});
    QVERIFY(openDocument(&reopened, saved));
    QCOMPARE(reopened.m_document->page(0)->annotations().size(), 2);
    const auto restored = reopened.m_document->page(0)->annotations().constFirst()->boundingRectangle();
    QVERIFY(qAbs(restored.left - originalRectangle.left) < 1e-5);
    QVERIFY(qAbs(restored.top - originalRectangle.top) < 1e-5);
    QVERIFY(qAbs(restored.right - originalRectangle.right) < 1e-5);
    QVERIFY(qAbs(restored.bottom - originalRectangle.bottom) < 1e-5);
    part.m_document->setRotation(Okular::Rotation0);
}

void PartTest::testReadingViewModeFormReplicas()
{
    Part part(nullptr, {});
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/formSamples.pdf")));
    part.m_document->setRotation(Okular::Rotation0);
    FormFieldButton *field = nullptr;
    for (FormField *candidate : part.m_document->page(0)->formFields()) {
        auto *button = dynamic_cast<FormFieldButton *>(candidate);
        if (button && button->buttonType() == FormFieldButton::CheckBox && !button->isReadOnly() && button->isVisible()) {
            field = button;
            break;
        }
    }
    QVERIFY(field);
    const bool initial = field->state();
    QString error;
    const QList<ReadingView> definitions{{QStringLiteral("b99f36a5-bb53-4cf0-9a8e-069b84c4df33"), 1, NormalizedRect(0, 0, 1, 1)},
                                        {QStringLiteral("488284dd-125e-4c0b-8e72-3169a11ef171"), 2, NormalizedRect(0, 0, 1, 1)}};
    QVERIFY(part.m_document->setReadingViews(0, definitions, &error));
    part.widget()->resize(1200, 900);
    part.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));
    PageView *view = part.m_pageView;
    Okular::View *capabilities = view;
    capabilities->setCapability(Okular::View::ViewModeModality, int(Okular::Settings::EnumViewMode::Facing));
    capabilities->setCapability(Okular::View::Continuous, true);
    capabilities->setCapability(Okular::View::ZoomModality, int(PageView::ZoomFitPage));
    view->setReadingViewMode(true);
    view->goToDisplayedPage(0);
    QAction *forms = part.actionCollection()->action(QStringLiteral("view_toggle_forms"));
    QVERIFY(forms && forms->isEnabled());
    if (!forms->isChecked()) {
        forms->trigger();
    }
    const auto replicas = [&] {
        QList<QCheckBox *> result;
        for (auto *box : view->findChildren<QCheckBox *>()) {
            const QVariant id = box->property("pdfFormFieldId");
            if (id.isValid() && id.toInt() == field->id()) {
                result.append(box);
            }
        }
        return result;
    };
    QTRY_COMPARE(replicas().size(), 2);
    auto boxes = replicas();
    QTRY_VERIFY(boxes[0]->isVisible() && boxes[1]->isVisible());
    QVERIFY(boxes[0]->isEnabled() && boxes[1]->isEnabled());
    QCOMPARE(boxes[0]->isChecked(), initial);
    QCOMPARE(boxes[1]->isChecked(), initial);
    QTest::mouseClick(boxes[1], Qt::LeftButton);
    QTRY_COMPARE(field->state(), !initial);
    QTRY_COMPARE(boxes[0]->isChecked(), !initial);
    QTRY_COMPARE(boxes[1]->isChecked(), !initial);
    part.m_document->undo();
    QTRY_COMPARE(field->state(), initial);
    QTRY_COMPARE(boxes[0]->isChecked(), initial);
    QTRY_COMPARE(boxes[1]->isChecked(), initial);
    part.m_document->redo();
    QTRY_COMPARE(field->state(), !initial);
    QTRY_COMPARE(boxes[0]->isChecked(), !initial);
    QTRY_COMPARE(boxes[1]->isChecked(), !initial);
    QVERIFY(view->readingViewMode());
    capabilities->setCapability(Okular::View::ViewModeModality, int(Okular::Settings::EnumViewMode::Single));
    view->setReadingViewMode(false);
}

void PartTest::testUnifiedEditingModes()
{
    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("modes.pdf"));
    QVERIFY(writeReadingViewFixture(input, 0, {}, true));
    Part part(nullptr, {});
    QVERIFY(openDocument(&part, input));
    part.widget()->resize(1100, 800);
    part.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));
    auto *selector = qobject_cast<KSelectAction *>(part.actionCollection()->action(QStringLiteral("editing_mode_selector")));
    QVERIFY(selector);
    QCOMPARE(selector->actions().size(), 5);
    QCOMPARE(selector->currentItem(), int(EditingMode::Reading));
    auto *named = part.actionCollection()->action(QStringLiteral("advanced_add_named_destination"));
    auto *ocr = part.actionCollection()->action(QStringLiteral("advanced_recognize_english_text"));
    auto *ocrEdit = part.actionCollection()->action(QStringLiteral("advanced_edit_ocr_text"));
    auto *insert = part.actionCollection()->action(QStringLiteral("tools_insert_page"));
    auto *draw = part.actionCollection()->action(QStringLiteral("advanced_add_reading_view"));
    auto *apply = part.actionCollection()->action(QStringLiteral("view_apply_views_to_document"));
    auto *highlight = part.actionCollection()->action(QStringLiteral("annotation_highlighter"));
    QVERIFY(named && ocr && ocrEdit && insert && draw && apply && highlight);
    QVERIFY(!part.actionCollection()->action(QStringLiteral("view_toggle_named_destinations"))->isVisible());
    QString error;
    const QList<ReadingView> views{{QStringLiteral("b99f36a5-bb53-4cf0-9a8e-069b84c4df33"), 1, NormalizedRect(.05, .1, .45, .4)}};
    QVERIFY(part.m_document->setReadingViews(0, views, &error));
    PageView *main = part.m_pageView;
    main->setReadingViewMode(true);
    highlight->trigger();
    QVERIFY(highlight->isChecked());
    QSignalSpy created(main, SIGNAL(createReadingViewRequested(int,QRectF)));
    for (EditingMode mode : {EditingMode::CrossReferences, EditingMode::Ocr, EditingMode::Pages, EditingMode::Views, EditingMode::Reading}) {
        selector->actions().at(int(mode))->trigger();
        QCOMPARE(part.m_editingMode, mode);
        QCOMPARE(selector->currentItem(), int(mode));
        QCOMPARE(main->namedDestinationsVisible(), mode == EditingMode::CrossReferences);
        QCOMPARE(main->ocrModeEnabled(), mode == EditingMode::Ocr);
        QCOMPARE(main->readingViewEditingEnabled(), mode == EditingMode::Views);
        QCOMPARE(named->isVisible(), mode == EditingMode::CrossReferences);
        QCOMPARE(ocr->isVisible(), mode == EditingMode::Ocr);
        QCOMPARE(ocrEdit->isVisible(), mode == EditingMode::Ocr);
        QCOMPARE(insert->isVisible(), mode == EditingMode::Pages);
        QCOMPARE(draw->isVisible(), mode == EditingMode::Views);
        QCOMPARE(apply->isVisible(), mode == EditingMode::Views);
        QVERIFY(main->readingViewMode());
        QVERIFY(!main->isOcrTextEditing());
        QVERIFY(highlight->isEnabled());
        QVERIFY(highlight->isChecked()); // changing task groups does not select another mouse tool
        QCOMPARE(part.m_document->readingViews(0), views);
    }
    QCOMPARE(created.count(), 0);
    selector->actions().at(int(EditingMode::Views))->trigger();
    QSignalSpy drawingCancelled(main, SIGNAL(readingViewCreationCancelled()));
    draw->trigger();
    const int previousCancellationCount = drawingCancelled.count();
    selector->actions().at(int(EditingMode::CrossReferences))->trigger();
    QVERIFY(drawingCancelled.count() > previousCancellationCount);
    QCOMPARE(part.m_document->readingViews(0), views);
    QSignalSpy namingCancelled(main, SIGNAL(namedDestinationCreationCancelled()));
    named->trigger();
    selector->actions().at(int(EditingMode::Ocr))->trigger();
    QVERIFY(namingCancelled.count() > 0);
    QVERIFY(!main->isOcrTextEditing());

    // New frames inherit the task mode, while their reading projection is independent.
    selector->actions().at(int(EditingMode::Views))->trigger();
    auto *openAuxiliary = part.actionCollection()->action(QStringLiteral("open_auxiliary_view"));
    QVERIFY(openAuxiliary && openAuxiliary->isEnabled());
    openAuxiliary->trigger();
    QTRY_COMPARE(part.m_documentWorkspace->auxiliaryViewCount(), 1);
    PageView *auxiliary = part.m_documentWorkspace->auxiliaryViews().constFirst();
    QVERIFY(auxiliary->readingViewEditingEnabled());
    auxiliary->setReadingViewMode(false);
    for (EditingMode mode : {EditingMode::Ocr, EditingMode::CrossReferences, EditingMode::Pages, EditingMode::Views, EditingMode::Reading}) {
        selector->actions().at(int(mode))->trigger();
        for (PageView *frame : {main, auxiliary}) {
            QCOMPARE(frame->ocrModeEnabled(), mode == EditingMode::Ocr);
            QCOMPARE(frame->namedDestinationsVisible(), mode == EditingMode::CrossReferences);
            QCOMPARE(frame->readingViewEditingEnabled(), mode == EditingMode::Views);
            QVERIFY(!frame->isOcrTextEditing());
        }
        QVERIFY(main->readingViewMode());
        QVERIFY(!auxiliary->readingViewMode());
        QCOMPARE(selector->currentItem(), int(mode));
    }
}

void PartTest::testToolbarButtonHeights_data()
{
    QTest::addColumn<int>("iconSize");
    QTest::addColumn<int>("pointSize");
    QTest::addColumn<int>("buttonStyle");
    for (int icon : {16, 24}) {
        for (int font : {9, 13}) {
            for (int style : {int(Qt::ToolButtonIconOnly), int(Qt::ToolButtonTextBesideIcon)}) {
                QTest::newRow(qPrintable(QStringLiteral("icon%1-font%2-style%3").arg(icon).arg(font).arg(style))) << icon << font << style;
            }
        }
    }
}

void PartTest::testToolbarButtonHeights()
{
    QFETCH(int, iconSize);
    QFETCH(int, pointSize);
    QFETCH(int, buttonStyle);
    QToolBar toolbar;
    toolbar.resize(900, 100);
    toolbar.setIconSize(QSize(iconSize, iconSize));
    toolbar.setToolButtonStyle(Qt::ToolButtonStyle(buttonStyle));
    QFont font = toolbar.font();
    font.setPointSize(pointSize);
    toolbar.setFont(font);
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::blue);
    auto *reference = toolbar.addAction(QIcon(pixmap), QStringLiteral("Pin"));
    auto *edit = toolbar.addAction(QStringLiteral("编辑 View"));
    edit->setCheckable(true);
    auto *draw = toolbar.addAction(QIcon(pixmap), QStringLiteral("画 View"));
    auto *modeCombo = new QComboBox(&toolbar);
    modeCombo->addItems({QStringLiteral("阅读 / 批注"), QStringLiteral("交叉引用"), QStringLiteral("OCR")});
    toolbar.addWidget(modeCombo);
    auto *referenceButton = qobject_cast<QToolButton *>(toolbar.widgetForAction(reference));
    auto *editButton = qobject_cast<QToolButton *>(toolbar.widgetForAction(edit));
    auto *drawButton = qobject_cast<QToolButton *>(toolbar.widgetForAction(draw));
    QVERIFY(referenceButton && editButton && drawButton);
    const int editWidth = editButton->sizeHint().width();
    ToolbarButtonHeight::install(&toolbar);
    ToolbarButtonHeight::install(&toolbar); // activation of another tab is idempotent
    toolbar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&toolbar));
    const auto aligned = [&] {
        const int height = referenceButton->height();
        const int center = referenceButton->mapTo(&toolbar, QPoint(0, height / 2)).y();
        for (auto *button : {referenceButton, editButton, drawButton}) {
            if (button->height() != height || height < button->sizeHint().height()
                || button->mapTo(&toolbar, QPoint(0, button->height() / 2)).y() != center) return false;
        }
        return modeCombo->height() == height && modeCombo->sizeHint().height() <= height
            && modeCombo->mapTo(&toolbar, QPoint(0, height / 2)).y() == center;
    };
    QTRY_VERIFY(aligned());
    QVERIFY(edit->icon().isNull()); // no replacement icon or text removal to mask the height mismatch
    QCOMPARE(edit->text(), QStringLiteral("编辑 View"));
    QCOMPARE(editButton->sizeHint().width(), editWidth);
    QCOMPARE(editButton->toolButtonStyle(), Qt::ToolButtonStyle(buttonStyle));
    QTest::mouseClick(editButton, Qt::LeftButton);
    QVERIFY(edit->isChecked());
    draw->setVisible(false);
    draw->setVisible(true);
    font.setPointSize(pointSize + 3);
    toolbar.setFont(font);
    toolbar.setIconSize(QSize(iconSize + 8, iconSize + 8));
    QTRY_VERIFY(aligned());
    auto *late = toolbar.addAction(QStringLiteral("Later text button"));
    auto *lateButton = qobject_cast<QToolButton *>(toolbar.widgetForAction(late));
    QVERIFY(lateButton);
    QTRY_COMPARE(lateButton->height(), referenceButton->height());
}

void PartTest::testReadingViewNativeRaster()
{
    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("native-raster.pdf"));
    QVERIFY(writeReadingViewFixture(input, 0, {}, true));
    Part part(nullptr, {});
    QVERIFY(openDocument(&part, input));
    part.m_document->setRotation(Okular::Rotation0);
    QString error;
    const QList<ReadingView> definitions{{QStringLiteral("b99f36a5-bb53-4cf0-9a8e-069b84c4df33"), 1, NormalizedRect(.12, .55, .15, .65)},
                                        {QStringLiteral("488284dd-125e-4c0b-8e72-3169a11ef171"), 2, NormalizedRect(0, 0, 1, 1)}};
    QVERIFY(part.m_document->setReadingViews(0, definitions, &error));
    part.widget()->resize(1200, 850);
    part.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));
    PageView *view = part.m_pageView;
    Okular::View *capabilities = view;
    capabilities->setCapability(Okular::View::ViewModeModality, int(Okular::Settings::EnumViewMode::Facing));
    capabilities->setCapability(Okular::View::Continuous, true);
    capabilities->setCapability(Okular::View::ZoomModality, int(PageView::ZoomFitPage));
    ReadingRenderTrace trace;
    view->setReadingViewMode(true);
    view->goToDisplayedPage(0);
    QTRY_VERIFY(view->displayedPagePixmapObserver(0));
    QTRY_VERIFY(view->displayedPagePixmapObserver(1));
    QVERIFY(view->displayedPagePixmapObserver(0) != view->displayedPagePixmapObserver(1));
    const double dpr = view->devicePixelRatioF();
    const auto ready = [&](int index, const NormalizedRect &region) {
        const QSize size = view->displayedPageUncroppedSize(index);
        auto *observer = view->displayedPagePixmapObserver(index);
        return observer && !size.isEmpty() && part.m_document->page(0)->hasPixmap(observer, int(std::ceil(size.width() * dpr)), int(std::ceil(size.height() * dpr)), region);
    };
    const NormalizedRect tinyInterior(.125, .56, .145, .64);
    const NormalizedRect wholeInterior(.1, .1, .9, .9);
    QTRY_VERIFY_WITH_TIMEOUT(ready(0, tinyInterior) && ready(1, wholeInterior), 15000);
    const QSize nativeSize = view->displayedPageUncroppedSize(0);
    QVERIFY(double(nativeSize.width()) * nativeSize.height() * dpr * dpr > 16.0 * 1024 * 1024);
    QVERIFY(readingRenderSubmissions.load() > 0); // validate the instrumentation, not just an empty counter
    const qint64 budget = qint64(view->viewport()->width()) * view->viewport()->height() * dpr * dpr * 16;
    QVERIFY2(readingLargestRender.load() <= budget, qPrintable(QString::number(readingLargestRender.load())));
    // Annotation refresh must retain each observer's own ROI instead of expanding
    // the tiny View's high-resolution request to the whole source page.
    auto *annotation = new GeomAnnotation;
    annotation->setBoundingRectangle(NormalizedRect(.126, .57, .14, .6));
    annotation->style().setColor(Qt::red);
    annotation->style().setWidth(2);
    part.m_document->addPageAnnotation(0, annotation);
    QTRY_VERIFY_WITH_TIMEOUT(ready(0, tinyInterior) && ready(1, wholeInterior), 15000);
    QVERIFY(readingLargestRender.load() <= budget);
    part.m_document->undo();
    QTRY_VERIFY_WITH_TIMEOUT(ready(0, tinyInterior) && ready(1, wholeInterior), 15000);
    QVERIFY(readingLargestRender.load() <= budget);
    qInfo() << "View native raster DPR" << dpr << "uncropped" << nativeSize
            << "actual generator submissions" << readingRenderSubmissions.load() << "largest raster pixels" << readingLargestRender.load();
    view->setReadingViewMode(false);
    QCoreApplication::processEvents();
}

void PartTest::testReadingViewHighlightInteraction_data()
{
    QTest::addColumn<int>("viewCount");
    QTest::newRow("two-views") << 2;
    QTest::newRow("thousand-views") << 1000;
}

void PartTest::testReadingViewHighlightInteraction()
{
    QFETCH(int, viewCount);
    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("highlight-performance.pdf"));
    QVERIFY(writeReadingViewFixture(input, 0, {}, true));
    Part part(nullptr, {});
    QVERIFY(openDocument(&part, input));
    part.m_document->setRotation(Okular::Rotation0);
    QString error;
    QList<ReadingView> definitions{{QStringLiteral("b99f36a5-bb53-4cf0-9a8e-069b84c4df33"), 1, NormalizedRect(.05, .1, .45, .4)},
                                  {QStringLiteral("488284dd-125e-4c0b-8e72-3169a11ef171"), 2, NormalizedRect(.05, .1, .45, .4)}};
    for (int i = 2; i < viewCount; ++i) definitions.append({QUuid::createUuid().toString(QUuid::WithoutBraces), i + 1, definitions[0].rectangle});
    QVERIFY(part.m_document->setReadingViews(0, definitions, &error));
    part.widget()->resize(1200, 850);
    part.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));
    PageView *view = part.m_pageView;
    Okular::View *capabilities = view;
    capabilities->setCapability(Okular::View::ViewModeModality, int(Okular::Settings::EnumViewMode::Facing));
    capabilities->setCapability(Okular::View::Continuous, true);
    capabilities->setCapability(Okular::View::ZoomModality, int(PageView::ZoomFitPage));
    view->setReadingViewMode(true);
    view->goToDisplayedPage(0);
    const double dpr = view->devicePixelRatioF();
    const auto ready = [&] {
        int visible = 0;
        for (int i = 0; i < viewCount; ++i) {
            auto *observer = view->displayedPagePixmapObserver(i);
            if (!observer) continue;
            ++visible;
            NormalizedRect region;
            if (!observer->visiblePixmapRect(0, &region)) return false;
            const QSize size = view->displayedPageUncroppedSize(i);
            if (!part.m_document->page(0)->hasPixmap(observer, int(std::ceil(size.width() * dpr)), int(std::ceil(size.height() * dpr)), region)) return false;
        }
        return visible >= 2;
    };
    QTRY_VERIFY_WITH_TIMEOUT(ready(), 15000);
    part.m_document->requestTextPage(0);
    QTRY_VERIFY(part.m_document->page(0)->hasTextPage());
    QWidget *canvas = view->viewport();
    const auto pointAt = [&](double nx, double ny) {
        QPoint best(-1, -1);
        double distance = 1e20;
        for (int y = 3; y < canvas->height() - 3; y += 3) {
            for (int x = 3; x < canvas->width() / 2; x += 3) {
                NormalizedPoint point;
                int page = -1;
                if (!view->mapGlobalPosToPagePoint(canvas->mapToGlobal(QPoint(x, y)), &page, &point) || page != 0) continue;
                const double delta = (point.x - nx) * (point.x - nx) + (point.y - ny) * (point.y - ny);
                if (delta < distance) { distance = delta; best = QPoint(x, y); }
            }
        }
        return distance < .001 ? best : QPoint(-1, -1);
    };
    const QPoint begin = pointAt(.072, .22), end = pointAt(.24, .22);
    QVERIFY(begin.x() >= 0 && end.x() >= 0);
    auto *highlighter = part.actionCollection()->action(QStringLiteral("annotation_highlighter"));
    QVERIFY(highlighter && highlighter->isEnabled());
    highlighter->trigger();
    QCoreApplication::processEvents();
    ReadingRenderTrace trace;
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, begin);
    QList<qint64> timings;
    for (int i = 1; i <= 200; ++i) {
        QElapsedTimer timer;
        timer.start();
        const QPoint point = begin + (end - begin) * i / 200;
        QTest::mouseMove(canvas, point, 0);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
        QCoreApplication::processEvents();
        timings.append(timer.nsecsElapsed());
    }
    QCOMPARE(readingRenderSubmissions.load(), 0); // preview is not a fresh PDF render per mouse move
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, end);
    QTRY_COMPARE(part.m_document->page(0)->annotations().size(), 1);
    QCOMPARE(part.m_document->page(0)->annotations().constFirst()->subType(), Annotation::AHighlight);
    QTRY_VERIFY_WITH_TIMEOUT(ready(), 15000);
    // Cache dimensions alone do not prove a forced refresh has finished. Check
    // the highlight pixels in every visible replica, not just the edited one.
    const auto allHighlighted = [&] {
        const QImage image = canvas->grab().toImage();
        for (int column = 0; column < 2; ++column) {
            bool sawHighlight = false;
            const int firstX = image.width() * (.5 * column + .035);
            const int lastX = image.width() * (.5 * column + .24);
            for (int y = 0; y < image.height(); y += 3) {
                int black = 0, yellow = 0, samples = 0;
                for (int x = firstX; x < lastX; x += 3) {
                    const QColor color = image.pixelColor(x, y);
                    ++samples;
                    if (color.red() < 64 && color.green() < 64 && color.blue() < 64) ++black;
                    if (color.red() > 180 && color.green() > 180 && color.blue() < 180) ++yellow;
                }
                // Ignore horizontal page borders, but reject any visible copy
                // of the fixture's black text that still lacks its highlight.
                if (black > 3 && black < samples * .75 && yellow < 3) return false;
                sawHighlight |= yellow > 3;
            }
            if (!sawHighlight) return false;
        }
        return true;
    };
    QTRY_VERIFY_WITH_TIMEOUT(allHighlighted(), 15000);
    const QString artifacts = qEnvironmentVariable("MENGSHEE_VIEW_RENDERING_ARTIFACTS");
    if (!artifacts.isEmpty()) {
        QVERIFY(canvas->grab().save(QDir(artifacts).filePath(QStringLiteral("highlight-dpr-%1-views-%2.png").arg(dpr).arg(viewCount))));
    }
    std::sort(timings.begin(), timings.end());
    qInfo() << "View highlighter count" << viewCount << "DPR" << dpr << "200 moves p50/p95 ms" << timings[100] / 1e6 << timings[190] / 1e6
            << "generator submissions after release" << readingRenderSubmissions.load();
    QVERIFY(view->readingViewMode());
    part.m_document->undo();
    QVERIFY(part.m_document->page(0)->annotations().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(ready(), 15000);
    view->setReadingViewMode(false);
}

void PartTest::testReadingViewTemplateApply()
{
    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("template.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/simple-multipage.pdf"), input));
    Part part(nullptr, {});
    QVERIFY(openDocument(&part, input));
    part.setEditingMode(EditingMode::Views);
    part.m_document->setRotation(Okular::Rotation0);
    auto *applyAction = part.actionCollection()->action(QStringLiteral("view_apply_views_to_document"));
    QVERIFY(applyAction);
    QVERIFY(!part.m_advancedModeEnabled);
    const QList<ReadingView> source{{QStringLiteral("b99f36a5-bb53-4cf0-9a8e-069b84c4df33"), 9, NormalizedRect(.03, .1, .47, .9)},
                                   {QStringLiteral("488284dd-125e-4c0b-8e72-3169a11ef171"), 2, NormalizedRect(.52, .2, .97, .88)}};
    const QList<ReadingView> previous{{QStringLiteral("90b59bba-c120-4057-ad74-95b0d64e2016"), 5, NormalizedRect(.1, .1, .9, .9)}};
    QString error;
    QVERIFY(part.m_document->setReadingViews(0, source, &error));
    QVERIFY(part.m_document->setReadingViews(1, previous, &error));
    const QString sourceToken = part.m_document->readingViewPageToken(0);
    const QString targetToken = part.m_document->readingViewPageToken(1);
    QTRY_VERIFY(applyAction->isEnabled());
    QTimer::singleShot(50, [] {
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
    });
    applyAction->trigger();
    QCOMPARE(part.m_document->readingViews(1), previous); // cancelled overwrite leaves every target intact
    QVERIFY(part.m_document->readingViews(2).isEmpty());
    QVERIFY(part.applyReadingViewsToDocument(0));
    QVERIFY(part.isModified());
    QCOMPARE(part.m_document->readingViews(0), source);
    QCOMPARE(part.m_document->readingViewPageToken(1), targetToken);
    QList<QList<ReadingView>> applied;
    QSet<QString> ids{source[0].id, source[1].id};
    const int count = int(part.m_document->pages());
    QVERIFY(count >= 3);
    for (int page = 1; page < count; ++page) {
        const auto definitions = part.m_document->readingViews(page);
        QCOMPARE(definitions.size(), 2);
        for (int i = 0; i < 2; ++i) {
            QCOMPARE(definitions[i].number, source[i].number);
            QVERIFY(readingViewRectClose(definitions[i].rectangle, source[i].rectangle));
            QVERIFY(!ids.contains(definitions[i].id));
            ids.insert(definitions[i].id);
        }
        applied.append(definitions);
    }
    part.m_document->undo(); // one undo restores all pages, not one page at a time
    QCOMPARE(part.m_document->readingViews(0), source);
    QCOMPARE(part.m_document->readingViews(1), previous);
    for (int page = 2; page < count; ++page) {
        QVERIFY(part.m_document->readingViews(page).isEmpty());
    }
    part.m_document->redo();
    for (int page = 1; page < count; ++page) {
        QCOMPARE(part.m_document->readingViews(page), applied[page - 1]);
    }
    const QString saved = dir.filePath(QStringLiteral("saved-template.pdf"));
    QVERIFY(part.saveAs(QUrl::fromLocalFile(saved), Part::NoSaveAsFlags));
    QVERIFY(!part.isModified());
    // Retained history resolves page identities after save/reload and a page move.
    QVERIFY(part.m_document->movePage(0, 2, &error));
    QCOMPARE(part.m_document->readingViewPageForToken(sourceToken), 2);
    part.m_document->undo();
    QCOMPARE(part.m_document->readingViews(2), source);
    QCOMPARE(part.m_document->readingViews(part.m_document->readingViewPageForToken(targetToken)), previous);
    part.m_document->redo();
    QCOMPARE(part.m_document->readingViews(2), source);
    QCOMPARE(part.m_document->readingViews(part.m_document->readingViewPageForToken(targetToken)), applied[0]);
    Part reopened(nullptr, {});
    QVERIFY(openDocument(&reopened, saved));
    QCOMPARE(reopened.m_document->readingViews(0), source);
    QCOMPARE(reopened.m_document->readingViews(1), applied[0]);
}

void PartTest::testReadingViewTemplateRejectsUnsupported()
{
    QTemporaryDir dir;
    const QString plain = dir.filePath(QStringLiteral("plain.pdf"));
    const QString unknown = dir.filePath(QStringLiteral("unknown.pdf"));
    const QString combined = dir.filePath(QStringLiteral("combined.pdf"));
    QVERIFY(writeReadingViewFixture(plain));
    QVERIFY(writeReadingViewFixture(unknown, 0, "/MengsheeViews << /Version 77 /Items [] >>"));
    Part part(nullptr, {});
    QVERIFY(openDocument(&part, plain));
    QString error;
    QVERIFY2(part.m_document->combinePdfFiles({plain, plain, unknown}, combined, true, &error), qPrintable(error));
    QVERIFY(openDocument(&part, combined));
    part.setEditingMode(EditingMode::Views);
    const QList<ReadingView> source{{QStringLiteral("b99f36a5-bb53-4cf0-9a8e-069b84c4df33"), 1, NormalizedRect(.1, .1, .45, .9)}};
    QVERIFY(part.m_document->setReadingViews(0, source, &error));
    const QString pageOneToken = part.m_document->readingViewPageToken(1);
    QVERIFY(!part.applyReadingViewsToDocument(0));
    QCOMPARE(part.m_document->readingViews(0), source);
    QVERIFY(part.m_document->readingViews(1).isEmpty());
    QCOMPARE(part.m_document->readingViewPageToken(1), pageOneToken); // no partial application before failure
    error.clear();
    part.m_document->readingViews(2, &error);
    QVERIFY(!error.isEmpty());
}

void PartTest::testReadingViewsUnsupportedMetadata()
{
    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("future.pdf"));
    QVERIFY(writeReadingViewFixture(input, 0, "/MengsheeViews << /Version 999 /Items [] >>"));
    Part part(nullptr, {});
    QVERIFY(openDocument(&part, input));
    QString error;
    QVERIFY(part.m_document->readingViews(0, &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(!part.m_document->setReadingViews(0, {}, &error));
    QVERIFY(!error.isEmpty());
}

void PartTest::testDeletePagePreservesInternalLinks()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString workingFile = tempDir.filePath(QStringLiteral("internal-links-source.pdf"));
    const QString editedFile = tempDir.filePath(QStringLiteral("internal-links-page-deleted.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/pdf_with_internal_links.pdf"), workingFile));

    Okular::Part editorPart(nullptr, {});
    QVERIFY(openDocument(&editorPart, workingFile));
    QString errorText;
    quint64 editId = 0;
    QVERIFY2(editorPart.m_document->detachPage(1, &editId, &errorText), qPrintable(errorText));
    QVERIFY2(editorPart.m_document->saveChanges(editedFile, &errorText), qPrintable(errorText));

    Okular::Part reopenedPart(nullptr, {});
    QVERIFY(openDocument(&reopenedPart, editedFile));
    QCOMPARE(reopenedPart.m_document->pages(), 2u);
    reopenedPart.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some GUI tests");
    }
    QVERIFY(QTest::qWaitForWindowExposed(reopenedPart.widget()));

    reopenedPart.m_document->setViewportPage(0);
    QTRY_VERIFY(reopenedPart.m_document->page(0)->hasPixmap(reopenedPart.m_pageView));
    reopenedPart.m_document->requestTextPage(0);
    QTRY_VERIFY(reopenedPart.m_document->page(0)->hasTextPage());

    QPoint internalLinkPosition;
    DocumentViewport internalLinkTarget;
    QString internalLinkTitle;
    const QString expectedLinkTitle = QStringLiteral("2.2 Example for list (enumerate)");
    QVERIFY(findVisibleInternalGotoLink(reopenedPart.m_pageView, reopenedPart.m_document, 0, 1, expectedLinkTitle, &internalLinkPosition, &internalLinkTarget, &internalLinkTitle));
    QCOMPARE(internalLinkTitle, expectedLinkTitle);
    QCOMPARE(internalLinkTarget.pageNumber, 1);
}

void PartTest::testClickBlankAfterHoveringLinkDoesNotFollowStaleLink()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/pdf_with_links.pdf")));
    part.widget()->resize(800, 600);
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));
    part.m_document->setViewportPage(0);
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));

    QAction *advancedMode = part.actionCollection()->action(QStringLiteral("view_toggle_named_destinations"));
    QVERIFY(advancedMode);
    advancedMode->setChecked(true);
    QVERIFY(part.m_pageView->advancedModeEnabled());

    const int width = part.m_pageView->horizontalScrollBar()->maximum() + part.m_pageView->viewport()->width();
    const int height = part.m_pageView->verticalScrollBar()->maximum() + part.m_pageView->viewport()->height();
    const QPoint linkPosition(width * 0.250, height * 0.127);
    const QPoint blankPosition(width * 0.75, height * 0.75);

    QDesktopServices::setUrlHandler(QStringLiteral("mailto"), this, "urlHandler");
    QSignalSpy openUrlSignalSpy(this, &PartTest::urlHandler);

    // Keep the hover state on the link, just as it is while its context menu
    // is open, then deliver a click elsewhere without an intervening move.
    QTest::mouseMove(part.m_pageView->viewport(), linkPosition);
    QTRY_COMPARE(part.m_pageView->cursor().shape(), Qt::SizeAllCursor);
    QTest::mouseClick(part.m_pageView->viewport(), Qt::LeftButton, Qt::NoModifier, blankPosition);
    QTest::qWait(100);

    QCOMPARE(openUrlSignalSpy.count(), 0);
}

void PartTest::testContentsEntryWithFitWidthNamedDestination()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString workingFile = tempDir.filePath(QStringLiteral("contents-source.pdf"));
    const QString outputFile = tempDir.filePath(QStringLiteral("contents-result.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/pdf_with_internal_links.pdf"), workingFile));

    Okular::Part part(nullptr, {});
    QVERIFY(openDocument(&part, workingFile));

    DocumentViewport target(1);
    target.rePos.enabled = true;
    target.rePos.normalizedX = 0.5;
    target.rePos.normalizedY = 0.37;
    target.rePos.pos = DocumentViewport::TopLeft;

    DocumentSynopsis synopsis;
    QDomElement entry = synopsis.createElement(QStringLiteral("New section"));
    entry.setAttribute(QStringLiteral("ViewportName"), QStringLiteral("New-section"));
    entry.setAttribute(QStringLiteral("Viewport"), target.toString());
    entry.setAttribute(QStringLiteral("CreateViewportName"), QStringLiteral("true"));
    synopsis.appendChild(entry);

    QString errorText;
    QVERIFY2(part.m_document->setDocumentSynopsis(synopsis, &errorText), qPrintable(errorText));
    QVERIFY2(part.m_document->saveChanges(outputFile, &errorText), qPrintable(errorText));

    QFile output(outputFile);
    QVERIFY(output.open(QIODevice::ReadOnly));
    QVERIFY(output.readAll().contains("/FitH"));

    Okular::Part reopenedPart(nullptr, {});
    QVERIFY(openDocument(&reopenedPart, outputFile));
    const DocumentViewport destination(reopenedPart.m_document->metaData(QStringLiteral("NamedViewport"), QStringLiteral("New-section")).toString());
    QVERIFY(destination.isValid());
    QCOMPARE(destination.pageNumber, 1);
    QVERIFY(destination.rePos.enabled);
    QVERIFY(qAbs(destination.rePos.normalizedY - 0.37) < 0.01);

    const DocumentSynopsis *reopenedSynopsis = reopenedPart.m_document->documentSynopsis();
    QVERIFY(reopenedSynopsis);
    const QDomElement reopenedEntry = reopenedSynopsis->firstChildElement();
    QCOMPARE(reopenedEntry.tagName(), QStringLiteral("New section"));
    QCOMPARE(reopenedEntry.attribute(QStringLiteral("ViewportName")), QStringLiteral("New-section"));
    QVERIFY(!reopenedEntry.hasAttribute(QStringLiteral("Viewport")));

    const DocumentSynopsis roundTrippedSynopsis = reopenedPart.m_toc->synopsisFromModel();
    QCOMPARE(roundTrippedSynopsis.firstChildElement().attribute(QStringLiteral("ViewportName")), QStringLiteral("New-section"));
}

void PartTest::testContentsDoesNotExposeInferredCurrentItem()
{
    Okular::Document document(nullptr);
    TOC contents(nullptr, &document);
    const QList<QByteArray> roleNames = contents.m_model->roleNames().values();
    QVERIFY(!roleNames.contains(QByteArrayLiteral("highlight")));
    QVERIFY(!roleNames.contains(QByteArrayLiteral("highlightedParent")));
}

void PartTest::testEditableContentsTree()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString workingFile = tempDir.filePath(QStringLiteral("editable-contents.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/file1.pdf"), workingFile));

    Okular::Part part(nullptr, {});
    QVERIFY(openDocument(&part, workingFile));
    QString errorText;
    QVERIFY2(part.m_document->setNamedDestination(QStringLiteral("target-a"), 1, 0.1, 0.2, &errorText), qPrintable(errorText));
    QVERIFY2(part.m_document->setNamedDestination(QStringLiteral("target-b"), 1, 0.3, 0.4, &errorText), qPrintable(errorText));

    DocumentSynopsis synopsis;
    for (const QString &title : {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}) {
        QDomElement entry = synopsis.createElement(title);
        entry.setAttribute(QStringLiteral("ViewportName"), QStringLiteral("target-a"));
        synopsis.appendChild(entry);
    }
    QVERIFY2(part.m_document->setDocumentSynopsis(synopsis, &errorText), qPrintable(errorText));

    part.setAdvancedModeEnabled(true);
    TOCModel *model = part.m_toc->m_model;
    QCOMPARE(model->rowCount(), 3);
    QSignalSpy modifiedSpy(part.m_toc.data(), &TOC::contentsModified);

    const QModelIndex firstEntry = model->index(0, 0);
    const QModelIndex secondEntry = model->index(1, 0);
    std::unique_ptr<QMimeData> nestMime(model->mimeData({secondEntry}));
    QVERIFY(nestMime);
    QVERIFY(model->dropMimeData(nestMime.get(), Qt::MoveAction, -1, 0, firstEntry));
    const QModelIndex nestedBeforeCommit = model->index(0, 0, model->index(0, 0));
    part.m_toc->m_treeView->expand(model->index(0, 0));
    part.m_toc->m_treeView->setCurrentIndex(nestedBeforeCommit);
    QTRY_COMPARE(modifiedSpy.count(), 1);
    model = part.m_toc->m_model;
    QVERIFY(part.m_toc->m_treeView->isExpanded(model->index(0, 0)));
    QCOMPARE(part.m_toc->m_treeView->currentIndex().data(Qt::DisplayRole).toString(), QStringLiteral("B"));

    const DocumentSynopsis *nestedSynopsis = part.m_document->documentSynopsis();
    QVERIFY(nestedSynopsis);
    const QDomElement nestedA = nestedSynopsis->firstChildElement();
    QCOMPARE(nestedA.tagName(), QStringLiteral("A"));
    QCOMPARE(nestedA.firstChildElement().tagName(), QStringLiteral("B"));
    QCOMPARE(nestedA.nextSiblingElement().tagName(), QStringLiteral("C"));

    const QModelIndex refreshedA = model->index(0, 0);
    const QModelIndex nestedB = model->index(0, 0, refreshedA);
    std::unique_ptr<QMimeData> unnestMime(model->mimeData({nestedB}));
    QVERIFY(unnestMime);
    QVERIFY(model->dropMimeData(unnestMime.get(), Qt::MoveAction, 2, 0, QModelIndex()));
    QTRY_COMPARE(modifiedSpy.count(), 2);
    model = part.m_toc->m_model;

    const DocumentSynopsis *reorderedSynopsis = part.m_document->documentSynopsis();
    QVERIFY(reorderedSynopsis);
    QCOMPARE(reorderedSynopsis->firstChildElement().tagName(), QStringLiteral("A"));
    QCOMPARE(reorderedSynopsis->firstChildElement().nextSiblingElement().tagName(), QStringLiteral("C"));
    QCOMPARE(reorderedSynopsis->firstChildElement().nextSiblingElement().nextSiblingElement().tagName(), QStringLiteral("B"));

    const QModelIndex refreshedB = model->index(2, 0);
    QVERIFY(part.m_toc->setEntryDestination(refreshedB, QStringLiteral("target-b"), std::nullopt));
    const DocumentSynopsis *retargetedSynopsis = part.m_document->documentSynopsis();
    QVERIFY(retargetedSynopsis);
    const QDomElement retargetedB = retargetedSynopsis->firstChildElement().nextSiblingElement().nextSiblingElement();
    QCOMPARE(retargetedB.attribute(QStringLiteral("ViewportName")), QStringLiteral("target-b"));

    DocumentViewport movedTarget(0);
    movedTarget.rePos.enabled = true;
    movedTarget.rePos.normalizedX = 0.7;
    movedTarget.rePos.normalizedY = 0.8;
    movedTarget.rePos.pos = DocumentViewport::TopLeft;
    QVERIFY2(part.applyLiveNamedDestination(QStringLiteral("target-b"), movedTarget, &errorText), qPrintable(errorText));
    model = part.m_toc->m_model;
    const QModelIndex liveContentsEntry = model->index(2, 0);
    QVERIFY(QMetaObject::invokeMethod(part.m_toc.data(), "slotExecuted", Qt::DirectConnection, Q_ARG(QModelIndex, liveContentsEntry)));
    QTRY_VERIFY([&] {
        const DocumentViewport viewport = part.m_pageView->documentViewport();
        return viewport.pageNumber == 0 && viewport.rePos.enabled && qAbs(viewport.rePos.normalizedX - 0.7) < 0.01 && qAbs(viewport.rePos.normalizedY - 0.8) < 0.01;
    }());
    part.closeUrl(false);
}

void PartTest::testAddNamedDestinationToEmptyContents()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString workingFile = tempDir.filePath(QStringLiteral("empty-contents-source.pdf"));
    const QString destinationFile = tempDir.filePath(QStringLiteral("empty-contents-with-destination.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/file1.pdf"), workingFile));

    Okular::Part sourcePart(nullptr, {});
    QVERIFY(openDocument(&sourcePart, workingFile));
    QString errorText;
    QVERIFY2(sourcePart.m_document->setNamedDestination(QStringLiteral("chapter-one"), 1, 0.2, 0.3, &errorText), qPrintable(errorText));
    QVERIFY2(sourcePart.m_document->saveChanges(destinationFile, &errorText), qPrintable(errorText));

    Okular::Part part(nullptr, {});
    QVERIFY(openDocument(&part, destinationFile));
    const DocumentSynopsis *initialSynopsis = part.m_document->documentSynopsis();
    QVERIFY(!initialSynopsis || initialSynopsis->firstChildElement().isNull());
    QVERIFY(!part.m_tocEnabled);

    part.m_toc->setEditingEnabled(true);
    part.m_toc->addNamedDestinationEntry(QStringLiteral("chapter-one"));

    const DocumentSynopsis *updatedSynopsis = part.m_document->documentSynopsis();
    QVERIFY(updatedSynopsis);
    const QDomElement entry = updatedSynopsis->firstChildElement();
    QCOMPARE(entry.tagName(), QStringLiteral("chapter-one"));
    QCOMPARE(entry.attribute(QStringLiteral("ViewportName")), QStringLiteral("chapter-one"));
    QVERIFY(!entry.hasAttribute(QStringLiteral("Viewport")));
    QVERIFY(part.m_tocEnabled);
}

void PartTest::testLiveNamedDestinationEditing()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString workingFile = tempDir.filePath(QStringLiteral("live-destination-source.pdf"));
    const QString savedFile = tempDir.filePath(QStringLiteral("live-destination-saved.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/file1.pdf"), workingFile));

    Okular::Part part(nullptr, {});
    QVERIFY(openDocument(&part, workingFile));
    part.setAdvancedModeEnabled(true);

    const QString name = QStringLiteral("live-destination");
    QVERIFY(part.addNamedDestinationWithName(0, Okular::NormalizedPoint(0.2, 0.3), name, false));
    QCOMPARE(part.url(), QUrl::fromLocalFile(workingFile));

    DocumentViewport destination(part.m_document->metaData(QStringLiteral("NamedViewport"), name).toString());
    QVERIFY(destination.isValid());
    QCOMPARE(destination.pageNumber, 0);
    QVERIFY(qAbs(destination.rePos.normalizedX - 0.2) < 0.01);
    QVERIFY(qAbs(destination.rePos.normalizedY - 0.3) < 0.01);

    QVERIFY(part.m_document->canUndo());
    part.m_document->undo();
    QVERIFY(!DocumentViewport(part.m_document->metaData(QStringLiteral("NamedViewport"), name).toString()).isValid());
    part.m_document->redo();
    QVERIFY(DocumentViewport(part.m_document->metaData(QStringLiteral("NamedViewport"), name).toString()).isValid());

    QString errorText;
    QVERIFY2(part.m_document->saveChanges(savedFile, &errorText), qPrintable(errorText));
    Okular::Part reopenedPart(nullptr, {});
    QVERIFY(openDocument(&reopenedPart, savedFile));
    QVERIFY(DocumentViewport(reopenedPart.m_document->metaData(QStringLiteral("NamedViewport"), name).toString()).isValid());
}

void PartTest::testNamedDestinationDragDoesNotStartTextSelection()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString workingFile = tempDir.filePath(QStringLiteral("named-destination-drag.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/file1.pdf"), workingFile));

    Okular::Part part(nullptr, {});
    QVERIFY(openDocument(&part, workingFile));
    part.widget()->resize(900, 700);
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some GUI tests");
    }
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));
    part.setAdvancedModeEnabled(true);
    part.m_document->setViewportPage(0);
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));
    part.m_document->requestTextPage(0);
    QTRY_VERIFY(part.m_document->page(0)->hasTextPage());

    const QString destinationName = QStringLiteral("drag-target");
    QVERIFY(part.addNamedDestinationWithName(0, Okular::NormalizedPoint(0.25, 0.25), destinationName, false));
    QApplication::processEvents();

    QPoint markerPoint(-1, -1);
    const auto findMarker = [&] {
        QApplication::processEvents();
        const QRect viewportRect = part.m_pageView->viewport()->rect();
        double bestDistance = std::numeric_limits<double>::max();
        for (int y = 0; y < viewportRect.height(); y += 8) {
            for (int x = 0; x < viewportRect.width(); x += 8) {
                const QPoint point(x, y);
                int pageNumber = -1;
                Okular::NormalizedPoint pagePoint;
                if (!part.m_pageView->mapGlobalPosToPagePoint(part.m_pageView->viewport()->mapToGlobal(point), &pageNumber, &pagePoint) || pageNumber != 0) {
                    continue;
                }
                const double distance = qAbs(pagePoint.x - 0.25) + qAbs(pagePoint.y - 0.25);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    markerPoint = point;
                }
            }
        }
        return markerPoint.x() >= 0;
    };
    QTRY_VERIFY_WITH_TIMEOUT(findMarker(), 3000);

    QAction *textSelectionAction = part.actionCollection()->action(QStringLiteral("mouse_textselect"));
    QVERIFY(textSelectionAction);
    textSelectionAction->trigger();
    QVERIFY(textSelectionAction->isChecked());
    QVERIFY(!part.m_document->page(0)->textSelection());

    const QRect viewportRect = part.m_pageView->viewport()->rect().adjusted(20, 20, -20, -20);
    const QPoint destinationPoint(qBound(viewportRect.left(), markerPoint.x() + 40, viewportRect.right()), qBound(viewportRect.top(), markerPoint.y() + 30, viewportRect.bottom()));
    QTest::mousePress(part.m_pageView->viewport(), Qt::LeftButton, Qt::NoModifier, markerPoint);
    QTest::mouseMove(part.m_pageView->viewport(), destinationPoint, 25);
    QTest::mouseRelease(part.m_pageView->viewport(), Qt::LeftButton, Qt::NoModifier, destinationPoint);
    QTRY_VERIFY([&] {
        const DocumentViewport movedDestination(part.m_document->metaData(QStringLiteral("NamedViewport"), destinationName).toString());
        return movedDestination.isValid() && (qAbs(movedDestination.rePos.normalizedX - 0.25) > 0.01 || qAbs(movedDestination.rePos.normalizedY - 0.25) > 0.01);
    }());

    const QPoint idleMovePoint(qBound(viewportRect.left(), destinationPoint.x() + 20, viewportRect.right()), qBound(viewportRect.top(), destinationPoint.y() + 20, viewportRect.bottom()));
    QTest::mouseMove(part.m_pageView->viewport(), idleMovePoint, 25);
    QApplication::processEvents();
    QVERIFY(textSelectionAction->isChecked());
    QVERIFY(!part.m_document->page(0)->textSelection());
    part.closeUrl(false);
}

void PartTest::testLivePdfLinkEditing()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString workingFile = tempDir.filePath(QStringLiteral("live-link-source.pdf"));
    const QString savedFile = tempDir.filePath(QStringLiteral("live-link-saved.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/pdf_with_internal_links.pdf"), workingFile));

    Okular::Part part(nullptr, {});
    QVERIFY(openDocument(&part, workingFile));
    QVERIFY(part.m_document->canEditPdfLinks());
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some GUI tests");
    }
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));
    part.m_document->setViewportPage(0);
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));

    const auto actionAt = [&part](const QRectF &expectedRectangle) -> const Okular::Action * {
        for (const Okular::ObjectRect *rect : part.m_document->page(0)->objectRects()) {
            if (!rect || rect->objectType() != Okular::ObjectRect::Action || !rect->object()) {
                continue;
            }
            const QRectF actual = rect->region().boundingRect();
            if (qAbs(actual.left() - expectedRectangle.left()) < 0.005 && qAbs(actual.top() - expectedRectangle.top()) < 0.005 && qAbs(actual.right() - expectedRectangle.right()) < 0.005
                && qAbs(actual.bottom() - expectedRectangle.bottom()) < 0.005) {
                return static_cast<const Okular::Action *>(rect->object());
            }
        }
        return nullptr;
    };

    QString errorText;
    const QRectF originalRectangle(QPointF(0.72, 0.76), QPointF(0.86, 0.83));
    QVERIFY2(part.m_document->createInternalLink(1, originalRectangle.left(), originalRectangle.top(), originalRectangle.right(), originalRectangle.bottom(), QString(), 2, 0.2, 0.3, &errorText), qPrintable(errorText));
    QCOMPARE(part.url().toLocalFile(), workingFile);
    const Okular::Action *action = actionAt(originalRectangle);
    QVERIFY(action);
    QCOMPARE(action->actionType(), Okular::Action::Goto);
    QCOMPARE(static_cast<const Okular::GotoAction *>(action)->destViewport().pageNumber, 1);

    const QString externalUrl = QStringLiteral("https://example.com/live-link");
    QVERIFY2(part.m_document->editExternalLinkDestination(1, originalRectangle.left(), originalRectangle.top(), originalRectangle.right(), originalRectangle.bottom(), externalUrl, &errorText), qPrintable(errorText));
    action = actionAt(originalRectangle);
    QVERIFY(action);
    const auto *browseAction = dynamic_cast<const Okular::BrowseAction *>(action);
    QVERIFY(browseAction);
    QCOMPARE(browseAction->url(), QUrl(externalUrl));

    const QRectF movedRectangle(QPointF(0.61, 0.69), QPointF(0.79, 0.78));
    QVERIFY2(part.m_document->editPdfLinkRectangle(1,
                                                   originalRectangle.left(),
                                                   originalRectangle.top(),
                                                   originalRectangle.right(),
                                                   originalRectangle.bottom(),
                                                   movedRectangle.left(),
                                                   movedRectangle.top(),
                                                   movedRectangle.right(),
                                                   movedRectangle.bottom(),
                                                   &errorText),
             qPrintable(errorText));
    QVERIFY(!actionAt(originalRectangle));
    QVERIFY(dynamic_cast<const Okular::BrowseAction *>(actionAt(movedRectangle)));

    QVERIFY2(part.m_document->deletePdfLink(1, movedRectangle.left(), movedRectangle.top(), movedRectangle.right(), movedRectangle.bottom(), &errorText), qPrintable(errorText));
    QVERIFY(!actionAt(movedRectangle));

    QVERIFY2(part.m_document->createExternalLink(1, movedRectangle.left(), movedRectangle.top(), movedRectangle.right(), movedRectangle.bottom(), externalUrl, &errorText), qPrintable(errorText));
    QVERIFY2(part.m_document->saveChanges(savedFile, &errorText), qPrintable(errorText));

    Okular::Part reopenedPart(nullptr, {});
    QVERIFY(openDocument(&reopenedPart, savedFile));
    reopenedPart.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(reopenedPart.widget()));
    reopenedPart.m_document->setViewportPage(0);
    QTRY_VERIFY(reopenedPart.m_document->page(0)->hasPixmap(reopenedPart.m_pageView));
    bool foundSavedLink = false;
    for (const Okular::ObjectRect *rect : reopenedPart.m_document->page(0)->objectRects()) {
        if (!rect || rect->objectType() != Okular::ObjectRect::Action || !rect->object()) {
            continue;
        }
        const QRectF actual = rect->region().boundingRect();
        const auto *savedBrowseAction = dynamic_cast<const Okular::BrowseAction *>(static_cast<const Okular::Action *>(rect->object()));
        if (savedBrowseAction && savedBrowseAction->url() == QUrl(externalUrl) && qAbs(actual.left() - movedRectangle.left()) < 0.005 && qAbs(actual.top() - movedRectangle.top()) < 0.005) {
            foundSavedLink = true;
            break;
        }
    }
    QVERIFY(foundSavedLink);
}

void PartTest::testLiveLinkSurvivesNamedDestinationUpdate()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString workingFile = tempDir.filePath(QStringLiteral("live-link-and-destinations.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/file1.pdf"), workingFile));

    Okular::Part part(nullptr, {});
    QVERIFY(openDocument(&part, workingFile));
    part.setAdvancedModeEnabled(true);

    const QString firstDestinationName = QStringLiteral("live-target-1");
    const QString secondDestinationName = QStringLiteral("live-target-2");
    QVERIFY(part.addNamedDestinationWithName(0, Okular::NormalizedPoint(0.2, 0.3), firstDestinationName, false));

    QString errorText;
    const QRectF linkRectangle(QPointF(0.72, 0.76), QPointF(0.86, 0.83));
    QVERIFY2(part.m_document->createInternalLink(1,
                                                 linkRectangle.left(),
                                                 linkRectangle.top(),
                                                 linkRectangle.right(),
                                                 linkRectangle.bottom(),
                                                 firstDestinationName,
                                                 1,
                                                 0.2,
                                                 0.3,
                                                 &errorText),
             qPrintable(errorText));

    const auto liveLinkCount = [&part, &firstDestinationName]() {
        int count = 0;
        for (const Okular::ObjectRect *rect : part.m_document->page(0)->objectRects()) {
            if (!rect || rect->objectType() != Okular::ObjectRect::Action || !rect->object()) {
                continue;
            }
            const auto *action = static_cast<const Okular::Action *>(rect->object());
            if (action->actionType() == Okular::Action::Goto && static_cast<const Okular::GotoAction *>(action)->destinationName() == firstDestinationName) {
                ++count;
            }
        }
        return count;
    };

    QCOMPARE(liveLinkCount(), 1);
    QVERIFY(part.addNamedDestinationWithName(0, Okular::NormalizedPoint(0.5, 0.6), secondDestinationName, false));
    QApplication::processEvents();
    QCOMPARE(liveLinkCount(), 1);

    DocumentViewport movedFirstDestination(0);
    movedFirstDestination.rePos.enabled = true;
    movedFirstDestination.rePos.normalizedX = 0.65;
    movedFirstDestination.rePos.normalizedY = 0.7;
    movedFirstDestination.rePos.pos = DocumentViewport::TopLeft;
    QVERIFY2(part.applyLiveNamedDestination(firstDestinationName, movedFirstDestination, &errorText), qPrintable(errorText));

    const Okular::GotoAction *namedLinkAction = nullptr;
    for (const Okular::ObjectRect *rect : part.m_document->page(0)->objectRects()) {
        if (!rect || rect->objectType() != Okular::ObjectRect::Action || !rect->object()) {
            continue;
        }
        const auto *action = static_cast<const Okular::Action *>(rect->object());
        if (action->actionType() != Okular::Action::Goto) {
            continue;
        }
        const auto *gotoAction = static_cast<const Okular::GotoAction *>(action);
        if (gotoAction->destinationName() == firstDestinationName) {
            namedLinkAction = gotoAction;
            break;
        }
    }
    QVERIFY(namedLinkAction);
    QVERIFY(!namedLinkAction->destViewport().isValid());
    part.m_document->processAction(namedLinkAction);
    QTRY_VERIFY([&] {
        const DocumentViewport viewport = part.m_document->viewport();
        return viewport.pageNumber == 0 && viewport.rePos.enabled && qAbs(viewport.rePos.normalizedX - 0.65) < 0.01 && qAbs(viewport.rePos.normalizedY - 0.7) < 0.01;
    }());
    part.closeUrl(false);
}

void PartTest::testEditPdfNamedDestinationAndLink()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString workingFile = tempDir.filePath(QStringLiteral("link-edit-source.pdf"));
    const QString destinationFile = tempDir.filePath(QStringLiteral("link-edit-destination.pdf"));
    const QString movedDestinationFile = tempDir.filePath(QStringLiteral("link-edit-destination-moved.pdf"));
    const QString createdLinkFile = tempDir.filePath(QStringLiteral("link-created-result.pdf"));
    const QString resizedLinkFile = tempDir.filePath(QStringLiteral("link-resized-result.pdf"));
    const QString deletedLinkFile = tempDir.filePath(QStringLiteral("link-deleted-result.pdf"));
    const QString editedLinkFile = tempDir.filePath(QStringLiteral("link-edit-result.pdf"));
    const QString renamedDestinationFile = tempDir.filePath(QStringLiteral("link-edit-renamed.pdf"));
    const QString deletedDestinationFile = tempDir.filePath(QStringLiteral("link-edit-deleted.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/pdf_with_internal_links.pdf"), workingFile));

    Okular::Part sourcePart(nullptr, {});
    QVERIFY(openDocument(&sourcePart, workingFile));
    QVERIFY(sourcePart.m_document->canEditPdfLinks());
    sourcePart.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some GUI tests");
    }
    QVERIFY(QTest::qWaitForWindowExposed(sourcePart.widget()));
    sourcePart.m_document->setViewportPage(0);
    QTRY_VERIFY(sourcePart.m_document->page(0)->hasPixmap(sourcePart.m_pageView));

    QRectF sourceLinkRectangle;
    for (const Okular::ObjectRect *rect : sourcePart.m_document->page(0)->objectRects()) {
        if (!rect || rect->objectType() != Okular::ObjectRect::Action || !rect->object()) {
            continue;
        }
        const auto *action = static_cast<const Okular::Action *>(rect->object());
        if (action->actionType() != Okular::Action::Goto || static_cast<const Okular::GotoAction *>(action)->isExternal()) {
            continue;
        }
        sourceLinkRectangle = rect->region().boundingRect();
        break;
    }
    QVERIFY(sourceLinkRectangle.isValid());

    QString errorText;
    const QString destinationName = QStringLiteral("user.eq2");
    QVERIFY2(sourcePart.m_document->setNamedDestination(destinationName, 2, 0.35, 0.45, &errorText), qPrintable(errorText));
    QVERIFY2(sourcePart.m_document->saveChanges(destinationFile, &errorText), qPrintable(errorText));

    DocumentViewport requestedViewport(0);
    requestedViewport.rePos.enabled = true;
    requestedViewport.rePos.normalizedX = 0.37;
    requestedViewport.rePos.normalizedY = 0.42;
    requestedViewport.rePos.pos = DocumentViewport::Center;
    sourcePart.m_pageView->goToDocumentViewport(requestedViewport, false, false);
    QApplication::processEvents();
    const DocumentViewport viewportBeforeEdit = sourcePart.m_pageView->documentViewport();
    QVERIFY(viewportBeforeEdit.isValid());
    QVERIFY(sourcePart.applyPageEditBackingFile(destinationFile, 0, false, true));
    const DocumentViewport viewportAfterEdit = sourcePart.m_pageView->documentViewport();
    QCOMPARE(viewportAfterEdit.pageNumber, viewportBeforeEdit.pageNumber);
    QCOMPARE(viewportAfterEdit.rePos.enabled, viewportBeforeEdit.rePos.enabled);
    QVERIFY(qAbs(viewportAfterEdit.rePos.normalizedX - viewportBeforeEdit.rePos.normalizedX) < 0.001);
    QVERIFY(qAbs(viewportAfterEdit.rePos.normalizedY - viewportBeforeEdit.rePos.normalizedY) < 0.001);
    QCOMPARE(viewportAfterEdit.rePos.pos, viewportBeforeEdit.rePos.pos);
    sourcePart.closeUrl();

    Okular::Part destinationPart(nullptr, {});
    QVERIFY(openDocument(&destinationPart, destinationFile));
    const DocumentViewport addedDestination(destinationPart.m_document->metaData(QStringLiteral("NamedViewport"), destinationName).toString());
    QVERIFY(addedDestination.isValid());
    QCOMPARE(addedDestination.pageNumber, 1);
    QVERIFY(addedDestination.rePos.enabled);
    QVERIFY(qAbs(addedDestination.rePos.normalizedX - 0.35) < 0.01);
    QVERIFY(qAbs(addedDestination.rePos.normalizedY - 0.45) < 0.01);

    const QRectF createdLinkRectangle(0.08, 0.08, 0.18, 0.07);
    QVERIFY2(destinationPart.m_document->createInternalLink(
                 1, createdLinkRectangle.left(), createdLinkRectangle.top(), createdLinkRectangle.right(), createdLinkRectangle.bottom(), destinationName, 2, 0.35, 0.45, &errorText),
             qPrintable(errorText));
    QVERIFY2(destinationPart.m_document->saveChanges(createdLinkFile, &errorText), qPrintable(errorText));

    Okular::Part createdLinkPart(nullptr, {});
    QVERIFY(openDocument(&createdLinkPart, createdLinkFile));
    createdLinkPart.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(createdLinkPart.widget()));
    createdLinkPart.m_document->setViewportPage(0);
    QTRY_VERIFY(createdLinkPart.m_document->page(0)->hasPixmap(createdLinkPart.m_pageView));
    bool foundCreatedLink = false;
    for (const Okular::ObjectRect *rect : createdLinkPart.m_document->page(0)->objectRects()) {
        if (!rect || rect->objectType() != Okular::ObjectRect::Action || !rect->object() || !rect->region().boundingRect().intersects(createdLinkRectangle)) {
            continue;
        }
        const auto *action = static_cast<const Okular::Action *>(rect->object());
        if (action->actionType() == Okular::Action::Goto && static_cast<const Okular::GotoAction *>(action)->destinationName() == destinationName) {
            foundCreatedLink = true;
            break;
        }
    }
    QVERIFY(foundCreatedLink);

    const QRectF resizedLinkRectangle(0.35, 0.1, 0.16, 0.09);
    QVERIFY2(createdLinkPart.m_document->editPdfLinkRectangle(1,
                                                                         createdLinkRectangle.left(),
                                                                         createdLinkRectangle.top(),
                                                                         createdLinkRectangle.right(),
                                                                         createdLinkRectangle.bottom(),
                                                                         resizedLinkRectangle.left(),
                                                                         resizedLinkRectangle.top(),
                                                                         resizedLinkRectangle.right(),
                                                                         resizedLinkRectangle.bottom(),
                                                                         &errorText),
             qPrintable(errorText));
    QVERIFY2(createdLinkPart.m_document->saveChanges(resizedLinkFile, &errorText), qPrintable(errorText));
    createdLinkPart.closeUrl();

    Okular::Part resizedLinkPart(nullptr, {});
    QVERIFY(openDocument(&resizedLinkPart, resizedLinkFile));
    resizedLinkPart.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(resizedLinkPart.widget()));
    resizedLinkPart.m_document->setViewportPage(0);
    QTRY_VERIFY(resizedLinkPart.m_document->page(0)->hasPixmap(resizedLinkPart.m_pageView));
    bool foundResizedLink = false;
    bool foundLinkAtOldRectangle = false;
    for (const Okular::ObjectRect *rect : resizedLinkPart.m_document->page(0)->objectRects()) {
        if (!rect || rect->objectType() != Okular::ObjectRect::Action || !rect->object()) {
            continue;
        }
        const auto *action = static_cast<const Okular::Action *>(rect->object());
        if (action->actionType() != Okular::Action::Goto || static_cast<const Okular::GotoAction *>(action)->destinationName() != destinationName) {
            continue;
        }
        const QRectF rectangle = rect->region().boundingRect();
        foundResizedLink = foundResizedLink || rectangle.intersects(resizedLinkRectangle);
        foundLinkAtOldRectangle = foundLinkAtOldRectangle || rectangle.intersects(createdLinkRectangle);
    }
    QVERIFY(foundResizedLink);
    QVERIFY(!foundLinkAtOldRectangle);

    QVERIFY2(resizedLinkPart.m_document->deletePdfLink(1, resizedLinkRectangle.left(), resizedLinkRectangle.top(), resizedLinkRectangle.right(), resizedLinkRectangle.bottom(), &errorText),
             qPrintable(errorText));
    QVERIFY2(resizedLinkPart.m_document->saveChanges(deletedLinkFile, &errorText), qPrintable(errorText));
    resizedLinkPart.closeUrl();

    Okular::Part deletedLinkPart(nullptr, {});
    QVERIFY(openDocument(&deletedLinkPart, deletedLinkFile));
    bool foundDeletedLink = false;
    for (const Okular::ObjectRect *rect : deletedLinkPart.m_document->page(0)->objectRects()) {
        if (rect && rect->objectType() == Okular::ObjectRect::Action && rect->object() && rect->region().boundingRect().intersects(createdLinkRectangle)) {
            foundDeletedLink = true;
            break;
        }
    }
    QVERIFY(!foundDeletedLink);
    deletedLinkPart.closeUrl();

    QVERIFY2(destinationPart.m_document->setNamedDestination(destinationName, 3, 0.2, 0.25, &errorText), qPrintable(errorText));
    QVERIFY2(destinationPart.m_document->saveChanges(movedDestinationFile, &errorText), qPrintable(errorText));
    destinationPart.closeUrl();

    Okular::Part movedDestinationPart(nullptr, {});
    QVERIFY(openDocument(&movedDestinationPart, movedDestinationFile));
    const DocumentViewport movedDestination(movedDestinationPart.m_document->metaData(QStringLiteral("NamedViewport"), destinationName).toString());
    QVERIFY(movedDestination.isValid());
    QCOMPARE(movedDestination.pageNumber, 2);
    QVERIFY(qAbs(movedDestination.rePos.normalizedX - 0.2) < 0.01);
    QVERIFY(qAbs(movedDestination.rePos.normalizedY - 0.25) < 0.01);
    movedDestinationPart.closeUrl();

    Okular::Part destinationPartForLink(nullptr, {});
    QVERIFY(openDocument(&destinationPartForLink, destinationFile));
    QVERIFY2(destinationPartForLink.m_document->editInternalLinkDestination(
                 1, sourceLinkRectangle.left(), sourceLinkRectangle.top(), sourceLinkRectangle.right(), sourceLinkRectangle.bottom(), destinationName, 2, 0.35, 0.45, &errorText),
             qPrintable(errorText));
    QVERIFY2(destinationPartForLink.m_document->saveChanges(editedLinkFile, &errorText), qPrintable(errorText));
    destinationPartForLink.closeUrl();

    Okular::Part editedPart(nullptr, {});
    QVERIFY(openDocument(&editedPart, editedLinkFile));
    editedPart.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(editedPart.widget()));
    editedPart.m_document->setViewportPage(0);
    QTRY_VERIFY(editedPart.m_document->page(0)->hasPixmap(editedPart.m_pageView));

    bool foundEditedLink = false;
    for (const Okular::ObjectRect *rect : editedPart.m_document->page(0)->objectRects()) {
        if (!rect || rect->objectType() != Okular::ObjectRect::Action || !rect->object() || !rect->region().boundingRect().intersects(sourceLinkRectangle)) {
            continue;
        }
        const auto *action = static_cast<const Okular::Action *>(rect->object());
        if (action->actionType() != Okular::Action::Goto) {
            continue;
        }
        const auto *gotoAction = static_cast<const Okular::GotoAction *>(action);
        if (gotoAction->destinationName() == destinationName) {
            foundEditedLink = true;
            break;
        }
    }
    QVERIFY(foundEditedLink);
    const DocumentViewport resolvedDestination(editedPart.m_document->metaData(QStringLiteral("NamedViewport"), destinationName).toString());
    QCOMPARE(resolvedDestination.pageNumber, 1);

    const QString renamedDestinationName = QStringLiteral("user.eq2.renamed");
    QVERIFY2(editedPart.m_document->renameNamedDestination(destinationName, renamedDestinationName, &errorText), qPrintable(errorText));
    QVERIFY2(editedPart.m_document->saveChanges(renamedDestinationFile, &errorText), qPrintable(errorText));
    editedPart.closeUrl();

    Okular::Part renamedPart(nullptr, {});
    QVERIFY(openDocument(&renamedPart, renamedDestinationFile));
    renamedPart.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(renamedPart.widget()));
    renamedPart.m_document->setViewportPage(0);
    QTRY_VERIFY(renamedPart.m_document->page(0)->hasPixmap(renamedPart.m_pageView));
    const DocumentViewport removedOldDestination(renamedPart.m_document->metaData(QStringLiteral("NamedViewport"), destinationName).toString());
    QVERIFY(!removedOldDestination.isValid());
    const DocumentViewport renamedDestination(renamedPart.m_document->metaData(QStringLiteral("NamedViewport"), renamedDestinationName).toString());
    QVERIFY(renamedDestination.isValid());
    QCOMPARE(renamedDestination.pageNumber, 1);

    bool foundRenamedLink = false;
    for (const Okular::ObjectRect *rect : renamedPart.m_document->page(0)->objectRects()) {
        if (!rect || rect->objectType() != Okular::ObjectRect::Action || !rect->object() || !rect->region().boundingRect().intersects(sourceLinkRectangle)) {
            continue;
        }
        const auto *action = static_cast<const Okular::Action *>(rect->object());
        if (action->actionType() == Okular::Action::Goto && static_cast<const Okular::GotoAction *>(action)->destinationName() == renamedDestinationName) {
            foundRenamedLink = true;
            break;
        }
    }
    QVERIFY(foundRenamedLink);

    QVERIFY2(renamedPart.m_document->deleteNamedDestination(renamedDestinationName, &errorText), qPrintable(errorText));
    QVERIFY2(renamedPart.m_document->saveChanges(deletedDestinationFile, &errorText), qPrintable(errorText));
    renamedPart.closeUrl();

    Okular::Part deletedPart(nullptr, {});
    QVERIFY(openDocument(&deletedPart, deletedDestinationFile));
    deletedPart.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(deletedPart.widget()));
    deletedPart.m_document->setViewportPage(0);
    QTRY_VERIFY(deletedPart.m_document->page(0)->hasPixmap(deletedPart.m_pageView));
    const DocumentViewport deletedDestination(deletedPart.m_document->metaData(QStringLiteral("NamedViewport"), renamedDestinationName).toString());
    QVERIFY(!deletedDestination.isValid());

    bool preservedUnresolvedLink = false;
    for (const Okular::ObjectRect *rect : deletedPart.m_document->page(0)->objectRects()) {
        if (!rect || rect->objectType() != Okular::ObjectRect::Action || !rect->object() || !rect->region().boundingRect().intersects(sourceLinkRectangle)) {
            continue;
        }
        const auto *action = static_cast<const Okular::Action *>(rect->object());
        if (action->actionType() == Okular::Action::Goto && static_cast<const Okular::GotoAction *>(action)->destinationName() == renamedDestinationName) {
            preservedUnresolvedLink = true;
            break;
        }
    }
    QVERIFY(preservedUnresolvedLink);
    deletedPart.closeUrl();
}

void PartTest::testEditExternalPdfLink()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString workingFile = tempDir.filePath(QStringLiteral("external-link-source.pdf"));
    const QString createdFile = tempDir.filePath(QStringLiteral("external-link-created.pdf"));
    const QString editedFile = tempDir.filePath(QStringLiteral("external-link-edited.pdf"));
    const QString internalFile = tempDir.filePath(QStringLiteral("external-link-made-internal.pdf"));
    const QString externalAgainFile = tempDir.filePath(QStringLiteral("external-link-made-external.pdf"));
    const QString deletedFile = tempDir.filePath(QStringLiteral("external-link-deleted.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/pdf_with_internal_links.pdf"), workingFile));

    const QRectF linkRectangle(0.70, 0.08, 0.18, 0.07);
    const QString firstUrl = QStringLiteral("https://example.com/first?from=mengshee");
    const QString secondUrl = QStringLiteral("https://example.org/second#target");
    const auto externalUrlAtRectangle = [&linkRectangle](Document *document) {
        for (const Okular::ObjectRect *rect : document->page(0)->objectRects()) {
            if (!rect || rect->objectType() != Okular::ObjectRect::Action || !rect->object() || !rect->region().boundingRect().intersects(linkRectangle)) {
                continue;
            }
            const auto *action = static_cast<const Okular::Action *>(rect->object());
            if (action->actionType() == Okular::Action::Browse) {
                return static_cast<const Okular::BrowseAction *>(action)->url();
            }
        }
        return QUrl();
    };

    QString errorText;
    Okular::Part sourcePart(nullptr, {});
    QVERIFY(openDocument(&sourcePart, workingFile));
    QVERIFY2(sourcePart.m_document->createExternalLink(1,
                                                                linkRectangle.left(),
                                                                linkRectangle.top(),
                                                                linkRectangle.right(),
                                                                linkRectangle.bottom(),
                                                                firstUrl,
                                                                &errorText),
             qPrintable(errorText));
    QVERIFY2(sourcePart.m_document->saveChanges(createdFile, &errorText), qPrintable(errorText));
    sourcePart.closeUrl();

    Okular::Part createdPart(nullptr, {});
    QVERIFY(openDocument(&createdPart, createdFile));
    QCOMPARE(externalUrlAtRectangle(createdPart.m_document), QUrl(firstUrl));
    QVERIFY2(createdPart.m_document->editExternalLinkDestination(1,
                                                                            linkRectangle.left(),
                                                                            linkRectangle.top(),
                                                                            linkRectangle.right(),
                                                                            linkRectangle.bottom(),
                                                                            secondUrl,
                                                                            &errorText),
             qPrintable(errorText));
    QVERIFY2(createdPart.m_document->saveChanges(editedFile, &errorText), qPrintable(errorText));
    createdPart.closeUrl();

    Okular::Part editedPart(nullptr, {});
    QVERIFY(openDocument(&editedPart, editedFile));
    QCOMPARE(externalUrlAtRectangle(editedPart.m_document), QUrl(secondUrl));
    QVERIFY2(editedPart.m_document->editInternalLinkDestination(1,
                                                                           linkRectangle.left(),
                                                                           linkRectangle.top(),
                                                                           linkRectangle.right(),
                                                                           linkRectangle.bottom(),
                                                                           QString(),
                                                                           2,
                                                                           0.2,
                                                                           0.3,
                                                                           &errorText),
             qPrintable(errorText));
    QVERIFY2(editedPart.m_document->saveChanges(internalFile, &errorText), qPrintable(errorText));
    editedPart.closeUrl();

    Okular::Part internalPart(nullptr, {});
    QVERIFY(openDocument(&internalPart, internalFile));
    QVERIFY(externalUrlAtRectangle(internalPart.m_document).isEmpty());
    bool foundInternalLink = false;
    for (const Okular::ObjectRect *rect : internalPart.m_document->page(0)->objectRects()) {
        if (!rect || rect->objectType() != Okular::ObjectRect::Action || !rect->object() || !rect->region().boundingRect().intersects(linkRectangle)) {
            continue;
        }
        const auto *action = static_cast<const Okular::Action *>(rect->object());
        foundInternalLink = action->actionType() == Okular::Action::Goto && !static_cast<const Okular::GotoAction *>(action)->isExternal();
        if (foundInternalLink) {
            break;
        }
    }
    QVERIFY(foundInternalLink);
    QVERIFY2(internalPart.m_document->editExternalLinkDestination(1,
                                                                             linkRectangle.left(),
                                                                             linkRectangle.top(),
                                                                             linkRectangle.right(),
                                                                             linkRectangle.bottom(),
                                                                             firstUrl,
                                                                             &errorText),
             qPrintable(errorText));
    QVERIFY2(internalPart.m_document->saveChanges(externalAgainFile, &errorText), qPrintable(errorText));
    internalPart.closeUrl();

    Okular::Part externalAgainPart(nullptr, {});
    QVERIFY(openDocument(&externalAgainPart, externalAgainFile));
    QCOMPARE(externalUrlAtRectangle(externalAgainPart.m_document), QUrl(firstUrl));
    QVERIFY2(externalAgainPart.m_document->deletePdfLink(1,
                                                                  linkRectangle.left(),
                                                                  linkRectangle.top(),
                                                                  linkRectangle.right(),
                                                                  linkRectangle.bottom(),
                                                                  &errorText),
             qPrintable(errorText));
    QVERIFY2(externalAgainPart.m_document->saveChanges(deletedFile, &errorText), qPrintable(errorText));
    externalAgainPart.closeUrl();

    Okular::Part deletedPart(nullptr, {});
    QVERIFY(openDocument(&deletedPart, deletedFile));
    QVERIFY(externalUrlAtRectangle(deletedPart.m_document).isEmpty());
}

void PartTest::testDuplicatePagePreservesInternalLinks()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString workingFile = tempDir.filePath(QStringLiteral("internal-links-source.pdf"));
    const QString editedFile = tempDir.filePath(QStringLiteral("internal-links-page-duplicated.pdf"));
    const QString editedAgainFile = tempDir.filePath(QStringLiteral("internal-links-page-duplicated-again.pdf"));
    const QString asIsFile = tempDir.filePath(QStringLiteral("internal-links-page-duplicated-as-is.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/pdf_with_internal_links.pdf"), workingFile));

    Okular::Part editorPart(nullptr, {});
    QVERIFY(openDocument(&editorPart, workingFile));
    QString errorText;
    quint64 editId = 0;
    QVERIFY2(editorPart.m_document->duplicatePage(0, true, &editId, &errorText), qPrintable(errorText));
    QVERIFY2(editorPart.m_document->saveChanges(editedFile, &errorText), qPrintable(errorText));

    Okular::Part reopenedPart(nullptr, {});
    QVERIFY(openDocument(&reopenedPart, editedFile));
    QCOMPARE(reopenedPart.m_document->pages(), 4u);
    reopenedPart.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some GUI tests");
    }
    QVERIFY(QTest::qWaitForWindowExposed(reopenedPart.widget()));

    reopenedPart.m_document->setViewportPage(0);
    QTRY_VERIFY(reopenedPart.m_document->page(0)->hasPixmap(reopenedPart.m_pageView));
    reopenedPart.m_document->requestTextPage(0);
    QTRY_VERIFY(reopenedPart.m_document->page(0)->hasTextPage());

    QPoint internalLinkPosition;
    DocumentViewport internalLinkTarget;
    QString internalLinkTitle;
    const QString expectedLinkTitle = QStringLiteral("2.2 Example for list (enumerate)");
    QVERIFY(findVisibleInternalGotoLink(reopenedPart.m_pageView, reopenedPart.m_document, 0, 3, expectedLinkTitle, &internalLinkPosition, &internalLinkTarget, &internalLinkTitle));
    QCOMPARE(internalLinkTitle, expectedLinkTitle);
    QCOMPARE(internalLinkTarget.pageNumber, 3);

    reopenedPart.m_document->setViewportPage(1);
    QTRY_VERIFY(reopenedPart.m_document->page(1)->hasPixmap(reopenedPart.m_pageView));
    reopenedPart.m_document->requestTextPage(1);
    QTRY_VERIFY(reopenedPart.m_document->page(1)->hasTextPage());

    const DocumentViewport sourceDestination(reopenedPart.m_document->metaData(QStringLiteral("NamedViewport"), QStringLiteral("section.1")).toString());
    const DocumentViewport cloneDestination(reopenedPart.m_document->metaData(QStringLiteral("NamedViewport"), QStringLiteral("section.1~2")).toString());
    QVERIFY(sourceDestination.isValid());
    QCOMPARE(sourceDestination.pageNumber, 0);
    QVERIFY(cloneDestination.isValid());
    QCOMPARE(cloneDestination.pageNumber, 1);
    QVERIFY(hasInternalGotoLinkToPage(reopenedPart.m_document, 1, 1));
    QVERIFY(hasInternalGotoLinkToPage(reopenedPart.m_document, 1, 2));
    QVERIFY(hasInternalGotoLinkToPage(reopenedPart.m_document, 1, 3));

    Okular::Part secondEditorPart(nullptr, {});
    QVERIFY(openDocument(&secondEditorPart, editedFile));
    QVERIFY2(secondEditorPart.m_document->duplicatePage(0, true, &editId, &errorText), qPrintable(errorText));
    QVERIFY2(secondEditorPart.m_document->saveChanges(editedAgainFile, &errorText), qPrintable(errorText));

    Okular::Part reopenedAgainPart(nullptr, {});
    QVERIFY(openDocument(&reopenedAgainPart, editedAgainFile));
    QCOMPARE(reopenedAgainPart.m_document->pages(), 5u);
    reopenedAgainPart.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(reopenedAgainPart.widget()));
    reopenedAgainPart.m_document->setViewportPage(1);
    QTRY_VERIFY(reopenedAgainPart.m_document->page(1)->hasPixmap(reopenedAgainPart.m_pageView));
    reopenedAgainPart.m_document->requestTextPage(1);
    QTRY_VERIFY(reopenedAgainPart.m_document->page(1)->hasTextPage());
    const DocumentViewport firstCloneDestination(reopenedAgainPart.m_document->metaData(QStringLiteral("NamedViewport"), QStringLiteral("section.1~2")).toString());
    const DocumentViewport secondCloneDestination(reopenedAgainPart.m_document->metaData(QStringLiteral("NamedViewport"), QStringLiteral("section.1~3")).toString());
    QVERIFY(firstCloneDestination.isValid());
    QCOMPARE(firstCloneDestination.pageNumber, 2);
    QVERIFY(secondCloneDestination.isValid());
    QCOMPARE(secondCloneDestination.pageNumber, 1);
    QVERIFY(hasInternalGotoLinkToPage(reopenedAgainPart.m_document, 1, 1));
    QVERIFY(hasInternalGotoLinkToPage(reopenedAgainPart.m_document, 1, 3));
    QVERIFY(hasInternalGotoLinkToPage(reopenedAgainPart.m_document, 1, 4));

    Okular::Part asIsEditorPart(nullptr, {});
    QVERIFY(openDocument(&asIsEditorPart, workingFile));
    QVERIFY2(asIsEditorPart.m_document->duplicatePage(0, false, &editId, &errorText), qPrintable(errorText));
    QVERIFY2(asIsEditorPart.m_document->saveChanges(asIsFile, &errorText), qPrintable(errorText));

    Okular::Part asIsPart(nullptr, {});
    QVERIFY(openDocument(&asIsPart, asIsFile));
    QCOMPARE(asIsPart.m_document->pages(), 4u);
    asIsPart.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(asIsPart.widget()));
    asIsPart.m_document->setViewportPage(1);
    QTRY_VERIFY(asIsPart.m_document->page(1)->hasPixmap(asIsPart.m_pageView));
    asIsPart.m_document->requestTextPage(1);
    QTRY_VERIFY(asIsPart.m_document->page(1)->hasTextPage());
    const DocumentViewport asIsSourceDestination(asIsPart.m_document->metaData(QStringLiteral("NamedViewport"), QStringLiteral("section.1")).toString());
    const DocumentViewport asIsCloneDestination(asIsPart.m_document->metaData(QStringLiteral("NamedViewport"), QStringLiteral("section.1~2")).toString());
    QVERIFY(asIsSourceDestination.isValid());
    QCOMPARE(asIsSourceDestination.pageNumber, 0);
    QVERIFY(!asIsCloneDestination.isValid());
    QVERIFY(hasInternalGotoLinkToPage(asIsPart.m_document, 1, 0));
    QVERIFY(hasInternalGotoLinkToPage(asIsPart.m_document, 1, 2));
    QVERIFY(hasInternalGotoLinkToPage(asIsPart.m_document, 1, 3));
}

void PartTest::testInsertPdfPagePreservesInternalLinks()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString workingFile = tempDir.filePath(QStringLiteral("internal-links-source.pdf"));
    const QString editedFile = tempDir.filePath(QStringLiteral("internal-links-page-inserted.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/pdf_with_internal_links.pdf"), workingFile));

    Okular::Part editorPart(nullptr, {});
    QVERIFY(openDocument(&editorPart, workingFile));
    QString errorText;
    quint64 editId = 0;
    QVERIFY2(editorPart.m_document->insertPdfPage(0, QStringLiteral(KDESRCDIR "data/file1.pdf"), 1, false, &editId, &errorText), qPrintable(errorText));
    QVERIFY2(editorPart.m_document->saveChanges(editedFile, &errorText), qPrintable(errorText));

    Okular::Part reopenedPart(nullptr, {});
    QVERIFY(openDocument(&reopenedPart, editedFile));
    QCOMPARE(reopenedPart.m_document->pages(), 4u);
    reopenedPart.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some GUI tests");
    }
    QVERIFY(QTest::qWaitForWindowExposed(reopenedPart.widget()));

    reopenedPart.m_document->setViewportPage(0);
    QTRY_VERIFY(reopenedPart.m_document->page(0)->hasPixmap(reopenedPart.m_pageView));
    reopenedPart.m_document->requestTextPage(0);
    QTRY_VERIFY(reopenedPart.m_document->page(0)->hasTextPage());

    QPoint internalLinkPosition;
    DocumentViewport internalLinkTarget;
    QString internalLinkTitle;
    const QString expectedLinkTitle = QStringLiteral("2.2 Example for list (enumerate)");
    QVERIFY(findVisibleInternalGotoLink(reopenedPart.m_pageView, reopenedPart.m_document, 0, 3, expectedLinkTitle, &internalLinkPosition, &internalLinkTarget, &internalLinkTitle));
    QCOMPARE(internalLinkTitle, expectedLinkTitle);
    QCOMPARE(internalLinkTarget.pageNumber, 3);

    const QString mergeHostFile = tempDir.filePath(QStringLiteral("merge-host.pdf"));
    const QString mergeFirstPageFile = tempDir.filePath(QStringLiteral("merge-first-page.pdf"));
    const QString mergeLinkedPagesFile = tempDir.filePath(QStringLiteral("merge-linked-pages.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/file1.pdf"), mergeHostFile));

    Okular::Part firstMergePart(nullptr, {});
    QVERIFY(openDocument(&firstMergePart, mergeHostFile));
    QVERIFY2(firstMergePart.m_document->insertPdfPage(0, workingFile, 1, false, &editId, &errorText), qPrintable(errorText));
    QVERIFY2(firstMergePart.m_document->saveChanges(mergeFirstPageFile, &errorText), qPrintable(errorText));

    Okular::Part secondMergePart(nullptr, {});
    QVERIFY(openDocument(&secondMergePart, mergeFirstPageFile));
    QVERIFY2(secondMergePart.m_document->insertPdfPage(1, workingFile, 3, false, &editId, &errorText), qPrintable(errorText));
    QVERIFY2(secondMergePart.m_document->saveChanges(mergeLinkedPagesFile, &errorText), qPrintable(errorText));

    Okular::Part mergedPart(nullptr, {});
    QVERIFY(openDocument(&mergedPart, mergeLinkedPagesFile));
    QCOMPARE(mergedPart.m_document->pages(), 3u);
    mergedPart.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(mergedPart.widget()));
    mergedPart.m_document->setViewportPage(1);
    QTRY_VERIFY(mergedPart.m_document->page(1)->hasPixmap(mergedPart.m_pageView));
    mergedPart.m_document->requestTextPage(1);
    QTRY_VERIFY(mergedPart.m_document->page(1)->hasTextPage());
    QVERIFY(hasInternalGotoLinkToPage(mergedPart.m_document, 1, 2));
}

void PartTest::testStandaloneCombineBackend()
{
    const QString sourceFile = qEnvironmentVariable("MENGSHEE_COMBINE_BACKEND_INPUT", QStringLiteral(KDESRCDIR "data/file1.pdf"));
    const QFileInfo sourceInfo(sourceFile);
    QVERIFY(sourceInfo.exists());

    QMimeDatabase mimeDatabase;
    const QMimeType mimeType = mimeDatabase.mimeTypeForFile(sourceInfo.absoluteFilePath(), QMimeDatabase::MatchContent);
    Okular::Document backendDocument(nullptr);
    QCOMPARE(backendDocument.openDocument(sourceInfo.absoluteFilePath(), QUrl::fromLocalFile(sourceInfo.absoluteFilePath()), mimeType), Okular::Document::OpenSuccess);
    QVERIFY(backendDocument.canCombinePdfFiles());

    QString errorText;
    QCOMPARE(backendDocument.pdfPageCount(sourceInfo.absoluteFilePath(), &errorText), static_cast<int>(backendDocument.pages()));
    QVERIFY2(errorText.isEmpty(), qPrintable(errorText));
}

void PartTest::testCombinePdfAvailableWithoutDocument()
{
    Okular::Part part(nullptr, {});
    QAction *combineAction = part.actionCollection()->action(QStringLiteral("file_combine_pdfs"));
    QVERIFY(combineAction);
    QVERIFY(combineAction->isEnabled());
    QVERIFY(!part.m_document->isOpened());

    bool dialogOpened = false;
    QTimer::singleShot(0, [&dialogOpened] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (dialog) {
            dialogOpened = true;
            dialog->reject();
        }
    });
    combineAction->trigger();

    QVERIFY(dialogOpened);
    QVERIFY(!part.m_document->isOpened());
}

void PartTest::testCombinePdfFilesPreservesSourceLinkNamespaces()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString sourceFile = tempDir.filePath(QStringLiteral("internal-links-source.pdf"));
    const QString namespacedFile = tempDir.filePath(QStringLiteral("combined-internal-links-namespaced.pdf"));
    const QString asIsFile = tempDir.filePath(QStringLiteral("combined-internal-links-as-is.pdf"));
    QVERIFY(QFile::copy(QStringLiteral(KDESRCDIR "data/pdf_with_internal_links.pdf"), sourceFile));

    Okular::Part editorPart(nullptr, {});
    QVERIFY(openDocument(&editorPart, sourceFile));
    QVERIFY(editorPart.m_document->canCombinePdfFiles());
    QString errorText;
    QCOMPARE(editorPart.m_document->pdfPageCount(sourceFile, &errorText), 3);
    QVERIFY2(errorText.isEmpty(), qPrintable(errorText));
    QVERIFY2(editorPart.m_document->combinePdfFiles(QStringList {sourceFile, sourceFile}, namespacedFile, true, &errorText), qPrintable(errorText));
    QVERIFY2(editorPart.m_document->combinePdfFiles(QStringList {sourceFile, sourceFile}, asIsFile, false, &errorText), qPrintable(errorText));

    const QString debugOutput = QString::fromLocal8Bit(qgetenv("MENGSHEE_COMBINE_TEST_OUTPUT"));
    if (!debugOutput.isEmpty()) {
        QFile::remove(debugOutput);
        QVERIFY(QFile::copy(namespacedFile, debugOutput));
    }

    Okular::Part combinedPart(nullptr, {});
    QVERIFY(openDocument(&combinedPart, namespacedFile));
    QCOMPARE(combinedPart.m_document->pages(), 6u);
    combinedPart.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some GUI tests");
    }
    QVERIFY(QTest::qWaitForWindowExposed(combinedPart.widget()));

    const DocumentViewport unsuffixedDestination(combinedPart.m_document->metaData(QStringLiteral("NamedViewport"), QStringLiteral("section.1")).toString());
    const DocumentViewport firstSourceDestination(combinedPart.m_document->metaData(QStringLiteral("NamedViewport"), QStringLiteral("section.1~1")).toString());
    const DocumentViewport secondSourceDestination(combinedPart.m_document->metaData(QStringLiteral("NamedViewport"), QStringLiteral("section.1~2")).toString());
    QVERIFY(!unsuffixedDestination.isValid());
    QVERIFY(firstSourceDestination.isValid());
    QCOMPARE(firstSourceDestination.pageNumber, 0);
    QVERIFY(secondSourceDestination.isValid());
    QCOMPARE(secondSourceDestination.pageNumber, 3);

    combinedPart.m_document->setViewportPage(0);
    QTRY_VERIFY(combinedPart.m_document->page(0)->hasPixmap(combinedPart.m_pageView));
    combinedPart.m_document->requestTextPage(0);
    QTRY_VERIFY(combinedPart.m_document->page(0)->hasTextPage());
    QVERIFY(hasInternalGotoLinkToPage(combinedPart.m_document, 0, 2));
    QVERIFY(!hasInternalGotoLinkToPage(combinedPart.m_document, 0, 5));

    combinedPart.m_document->setViewportPage(3);
    QTRY_VERIFY(combinedPart.m_document->page(3)->hasPixmap(combinedPart.m_pageView));
    combinedPart.m_document->requestTextPage(3);
    QTRY_VERIFY(combinedPart.m_document->page(3)->hasTextPage());
    QVERIFY(hasInternalGotoLinkToPage(combinedPart.m_document, 3, 5));
    QVERIFY(!hasInternalGotoLinkToPage(combinedPart.m_document, 3, 2));

    Okular::Part asIsPart(nullptr, {});
    QVERIFY(openDocument(&asIsPart, asIsFile));
    QCOMPARE(asIsPart.m_document->pages(), 6u);
    asIsPart.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(asIsPart.widget()));

    const DocumentViewport asIsDestination(asIsPart.m_document->metaData(QStringLiteral("NamedViewport"), QStringLiteral("section.1")).toString());
    const DocumentViewport unexpectedSuffix(asIsPart.m_document->metaData(QStringLiteral("NamedViewport"), QStringLiteral("section.1~2")).toString());
    QVERIFY(asIsDestination.isValid());
    QCOMPARE(asIsDestination.pageNumber, 0);
    QVERIFY(!unexpectedSuffix.isValid());

    asIsPart.m_document->setViewportPage(3);
    QTRY_VERIFY(asIsPart.m_document->page(3)->hasPixmap(asIsPart.m_pageView));
    asIsPart.m_document->requestTextPage(3);
    QTRY_VERIFY(asIsPart.m_document->page(3)->hasTextPage());
    QVERIFY(hasInternalGotoLinkToPage(asIsPart.m_document, 3, 2));
    QVERIFY(!hasInternalGotoLinkToPage(asIsPart.m_document, 3, 5));
}

void PartTest::testOpenUrlArguments()
{
    Okular::Part part(nullptr, {});

    KParts::OpenUrlArguments args;
    args.setMimeType(QStringLiteral("text/rtf"));

    part.setArguments(args);

    part.openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")));

    QCOMPARE(part.arguments().mimeType(), QStringLiteral("text/rtf"));
}

void PartTest::test388288()
{
    Okular::Part part(nullptr, {});

    part.openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")));
    new QAbstractItemModelTester(part.annotationsModel(), &part);

    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotSetMouseNormal"));

    auto annot = new Okular::HighlightAnnotation();
    annot->setHighlightType(Okular::HighlightAnnotation::Highlight);
    const Okular::NormalizedRect r(0.36, 0.16, 0.51, 0.17);
    annot->setBoundingRectangle(r);
    Okular::HighlightAnnotation::Quad q;
    q.setCapStart(false);
    q.setCapEnd(false);
    q.setFeather(1.0);
    q.setPoint(Okular::NormalizedPoint(r.left, r.bottom), 0);
    q.setPoint(Okular::NormalizedPoint(r.right, r.bottom), 1);
    q.setPoint(Okular::NormalizedPoint(r.right, r.top), 2);
    q.setPoint(Okular::NormalizedPoint(r.left, r.top), 3);
    annot->highlightQuads().append(q);

    part.m_document->addPageAnnotation(0, annot);

    const int width = part.m_pageView->horizontalScrollBar()->maximum() + part.m_pageView->viewport()->width();
    const int height = part.m_pageView->verticalScrollBar()->maximum() + part.m_pageView->viewport()->height();

    QTest::mouseMove(part.m_pageView->viewport(), QPoint(width * 0.5, height * 0.5));
    QTRY_COMPARE(part.m_pageView->cursor().shape(), Qt::OpenHandCursor);

    QTest::mouseMove(part.m_pageView->viewport(), QPoint(width * 0.4, height * 0.165));
    QTRY_COMPARE(part.m_pageView->cursor().shape(), Qt::ArrowCursor);

    QTest::mouseMove(part.m_pageView->viewport(), QPoint(width * 0.1, height * 0.165));

    part.m_document->undo();

    annot = new Okular::HighlightAnnotation();
    annot->setHighlightType(Okular::HighlightAnnotation::Highlight);
    annot->setBoundingRectangle(r);
    annot->highlightQuads().append(q);

    part.m_document->addPageAnnotation(0, annot);

    QTest::mouseMove(part.m_pageView->viewport(), QPoint(width * 0.5, height * 0.5));
    QTRY_COMPARE(part.m_pageView->cursor().shape(), Qt::OpenHandCursor);
}

void PartTest::testCheckBoxReadOnly()
{
    const QString testFile = QStringLiteral(KDESRCDIR "data/checkbox_ro.pdf");
    Okular::Part part(nullptr, {});
    part.openDocument(testFile);

    // The test document uses the activation action of checkboxes
    // to update the read only state. For this we need the part so that
    // undo / redo activates the activation action.

    QVERIFY(part.m_document->isOpened());

    const Okular::Page *page = part.m_document->page(0);

    QMap<QString, Okular::FormField *> fields;

    // Field names in test document are:
    // CBMakeRW, CBMakeRO, TargetDefaultRO, TargetDefaultRW

    const QList<Okular::FormField *> pageFormFields = page->formFields();
    for (Okular::FormField *ff : pageFormFields) {
        fields.insert(ff->name(), static_cast<Okular::FormField *>(ff));
    }

    // First grab all fields and check that the setup is as expected.
    auto cbMakeRW = dynamic_cast<Okular::FormFieldButton *>(fields[QStringLiteral("CBMakeRW")]);
    auto cbMakeRO = dynamic_cast<Okular::FormFieldButton *>(fields[QStringLiteral("CBMakeRO")]);

    auto targetDefaultRW = dynamic_cast<Okular::FormFieldText *>(fields[QStringLiteral("TargetDefaultRw")]);
    auto targetDefaultRO = dynamic_cast<Okular::FormFieldText *>(fields[QStringLiteral("TargetDefaultRo")]);

    QVERIFY(cbMakeRW);
    QVERIFY(cbMakeRO);
    QVERIFY(targetDefaultRW);
    QVERIFY(targetDefaultRO);

    QVERIFY(!cbMakeRW->state());
    QVERIFY(!cbMakeRO->state());

    QVERIFY(!targetDefaultRW->isReadOnly());
    QVERIFY(targetDefaultRO->isReadOnly());

    QList<Okular::FormFieldButton *> btns;
    btns << cbMakeRW << cbMakeRO;

    // Now check both boxes
    QList<bool> btnStates;
    btnStates << true << true;

    part.m_document->editFormButtons(0, btns, btnStates);

    // Read only should be inverted
    QVERIFY(targetDefaultRW->isReadOnly());
    QVERIFY(!targetDefaultRO->isReadOnly());

    // Test that undo / redo works
    QVERIFY(part.m_document->canUndo());
    part.m_document->undo();
    QVERIFY(!targetDefaultRW->isReadOnly());
    QVERIFY(targetDefaultRO->isReadOnly());

    part.m_document->redo();
    QVERIFY(targetDefaultRW->isReadOnly());
    QVERIFY(!targetDefaultRO->isReadOnly());

    btnStates.clear();
    btnStates << false << true;

    part.m_document->editFormButtons(0, btns, btnStates);
    QVERIFY(targetDefaultRW->isReadOnly());
    QVERIFY(targetDefaultRO->isReadOnly());

    // Now set both to checked again and confirm that
    // save / load works.
    btnStates.clear();
    btnStates << true << true;
    part.m_document->editFormButtons(0, btns, btnStates);

    QTemporaryFile saveFile(QStringLiteral("%1/okrXXXXXX.pdf").arg(QDir::tempPath()));
    QVERIFY(saveFile.open());
    saveFile.close();

    // Save
    QVERIFY(part.saveAs(QUrl::fromLocalFile(saveFile.fileName()), Part::NoSaveAsFlags));
    part.closeUrl();

    // Load
    part.openDocument(saveFile.fileName());
    QVERIFY(part.m_document->isOpened());

    page = part.m_document->page(0);

    fields.clear();

    {
        const QList<Okular::FormField *> pageFormFields = page->formFields();
        for (Okular::FormField *ff : pageFormFields) {
            fields.insert(ff->name(), static_cast<Okular::FormField *>(ff));
        }
    }

    cbMakeRW = dynamic_cast<Okular::FormFieldButton *>(fields[QStringLiteral("CBMakeRW")]);
    cbMakeRO = dynamic_cast<Okular::FormFieldButton *>(fields[QStringLiteral("CBMakeRO")]);

    targetDefaultRW = dynamic_cast<Okular::FormFieldText *>(fields[QStringLiteral("TargetDefaultRw")]);
    targetDefaultRO = dynamic_cast<Okular::FormFieldText *>(fields[QStringLiteral("TargetDefaultRo")]);

    QVERIFY(cbMakeRW->state());
    QVERIFY(cbMakeRO->state());
    QVERIFY(targetDefaultRW->isReadOnly());
    QVERIFY(!targetDefaultRO->isReadOnly());
}

void PartTest::testCrashTextEditDestroy()
{
    const QString testFile = QStringLiteral(KDESRCDIR "data/formSamples.pdf");
    Okular::Part part(nullptr, {});
    part.openDocument(testFile);
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    part.widget()->findChild<QTextEdit *>()->setText(QStringLiteral("HOLA"));
    part.actionCollection()->action(QStringLiteral("view_toggle_forms"))->trigger();
}

void PartTest::testAnnotWindow()
{
    Okular::Part part(nullptr, {});
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/file1.pdf")));
    part.widget()->show();
    part.widget()->resize(800, 600);
    new QAbstractItemModelTester(part.annotationsModel(), &part);
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    part.m_document->setViewportPage(0);

    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotSetMouseNormal"));

    QCOMPARE(part.m_document->currentPage(), 0u);
    const int initialAnnotationCount = part.m_document->page(0)->annotations().size();

    // Create two distinct text annotations
    Okular::Annotation *annot1 = new Okular::TextAnnotation();
    annot1->setBoundingRectangle(Okular::NormalizedRect(0.8, 0.1, 0.85, 0.15));
    annot1->setContents(QStringLiteral("Annot contents 111111"));

    Okular::Annotation *annot2 = new Okular::TextAnnotation();
    annot2->setBoundingRectangle(Okular::NormalizedRect(0.8, 0.3, 0.85, 0.35));
    annot2->setContents(QStringLiteral("Annot contents 222222"));
    annot2->style().setColor(QColor(32, 32, 32));

    // Add annot1 and annot2 to document
    part.m_document->addPageAnnotation(0, annot1);
    part.m_document->addPageAnnotation(0, annot2);
    QCOMPARE(part.m_document->page(0)->annotations().size(), initialAnnotationCount + 2);

    QTimer *delayResizeEventTimer = part.m_pageView->findChildren<QTimer *>(QStringLiteral("delayResizeEventTimer")).at(0);
    QVERIFY(delayResizeEventTimer->isActive());
    QTest::qWait(delayResizeEventTimer->interval() * 2);

    // wait for pixmap
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));

    const int width = part.m_pageView->horizontalScrollBar()->maximum() + part.m_pageView->viewport()->width();
    const int height = part.m_pageView->verticalScrollBar()->maximum() + part.m_pageView->viewport()->height();

    // Double click the first annotation to open its window (move mouse for visual feedback)
    const NormalizedPoint annot1pt = annot1->boundingRectangle().center();
    QTest::mouseMove(part.m_pageView->viewport(), QPoint(width * annot1pt.x, height * annot1pt.y));
    QTest::mouseDClick(part.m_pageView->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(width * annot1pt.x, height * annot1pt.y));
    QTRY_COMPARE(part.m_pageView->findChildren<QFrame *>(QStringLiteral("AnnotWindow")).size(), 1);
    // Verify that the window is visible
    QFrame *win1 = part.m_pageView->findChild<QFrame *>(QStringLiteral("AnnotWindow"));
    QVERIFY(!win1->visibleRegion().isEmpty());

    // Double click the second annotation to open its window (move mouse for visual feedback)
    const NormalizedPoint annot2pt = annot2->boundingRectangle().center();
    QTest::mouseMove(part.m_pageView->viewport(), QPoint(width * annot2pt.x, height * annot2pt.y));
    QTest::mouseDClick(part.m_pageView->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(width * annot2pt.x, height * annot2pt.y));
    QTRY_COMPARE(part.m_pageView->findChildren<QFrame *>(QStringLiteral("AnnotWindow")).size(), 2);
    // Verify that the first window is hidden covered by the second, which is visible
    QList<QFrame *> lstWin = part.m_pageView->findChildren<QFrame *>(QStringLiteral("AnnotWindow"));
    QFrame *win2;
    if (lstWin[0] == win1) {
        win2 = lstWin[1];
    } else {
        win2 = lstWin[0];
    }
    QVERIFY(win1->visibleRegion().isEmpty());
    QVERIFY(!win2->visibleRegion().isEmpty());

    // Double click the first annotation to raise its window (move mouse for visual feedback)
    QTest::mouseMove(part.m_pageView->viewport(), QPoint(width * annot1pt.x, height * annot1pt.y));
    QTest::mouseDClick(part.m_pageView->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(width * annot1pt.x, height * annot1pt.y));
    // Verify that the second window is hidden covered by the first, which is visible
    QVERIFY(!win1->visibleRegion().isEmpty());
    QVERIFY(win2->visibleRegion().isEmpty());

    // Move annotation window 1 to partially show annotation window 2
    win1->move(QPoint(win2->pos().x(), win2->pos().y() + 50));
    // Verify that both windows are partially visible
    QVERIFY(!win1->visibleRegion().isEmpty());
    QVERIFY(!win2->visibleRegion().isEmpty());

    // Click the second annotation window to raise it (move mouse for visual feedback)
    auto widget = win2->window()->childAt(win2->mapTo(win2->window(), QPoint(10, 10)));
    QTest::mouseMove(win2->window(), win2->mapTo(win2->window(), QPoint(10, 10)));
    QTest::mouseClick(widget, Qt::LeftButton, Qt::NoModifier, widget->mapFrom(win2, QPoint(10, 10)));
    QCOMPARE(win1->visibleRegion().boundingRect().size().width(), 300);
    QCOMPARE(win1->visibleRegion().boundingRect().size().height(), 50);
    QCOMPARE(win2->visibleRegion().boundingRect().size().width(), 300);
    QCOMPARE(win2->visibleRegion().boundingRect().size().height(), 300);

    // Resizing must survive the resulting resize event instead of snapping back
    // to the default annotation window size.
    win2->resize(350, 350);
    QTRY_COMPARE(win2->size(), QSize(350, 350));

    auto *textEdit = win2->findChild<QTextEdit *>();
    QVERIFY(textEdit);
    QCOMPARE(textEdit->cursorWidth(), 3);
    const QColor background = textEdit->palette().color(QPalette::Base);
    const QColor foreground = textEdit->palette().color(QPalette::Text);
    QVERIFY(qAbs(qGray(background.rgb()) - qGray(foreground.rgb())) >= 100);

    textEdit->setFocus();
    QTRY_VERIFY(textEdit->hasFocus());
    const int oldCursorFlashTime = QApplication::cursorFlashTime();
    QApplication::setCursorFlashTime(1000);
    QTest::keyClick(textEdit, Qt::Key_End);
    QTest::qWait(50);
    const QImage caretOnFrame = textEdit->grab().toImage();
    QTest::qWait(550);
    const QImage caretOffFrame = textEdit->grab().toImage();
    QApplication::setCursorFlashTime(oldCursorFlashTime);
    QCOMPARE(caretOnFrame, caretOffFrame);

    const QRect caretRect = textEdit->cursorRect();
    QVERIFY(caretRect.isValid());
    const QImage caretPixels = textEdit->viewport()->grab(caretRect).toImage();
    bool hasVisibleCaretPixel = false;
    for (int y = 0; y < caretPixels.height() && !hasVisibleCaretPixel; ++y) {
        for (int x = 0; x < caretPixels.width(); ++x) {
            if (qAbs(qGray(caretPixels.pixel(x, y)) - qGray(background.rgb())) >= 100) {
                hasVisibleCaretPixel = true;
                break;
            }
        }
    }
    QVERIFY(hasVisibleCaretPixel);

    const QList<QFrame *> existingWindows = part.m_pageView->findChildren<QFrame *>(QStringLiteral("AnnotWindow"));
    auto *latexAnnot = new Okular::StampAnnotation();
    latexAnnot->setBoundingRectangle(Okular::NormalizedRect(0.6, 0.5, 0.75, 0.6));
    latexAnnot->setContents(QStringLiteral("\\LaTeX{}"));
    latexAnnot->setOkularLatex(true);
    part.m_document->addPageAnnotation(0, latexAnnot);
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "openAnnotationWindow", Qt::DirectConnection, Q_ARG(Okular::Annotation *, latexAnnot), Q_ARG(int, 0)));

    QTRY_COMPARE(part.m_pageView->findChildren<QFrame *>(QStringLiteral("AnnotWindow")).size(), existingWindows.size() + 1);
    QFrame *latexWindow = nullptr;
    for (QFrame *window : part.m_pageView->findChildren<QFrame *>(QStringLiteral("AnnotWindow"))) {
        if (!existingWindows.contains(window)) {
            latexWindow = window;
            break;
        }
    }
    QVERIFY(latexWindow);

    QWidget *latexEditor = nullptr;
    for (QWidget *widget : latexWindow->findChildren<QWidget *>()) {
        if (QLatin1String(widget->metaObject()->className()) == QLatin1String("QsciScintilla")) {
            latexEditor = widget;
            break;
        }
    }
    if (latexEditor) {
        latexEditor->setFocus();
        QTRY_VERIFY(latexEditor->hasFocus());
        QTest::keyClick(latexEditor, Qt::Key_End);
        QTest::qWait(50);
        const QImage latexCaretFirstFrame = latexEditor->grab().toImage();
        QTest::qWait(550);
        const QImage latexCaretSecondFrame = latexEditor->grab().toImage();
        QCOMPARE(latexCaretFirstFrame, latexCaretSecondFrame);
    }
    QSignalSpy latexWindowDestroyed(latexWindow, &QObject::destroyed);
    latexWindow->close();
    QTRY_COMPARE(latexWindowDestroyed.count(), 1);
}

void PartTest::testOcrTextLayout()
{
    QFont font = OcrTextLayout::font();
    font.setPixelSize(1000);
    QFontMetricsF metrics(font);
    QList<OcrTextWord> words;
    const double size = 0.022;
    const double baseline = 0.18;
    const double aspect = 0.707;
    double x = 0.10;
    for (const QString &text : QStringLiteral("This is some random text with different letter heights").split(QLatin1Char(' '))) {
        const QRectF glyph = metrics.tightBoundingRect(text);
        words.append({text, QRectF(x + glyph.left() * size / 1000 / aspect, baseline + glyph.top() * size / 1000, glyph.width() * size / 1000 / aspect, glyph.height() * size / 1000)});
        x += metrics.horizontalAdvance(text + QLatin1Char(' ')) * size / 1000 / aspect;
    }
    QVERIFY(words[2].rectangle.height() < words.last().rectangle.height());
    auto placement = OcrTextLayout::arrange(words);
    QCOMPARE(placement.size(), words.size());
    for (const auto &word : placement) {
        QVERIFY(qAbs(word.fontSize - size) < 0.000001);
        QVERIFY(qAbs(word.baseline - baseline) < 0.000001);
    }
    // Box widths must not squeeze/stretch the glyphs or change a line's size.
    auto narrow = words;
    narrow[2].rectangle.setWidth(narrow[2].rectangle.width() / 3);
    const auto narrowed = OcrTextLayout::arrange(narrow);
    QCOMPARE(narrowed[2].fontSize, placement[2].fontSize);
    QCOMPARE(narrowed[2].baseline, placement[2].baseline);
    // Nearby rows remain separate; neither row inherits the other's baseline.
    for (const auto &word : std::as_const(narrow)) {
        words.append({word.text, word.rectangle.translated(0, 0.035)});
    }
    placement = OcrTextLayout::arrange(words);
    QVERIFY(qAbs(placement.last().baseline - baseline - 0.035) < 0.000001);
    QVERIFY(qAbs(placement.first().baseline - baseline) < 0.000001);
    const QString output = qEnvironmentVariable("MENGSHEE_OCR_TEST_OUTPUT");
    if (!output.isEmpty()) {
        std::vector<PdfPageSequenceEditor::OcrWord> nativeWords;
        for (const auto &word : std::as_const(words)) {
            nativeWords.push_back({word.text.toStdString(), word.rectangle.left(), word.rectangle.top(), word.rectangle.right(), word.rectangle.bottom()});
        }
        const QString preview = output + QStringLiteral("-natural.pdf");
        const auto result = PdfPageSequenceEditor::addOcrTextLayers(std::string(KDESRCDIR "data/file1.pdf"), preview.toStdString(), {{1, nativeWords}});
        QVERIFY2(result.ok(), result.message.c_str());
        Part part(nullptr, {});
        QVERIFY(openDocument(&part, preview));
        part.widget()->resize(1100, 800);
        part.widget()->show();
        QVERIFY(QTest::qWaitForWindowExposed(part.widget()));
        part.setEditingMode(EditingMode::Ocr);
        part.actionCollection()->action(QStringLiteral("advanced_edit_ocr_text"))->trigger();
        QVERIFY(part.m_pageView->isOcrTextEditing());
        QVERIFY(part.m_pageView->viewport()->grab().save(output + QStringLiteral("-natural.png")));
    }
}

void PartTest::testOcrPdfGeometry()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = directory.filePath(QStringLiteral("first.pdf"));
    const QString replaced = directory.filePath(QStringLiteral("replaced.pdf"));
    const std::string input = std::string(KDESRCDIR "data/file1.pdf");
    const auto firstResult = PdfPageSequenceEditor::addOcrTextLayers(
        input, first.toStdString(), {{1, {{"oldlayerprobe", 0.1, 0.2, 0.3, 0.24, 0.1, 0.23, 0.3, 0.23}}}});
    QVERIFY2(firstResult.ok(), firstResult.message.c_str());
    const auto replaceResult = PdfPageSequenceEditor::addOcrTextLayers(
        first.toStdString(), replaced.toStdString(), {{1, {{"newlayerprobe", 0.2, 0.6, 0.4, 0.64, 0.2, 0.63, 0.4, 0.63}}}});
    QVERIFY2(replaceResult.ok(), replaceResult.message.c_str());

    auto document = Poppler::Document::load(replaced);
    QVERIFY(document);
    std::unique_ptr<Poppler::Page> page = document->page(0);
    QVERIFY(page);
    const auto boxes = page->textList();
    const Poppler::TextBox *probe = nullptr;
    for (const auto &box : boxes) {
        QVERIFY(box->text() != QStringLiteral("oldlayerprobe"));
        if (box->text() == QStringLiteral("newlayerprobe")) {
            probe = box.get();
        }
    }
    QVERIFY(probe);
    const QRectF box = probe->boundingBox();
    const QSizeF size = page->pageSizeF();
    QVERIFY(qAbs(box.left() / size.width() - 0.2) < 0.003);
    QVERIFY(qAbs(box.top() / size.height() - 0.6) < 0.003);
    QVERIFY(box.height() / size.height() < 0.06);

    const QString realSource = qEnvironmentVariable("MENGSHEE_OCR_SOURCE");
    if (!realSource.isEmpty()) {
        Part realPart(nullptr, {});
        QVERIFY(openDocument(&realPart, realSource));
        const QString realOutput = qEnvironmentVariable("MENGSHEE_OCR_OUTPUT");
        QVERIFY(!realOutput.isEmpty());
        const OcrResult result = realPart.m_document->saveWithEnglishOcr(realSource, realOutput, {1}, false, [](int, int, int) { return true; });
        QVERIFY2(result.success, qPrintable(result.errorText));
        QCOMPARE(result.recognizedPages, 1);
        QVERIFY(result.recognizedWords > 0);
    }

    Part part(nullptr, {});
    QVERIFY(openDocument(&part, replaced));
    QList<OcrTextWord> words;
    QString error;
    QVERIFY2(part.m_document->readOcrTextLayer(0, &words, &error), qPrintable(error));
    QCOMPARE(words.size(), 1);
    QCOMPARE(words[0].text, QStringLiteral("newlayerprobe"));
}

void PartTest::testOcrTextLayerEditing()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString input = QStringLiteral(KDESRCDIR "data/file1.pdf");
    const QString recognized = directory.filePath(QStringLiteral("ocr.pdf"));
    const auto result = PdfPageSequenceEditor::addOcrTextLayers(input.toStdString(), recognized.toStdString(), {{1, {{"WRONG", 0.15, 0.2, 0.35, 0.25}, {"a(b)\\c", 0.4, 0.2, 0.6, 0.25}}}});
    QVERIFY2(result.ok(), result.message.c_str());
    Part part(nullptr, {});
    QVERIFY(openDocument(&part, recognized));
    QList<OcrTextWord> before;
    QString error;
    QVERIFY2(part.m_document->readOcrTextLayer(0, &before, &error), qPrintable(error));
    QCOMPARE(before.size(), 2);
    QCOMPARE(before[0].text, QStringLiteral("WRONG"));
    QCOMPARE(before[1].text, QStringLiteral("a(b)\\c"));
    QVERIFY(qAbs(before[1].rectangle.width() - 0.2) < 0.00001);
    part.m_document->requestTextPage(0);
    QVERIFY(part.m_document->page(0)->text(nullptr).contains(QStringLiteral("WRONG")));
    QAction *recognize = part.actionCollection()->action(QStringLiteral("advanced_recognize_english_text"));
    QAction *edit = part.actionCollection()->action(QStringLiteral("advanced_edit_ocr_text"));
    QVERIFY(recognize && edit);
    QVERIFY(!recognize->isVisible());
    QVERIFY(!edit->isVisible());
    part.setEditingMode(EditingMode::Ocr);
    QVERIFY(!part.m_pageView->namedDestinationsVisible());
    QVERIFY(!part.m_pageView->isOcrTextEditing()); // mode selection does not start a tool
    QVERIFY(recognize->isVisible());
    QVERIFY(recognize->isEnabled());
    QVERIFY(edit && edit->isEnabled() && edit->isVisible());
    part.widget()->resize(900, 700);
    part.widget()->show();
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));
    edit->trigger();
    QVERIFY(part.m_pageView->isOcrTextEditing());
    QVERIFY(edit->isChecked());
    auto *viewport = part.m_pageView->viewport();
    QPoint wordPosition(-1, -1);
    QTRY_VERIFY(viewport->width() > 100);
    for (int y = 0; y < viewport->height() && wordPosition.x() < 0; y += 3) {
        for (int x = 0; x < viewport->width(); x += 3) {
            int page = -1;
            NormalizedPoint point;
            if (part.m_pageView->mapGlobalPosToPagePoint(viewport->mapToGlobal(QPoint(x, y)), &page, &point)
                && page == 0 && point.x > 0.20 && point.x < 0.30 && point.y > 0.21 && point.y < 0.24) {
                wordPosition = QPoint(x, y);
                break;
            }
        }
    }
    QVERIFY(wordPosition.x() >= 0);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, wordPosition);
    auto *editor = viewport->findChild<QLineEdit *>(QStringLiteral("ocrInlineEditor"));
    QVERIFY(editor && editor->isVisible());
    QCOMPARE(editor->text(), QStringLiteral("WRONG"));
    QTest::keyClicks(editor, QStringLiteral("CORRECTED"));
    QTest::keyClick(editor, Qt::Key_Return);
    QVERIFY(part.m_document->page(0)->text(nullptr).contains(QStringLiteral("CORRECTED")));
    QVERIFY(!part.m_document->page(0)->text(nullptr).contains(QStringLiteral("WRONG")));
    part.m_document->undo();
    QVERIFY(part.m_document->page(0)->text(nullptr).contains(QStringLiteral("WRONG")));
    part.m_document->redo();
    QVERIFY(part.m_document->page(0)->text(nullptr).contains(QStringLiteral("CORRECTED")));
    QPoint bottomRight(-1, -1);
    for (int y = 0; y < viewport->height() && bottomRight.x() < 0; ++y) {
        for (int x = 0; x < viewport->width(); ++x) {
            int page = -1;
            NormalizedPoint point;
            if (part.m_pageView->mapGlobalPosToPagePoint(viewport->mapToGlobal(QPoint(x, y)), &page, &point)
                && page == 0 && qAbs(point.x - 0.35) < 0.0015 && qAbs(point.y - 0.25) < 0.0015) {
                bottomRight = QPoint(x, y);
                break;
            }
        }
    }
    QVERIFY(bottomRight.x() >= 0);
    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, bottomRight);
    QTest::mouseMove(viewport, bottomRight + QPoint(30, 20));
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, bottomRight + QPoint(30, 20));
    QList<OcrTextWord> resized;
    QTRY_VERIFY(part.m_document->readOcrTextLayer(0, &resized, &error));
    QTRY_VERIFY(resized[0].rectangle.right() > 0.35 && resized[0].rectangle.bottom() > 0.25);
    part.m_document->undo();
    QVERIFY(part.m_document->readOcrTextLayer(0, &resized, &error));
    QVERIFY(qAbs(resized[0].rectangle.right() - 0.35) < 0.00001);
    part.m_document->redo();
    QVERIFY(part.m_document->readOcrTextLayer(0, &resized, &error));
    QVERIFY(resized[0].rectangle.right() > 0.35);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, wordPosition);
    editor = viewport->findChild<QLineEdit *>(QStringLiteral("ocrInlineEditor"));
    QVERIFY(editor && editor->isVisible());
    QTest::keyClicks(editor, QStringLiteral("DISCARDED"));
    QTest::keyClick(editor, Qt::Key_Escape);
    QVERIFY(part.m_document->page(0)->text(nullptr).contains(QStringLiteral("CORRECTED")));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, wordPosition);
    editor = viewport->findChild<QLineEdit *>(QStringLiteral("ocrInlineEditor"));
    QVERIFY(editor && editor->isVisible());
    QTest::keyClick(editor, Qt::Key_Backspace);
    QTest::keyClick(editor, Qt::Key_Return);
    QVERIFY(!part.m_document->page(0)->text(nullptr).contains(QStringLiteral("CORRECTED")));
    part.m_document->undo();
    QVERIFY(part.m_document->page(0)->text(nullptr).contains(QStringLiteral("CORRECTED")));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTest::mouseDClick(viewport, Qt::LeftButton, Qt::NoModifier, wordPosition + QPoint(0, 80));
    editor = viewport->findChild<QLineEdit *>(QStringLiteral("ocrInlineEditor"));
    QVERIFY(editor && editor->isVisible());
    QVERIFY(editor->text().isEmpty());
    QTest::keyClicks(editor, QStringLiteral("ADDED"));
    QTest::keyClick(editor, Qt::Key_Return);
    QVERIFY(part.m_document->page(0)->text(nullptr).contains(QStringLiteral("ADDED")));
    part.m_document->undo();
    QVERIFY(!part.m_document->page(0)->text(nullptr).contains(QStringLiteral("ADDED")));
    if (!qEnvironmentVariable("MENGSHEE_OCR_TEST_OUTPUT").isEmpty()) {
        QVERIFY(viewport->grab().save(qEnvironmentVariable("MENGSHEE_OCR_TEST_OUTPUT") + QStringLiteral(".png")));
    }
    edit->trigger();
    QVERIFY(!part.m_pageView->isOcrTextEditing());
    QVERIFY(!edit->isChecked());
    const QString saved = directory.filePath(QStringLiteral("saved.pdf"));
    QVERIFY2(part.m_document->saveChanges(saved, &error), qPrintable(error));
    auto originalPdf = Poppler::Document::load(recognized);
    auto editedPdf = Poppler::Document::load(saved);
    QVERIFY(originalPdf && editedPdf);
    QCOMPARE(originalPdf->page(0)->renderToImage(100, 100), editedPdf->page(0)->renderToImage(100, 100));
    if (!qEnvironmentVariable("MENGSHEE_OCR_TEST_OUTPUT").isEmpty()) {
        QVERIFY(QFile::copy(recognized, qEnvironmentVariable("MENGSHEE_OCR_TEST_OUTPUT")));
    }
    Part reopened(nullptr, {});
    QVERIFY(openDocument(&reopened, saved));
    QList<OcrTextWord> after;
    QVERIFY2(reopened.m_document->readOcrTextLayer(0, &after, &error), qPrintable(error));
    QCOMPARE(after[0].text, QStringLiteral("CORRECTED"));
    QVERIFY(qAbs(after[0].rectangle.left() - 0.15) < 0.00001);
    for (int rotation : {90, 180, 270, 0}) {
        QVERIFY2(reopened.m_document->rotatePage(0, rotation, &error), qPrintable(error));
        QVERIFY2(reopened.m_document->readOcrTextLayer(0, &after, &error), qPrintable(error));
        QVERIFY2(reopened.m_document->replaceOcrTextLayer(0, after, &error), qPrintable(error));
        QList<OcrTextWord> again;
        QVERIFY(reopened.m_document->readOcrTextLayer(0, &again, &error));
        QCOMPARE(again.size(), after.size());
        QVERIFY(qAbs(again[0].rectangle.x() - after[0].rectangle.x()) < 0.00001);
        QVERIFY(qAbs(again[0].rectangle.y() - after[0].rectangle.y()) < 0.00001);
    }
    QVERIFY(reopened.m_document->replaceOcrTextLayer(0, {}, &error));
    QVERIFY(reopened.m_document->readOcrTextLayer(0, &after, &error));
    QVERIFY(after.isEmpty());
    QVERIFY(reopened.m_document->replaceOcrTextLayer(0, before, &error));
    QVERIFY(reopened.m_document->readOcrTextLayer(0, &after, &error));
    QCOMPARE(after.size(), before.size());
    QList<OcrTextWord> invalid = before;
    invalid[0].rectangle.setWidth(-1);
    QVERIFY(!reopened.m_document->replaceOcrTextLayer(0, invalid, &error));
    QVERIFY(reopened.m_document->readOcrTextLayer(0, &after, &error));
    QCOMPARE(after.size(), before.size());
}

void PartTest::testAnnotWindowInTextSelectionMode()
{
    Okular::Part part(nullptr, {});
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/file1.pdf")));
    part.widget()->resize(800, 600);
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some GUI tests");
    }
    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    part.m_document->setViewportPage(0);
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));
    part.m_document->requestTextPage(0);
    QTRY_VERIFY(part.m_document->page(0)->hasTextPage());

    const Okular::TextEntity::List words = part.m_document->page(0)->words(nullptr, Okular::TextPage::AnyPixelTextAreaInclusionBehaviour);
    QVERIFY(!words.isEmpty());
    const Okular::NormalizedRect wordRect = words.constFirst().area();
    const Okular::NormalizedPoint wordCenter = wordRect.center();
    QVERIFY(part.m_document->page(0)->wordAt(wordCenter));

    const Okular::NormalizedRect annotationRect(qMax(0.0, wordRect.left - 0.03),
                                                 qMax(0.0, wordRect.top - 0.03),
                                                 qMin(1.0, wordRect.right + 0.03),
                                                 qMin(1.0, wordRect.bottom + 0.03));

    auto *annotation = new Okular::TextAnnotation();
    annotation->setTextType(Okular::TextAnnotation::InPlace);
    annotation->setBoundingRectangle(annotationRect);
    annotation->setContents(QStringLiteral("Annotation over selectable text"));
    part.m_document->addPageAnnotation(0, annotation);

    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotSetMouseTextSelect"));
    const int width = part.m_pageView->horizontalScrollBar()->maximum() + part.m_pageView->viewport()->width();
    const int height = part.m_pageView->verticalScrollBar()->maximum() + part.m_pageView->viewport()->height();
    const auto viewportPoint = [width, height](const Okular::NormalizedPoint &point) {
        return QPoint(qRound(width * point.x), qRound(height * point.y));
    };
    const QPoint annotationPosition = viewportPoint(wordCenter);

    QTest::mouseMove(part.m_pageView->viewport(), annotationPosition);
    QTest::mouseDClick(part.m_pageView->viewport(), Qt::LeftButton, Qt::NoModifier, annotationPosition);
    QTRY_COMPARE(part.m_pageView->findChildren<QFrame *>(QStringLiteral("AnnotWindow")).size(), 1);
    QVERIFY(!part.m_document->page(0)->textSelection());

    part.m_pageView->findChild<QFrame *>(QStringLiteral("AnnotWindow"))->close();
    QTRY_COMPARE(part.m_pageView->findChildren<QFrame *>(QStringLiteral("AnnotWindow")).size(), 0);

    QTest::mouseClick(part.m_pageView->viewport(), Qt::LeftButton, Qt::NoModifier, annotationPosition);

    const Okular::NormalizedRect beforeMove = annotation->boundingRectangle();
    QTest::mousePress(part.m_pageView->viewport(), Qt::LeftButton, Qt::NoModifier, annotationPosition);
    QTest::mouseMove(part.m_pageView->viewport(), annotationPosition + QPoint(30, 24));
    QTest::mouseRelease(part.m_pageView->viewport(), Qt::LeftButton, Qt::NoModifier, annotationPosition + QPoint(30, 24));
    QTRY_VERIFY(annotation->boundingRectangle().left > beforeMove.left && annotation->boundingRectangle().top > beforeMove.top);
    QVERIFY(!part.m_document->page(0)->textSelection());
}

void PartTest::testAnnotWindowAppearance()
{
    Okular::Settings::setAnnotationPopupTextFontSize(15);

    Okular::Part part(nullptr, {});
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/file1.pdf")));

    auto *annotation = new Okular::TextAnnotation();
    annotation->setBoundingRectangle(Okular::NormalizedRect(0.1, 0.1, 0.2, 0.2));
    annotation->setContents(QStringLiteral("Readable popup contents"));
    annotation->style().setColor(QColor(Qt::red));
    part.m_document->addPageAnnotation(0, annotation);

    const QList<QFrame *> existingWindows = part.m_pageView->findChildren<QFrame *>(QStringLiteral("AnnotWindow"));
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "openAnnotationWindow", Qt::DirectConnection, Q_ARG(Okular::Annotation *, annotation), Q_ARG(int, 0)));
    QTRY_COMPARE(part.m_pageView->findChildren<QFrame *>(QStringLiteral("AnnotWindow")).size(), existingWindows.size() + 1);

    QFrame *window = nullptr;
    for (QFrame *candidate : part.m_pageView->findChildren<QFrame *>(QStringLiteral("AnnotWindow"))) {
        if (!existingWindows.contains(candidate)) {
            window = candidate;
            break;
        }
    }
    QVERIFY(window);

    auto *textEdit = window->findChild<QTextEdit *>();
    QVERIFY(textEdit);

    const QColor background = textEdit->palette().color(QPalette::Base);
    QCOMPARE(textEdit->palette().color(QPalette::Text), QColor(Qt::black));
    QCOMPARE(window->palette().color(QPalette::WindowText), QColor(Qt::black));
    QCOMPARE(background.hslHue(), QColor(Qt::red).hslHue());
    QCOMPARE(background.hslSaturation(), QColor(Qt::red).hslSaturation());
    QVERIFY(background.lightnessF() >= 0.90);
    QCOMPARE(textEdit->font().pointSize(), 15);

    Okular::Settings::setAnnotationPopupTextFontSize(18);
    Okular::Settings::self()->save();
    QTRY_COMPARE(textEdit->font().pointSize(), 18);

    QSignalSpy windowDestroyed(window, &QObject::destroyed);
    window->close();
    QTRY_COMPARE(windowDestroyed.count(), 1);
}

// Helper for testAdditionalActionTriggers
static void verifyTargetStates(const QString &triggerName, const QMap<QString, Okular::FormField *> &fields, bool focusVisible, bool cursorVisible, bool mouseVisible, int line)
{
    Okular::FormField *focusTarget = fields.value(triggerName + QStringLiteral("_focus_target"));
    Okular::FormField *cursorTarget = fields.value(triggerName + QStringLiteral("_cursor_target"));
    Okular::FormField *mouseTarget = fields.value(triggerName + QStringLiteral("_mouse_target"));

    QVERIFY(focusTarget);
    QVERIFY(cursorTarget);
    QVERIFY(mouseTarget);

    QTRY_VERIFY2(focusTarget->isVisible() == focusVisible, QStringLiteral("line: %1 focus for %2 not matched. Expected %3 Actual %4").arg(line).arg(triggerName).arg(focusTarget->isVisible()).arg(focusVisible).toUtf8().constData());
    QTRY_VERIFY2(cursorTarget->isVisible() == cursorVisible, QStringLiteral("line: %1 cursor for %2 not matched. Actual %3 Expected %4").arg(line).arg(triggerName).arg(cursorTarget->isVisible()).arg(cursorVisible).toUtf8().constData());
    QTRY_VERIFY2(mouseTarget->isVisible() == mouseVisible, QStringLiteral("line: %1 mouse for %2 not matched. Expected %3 Actual %4").arg(line).arg(triggerName).arg(mouseTarget->isVisible()).arg(mouseVisible).toUtf8().constData());
}

void PartTest::testAdditionalActionTriggers()
{
    const QString testFile = QStringLiteral(KDESRCDIR "data/additionalFormActions.pdf");
    Okular::Part part(nullptr, QVariantList());
    part.openDocument(testFile);
    part.widget()->resize(800, 600);

    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    QTimer *delayResizeEventTimer = part.m_pageView->findChildren<QTimer *>(QStringLiteral("delayResizeEventTimer")).at(0);
    QVERIFY(delayResizeEventTimer->isActive());
    QTest::qWait(delayResizeEventTimer->interval() * 2);

    part.m_document->setViewportPage(0);

    // wait for pixmap
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));

    QMap<QString, Okular::FormField *> fields;
    // Field names in test document are:
    // For trigger fields: tf, cb, rb, dd, pb
    // For target fields: <trigger_name>_focus_target, <trigger_name>_cursor_target,
    // <trigger_name>_mouse_target
    const Okular::Page *page = part.m_document->page(0);
    const QList<Okular::FormField *> pageFormFields = page->formFields();
    for (Okular::FormField *ff : pageFormFields) {
        fields.insert(ff->name(), static_cast<Okular::FormField *>(ff));
    }

    // Verify that everything is set up.
    verifyTargetStates(QStringLiteral("tf"), fields, true, true, true, __LINE__);
    verifyTargetStates(QStringLiteral("cb"), fields, true, true, true, __LINE__);
    verifyTargetStates(QStringLiteral("rb"), fields, true, true, true, __LINE__);
    verifyTargetStates(QStringLiteral("dd"), fields, true, true, true, __LINE__);
    verifyTargetStates(QStringLiteral("pb"), fields, true, true, true, __LINE__);

    const int width = part.m_pageView->horizontalScrollBar()->maximum() + part.m_pageView->viewport()->width();
    const int height = part.m_pageView->verticalScrollBar()->maximum() + part.m_pageView->viewport()->height();

    part.actionCollection()->action(QStringLiteral("view_toggle_forms"))->trigger();

    QPoint tfPos(width * 0.045, height * 0.05);
    QPoint cbPos(width * 0.045, height * 0.08);
    QPoint rbPos(width * 0.045, height * 0.12);
    QPoint ddPos(width * 0.045, height * 0.16);
    QPoint pbPos(width * 0.045, height * 0.26);

    // Test text field
    auto widget = part.m_pageView->viewport()->childAt(tfPos);
    QVERIFY(widget);

    QTest::mouseMove(part.m_pageView->viewport(), QPoint(tfPos));
    verifyTargetStates(QStringLiteral("tf"), fields, true, false, true, __LINE__);
    QTest::mousePress(widget, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    verifyTargetStates(QStringLiteral("tf"), fields, false, false, false, __LINE__);
    QTest::mouseRelease(widget, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    verifyTargetStates(QStringLiteral("tf"), fields, false, false, true, __LINE__);

    // Checkbox
    widget = part.m_pageView->viewport()->childAt(cbPos);
    QVERIFY(widget);

    QTest::mouseMove(part.m_pageView->viewport(), QPoint(cbPos));
    verifyTargetStates(QStringLiteral("cb"), fields, true, false, true, __LINE__);
    QTest::mousePress(widget, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    verifyTargetStates(QStringLiteral("cb"), fields, false, false, false, __LINE__);
    // Confirm that the textfield no longer has any invisible
    verifyTargetStates(QStringLiteral("tf"), fields, true, true, true, __LINE__);
    QTest::mouseRelease(widget, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    verifyTargetStates(QStringLiteral("cb"), fields, false, false, true, __LINE__);

    // Radio
    widget = part.m_pageView->viewport()->childAt(rbPos);
    QVERIFY(widget);

    QTest::mouseMove(part.m_pageView->viewport(), QPoint(rbPos));
    verifyTargetStates(QStringLiteral("rb"), fields, true, false, true, __LINE__);
    QTest::mousePress(widget, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    verifyTargetStates(QStringLiteral("rb"), fields, false, false, false, __LINE__);
    QTest::mouseRelease(widget, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    verifyTargetStates(QStringLiteral("rb"), fields, false, false, true, __LINE__);

    // Dropdown
    widget = part.m_pageView->viewport()->childAt(ddPos);
    QVERIFY(widget);

    QTest::mouseMove(part.m_pageView->viewport(), QPoint(ddPos));
    verifyTargetStates(QStringLiteral("dd"), fields, true, false, true, __LINE__);
    QTest::mousePress(widget, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    verifyTargetStates(QStringLiteral("dd"), fields, false, false, false, __LINE__);
    QTest::mouseRelease(widget, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    verifyTargetStates(QStringLiteral("dd"), fields, false, false, true, __LINE__);

    // Pushbutton
    widget = part.m_pageView->viewport()->childAt(pbPos);
    QVERIFY(widget);

    QTest::mouseMove(part.m_pageView->viewport(), QPoint(pbPos));
    verifyTargetStates(QStringLiteral("pb"), fields, true, false, true, __LINE__);
    QTest::mousePress(widget, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    verifyTargetStates(QStringLiteral("pb"), fields, false, false, false, __LINE__);
    QTest::mouseRelease(widget, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    verifyTargetStates(QStringLiteral("pb"), fields, false, false, true, __LINE__);

    // Confirm that a mouse release outside does not trigger the show action.
    QTest::mousePress(widget, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    verifyTargetStates(QStringLiteral("pb"), fields, false, false, false, __LINE__);
    QTest::mouseRelease(part.m_pageView->viewport(), Qt::LeftButton, Qt::NoModifier, tfPos);
    verifyTargetStates(QStringLiteral("pb"), fields, false, false, false, __LINE__);
}

void PartTest::testTypewriterAnnotTool()
{
    Okular::Part part(nullptr, QVariantList());

    part.openUrl(QUrl::fromLocalFile(QStringLiteral(KDESRCDIR "data/file1.pdf")));

    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    const int width = part.m_pageView->horizontalScrollBar()->maximum() + part.m_pageView->viewport()->width();
    const int height = part.m_pageView->verticalScrollBar()->maximum() + part.m_pageView->viewport()->height();

    part.m_document->setViewportPage(0);

    // Find the TypeWriter annotation
    QAction *typeWriterAction = part.actionCollection()->action(QStringLiteral("annotation_typewriter"));
    QVERIFY(typeWriterAction);

    const auto existingAnnotations = part.m_document->page(0)->annotations();
    typeWriterAction->trigger();

    QTest::qWait(1000); // Wait for the "add new note" dialog to appear
    TestingUtils::CloseDialogHelper closeDialogHelper(QDialogButtonBox::Ok);

    QTest::mouseClick(part.m_pageView->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(width * 0.5, height * 0.2));

    QTRY_COMPARE(part.m_document->page(0)->annotations().size(), existingAnnotations.size() + 1);
    Annotation *annot = nullptr;
    for (Annotation *candidate : part.m_document->page(0)->annotations()) {
        if (!existingAnnotations.contains(candidate)) {
            annot = candidate;
            break;
        }
    }
    QVERIFY(annot);
    TextAnnotation *ta = dynamic_cast<TextAnnotation *>(annot);
    QVERIFY(ta);
    QCOMPARE(annot->subType(), Okular::Annotation::AText);
    QCOMPARE(annot->style().color(), QColor(255, 255, 255, 0));
    QCOMPARE(ta->textType(), Okular::TextAnnotation::InPlace);
    QCOMPARE(ta->inplaceIntent(), Okular::TextAnnotation::TypeWriter);
}

void PartTest::testJumpToPage()
{
    const QString testFile = QStringLiteral(KDESRCDIR "data/simple-multipage.pdf");
    const int targetPage = 25;
    Okular::Part part(nullptr, QVariantList());
    part.openDocument(testFile);
    part.widget()->resize(800, 600);
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    part.m_document->pages();
    part.m_document->setViewportPage(targetPage);

    /* Document::setViewportPage triggers pixmap rendering in another thread.
     * We want to test how things look AFTER finished signal arrives back,
     * because PageView::slotRelayoutPages may displace the viewport again.
     */
    QTRY_VERIFY(part.m_document->page(targetPage)->hasPixmap(part.m_pageView));

    const int contentAreaHeight = part.m_pageView->verticalScrollBar()->maximum() + part.m_pageView->viewport()->height();
    const int pageWithSpaceTop = contentAreaHeight / part.m_document->pages() * targetPage;

    /*
     * This is a test for a "known by trial" displacement.
     * We'd need access to part.m_pageView->d->items[targetPage]->croppedGeometry().top(),
     * to determine the expected viewport position, but we don't have access.
     */
    QCOMPARE(part.m_pageView->verticalScrollBar()->value(), pageWithSpaceTop - 4);
}

void PartTest::testOpenAtPage()
{
    const QString testFile = QStringLiteral(KDESRCDIR "data/simple-multipage.pdf");
    QUrl url = QUrl::fromLocalFile(testFile);
    Okular::Part part(nullptr, QVariantList());

    const uint targetPageNumA = 25;
    const uint expectedPageA = targetPageNumA - 1;
    url.setFragment(QString::number(targetPageNumA));
    part.openUrl(url);
    QCOMPARE(part.m_document->currentPage(), expectedPageA);

    // 'page=<pagenum>' param as specified in RFC 3778
    const uint targetPageNumB = 15;
    const uint expectedPageB = targetPageNumB - 1;
    url.setFragment(QStringLiteral("page=") + QString::number(targetPageNumB));
    part.openUrl(url);
    QCOMPARE(part.m_document->currentPage(), expectedPageB);
}

void PartTest::testForwardBackwardNavigation()
{
    const QString testFile = QStringLiteral(KDESRCDIR "data/simple-multipage.pdf");
    Okular::Part part(nullptr, QVariantList());
    part.openDocument(testFile);
    part.widget()->resize(800, 600);
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    // Go to some page
    const int targetPageA = 15;
    part.m_document->setViewportPage(targetPageA);

    QVERIFY(part.m_document->viewport() == DocumentViewport(targetPageA));

    // Go to some other page
    const int targetPageB = 25;
    part.m_document->setViewportPage(targetPageB);
    QVERIFY(part.m_document->viewport() == DocumentViewport(targetPageB));

    // Go back to page A
    QVERIFY(QMetaObject::invokeMethod(&part, "slotHistoryBack"));
    QCOMPARE(part.m_document->viewport().pageNumber, targetPageA);

    // Go back to page B
    QVERIFY(QMetaObject::invokeMethod(&part, "slotHistoryNext"));
    QCOMPARE(part.m_document->viewport().pageNumber, targetPageB);
}

void PartTest::testWorkspaceMainViewRetainsPositionWhenDemoted()
{
    Okular::Part part(nullptr, {});
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/simple-multipage.pdf")));
    QVERIFY(part.m_document->pages() > 2);

    PageView *view = part.m_pageView;
    const int initialPage = static_cast<int>(part.m_document->currentPage());
    const int auxiliaryPage = initialPage == 0 ? 1 : 0;
    const int promotedPage = initialPage == 2 ? 1 : 2;

    // Build a default history with both Back and Forward entries before this
    // original main view has ever owned an independent session.
    part.m_document->setViewportPage(auxiliaryPage);
    part.m_document->setViewportPage(promotedPage);
    part.m_document->setPrevViewport();
    QCOMPARE(view->documentViewport().pageNumber, auxiliaryPage);
    QVERIFY(!part.m_document->historyAtBegin());
    QVERIFY(!part.m_document->historyAtEnd());

    // The first demotion must copy the complete default history, not merely
    // seed a new session at the current position.
    view->setWorkspaceMainView(false);
    QCOMPARE(view->documentViewport().pageNumber, auxiliaryPage);
    QVERIFY(!view->viewportHistoryAtBegin());
    QVERIFY(!view->viewportHistoryAtEnd());
    view->goToPreviousViewport();
    QCOMPARE(view->documentViewport().pageNumber, initialPage);
    view->goToNextViewport();
    QCOMPARE(view->documentViewport().pageNumber, auxiliaryPage);
    view->goToNextViewport();
    QCOMPARE(view->documentViewport().pageNumber, promotedPage);
    view->goToPreviousViewport();

    // Promotion installs this frame's own history as the Document default,
    // including its forward stack. A later demotion copies it back again.
    view->setWorkspaceMainView(true);
    QCOMPARE(part.m_document->currentPage(), static_cast<uint>(auxiliaryPage));
    QVERIFY(!part.m_document->historyAtEnd());
    part.m_document->setNextViewport();
    QCOMPARE(part.m_document->currentPage(), static_cast<uint>(promotedPage));
    part.m_document->setPrevViewport();
    part.m_document->setPrevViewport();
    QCOMPARE(part.m_document->currentPage(), static_cast<uint>(initialPage));

    view->setWorkspaceMainView(false);
    QCOMPARE(view->documentViewport().pageNumber, initialPage);
    QVERIFY(!view->viewportHistoryAtEnd());
    view->setWorkspaceMainView(true);
}

void PartTest::testTabletProximityBehavior()
{
    QVariantList dummyArgs;
    Okular::Part part {nullptr, dummyArgs};
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/file1.pdf")));
    part.slotShowPresentation();
    PresentationWidget *w = part.m_presentationWidget;
    QVERIFY(w);
    part.widget()->show();

    // close the KMessageBox "There are two ways of exiting[...]"
    TestingUtils::CloseDialogHelper closeDialogHelper(w, QDialogButtonBox::Ok); // confirm the "To leave, press ESC"

    auto pointingDevice = new QPointingDevice(QStringLiteral("test"), 42, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen, QInputDevice::Capability::All, 3, 3);
    QTabletEvent enterProximityEvent {QEvent::TabletEnterProximity, pointingDevice, QPointF(10, 10), QPointF(10, 10), 1., 0, 0, 1., 1., 0, Qt::NoModifier, Qt::NoButton, Qt::NoButton};
    QTabletEvent leaveProximityEvent {QEvent::TabletLeaveProximity, pointingDevice, QPointF(10, 10), QPointF(10, 10), 1., 0, 0, 1., 1., 0, Qt::NoModifier, Qt::NoButton, Qt::NoButton};

    // Test with the Okular::Settings::EnumSlidesCursor::Visible setting
    Okular::Settings::self()->setSlidesCursor(Okular::Settings::EnumSlidesCursor::Visible);

    // Send an enterProximity event
    qApp->notify(qApp, &enterProximityEvent);

    // The cursor should be a cross-hair
    QVERIFY(w->cursor().shape() == Qt::CursorShape(Qt::CrossCursor));

    // Send a leaveProximity event
    qApp->notify(qApp, &leaveProximityEvent);

    // After the leaveProximityEvent, the cursor should be an arrow again, because
    // we have set the slidesCursor mode to 'Visible'
    QVERIFY(w->cursor().shape() == Qt::CursorShape(Qt::ArrowCursor));

    // Test with the Okular::Settings::EnumSlidesCursor::Hidden setting
    Okular::Settings::self()->setSlidesCursor(Okular::Settings::EnumSlidesCursor::Hidden);

    qApp->notify(qApp, &enterProximityEvent);
    QVERIFY(w->cursor().shape() == Qt::CursorShape(Qt::CrossCursor));
    qApp->notify(qApp, &leaveProximityEvent);
    QVERIFY(w->cursor().shape() == Qt::CursorShape(Qt::BlankCursor));

    // Moving the mouse should not bring the cursor back
    QTest::mouseMove(w, QPoint(100, 100));
    QVERIFY(w->cursor().shape() == Qt::CursorShape(Qt::BlankCursor));

    // First test with the Okular::Settings::EnumSlidesCursor::HiddenDelay setting
    Okular::Settings::self()->setSlidesCursor(Okular::Settings::EnumSlidesCursor::HiddenDelay);

    qApp->notify(qApp, &enterProximityEvent);
    QVERIFY(w->cursor().shape() == Qt::CursorShape(Qt::CrossCursor));
    qApp->notify(qApp, &leaveProximityEvent);

    // After the leaveProximityEvent, the cursor should be blank, because
    // we have set the slidesCursor mode to 'HiddenDelay'
    QVERIFY(w->cursor().shape() == Qt::CursorShape(Qt::BlankCursor));

    // Moving the mouse should bring the cursor back
    QTest::mouseMove(w, QPoint(150, 150));
    QVERIFY(w->cursor().shape() == Qt::CursorShape(Qt::ArrowCursor));
}

void PartTest::testOpenPrintPreview()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/file1.pdf")));
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));
#ifdef Q_OS_WIN
    TestingUtils::CloseDialogHelper closeDialogHelper(QDialogButtonBox::Cancel);
#else
    TestingUtils::CloseDialogHelper closeDialogHelper(QDialogButtonBox::Close);
#endif
    part.slotPrintPreview();
}

void PartTest::testDisjointPrintPageRanges()
{
    QPrinter printer(QPrinter::ScreenResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    QPageRanges ranges;
    ranges.addRange(1, 4);
    ranges.addPage(8);
    ranges.addRange(11, 13);
    printer.setPrintRange(QPrinter::PageRange);
    printer.setFromTo(1, 13);
    printer.setPageRanges(ranges);

    const QList<int> expectedPages = {1, 2, 3, 4, 8, 11, 12, 13};
    QCOMPARE(Okular::FilePrinter::pageList(printer, 20, 1, {}), expectedPages);
}

void PartTest::testMouseModeMenu()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/file1.pdf")));

    QCOMPARE(Okular::Settings::mouseMode(), (int)Okular::Settings::EnumMouseMode::TextSelect);

    QMetaObject::invokeMethod(part.m_pageView, "slotSetMouseNormal");

    // Get mouse mode menu action
    QAction *mouseModeAction = part.actionCollection()->action(QStringLiteral("mouse_selecttools"));
    QVERIFY(mouseModeAction);
    QMenu *mouseModeActionMenu = mouseModeAction->menu();

    // Test that actions are usable (not disabled)
    QVERIFY(mouseModeActionMenu->actions().at(0)->isEnabled());
    QVERIFY(mouseModeActionMenu->actions().at(1)->isEnabled());
    QVERIFY(mouseModeActionMenu->actions().at(2)->isEnabled());

    // Test activating area selection mode
    mouseModeActionMenu->actions().at(0)->trigger();
    QCOMPARE(Okular::Settings::mouseMode(), (int)Okular::Settings::EnumMouseMode::RectSelect);

    // Test activating text selection mode
    mouseModeActionMenu->actions().at(1)->trigger();
    QCOMPARE(Okular::Settings::mouseMode(), (int)Okular::Settings::EnumMouseMode::TextSelect);

    // Test activating table selection mode
    mouseModeActionMenu->actions().at(2)->trigger();
    QCOMPARE(Okular::Settings::mouseMode(), (int)Okular::Settings::EnumMouseMode::TableSelect);
}

void PartTest::testFullScreenRequest()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);

    // Open file.  For this particular file, a dialog has to appear asking whether
    // one wants to comply with the wish to go to presentation mode directly.
    // Answer 'no'
    auto dialogHelper = std::make_unique<TestingUtils::CloseDialogHelper>(&part, QDialogButtonBox::No);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/RequestFullScreen.pdf")));

    // Check that we are not in presentation mode
    QEXPECT_FAIL("", "The presentation widget should not be shown because we clicked No in the dialog", Continue);
    QTRY_VERIFY_WITH_TIMEOUT(part.m_presentationWidget, 1000);

    // Reload the file.  The initial dialog should no appear again.
    // (This is https://bugs.kde.org/show_bug.cgi?id=361740)
    part.reload();

    // Open the file again.  Now we answer "yes, go to presentation mode"
    dialogHelper = std::make_unique<TestingUtils::CloseDialogHelper>(&part, QDialogButtonBox::Yes);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/RequestFullScreen.pdf")));

    // Test whether we really are in presentation mode
    QTRY_VERIFY(part.m_presentationWidget);
}

void PartTest::testZoomInFacingPages()
{
    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/file2.pdf")));
    QAction *facingAction = part.m_pageView->findChild<QAction *>(QStringLiteral("view_render_mode_facing"));
    KSelectAction *zoomSelectAction = part.m_pageView->findChild<KSelectAction *>(QStringLiteral("zoom_to"));
    part.widget()->resize(600, 400);
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));
    facingAction->trigger();
    while (zoomSelectAction->currentText() != QStringLiteral("12%")) {
        QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotZoomOut"));
    }
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotZoomIn"));
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotZoomIn"));
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotZoomIn"));
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotZoomIn"));
    QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotZoomIn"));
    QTRY_COMPARE(zoomSelectAction->currentText(), QStringLiteral("66%"));

    // Back to single mode
    part.m_pageView->findChild<QAction *>(QStringLiteral("view_render_mode_single"))->trigger();
}

void PartTest::testZoomWithCrop()
{
    // We test that all zoom levels can be achieved with cropped pages, bug 342003

    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/file2.pdf")));

    KActionMenu *cropMenu = part.m_pageView->findChild<KActionMenu *>(QStringLiteral("view_trim_mode"));
    KToggleAction *cropAction = cropMenu->menu()->findChild<KToggleAction *>(QStringLiteral("view_trim_margins"));
    KSelectAction *zoomSelectAction = part.m_pageView->findChild<KSelectAction *>(QStringLiteral("zoom_to"));

    part.widget()->resize(600, 400);
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    // Activate "Trim Margins"
    QVERIFY(!Okular::Settings::trimMargins());
    cropAction->trigger();
    QVERIFY(Okular::Settings::trimMargins());

    // Wait for the bounding boxes
    QTRY_VERIFY(part.m_document->page(0)->isBoundingBoxKnown());
    QTRY_VERIFY(part.m_document->page(1)->isBoundingBoxKnown());

    // Zoom out
    for (int i = 0; i < 20; i++) {
        QVERIFY(QMetaObject::invokeMethod(part.m_pageView, "slotZoomOut"));
    }
    QCOMPARE(zoomSelectAction->currentText(), QStringLiteral("12%"));

    // Zoom in and out and check that all zoom levels appear
    QSet<QString> zooms_ref {QStringLiteral("12%"),
                             QStringLiteral("25%"),
                             QStringLiteral("33%"),
                             QStringLiteral("50%"),
                             QStringLiteral("66%"),
                             QStringLiteral("75%"),
                             QStringLiteral("100%"),
                             QStringLiteral("125%"),
                             QStringLiteral("150%"),
                             QStringLiteral("200%"),
                             QStringLiteral("400%"),
                             QStringLiteral("800%"),
                             QStringLiteral("1,600%"),
                             QStringLiteral("2,500%"),
                             QStringLiteral("5,000%"),
                             QStringLiteral("10,000%")};

    for (int j = 0; j < 2; j++) {
        QSet<QString> zooms;
        for (int i = 0; i < 18; i++) {
            zooms << zoomSelectAction->currentText();
            QVERIFY(QMetaObject::invokeMethod(part.m_pageView, j == 0 ? "slotZoomIn" : "slotZoomOut"));
        }

        QVERIFY(zooms.contains(zooms_ref));
    }

    // Deactivate "Trim Margins"
    QVERIFY(Okular::Settings::trimMargins());
    cropAction->trigger();
    QVERIFY(!Okular::Settings::trimMargins());
}

void PartTest::testLinkWithCrop()
{
    // We test that link targets are correct with cropping, related to bug 198427

    QVariantList dummyArgs;
    Okular::Part part(nullptr, dummyArgs);
    QVERIFY(openDocument(&part, QStringLiteral(KDESRCDIR "data/pdf_with_internal_links.pdf")));

    KActionMenu *cropMenu = part.m_pageView->findChild<KActionMenu *>(QStringLiteral("view_trim_mode"));
    KToggleAction *cropAction = cropMenu->menu()->findChild<KToggleAction *>(QStringLiteral("view_trim_selection"));

    part.widget()->resize(600, 400);
    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    // wait for pixmap
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));
    part.m_document->requestTextPage(0);
    QTRY_VERIFY(part.m_document->page(0)->hasTextPage());

    const int width = part.m_pageView->viewport()->width();
    const int height = part.m_pageView->viewport()->height();

    // Move to a location without a link
    QTest::mouseMove(part.m_pageView->viewport(), QPoint(width * 0.1, width * 0.1));

    // The cursor should be normal
    QTRY_COMPARE(part.m_pageView->cursor().shape(), Qt::CursorShape(Qt::OpenHandCursor));

    // Activate "Trim Margins"
    cropAction->trigger();

    // The cursor should be a cross-hair
    QTRY_COMPARE(part.m_pageView->cursor().shape(), Qt::CursorShape(Qt::CrossCursor));

    const int mouseStartY = height * 0.2;
    const int mouseEndY = height * 0.8;
    const int mouseStartX = width * 0.2;
    const int mouseEndX = width * 0.8;

    // Trim the page
    simulateMouseSelection(mouseStartX, mouseStartY, mouseEndX, mouseEndY, part.m_pageView->viewport());

    // Move away while the cropped layout settles.
    QTest::mouseMove(part.m_pageView->viewport(), QPoint(width * 0.1, width * 0.1));
    part.m_document->setViewportPage(0);
    QCoreApplication::sendPostedEvents(part.m_pageView, QEvent::MetaCall);
    QCoreApplication::processEvents();
    QTimer *resizeTimer = part.m_pageView->findChild<QTimer *>(QStringLiteral("delayResizeEventTimer"));
    QVERIFY(resizeTimer);
    QTRY_VERIFY_WITH_TIMEOUT(!resizeTimer->isActive(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(part.m_document->page(0)->hasPixmap(part.m_pageView), 5000);

    // The cursor should be normal again
    QTRY_COMPARE(part.m_pageView->cursor().shape(), Qt::CursorShape(Qt::OpenHandCursor));

    // Locate a known link through the cropped PageView mapping rather than
    // assuming the workspace wrapper leaves it at a fixed widget coordinate.
    QPoint clickPosition;
    DocumentViewport linkTarget;
    QString title;
    const QString expectedLinkTitle = QStringLiteral("2.1 Example for list (itemize)");
    QVERIFY(findVisibleInternalGotoLink(part.m_pageView, part.m_document, 0, 1, expectedLinkTitle, &clickPosition, &linkTarget, &title));
    QCOMPARE(title, expectedLinkTitle);
    QVERIFY(linkTarget.rePos.enabled);
    QTest::mouseMove(part.m_pageView->viewport(), clickPosition);
    QTest::mouseClick(part.m_pageView->viewport(), Qt::LeftButton, Qt::NoModifier, clickPosition);

    QTRY_COMPARE(part.m_document->currentPage(), static_cast<uint>(linkTarget.pageNumber));
    QTRY_VERIFY2_WITH_TIMEOUT(qAbs(part.m_document->viewport().rePos.normalizedY - linkTarget.rePos.normalizedY) < 0.01,
                              qPrintable(QStringLiteral("Expected target y %1, got %2").arg(linkTarget.rePos.normalizedY).arg(part.m_document->viewport().rePos.normalizedY)),
                              500);

    // Deactivate "Trim Margins"
    cropAction->trigger();
}

void PartTest::testFieldFormatting()
{
    // Test field formatting. This has to be a parttest so that we
    // can properly test focus in / out which triggers formatting.
    const QString testFile = QStringLiteral(KDESRCDIR "data/fieldFormat.pdf");
    Okular::Part part(nullptr, QVariantList());
    part.openDocument(testFile);
    part.widget()->resize(800, 600);

    part.widget()->show();
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(part.widget()));

    // Field names in test document are:
    //
    // us_currency_fmt for formatting like "$ 1,234.56"
    // de_currency_fmt for formatting like "1.234,56 €"
    // de_simple_sum for calculation test and formatting like "1.234,56€"
    // date_mm_dd_yyyy for dates like "18/06/2018"
    // date_dd_mm_yyyy for dates like "06/18/2018"
    // percent_fmt for percent format like "100,00%" if you enter 1
    // time_HH_MM_fmt for times like "23:12"
    // time_HH_MM_ss_fmt for times like "23:12:34"
    // special_phone_number for an example of a special format selectable in Acrobat.
    QMap<QString, Okular::FormField *> fields;
    const Okular::Page *page = part.m_document->page(0);
    const auto formFields = page->formFields();
    for (Okular::FormField *ff : formFields) {
        fields.insert(ff->name(), static_cast<Okular::FormField *>(ff));
    }

    const int width = part.m_pageView->horizontalScrollBar()->maximum() + part.m_pageView->viewport()->width();
    const int height = part.m_pageView->verticalScrollBar()->maximum() + part.m_pageView->viewport()->height();

    part.m_document->setViewportPage(0);

    // wait for pixmap
    QTRY_VERIFY(part.m_document->page(0)->hasPixmap(part.m_pageView));

    part.actionCollection()->action(QStringLiteral("view_toggle_forms"))->trigger();

    // Note as of version 1.5:
    // The test document is prepared for future extensions to formatting for dates etc.
    // Currently we only have the number format to test.
    const auto ff_us = dynamic_cast<Okular::FormFieldText *>(fields.value(QStringLiteral("us_currency_fmt")));
    const auto ff_de = dynamic_cast<Okular::FormFieldText *>(fields.value(QStringLiteral("de_currency_fmt")));
    const auto ff_sum = dynamic_cast<Okular::FormFieldText *>(fields.value(QStringLiteral("de_simple_sum")));

    const QPoint usPos(width * 0.25, height * 0.025);
    const QPoint dePos(width * 0.25, height * 0.05);
    const QPoint deSumPos(width * 0.25, height * 0.075);

    const auto viewport = part.m_pageView->viewport();

    QVERIFY(viewport);

    auto usCurrencyWidget = dynamic_cast<QLineEdit *>(viewport->childAt(usPos));
    auto deCurrencyWidget = dynamic_cast<QLineEdit *>(viewport->childAt(dePos));
    auto sumCurrencyWidget = dynamic_cast<QLineEdit *>(viewport->childAt(deSumPos));

    // Check that the widgets were found at the right position
    QVERIFY(usCurrencyWidget);
    QVERIFY(deCurrencyWidget);
    QVERIFY(sumCurrencyWidget);

    QTest::mousePress(usCurrencyWidget, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    QTRY_VERIFY(usCurrencyWidget->hasFocus());
    // locale is en_US for this test. Enter a value and check it.
    usCurrencyWidget->setText(QStringLiteral("1234.56"));
    // Check that the internal text matches
    QCOMPARE(ff_us->text(), QStringLiteral("1234.56"));

    // Now move the focus to trigger formatting.
    QTest::mousePress(deCurrencyWidget, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    QTRY_VERIFY(deCurrencyWidget->hasFocus());

    QCOMPARE(usCurrencyWidget->text(), QStringLiteral("$ 1,234.56"));
    QCOMPARE(ff_us->text(), QStringLiteral("1234.56"));

    // And again with an invalid number
    QTest::mousePress(usCurrencyWidget, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    QTRY_VERIFY(usCurrencyWidget->hasFocus());

    usCurrencyWidget->setText(QStringLiteral("131234.567"));
    QTest::mousePress(deCurrencyWidget, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    QTRY_VERIFY(deCurrencyWidget->hasFocus());
    // Check that the internal text still contains it.
    QCOMPARE(ff_us->text(), QStringLiteral("131234.567"));

    // Just check that the text does not match the internal text.
    // We don't check for a concrete value to keep NaN handling flexible
    QVERIFY(ff_us->text() != usCurrencyWidget->text());

    // Move the focus back and modify it a bit more
    QTest::mousePress(usCurrencyWidget, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    QTRY_VERIFY(usCurrencyWidget->hasFocus());

    usCurrencyWidget->setText(QStringLiteral("1234.567"));
    QTest::mousePress(deCurrencyWidget, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    QTRY_VERIFY(deCurrencyWidget->hasFocus());

    QCOMPARE(usCurrencyWidget->text(), QStringLiteral("$ 1,234.57"));

    // Sum should already match
    QCOMPARE(sumCurrencyWidget->text(), QStringLiteral("1.234,57€"));

    // Set a text in the de field
    deCurrencyWidget->setText(QStringLiteral("1123234,567"));
    QTest::mousePress(usCurrencyWidget, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    QTRY_VERIFY(usCurrencyWidget->hasFocus());

    QCOMPARE(deCurrencyWidget->text(), QStringLiteral("1.123.234,57 €"));
    QCOMPARE(ff_de->text(), QStringLiteral("1123234,567"));
    QCOMPARE(sumCurrencyWidget->text(), QStringLiteral("1.124.469,13€"));
    QCOMPARE(ff_sum->text(), QStringLiteral("1124469.1340000000782310962677002"));
}

} // namespace Okular

int main(int argc, char *argv[])
{
    // Force consistent locale
    QLocale locale(QStringLiteral("en_US.UTF-8"));
    if (locale == QLocale::c()) { // This is the way to check if the above worked
        locale = QLocale(QLocale::English, QLocale::UnitedStates);
    }

    QLocale::setDefault(locale);
    qputenv("LC_ALL", "en_US.UTF-8"); // For UNIX, third-party libraries

    // Ensure consistent configs/caches
    QTemporaryDir homeDir; // QTemporaryDir automatically cleans up when it goes out of scope
    Q_ASSERT(homeDir.isValid());
    QByteArray homePath = QFile::encodeName(homeDir.path());
    qDebug() << homePath;
    qputenv("USERPROFILE", homePath);
    qputenv("HOME", homePath);
    qputenv("XDG_DATA_HOME", QByteArray(homePath + "/.local"));
    qputenv("XDG_CONFIG_HOME", QByteArray(homePath + "/.kde-unit-test/xdg/config"));

    // Disable fancy debug output
    qunsetenv("QT_MESSAGE_PATTERN");

    Okular::Settings::instance(QStringLiteral("okularparttest"));

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("okularparttest"));
    app.setOrganizationDomain(QStringLiteral("kde.org"));
    app.setQuitOnLastWindowClosed(false);

    qRegisterMetaType<QUrl>(); /*as done by kapplication*/
    qRegisterMetaType<QList<QUrl>>();

    Okular::PartTest test;

    return QTest::qExec(&test, argc, argv);
}

#include "parttest.moc"
