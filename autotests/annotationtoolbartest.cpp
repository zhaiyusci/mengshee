/*
    SPDX-FileCopyrightText: 2020 Simone Gaiarin <simgunz@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// clazy:excludeall=qstring-allocations

#include <QTest>

#include <QMenu>
#include <QComboBox>
#include <QToolButton>
#include <QDir>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QWidget>

#include <KActionCollection>
#include <KSelectAction>

#include "../core/page.h"
#include "../part/pageview.h"
#include "../part/part.h"
#include "../part/toggleactionmenu.h"
#include "../settings.h"
#include "../shell/okular_main.h"
#include "../shell/shell.h"
#include "../shell/shellutils.h"
#include "closedialoghelper.h"

namespace Okular
{
class PartTest
{
public:
    Okular::Document *partDocument(Okular::Part *part) const
    {
        return part->m_document;
    }
    PageView *pageView(Okular::Part *part) const
    {
        return part->m_pageView;
    }
};
}

class AnnotationToolBarTest : public QObject, public Okular::PartTest
{
    Q_OBJECT

public:
    static void initMain();
    static QTabWidget *tabWidget(Shell *s)
    {
        return s->m_tabWidget;
    }

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testModeSelectorToolBar();
    void testAnnotationToolBar();
    void testAnnotationToolBar_data();
    void testAnnotationToolBarActionsEnabledState();
    void testAnnotationToolBarActionsEnabledState_data();
    void testAnnotationToolBarConfigActionsEnabledState();
    void testAnnotationToolBarConfigActionsEnabledState_data();

private:
    bool simulateAddPopupAnnotation(Okular::Part *part, int mouseX, int mouseY);
};

Shell *findShell(Shell *ignore = nullptr)
{
    const QWidgetList wList = QApplication::topLevelWidgets();
    for (QWidget *widget : wList) {
        Shell *s = qobject_cast<Shell *>(widget);
        if (s && s != ignore) {
            return s;
        }
    }
    return nullptr;
}

void AnnotationToolBarTest::initMain()
{
    // Ensure consistent configs/caches and Default UI
    static QTemporaryDir homeDir;
    Q_ASSERT(homeDir.isValid());
    QByteArray homePath = QFile::encodeName(homeDir.path());
    qputenv("USERPROFILE", homePath);
    qputenv("HOME", homePath);
    qputenv("XDG_DATA_HOME", QByteArray(homePath + "/.local"));
    qputenv("XDG_CONFIG_HOME", QByteArray(homePath + "/.kde-unit-test/xdg/config"));
}

void AnnotationToolBarTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    const QString iconPath = qEnvironmentVariable("MENGSHEE_TEST_ICON_PATH");
    if (!iconPath.isEmpty()) {
        QIcon::setThemeSearchPaths({iconPath});
        QIcon::setThemeName(QStringLiteral("breeze"));
        QApplication::setStyle(QStringLiteral("breeze"));
    }
    // Don't pollute people's okular settings
    Okular::Settings::instance(QStringLiteral("annotationtoolbartest"));
}

void AnnotationToolBarTest::init()
{
    // Default settings for every test
    Okular::Settings::self()->setDefaults();
}

void AnnotationToolBarTest::cleanup()
{
    Shell *s;
    while ((s = findShell())) {
        delete s;
    }
}

bool AnnotationToolBarTest::simulateAddPopupAnnotation(Okular::Part *part, int mouseX, int mouseY)
{
    int annotationCount = partDocument(part)->page(0)->annotations().size();
    QTest::mouseMove(pageView(part)->viewport(), QPoint(mouseX, mouseY));
    QTest::mouseClick(pageView(part)->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(mouseX, mouseY));
    bool annotationAdded = partDocument(part)->page(0)->annotations().size() == annotationCount + 1;
    return annotationAdded;
}

void AnnotationToolBarTest::testModeSelectorToolBar()
{
    Okular::Settings::self()->setShellOpenFileInTabs(true);
    const QString options = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());
    QCOMPARE(Okular::main({QStringLiteral(KDESRCDIR "data/file1.pdf"), QStringLiteral(KDESRCDIR "data/file2.pdf")}, options), Okular::Success);
    Shell *shell = findShell();
    QVERIFY(shell);
    shell->resize(1400, 850);
    QVERIFY(QTest::qWaitForWindowExposed(shell));
    QCOMPARE(shell->m_tabs.size(), 2);
    shell->m_tabWidget->setCurrentIndex(0);
    auto *part = dynamic_cast<Okular::Part *>(shell->m_tabs.constFirst().part);
    QVERIFY(part);
    auto *toolbar = shell->findChild<QToolBar *>(QStringLiteral("mainToolBar"));
    auto *tools = shell->findChild<QToolBar *>(QStringLiteral("advancedToolBar"));
    auto *annotationToolbar = shell->findChild<QToolBar *>(QStringLiteral("annotationToolBar"));
    auto *mainToolbar = toolbar;
    QVERIFY(annotationToolbar);
    QVERIFY(toolbar && tools && mainToolbar);
    auto *mode = qobject_cast<KSelectAction *>(part->actionCollection()->action(QStringLiteral("editing_mode_selector")));
    QVERIFY(mode);
    auto *combo = qobject_cast<QComboBox *>(toolbar->widgetForAction(mode));
    QVERIFY(combo);
    QCOMPARE(combo->count(), 5);
    auto *pin = part->actionCollection()->action(QStringLiteral("open_auxiliary_view"));
    auto *highlight = part->actionCollection()->action(QStringLiteral("annotation_highlighter"));
    QVERIFY(pin && highlight);
    auto *pinButton = qobject_cast<QToolButton *>(toolbar->widgetForAction(pin));
    QVERIFY(pinButton);
    QVERIFY(toolbar->actions().indexOf(mode) > toolbar->actions().indexOf(pin));
    QVERIFY(mainToolbar->actions().contains(mode));
    QVERIFY(!annotationToolbar->actions().contains(mode));
    QCOMPARE(mainToolbar->actions().constLast(), mode);
    QVERIFY(mainToolbar->actions().contains(part->actionCollection()->action(QStringLiteral("view_read_by_views"))));
    QVERIFY(!part->actionCollection()->action(QStringLiteral("view_edit_views")));
    QVERIFY(!mainToolbar->actions().contains(part->actionCollection()->action(QStringLiteral("view_toggle_named_destinations"))));
    highlight->trigger();
    QVERIFY(highlight->isChecked());
    // Drive the actual combo, not merely the underlying QAction list.
    combo->setFocus();
    QTest::keyClick(combo, Qt::Key_End);
    for (int index = 4; index >= 0; --index) {
        QTRY_COMPARE(mode->currentItem(), index);
        QTRY_COMPARE(combo->currentIndex(), index);
        QTRY_VERIFY(toolbar->isVisible());
        QTRY_COMPARE(tools->isVisible(), index != 0);
        QVERIFY(highlight->isEnabled());
        QVERIFY(highlight->isChecked());
        QCOMPARE(pageView(part)->readingViewEditingEnabled(), index == 4);
        QCOMPARE(pageView(part)->ocrModeEnabled(), index == 2);
        QCOMPARE(pageView(part)->namedDestinationsVisible(), index == 1);
        QVERIFY(!pageView(part)->isOcrTextEditing());
        QTRY_COMPARE(combo->height(), pinButton->height());
        QTRY_COMPARE(combo->mapTo(toolbar, QPoint(0, combo->height() / 2)).y(), pinButton->mapTo(toolbar, QPoint(0, pinButton->height() / 2)).y());
        const QString artifacts = qEnvironmentVariable("MENGSHEE_MODE_ARTIFACTS");
        if (!artifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(artifacts));
            QVERIFY(shell->grab().save(QDir(artifacts).filePath(QStringLiteral("mode-%1.png").arg(index))));
        }
        if (index) QTest::keyClick(combo, Qt::Key_Up);
    }
    // GUI merging must restore each document's mode, not turn it into Reading.
    mode->actions().at(2)->trigger();
    shell->m_tabWidget->setCurrentIndex(1);
    auto *secondPart = dynamic_cast<Okular::Part *>(shell->m_tabs.at(1).part);
    QVERIFY(secondPart);
    auto *secondMode = qobject_cast<KSelectAction *>(secondPart->actionCollection()->action(QStringLiteral("editing_mode_selector")));
    QVERIFY(secondMode);
    QCOMPARE(secondMode->currentItem(), 0);
    secondMode->actions().at(3)->trigger();
    shell->m_tabWidget->setCurrentIndex(0);
    QCOMPARE(mode->currentItem(), 2);
    toolbar = shell->findChild<QToolBar *>(QStringLiteral("mainToolBar"));
    QVERIFY(toolbar); // XMLGUI may replace toolbar widgets while switching clients.
    auto *restoredCombo = qobject_cast<QComboBox *>(toolbar->widgetForAction(mode));
    QVERIFY(restoredCombo);
    QCOMPARE(restoredCombo->currentIndex(), 2);
    QVERIFY(pageView(part)->ocrModeEnabled());
    QCOMPARE(secondMode->currentItem(), 3);
    part->actionCollection()->action(QStringLiteral("open_auxiliary_view"))->trigger();
    toolbar = shell->findChild<QToolBar *>(QStringLiteral("mainToolBar"));
    QVERIFY(toolbar);
    auto *auxiliaryCombo = qobject_cast<QComboBox *>(toolbar->widgetForAction(mode));
    QVERIFY(auxiliaryCombo);
    QCOMPARE(auxiliaryCombo->currentIndex(), 2);
    tools = shell->findChild<QToolBar *>(QStringLiteral("advancedToolBar"));
    QVERIFY(tools);
    QTRY_VERIFY(tools->isVisible());
    pinButton = qobject_cast<QToolButton *>(toolbar->widgetForAction(pin));
    QVERIFY(pinButton);
    QTRY_COMPARE(auxiliaryCombo->height(), pinButton->height());
}

void AnnotationToolBarTest::testAnnotationToolBar()
{
    // Using tabs we test that the annotation toolbar works on each Okular::Part
    Okular::Settings::self()->setShellOpenFileInTabs(true);

    const QStringList paths = {QStringLiteral(KDESRCDIR "data/file1.pdf"), QStringLiteral(KDESRCDIR "data/file2.pdf")};
    QString serializedOptions = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());

    Okular::Status status = Okular::main(paths, serializedOptions);
    QCOMPARE(status, Okular::Success);
    Shell *s = findShell();
    QVERIFY(s);
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(s));
    QFETCH(int, tabIndex);
    s->m_tabWidget->tabBar()->setCurrentIndex(tabIndex);
    Okular::Part *part = dynamic_cast<Okular::Part *>(s->m_tabs[tabIndex].part);
    QVERIFY(part);

    QToolBar *mainToolBar = s->findChild<QToolBar *>(QStringLiteral("mainToolBar"));
    QToolBar *annToolBar = s->findChild<QToolBar *>(QStringLiteral("annotationToolBar"));
    QToolBar *advancedToolBar = s->findChild<QToolBar *>(QStringLiteral("advancedToolBar"));
    QVERIFY(mainToolBar);
    QVERIFY(annToolBar);
    QVERIFY(advancedToolBar);

    // Check config action default enabled states
    QAction *aQuickTools = part->actionCollection()->action(QStringLiteral("annotation_favorites"));
    QAction *aAddToQuickTools = part->actionCollection()->action(QStringLiteral("annotation_bookmark"));
    QAction *aAdvancedSettings = part->actionCollection()->action(QStringLiteral("annotation_settings_advanced"));
    QAction *aContinuousMode = part->actionCollection()->action(QStringLiteral("annotation_settings_pin"));
    QVERIFY(aQuickTools->isEnabled());
    QVERIFY(mainToolBar->actions().contains(aQuickTools));
    QVERIFY(!annToolBar->actions().contains(aQuickTools));
    QVERIFY(!aAddToQuickTools->isEnabled());
    QVERIFY(!aAdvancedSettings->isEnabled());
    QVERIFY(aContinuousMode->isEnabled());

    // Ensure that the 'Quick Annotations' action is correctly populated
    // (at least the 'Configure Annotations...' action must be present)
    QVERIFY(!aQuickTools->menu()->actions().isEmpty());

    // The current mode owns its toolbar. Users cannot hide either mode toolbar.
    QVERIFY(!part->actionCollection()->action(QStringLiteral("mouse_toggle_annotate")));
    QVERIFY(!part->actionCollection()->action(QStringLiteral("hide_annotation_toolbar")));
    QTRY_VERIFY(annToolBar->isVisible());
    QTRY_VERIFY(!advancedToolBar->isVisible());
    QVERIFY(annToolBar->geometry().top() > mainToolBar->geometry().top());
    QCOMPARE(annToolBar->contextMenuPolicy(), Qt::PreventContextMenu);
    QVERIFY(!annToolBar->toggleViewAction()->isVisible());

    auto *mode = qobject_cast<KSelectAction *>(part->actionCollection()->action(QStringLiteral("editing_mode_selector")));
    QVERIFY(mode);
    QCOMPARE(mode->actions().size(), 5);
    QVERIFY(!annToolBar->actions().contains(mode));
    QVERIFY(mainToolBar->actions().contains(mode));
    for (int index : {1, 2, 3, 4}) {
        mode->actions().at(index)->trigger();
        QTRY_VERIFY(annToolBar->isVisible());
        QTRY_VERIFY(advancedToolBar->isVisible());
        QVERIFY(advancedToolBar->geometry().top() > mainToolBar->geometry().top());
        QCOMPARE(advancedToolBar->geometry().top(), annToolBar->geometry().top());
        QCOMPARE(advancedToolBar->contextMenuPolicy(), Qt::PreventContextMenu);
        QVERIFY(!advancedToolBar->toggleViewAction()->isVisible());
        QVERIFY(aContinuousMode->isEnabled());
    }
    mode->actions().at(0)->trigger();
    QTRY_VERIFY(annToolBar->isVisible());
    QTRY_VERIFY(!advancedToolBar->isVisible());

    // set mouse mode to browse before starting the tests on the annotation actions
    QAction *aMouseNormal = part->actionCollection()->action(QStringLiteral("mouse_drag"));
    QVERIFY(aMouseNormal);
    aMouseNormal->trigger();
    QTRY_COMPARE(Okular::Settings::mouseMode(), static_cast<int>(Okular::Settings::EnumMouseMode::Browse));

    // Click an annotation action to enable it
    QAction *aPopupNote = part->actionCollection()->action(QStringLiteral("annotation_popup_note"));
    QVERIFY(aPopupNote);
    aPopupNote->trigger();
    int mouseX = 350;
    int mouseY = 100;
    QCOMPARE(simulateAddPopupAnnotation(part, mouseX, mouseY), true);
    QTRY_COMPARE(aMouseNormal->isChecked(), false);

    // Click again the same annotation action to disable it
    aPopupNote->trigger();
    mouseY = 150;
    QCOMPARE(simulateAddPopupAnnotation(part, mouseX, mouseY), false);
    QTRY_COMPARE(aMouseNormal->isChecked(), true);

    // Trigger the action using a shortcut
    QTest::keyClick(part->widget(), Qt::Key_7, Qt::AltModifier);
    mouseY = 200;
    QCOMPARE(simulateAddPopupAnnotation(part, mouseX, mouseY), true);
    QTRY_COMPARE(aMouseNormal->isChecked(), false);

    // Click Esc to disable all annotations
    QTest::keyClick(pageView(part), Qt::Key_Escape);
    mouseY = 250;
    QCOMPARE(simulateAddPopupAnnotation(part, mouseX, mouseY), false);
    QTRY_COMPARE(aMouseNormal->isChecked(), true);

    // Trigger the action using a quick annotation shortcut
    QTest::keyClick(part->widget(), Qt::Key_6);
    QCOMPARE(simulateAddPopupAnnotation(part, mouseX, mouseY), true);
    QTRY_COMPARE(aMouseNormal->isChecked(), false);

    // Test pin/continuous mode action
    QVERIFY(aContinuousMode->isEnabled());
    QCOMPARE(simulateAddPopupAnnotation(part, mouseX, mouseY), true);
    QCOMPARE(simulateAddPopupAnnotation(part, mouseX, mouseY), true);
    aContinuousMode->trigger();
    QCOMPARE(simulateAddPopupAnnotation(part, mouseX, mouseY), true);
    QCOMPARE(simulateAddPopupAnnotation(part, mouseX, mouseY), false);

    // Test adding a tool to the quick tool list using the bookmark action
    QScopedPointer<TestingUtils::CloseDialogHelper> closeDialogHelper;
    closeDialogHelper.reset(new TestingUtils::CloseDialogHelper(QDialogButtonBox::Ok));
    aPopupNote->trigger();
    QVERIFY(aPopupNote->isChecked());
    int quickActionCount = aQuickTools->menu()->actions().size();
    aAddToQuickTools->trigger();
    QTRY_COMPARE(aQuickTools->menu()->actions().size(), quickActionCount + 1);
    // Trigger the quick tool that was just added
    aQuickTools->menu()->actions().at(6)->trigger();
    QCOMPARE(simulateAddPopupAnnotation(part, mouseX, mouseY), true);
}

void AnnotationToolBarTest::testAnnotationToolBar_data()
{
    QTest::addColumn<int>("tabIndex");
    QTest::addRow("first tab") << 0;
    QTest::addRow("second tab") << 1;
}

void AnnotationToolBarTest::testAnnotationToolBarActionsEnabledState()
{
    QFETCH(QString, document);

    const QStringList paths = {document};
    QString serializedOptions = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());

    Okular::Status status = Okular::main(paths, serializedOptions);
    QCOMPARE(status, Okular::Success);
    Shell *s = findShell();
    QVERIFY(s);
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(s));

    Okular::Part *part = s->findChild<Okular::Part *>();
    QVERIFY(part);

    KActionCollection *ac = part->actionCollection();
    QAction *aQuickTools = ac->action(QStringLiteral("annotation_favorites"));
    QAction *aHighlighter = ac->action(QStringLiteral("annotation_highlighter"));
    QAction *aUnderline = ac->action(QStringLiteral("annotation_underline"));
    QAction *aSquiggle = ac->action(QStringLiteral("annotation_squiggle"));
    QAction *aStrikeout = ac->action(QStringLiteral("annotation_strike_out"));
    QAction *aTypewriter = ac->action(QStringLiteral("annotation_typewriter"));
    QAction *aInlineNote = ac->action(QStringLiteral("annotation_inline_note"));
    QAction *aPopupNote = ac->action(QStringLiteral("annotation_popup_note"));
    QAction *aFreehandLine = ac->action(QStringLiteral("annotation_freehand_line"));
    QAction *aGeomShapes = ac->action(QStringLiteral("annotation_geometrical_shape"));
    QAction *aStamp = ac->action(QStringLiteral("annotation_stamp"));
    QAction *aAddLatexNote = ac->action(QStringLiteral("annotation_add_latex_note"));

    QFETCH(bool, aQuickToolsEnabled);
    QFETCH(bool, aHighlighterEnabled);
    QFETCH(bool, aUnderlineEnabled);
    QFETCH(bool, aSquiggleEnabled);
    QFETCH(bool, aStrikeoutEnabled);
    QFETCH(bool, aTypewriterEnabled);
    QFETCH(bool, aInlineNoteEnabled);
    QFETCH(bool, aPopupNoteEnabled);
    QFETCH(bool, aFreehandLineEnabled);
    QFETCH(bool, aGeomShapesEnabled);
    QFETCH(bool, aStampEnabled);
    QFETCH(bool, aAddLatexNoteEnabled);

    QCOMPARE(aQuickTools->isEnabled(), aQuickToolsEnabled);
    QCOMPARE(aHighlighter->isEnabled(), aHighlighterEnabled);
    QCOMPARE(aUnderline->isEnabled(), aUnderlineEnabled);
    QCOMPARE(aSquiggle->isEnabled(), aSquiggleEnabled);
    QCOMPARE(aStrikeout->isEnabled(), aStrikeoutEnabled);
    QCOMPARE(aTypewriter->isEnabled(), aTypewriterEnabled);
    QCOMPARE(aInlineNote->isEnabled(), aInlineNoteEnabled);
    QCOMPARE(aPopupNote->isEnabled(), aPopupNoteEnabled);
    QCOMPARE(aFreehandLine->isEnabled(), aFreehandLineEnabled);
    QCOMPARE(aGeomShapes->isEnabled(), aGeomShapesEnabled);
    QCOMPARE(aStamp->isEnabled(), aStampEnabled);
    QCOMPARE(aAddLatexNote->isEnabled(), aAddLatexNoteEnabled);

    // trigger a reparsing of the tools to ensure that the enabled/disabled state is not changed (bug: 424296)
    QAction *aMouseSelect = ac->action(QStringLiteral("mouse_select"));
    QAction *aMouseNormal = ac->action(QStringLiteral("mouse_drag"));
    aMouseSelect->trigger();
    aMouseNormal->trigger();

    QCOMPARE(aQuickTools->isEnabled(), aQuickToolsEnabled);
    QCOMPARE(aHighlighter->isEnabled(), aHighlighterEnabled);
    QCOMPARE(aUnderline->isEnabled(), aUnderlineEnabled);
    QCOMPARE(aSquiggle->isEnabled(), aSquiggleEnabled);
    QCOMPARE(aStrikeout->isEnabled(), aStrikeoutEnabled);
    QCOMPARE(aTypewriter->isEnabled(), aTypewriterEnabled);
    QCOMPARE(aInlineNote->isEnabled(), aInlineNoteEnabled);
    QCOMPARE(aPopupNote->isEnabled(), aPopupNoteEnabled);
    QCOMPARE(aFreehandLine->isEnabled(), aFreehandLineEnabled);
    QCOMPARE(aGeomShapes->isEnabled(), aGeomShapesEnabled);
    QCOMPARE(aStamp->isEnabled(), aStampEnabled);
    QCOMPARE(aAddLatexNote->isEnabled(), aAddLatexNoteEnabled);
}

void AnnotationToolBarTest::testAnnotationToolBarActionsEnabledState_data()
{
    QTest::addColumn<QString>("document");
    QTest::addColumn<bool>("aQuickToolsEnabled");
    QTest::addColumn<bool>("aHighlighterEnabled");
    QTest::addColumn<bool>("aUnderlineEnabled");
    QTest::addColumn<bool>("aSquiggleEnabled");
    QTest::addColumn<bool>("aStrikeoutEnabled");
    QTest::addColumn<bool>("aTypewriterEnabled");
    QTest::addColumn<bool>("aInlineNoteEnabled");
    QTest::addColumn<bool>("aPopupNoteEnabled");
    QTest::addColumn<bool>("aFreehandLineEnabled");
    QTest::addColumn<bool>("aGeomShapesEnabled");
    QTest::addColumn<bool>("aStampEnabled");
    QTest::addColumn<bool>("aAddLatexNoteEnabled");

    QTest::addRow("pdf") << QStringLiteral(KDESRCDIR "data/file1.pdf") << true << true << true << true << true << true << true << true << true << true << true << true;
    QTest::addRow("protected-pdf") << QStringLiteral(KDESRCDIR "data/protected.pdf") << false << false << false << false << false << false << false << false << false << false << false << false;
    QTest::addRow("image") << QStringLiteral(KDESRCDIR "data/potato.jpg") << true << false << false << false << false << true << true << true << true << true << true << true;
}

void AnnotationToolBarTest::testAnnotationToolBarConfigActionsEnabledState()
{
    const QStringList paths = {QStringLiteral(KDESRCDIR "data/file1.pdf")};
    QString serializedOptions = ShellUtils::serializeOptions(false, false, false, false, false, QString(), QString(), QString());

    Okular::Status status = Okular::main(paths, serializedOptions);
    QCOMPARE(status, Okular::Success);
    Shell *s = findShell();
    QVERIFY(s);
    if (qgetenv("KDECI_CANNOT_CREATE_WINDOWS") == "1") {
        QSKIP("KDE CI can't create a window on this platform, skipping some gui tests");
    }

    QVERIFY(QTest::qWaitForWindowExposed(s));

    Okular::Part *part = s->findChild<Okular::Part *>();
    QVERIFY(part);

    KActionCollection *ac = part->actionCollection();
    QAction *aWidth = ac->action(QStringLiteral("annotation_settings_width"));
    QAction *aColor = ac->action(QStringLiteral("annotation_settings_color"));
    QAction *aInnerColor = ac->action(QStringLiteral("annotation_settings_inner_color"));
    QAction *aOpacity = ac->action(QStringLiteral("annotation_settings_opacity"));
    QAction *aFont = ac->action(QStringLiteral("annotation_settings_font"));

    QFETCH(QString, annotationActionName);
    QFETCH(bool, widthEnabled);
    QFETCH(bool, colorEnabled);
    QFETCH(bool, innerColorEnabled);
    QFETCH(bool, opacityEnabled);
    QFETCH(bool, fontEnabled);

    QAction *annotationAction = ac->action(annotationActionName);
    annotationAction->trigger();

    QCOMPARE(aWidth->isEnabled(), widthEnabled);
    QCOMPARE(aColor->isEnabled(), colorEnabled);
    QCOMPARE(aInnerColor->isEnabled(), innerColorEnabled);
    QCOMPARE(aOpacity->isEnabled(), opacityEnabled);
    QCOMPARE(aFont->isEnabled(), fontEnabled);
}

void AnnotationToolBarTest::testAnnotationToolBarConfigActionsEnabledState_data()
{
    QTest::addColumn<QString>("annotationActionName");
    QTest::addColumn<bool>("widthEnabled");
    QTest::addColumn<bool>("colorEnabled");
    QTest::addColumn<bool>("innerColorEnabled");
    QTest::addColumn<bool>("opacityEnabled");
    QTest::addColumn<bool>("fontEnabled");

    QTest::addRow("annotation_highlighter") << QStringLiteral("annotation_highlighter") << false << true << false << true << false;
    QTest::addRow("annotation_underline") << QStringLiteral("annotation_underline") << false << true << false << true << false;
    QTest::addRow("annotation_squiggle") << QStringLiteral("annotation_squiggle") << false << true << false << true << false;
    QTest::addRow("annotation_strike_out") << QStringLiteral("annotation_strike_out") << false << true << false << true << false;
    QTest::addRow("annotation_typewriter") << QStringLiteral("annotation_typewriter") << false << true << false << true << true;
    QTest::addRow("annotation_inline_note") << QStringLiteral("annotation_inline_note") << false << true << false << true << true;
    QTest::addRow("annotation_popup_note") << QStringLiteral("annotation_popup_note") << false << true << false << true << false;
    QTest::addRow("annotation_freehand_line") << QStringLiteral("annotation_freehand_line") << true << true << false << true << false;
    QTest::addRow("annotation_line") << QStringLiteral("annotation_straight_line") << true << true << false << true << false;
    QTest::addRow("annotation_rectangle") << QStringLiteral("annotation_rectangle") << true << true << true << true << false;
}

QTEST_MAIN(AnnotationToolBarTest)
#include "annotationtoolbartest.moc"
