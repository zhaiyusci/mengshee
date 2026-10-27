/*
    SPDX-FileCopyrightText: 2026 Mengshee contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "mengsheeprintdialog.h"

#include "core/printoptionswidget.h"
#include "core/printjob.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsView>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPageRanges>
#include <QPainter>
#include <QPrintPreviewWidget>
#include <QPrinter>
#include <QPrinterInfo>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QtConcurrentRun>
#include <QToolButton>
#include <QVBoxLayout>

#include <KLocalizedString>

#include <utility>

using namespace Okular;

namespace
{
constexpr int previewResolutionDpi = 72;
class PagePreview final : public QWidget {
public:
    using QWidget::QWidget;
    QImage image;
    QPageLayout layout;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), palette().brush(QPalette::Dark));
        const QRectF paper = layout.fullRect(QPageLayout::Point);
        if (paper.isEmpty()) return;
        const QSizeF available = QSizeF(size()) - QSizeF(32, 32);
        if (available.width() <= 0 || available.height() <= 0) return;
        const qreal scale = qMin(available.width() / paper.width(), available.height() / paper.height());
        const QSizeF display = paper.size() * scale;
        const QPointF origin((width() - display.width()) / 2, (height() - display.height()) / 2);
        painter.translate(origin);
        painter.scale(scale, scale);
        painter.fillRect(paper, Qt::white);
        if (image.isNull()) return;
        const QRectF area = layout.paintRect(QPageLayout::Point);
        QSizeF target = image.size();
        target.scale(area.size(), Qt::KeepAspectRatio);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(QRectF(area.center() - QPointF(target.width()/2, target.height()/2), target), image);
    }
};
}

MengsheePrintDialog::MengsheePrintDialog(QPrinter *printer, int documentPageCount, int currentPage, QPageLayout::Orientation documentOrientation, bool hasPageSelection, QWidget *printOptionsWidget, QWidget *parent)
    : QDialog(parent)
    , m_printer(printer)
    , m_documentPageCount(qMax(1, documentPageCount))
    , m_currentDocumentPage(qBound(1, currentPage, qMax(1, documentPageCount)))
    , m_documentOrientation(documentOrientation)
    , m_hasPageSelection(hasPageSelection)
    , m_printOptionsWidget(printOptionsWidget)
{
    if (m_printer) {
        m_initialPrinterName = m_printer->printerName();
        m_nativeOutput = m_printer->outputFormat() == QPrinter::NativeFormat;
        m_printer->setOutputFormat(QPrinter::PdfFormat);
    }

    setWindowTitle(i18nc("@title:window", "Print"));
    setModal(true);
    resize(980, 680);
    setMinimumSize(820, 580);

    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(0);
    connect(m_previewTimer, &QTimer::timeout, this, &MengsheePrintDialog::startPreviewRender);
    m_previewWatcher = new QFutureWatcher<PrintPreviewImage>(this);
    buildUi();
    populatePageSizes();
    populateDuplexOptions();
    populatePrinters();
    connectPrintOptionChanges();
    updatePreviewNavigation();
    validateSettings();
}

void MengsheePrintDialog::buildUi()
{
    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    auto splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    mainLayout->addWidget(splitter, 1);

    auto settingsScroll = new QScrollArea(splitter);
    settingsScroll->setWidgetResizable(true);
    settingsScroll->setFrameShape(QFrame::NoFrame);
    settingsScroll->setMinimumWidth(475);

    auto settings = new QWidget(settingsScroll);
    auto settingsLayout = new QVBoxLayout(settings);
    settingsLayout->setContentsMargins(0, 0, 8, 0);
    settingsLayout->setSpacing(8);
    settingsScroll->setWidget(settings);

    auto printerGroup = new QGroupBox(i18n("Printer"), settings);
    auto printerLayout = new QGridLayout(printerGroup);
    printerLayout->addWidget(new QLabel(i18n("Printer:"), printerGroup), 0, 0);
    m_printerCombo = new QComboBox(printerGroup);
    m_printerCombo->setObjectName(QStringLiteral("printerCombo"));
    printerLayout->addWidget(m_printerCombo, 0, 1, 1, 3);
    printerLayout->addWidget(new QLabel(i18n("Copies:"), printerGroup), 1, 0);
    m_copiesSpin = new QSpinBox(printerGroup);
    m_copiesSpin->setObjectName(QStringLiteral("copiesSpin"));
    m_copiesSpin->setRange(1, 999);
    m_copiesSpin->setValue(1);
    printerLayout->addWidget(m_copiesSpin, 1, 1);
    m_grayscaleCheck = new QCheckBox(i18n("Print in grayscale"), printerGroup);
    m_grayscaleCheck->setObjectName(QStringLiteral("grayscaleCheck"));
    printerLayout->addWidget(m_grayscaleCheck, 1, 2, 1, 2);
    settingsLayout->addWidget(printerGroup);

    auto pagesGroup = new QGroupBox(i18n("Pages to Print"), settings);
    auto pagesLayout = new QGridLayout(pagesGroup);
    m_allPagesRadio = new QRadioButton(i18n("All pages"), pagesGroup);
    m_allPagesRadio->setObjectName(QStringLiteral("allPagesRadio"));
    m_currentPageRadio = new QRadioButton(i18n("Current page"), pagesGroup);
    m_currentPageRadio->setObjectName(QStringLiteral("currentPageRadio"));
    m_rangePagesRadio = new QRadioButton(i18n("Pages:"), pagesGroup);
    m_rangePagesRadio->setObjectName(QStringLiteral("rangePagesRadio"));
    m_pageRangeEdit = new QLineEdit(pagesGroup);
    m_pageRangeEdit->setObjectName(QStringLiteral("pageRangeEdit"));
    m_pageRangeEdit->setText(QStringLiteral("1-%1").arg(m_documentPageCount));
    m_pageRangeEdit->setPlaceholderText(i18n("For example: 1-4, 8, 11-13"));
    m_selectedPagesRadio = new QRadioButton(i18n("Bookmarked pages"), pagesGroup);
    m_selectedPagesRadio->setObjectName(QStringLiteral("selectedPagesRadio"));
    m_selectedPagesRadio->setVisible(m_hasPageSelection);
    m_pageRangeError = new QLabel(i18n("Enter pages between 1 and %1.", m_documentPageCount), pagesGroup);
    m_pageRangeError->setObjectName(QStringLiteral("pageRangeError"));
    m_pageRangeError->setStyleSheet(QStringLiteral("color: #b00020;"));
    m_pageRangeError->hide();
    pagesLayout->addWidget(m_allPagesRadio, 0, 0);
    pagesLayout->addWidget(m_currentPageRadio, 0, 1);
    pagesLayout->addWidget(m_rangePagesRadio, 1, 0);
    pagesLayout->addWidget(m_pageRangeEdit, 1, 1, 1, 2);
    pagesLayout->addWidget(m_selectedPagesRadio, 2, 0, 1, 2);
    pagesLayout->addWidget(m_pageRangeError, 3, 0, 1, 3);
    m_allPagesRadio->setChecked(true);
    m_pageRangeEdit->setEnabled(false);
    settingsLayout->addWidget(pagesGroup);

    auto sizingGroup = new QGroupBox(i18n("Page Sizing && Handling"), settings);
    auto sizingLayout = new QVBoxLayout(sizingGroup);
    auto modeLayout = new QHBoxLayout;
    m_layoutModeGroup = new QButtonGroup(sizingGroup);
    m_layoutModeGroup->setExclusive(true);
    const bool supportsLayouts = m_printOptionsWidget && m_printOptionsWidget->property(PrintLayout::supportsProperty).toBool();
    const QList<QPair<QString, int>> modes = {
        {i18n("Size"), PrintLayout::Size},
        {i18n("Poster"), PrintLayout::Poster},
        {i18n("Multiple"), PrintLayout::Multiple},
        {i18n("Booklet"), PrintLayout::Booklet},
    };
    for (const auto &mode : modes) {
        auto button = new QToolButton(sizingGroup);
        button->setText(mode.first);
        button->setCheckable(true);
        button->setChecked(mode.second == PrintLayout::Size);
        button->setEnabled(mode.second == PrintLayout::Size || supportsLayouts);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_layoutModeGroup->addButton(button, mode.second);
        modeLayout->addWidget(button);
    }
    sizingLayout->addLayout(modeLayout);

    auto paperForm = new QFormLayout;
    m_pageSizeCombo = new QComboBox(sizingGroup);
    m_pageSizeCombo->setObjectName(QStringLiteral("pageSizeCombo"));
    paperForm->addRow(i18n("Paper size:"), m_pageSizeCombo);
    sizingLayout->addLayout(paperForm);

    m_layoutSettingsStack = new QStackedWidget(sizingGroup);
    m_layoutSettingsStack->setObjectName(QStringLiteral("layoutSettingsStack"));

    auto sizeSettings = new QWidget(m_layoutSettingsStack);
    auto sizeSettingsLayout = new QVBoxLayout(sizeSettings);
    sizeSettingsLayout->setContentsMargins(0, 0, 0, 0);
    sizeSettingsLayout->addWidget(new QLabel(i18n("Print one document page on each sheet."), sizeSettings));
    m_layoutSettingsStack->addWidget(sizeSettings);

    auto posterSettings = new QWidget(m_layoutSettingsStack);
    auto posterForm = new QFormLayout(posterSettings);
    posterForm->setContentsMargins(0, 0, 0, 0);
    m_posterScaleSpin = new QDoubleSpinBox(posterSettings);
    m_posterScaleSpin->setObjectName(QStringLiteral("posterScaleSpin"));
    m_posterScaleSpin->setRange(10.0, 1000.0);
    m_posterScaleSpin->setDecimals(0);
    m_posterScaleSpin->setSuffix(i18nc("Percentage suffix", "%"));
    m_posterScaleSpin->setValue(100.0);
    posterForm->addRow(i18n("Tile scale:"), m_posterScaleSpin);
    m_posterOverlapSpin = new QDoubleSpinBox(posterSettings);
    m_posterOverlapSpin->setObjectName(QStringLiteral("posterOverlapSpin"));
    m_posterOverlapSpin->setRange(0.0, 50.0);
    m_posterOverlapSpin->setDecimals(1);
    m_posterOverlapSpin->setSuffix(i18nc("Millimeter suffix", " mm"));
    m_posterOverlapSpin->setValue(5.0);
    posterForm->addRow(i18n("Overlap:"), m_posterOverlapSpin);
    m_posterCutMarksCheck = new QCheckBox(i18n("Cut marks"), posterSettings);
    posterForm->addRow(QString(), m_posterCutMarksCheck);
    m_layoutSettingsStack->addWidget(posterSettings);

    auto multipleSettings = new QWidget(m_layoutSettingsStack);
    auto multipleForm = new QFormLayout(multipleSettings);
    multipleForm->setContentsMargins(0, 0, 0, 0);
    m_pagesPerSheetCombo = new QComboBox(multipleSettings);
    m_pagesPerSheetCombo->setObjectName(QStringLiteral("pagesPerSheetCombo"));
    for (const int pagesPerSheet : {2, 4, 6, 9, 16}) {
        m_pagesPerSheetCombo->addItem(i18np("%1 page", "%1 pages", pagesPerSheet), pagesPerSheet);
    }
    multipleForm->addRow(i18n("Pages per sheet:"), m_pagesPerSheetCombo);
    m_pageOrderCombo = new QComboBox(multipleSettings);
    m_pageOrderCombo->setObjectName(QStringLiteral("pageOrderCombo"));
    m_pageOrderCombo->addItem(i18n("Horizontal"), PrintLayout::Horizontal);
    m_pageOrderCombo->addItem(i18n("Horizontal reversed"), PrintLayout::HorizontalReversed);
    m_pageOrderCombo->addItem(i18n("Vertical"), PrintLayout::Vertical);
    m_pageOrderCombo->addItem(i18n("Vertical reversed"), PrintLayout::VerticalReversed);
    multipleForm->addRow(i18n("Page order:"), m_pageOrderCombo);
    m_pageBorderCheck = new QCheckBox(i18n("Print page border"), multipleSettings);
    multipleForm->addRow(QString(), m_pageBorderCheck);
    m_layoutSettingsStack->addWidget(multipleSettings);

    auto bookletSettings = new QWidget(m_layoutSettingsStack);
    auto bookletForm = new QFormLayout(bookletSettings);
    bookletForm->setContentsMargins(0, 0, 0, 0);
    m_bookletSubsetCombo = new QComboBox(bookletSettings);
    m_bookletSubsetCombo->setObjectName(QStringLiteral("bookletSubsetCombo"));
    m_bookletSubsetCombo->addItem(i18n("Both sides"), PrintLayout::BothSides);
    m_bookletSubsetCombo->addItem(i18n("Front sides only"), PrintLayout::FrontSides);
    m_bookletSubsetCombo->addItem(i18n("Back sides only"), PrintLayout::BackSides);
    bookletForm->addRow(i18n("Booklet subset:"), m_bookletSubsetCombo);
    m_bookletBindingCombo = new QComboBox(bookletSettings);
    m_bookletBindingCombo->setObjectName(QStringLiteral("bookletBindingCombo"));
    m_bookletBindingCombo->addItem(i18nc("Booklet binding side", "Left"), PrintLayout::BindLeft);
    m_bookletBindingCombo->addItem(i18nc("Booklet binding side", "Right"), PrintLayout::BindRight);
    bookletForm->addRow(i18n("Booklet binding:"), m_bookletBindingCombo);
    m_layoutSettingsStack->addWidget(bookletSettings);

    sizingLayout->addWidget(m_layoutSettingsStack);

    if (m_printOptionsWidget) {
        m_printOptionsWidget->setParent(sizingGroup);
        m_printOptionsWidget->show();
        sizingLayout->addWidget(m_printOptionsWidget);
    }
    syncLayoutProperties(false);
    settingsLayout->addWidget(sizingGroup);

    auto finishingGroup = new QGroupBox(settings);
    auto finishingLayout = new QVBoxLayout(finishingGroup);
    m_duplexCheck = new QCheckBox(i18n("Print on both sides"), finishingGroup);
    m_duplexCheck->setObjectName(QStringLiteral("duplexCheck"));
    finishingLayout->addWidget(m_duplexCheck);

    auto duplexBindingLayout = new QHBoxLayout;
    duplexBindingLayout->addWidget(new QLabel(i18n("Binding:"), finishingGroup));
    m_duplexBindingCombo = new QComboBox(finishingGroup);
    m_duplexBindingCombo->setObjectName(QStringLiteral("duplexBindingCombo"));
    m_duplexBindingCombo->addItem(i18n("Flip on long edge"), QPrinter::DuplexLongSide);
    m_duplexBindingCombo->addItem(i18n("Flip on short edge"), QPrinter::DuplexShortSide);
    m_duplexBindingCombo->setEnabled(false);
    duplexBindingLayout->addWidget(m_duplexBindingCombo, 1);
    finishingLayout->addLayout(duplexBindingLayout);

    auto orientationLayout = new QHBoxLayout;
    orientationLayout->addWidget(new QLabel(i18n("Orientation:"), finishingGroup));
    m_autoOrientationRadio = new QRadioButton(i18n("Auto"), finishingGroup);
    m_autoOrientationRadio->setObjectName(QStringLiteral("autoOrientationRadio"));
    m_portraitRadio = new QRadioButton(i18n("Portrait"), finishingGroup);
    m_portraitRadio->setObjectName(QStringLiteral("portraitRadio"));
    m_landscapeRadio = new QRadioButton(i18n("Landscape"), finishingGroup);
    m_landscapeRadio->setObjectName(QStringLiteral("landscapeRadio"));
    orientationLayout->addWidget(m_autoOrientationRadio);
    orientationLayout->addWidget(m_portraitRadio);
    orientationLayout->addWidget(m_landscapeRadio);
    orientationLayout->addStretch(1);
    m_autoOrientationRadio->setChecked(true);
    finishingLayout->addLayout(orientationLayout);
    settingsLayout->addWidget(finishingGroup);
    settingsLayout->addStretch(1);

    auto previewPanel = new QWidget(splitter);
    previewPanel->setMinimumWidth(320);
    auto previewLayout = new QVBoxLayout(previewPanel);
    previewLayout->setContentsMargins(8, 0, 0, 0);
    previewLayout->setSpacing(8);
    m_previewDescription = new QLabel(previewPanel);
    m_previewDescription->setObjectName(QStringLiteral("previewDescription"));
    m_previewDescription->setAlignment(Qt::AlignCenter);
    previewLayout->addWidget(m_previewDescription);

    if (!m_printer) {
        m_previewCanvas = new PagePreview(previewPanel);
        m_previewCanvas->setObjectName(QStringLiteral("printPreview"));
        previewLayout->addWidget(m_previewCanvas, 1);
    } else {
    // Use QPrintPreviewWidget's internally owned printer. Keeping the printer
    // as a dialog member would destroy it before QDialog destroys its child
    // widgets, leaving the preview widget with a dangling printer on close.
    m_previewWidget = new QPrintPreviewWidget(previewPanel);
    m_previewWidget->setObjectName(QStringLiteral("printPreview"));
    m_previewWidget->setZoomMode(QPrintPreviewWidget::FitInView);
    m_previewWidget->setViewMode(QPrintPreviewWidget::SinglePageView);
    // QPrintPreviewWidget replays each preview page through an internal
    // QGraphicsView. Its default render hints omit smooth image scaling, so
    // high-resolution raster pages still look jagged after FitInView scales
    // them down to the preview pane.
    if (QGraphicsView *previewView = m_previewWidget->findChild<QGraphicsView *>()) {
        previewView->setRenderHints(previewView->renderHints() | QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    }
    previewLayout->addWidget(m_previewWidget, 1);

    }

    auto navigationLayout = new QHBoxLayout;
    m_previousPageButton = new QPushButton(previewPanel);
    m_previousPageButton->setObjectName(QStringLiteral("previousPreviewPage"));
    m_previousPageButton->setIcon(style()->standardIcon(QStyle::SP_ArrowLeft));
    m_previousPageButton->setToolTip(i18n("Previous sheet"));
    m_nextPageButton = new QPushButton(previewPanel);
    m_nextPageButton->setObjectName(QStringLiteral("nextPreviewPage"));
    m_nextPageButton->setIcon(style()->standardIcon(QStyle::SP_ArrowRight));
    m_nextPageButton->setToolTip(i18n("Next sheet"));
    m_pageSlider = new QSlider(Qt::Horizontal, previewPanel);
    m_pageSlider->setObjectName(QStringLiteral("previewPageSlider"));
    m_pageSlider->setRange(1, 1);
    m_pageSlider->setValue(1);
    navigationLayout->addWidget(m_previousPageButton);
    navigationLayout->addWidget(m_pageSlider, 1);
    navigationLayout->addWidget(m_nextPageButton);
    previewLayout->addLayout(navigationLayout);
    m_pageNumberLabel = new QLabel(previewPanel);
    m_pageNumberLabel->setObjectName(QStringLiteral("previewPageNumber"));
    m_pageNumberLabel->setAlignment(Qt::AlignCenter);
    previewLayout->addWidget(m_pageNumberLabel);

    splitter->addWidget(settingsScroll);
    splitter->addWidget(previewPanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({570, 390});

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttonBox->setObjectName(QStringLiteral("printDialogButtons"));
    m_buttonBox->button(QDialogButtonBox::Ok)->setText(i18n("Print"));
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &MengsheePrintDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(m_buttonBox);

    connect(m_printerCombo, &QComboBox::currentIndexChanged, this, &MengsheePrintDialog::printerChanged);
    // Copies affect the print job only; accept() reads the current value.
    connect(m_grayscaleCheck, &QCheckBox::toggled, this, &MengsheePrintDialog::schedulePreviewUpdate);
    connect(m_duplexCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_duplexBindingCombo->setEnabled(checked && m_duplexCheck->isEnabled());
        schedulePreviewUpdate();
    });
    connect(m_duplexBindingCombo, &QComboBox::currentIndexChanged, this, &MengsheePrintDialog::schedulePreviewUpdate);
    connect(m_pageSizeCombo, &QComboBox::currentIndexChanged, this, &MengsheePrintDialog::pageSizeChanged);
    connect(m_layoutModeGroup, &QButtonGroup::idClicked, this, &MengsheePrintDialog::layoutModeChanged);
    connect(m_posterScaleSpin, &QDoubleSpinBox::valueChanged, this, &MengsheePrintDialog::layoutSettingsChanged);
    connect(m_posterOverlapSpin, &QDoubleSpinBox::valueChanged, this, &MengsheePrintDialog::layoutSettingsChanged);
    connect(m_posterCutMarksCheck, &QCheckBox::toggled, this, &MengsheePrintDialog::layoutSettingsChanged);
    connect(m_pagesPerSheetCombo, &QComboBox::currentIndexChanged, this, &MengsheePrintDialog::layoutSettingsChanged);
    connect(m_pageOrderCombo, &QComboBox::currentIndexChanged, this, &MengsheePrintDialog::layoutSettingsChanged);
    connect(m_pageBorderCheck, &QCheckBox::toggled, this, &MengsheePrintDialog::layoutSettingsChanged);
    connect(m_bookletSubsetCombo, &QComboBox::currentIndexChanged, this, &MengsheePrintDialog::layoutSettingsChanged);
    connect(m_bookletBindingCombo, &QComboBox::currentIndexChanged, this, &MengsheePrintDialog::layoutSettingsChanged);
    connect(m_allPagesRadio, &QRadioButton::toggled, this, &MengsheePrintDialog::printRangeChanged);
    connect(m_currentPageRadio, &QRadioButton::toggled, this, &MengsheePrintDialog::printRangeChanged);
    connect(m_rangePagesRadio, &QRadioButton::toggled, this, &MengsheePrintDialog::printRangeChanged);
    connect(m_selectedPagesRadio, &QRadioButton::toggled, this, &MengsheePrintDialog::printRangeChanged);
    connect(m_pageRangeEdit, &QLineEdit::textChanged, this, &MengsheePrintDialog::printRangeChanged);
    connect(m_autoOrientationRadio, &QRadioButton::toggled, this, &MengsheePrintDialog::orientationChanged);
    connect(m_portraitRadio, &QRadioButton::toggled, this, &MengsheePrintDialog::orientationChanged);
    connect(m_landscapeRadio, &QRadioButton::toggled, this, &MengsheePrintDialog::orientationChanged);
    connect(m_pageSlider, &QSlider::valueChanged, this, &MengsheePrintDialog::previewPageChanged);
    connect(m_previousPageButton, &QPushButton::clicked, this, &MengsheePrintDialog::previousPreviewPage);
    connect(m_nextPageButton, &QPushButton::clicked, this, &MengsheePrintDialog::nextPreviewPage);
    if (m_previewWidget) connect(m_previewWidget, &QPrintPreviewWidget::paintRequested, this, &MengsheePrintDialog::paintPreview);
}

void MengsheePrintDialog::populatePrinters()
{
    m_printerCombo->setEnabled(false);
    auto *watcher = new QFutureWatcher<QPair<QStringList, QString>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        const auto result = watcher->result();
        watcher->deleteLater();
        {
            const QSignalBlocker blocker(m_printerCombo);
            m_printerCombo->clear();
            for (const auto &name : result.first)
                m_printerCombo->addItem(name == result.second ? i18nc("Printer name followed by its status", "%1 (Default)", name) : name, name);
            const int index = m_printerCombo->findData(m_initialPrinterName.isEmpty() ? result.second : m_initialPrinterName);
            if (index >= 0) m_printerCombo->setCurrentIndex(index);
        }
        m_printerCombo->setEnabled(true);
        printerChanged();
    });
    watcher->setFuture(QtConcurrent::run([] {
        return qMakePair(QPrinterInfo::availablePrinterNames(), QPrinterInfo::defaultPrinterName());
    }));
}

void MengsheePrintDialog::printerChanged()
{
    const QString name = m_printerCombo->currentData().toString();
    const auto revision = ++m_printerRevision;
    m_printerReady = false;
    validateSettings();
    if (name.isEmpty()) return;
    const auto apply = [this, revision](const PrinterCapabilities &capabilities) {
        if (revision != m_printerRevision) return;
        m_capabilities = capabilities;
        populatePageSizes();
        populateDuplexOptions();
        const bool supportsColor = capabilities.colors.contains(QPrinter::Color);
        m_grayscaleCheck->setEnabled(supportsColor);
        {
            const QSignalBlocker blocker(m_grayscaleCheck);
            m_grayscaleCheck->setChecked(!supportsColor || (m_printer && m_printer->colorMode() == QPrinter::GrayScale));
        }
        m_printerReady = true;
        validateSettings();
        schedulePreviewUpdate();
    };
    if (m_printerCapabilities.contains(name)) {
        apply(m_printerCapabilities.value(name));
        return;
    }
    auto *watcher = new QFutureWatcher<PrinterCapabilities>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, name, apply] {
        const auto capabilities = watcher->result();
        watcher->deleteLater();
        m_printerCapabilities.insert(name, capabilities);
        apply(capabilities);
    });
    watcher->setFuture(QtConcurrent::run([name] {
        const auto info = QPrinterInfo::printerInfo(name);
        QPrinter printer(info);
        return PrinterCapabilities{info.supportedPageSizes(), info.supportedDuplexModes(), info.supportedColorModes(), info.defaultPageSize(), printer.pageLayout().margins(QPageLayout::Millimeter)};
    }));
}

void MengsheePrintDialog::populatePageSizes()
{
    const QString oldKey = selectedPageSize().key();
    QList<QPageSize> sizes = m_capabilities.sizes;
    if (sizes.isEmpty()) {
        sizes << QPageSize(QPageSize::A4) << QPageSize(QPageSize::Letter);
    }

    const QSignalBlocker blocker(m_pageSizeCombo);
    m_pageSizes = sizes;
    m_pageSizeCombo->clear();
    for (const QPageSize &pageSize : std::as_const(m_pageSizes)) {
        m_pageSizeCombo->addItem(pageSizeDescription(pageSize));
    }

    QString preferredKey = oldKey;
    if (preferredKey.isEmpty()) {
        if (m_printer) preferredKey = m_printer->pageLayout().pageSize().key();
    }
    if (preferredKey.isEmpty()) {
        preferredKey = m_capabilities.defaultSize.key();
    }

    int preferredIndex = 0;
    for (int i = 0; i < m_pageSizes.size(); ++i) {
        if (m_pageSizes.at(i).key() == preferredKey) {
            preferredIndex = i;
            break;
        }
    }
    m_pageSizeCombo->setCurrentIndex(preferredIndex);
}

void MengsheePrintDialog::populateDuplexOptions()
{
    const QList<QPrinter::DuplexMode> modes = m_capabilities.duplex;
    const bool canDuplex = modes.contains(QPrinter::DuplexAuto) || modes.contains(QPrinter::DuplexLongSide) || modes.contains(QPrinter::DuplexShortSide);

    const QPrinter::DuplexMode currentMode = m_printer ? m_printer->duplex() : QPrinter::DuplexNone;
    const int preferredIndex = m_duplexBindingCombo->findData(currentMode == QPrinter::DuplexShortSide ? QPrinter::DuplexShortSide : QPrinter::DuplexLongSide);
    if (preferredIndex >= 0) {
        const QSignalBlocker blocker(m_duplexBindingCombo);
        m_duplexBindingCombo->setCurrentIndex(preferredIndex);
    }

    m_duplexCheck->setEnabled(canDuplex);
    {
        const QSignalBlocker blocker(m_duplexCheck);
        m_duplexCheck->setChecked(canDuplex && currentMode != QPrinter::DuplexNone);
    }
    m_duplexBindingCombo->setEnabled(canDuplex && m_duplexCheck->isChecked());
}

void MengsheePrintDialog::connectPrintOptionChanges()
{
    if (!m_printOptionsWidget) {
        return;
    }

    const auto comboBoxes = m_printOptionsWidget->findChildren<QComboBox *>();
    for (QComboBox *comboBox : comboBoxes) {
        connect(comboBox, &QComboBox::currentIndexChanged, this, &MengsheePrintDialog::schedulePreviewUpdate);
    }
    const auto checkBoxes = m_printOptionsWidget->findChildren<QCheckBox *>();
    for (QCheckBox *checkBox : checkBoxes) {
        connect(checkBox, &QCheckBox::toggled, this, &MengsheePrintDialog::schedulePreviewUpdate);
    }
    const auto spinBoxes = m_printOptionsWidget->findChildren<QSpinBox *>();
    for (QSpinBox *spinBox : spinBoxes) {
        connect(spinBox, &QSpinBox::valueChanged, this, &MengsheePrintDialog::schedulePreviewUpdate);
    }
}

void MengsheePrintDialog::printRangeChanged()
{
    m_pageRangeEdit->setEnabled(m_rangePagesRadio->isChecked());
    resetPreviewSheet();
    validateSettings();
    schedulePreviewUpdate();
}

void MengsheePrintDialog::pageSizeChanged()
{
    // Apply the paper immediately so QPrintPreviewWidget rebuilds its page
    // frame with the new physical dimensions, rather than only changing the
    // descriptive label above it.
    if (m_printer) applyCommonSettings(*m_printer);
    resetPreviewSheet();
    updatePreviewNavigation();
    schedulePreviewUpdate();
}

void MengsheePrintDialog::orientationChanged()
{
    resetPreviewSheet();
    schedulePreviewUpdate();
}

void MengsheePrintDialog::layoutModeChanged(int mode)
{
    m_layoutSettingsStack->setCurrentIndex(qBound(static_cast<int>(PrintLayout::Size), mode, static_cast<int>(PrintLayout::Booklet)));
    resetPreviewSheet();
    syncLayoutProperties(false);
    schedulePreviewUpdate();
}

void MengsheePrintDialog::layoutSettingsChanged()
{
    resetPreviewSheet();
    syncLayoutProperties(false);
    schedulePreviewUpdate();
}

void MengsheePrintDialog::setPreviewRenderer(PrintPreviewRenderer renderer, const QString &selectedPages, const QString &error)
{
    m_previewRenderer = std::move(renderer);
    m_selectedPages = selectedPages;
    m_previewError = error;
    m_asyncPreview = bool(m_previewRenderer) || !error.isEmpty();
    schedulePreviewUpdate();
}

void MengsheePrintDialog::schedulePreviewUpdate()
{
    ++m_previewRevision; // Coalesce this event-loop turn without a fixed delay.
    m_previewTimer->start();
}

void MengsheePrintDialog::paintPreview(QPrinter *printer)
{
    printer->setOutputFormat(QPrinter::PdfFormat);
    printer->setResolution(previewResolutionDpi);
    printer->setDocName(m_printer->docName());
    applyCommonSettings(*printer);
    printer->setCopyCount(1);
    printer->setPageLayout(m_printer->pageLayout());
    if (!m_asyncPreview) {
        applyPrintRange(*printer, true);
        syncLayoutProperties(true);
        Q_EMIT paintRequested(printer);
        syncLayoutProperties(false);
        return;
    }
    // Replaying the ready bitmap is the only preview painting done on the UI
    // thread. No document rendering or live QPrinter access happens in a worker.
    QPainter painter(printer);
    if (!m_previewImage.isNull()) {
        const auto image = m_grayscaleCheck->isChecked() ? m_previewImage.convertToFormat(QImage::Format_Grayscale8) : m_previewImage;
        const QRect area = printer->pageLayout().paintRectPixels(printer->resolution());
        QSize size = image.size();
        size.scale(area.size(), Qt::KeepAspectRatio);
        painter.drawImage(QRect(area.center() - QPoint(size.width() / 2, size.height() / 2), size), image);
    }
}

void MengsheePrintDialog::startPreviewRender()
{
    if (m_printer) applyCommonSettings(*m_printer);
    if (!m_asyncPreview) {
        if (!m_previewWidget) return;
        syncLayoutProperties(false);
        refreshPreview();
        if (m_printOptionsWidget)
            m_previewSheetCount = qMax(1, m_printOptionsWidget->property(PrintLayout::previewSheetCountProperty).toInt());
        updatePreviewNavigation();
        return;
    }
    if (!m_previewRenderer) {
        m_previewDescription->setText(m_previewError);
        return;
    }
    if (m_rangePagesRadio->isChecked() && !pageRangeIsValid()) {
        m_previewImage = {};
        refreshPreview();
        return;
    }
    QPageRanges ranges;
    if (m_currentPageRadio->isChecked()) ranges.addPage(m_currentDocumentPage);
    else if (m_rangePagesRadio->isChecked()) ranges = QPageRanges::fromString(m_pageRangeEdit->text());
    else if (m_selectedPagesRadio->isChecked()) ranges = QPageRanges::fromString(m_selectedPages);
    else ranges.addRange(1, m_documentPageCount);
    QList<int> pages;
    for (const auto &range : ranges.toRangeList())
        for (int page = qMax(1, range.from); page <= qMin(m_documentPageCount, range.to); ++page) pages.append(page - 1);
    m_previewSheetCount = qMax(1, pages.size());
    updatePreviewNavigation();
    if (pages.isEmpty()) return;
    int page = pages[m_printer && m_printer->pageOrder() == QPrinter::LastPageFirst ? pages.size() - m_previewSheet : m_previewSheet - 1];
    // The renderer caps resolution at 72 DPI. A stable bounded size lets
    // layout and dialog-size changes reuse the page instead of rerasterizing.
    const QSize pixels(2048, 2048);
    auto cacheKey = [pixels](int index) { return QStringLiteral("%1:%2:%3").arg(index).arg(pixels.width()).arg(pixels.height()); };
    QString key = cacheKey(page);
    bool prefetch = false;
    if (const auto *cached = m_previewCache.object(key)) {
        m_previewImage = *cached;
        refreshPreview();
        if (m_previewRunning) return;
        // Only a bounded neighborhood, never the entire document. Current
        // requests take priority when the single in-flight job completes.
        const int position = pages.indexOf(page);
        for (int offset : {1, -1}) {
            const int next = position + offset;
            if (next < 0 || next >= pages.size()) continue;
            const auto candidate = cacheKey(pages[next]);
            if (m_previewCache.contains(candidate)) continue;
            page = pages[next];
            key = candidate;
            prefetch = true;
            break;
        }
        if (!prefetch) return;
    }
    if (!prefetch) {
        m_previewDescription->setText(i18n("Generating preview…"));
        m_previewImage = {};
        refreshPreview();
    }
    if (m_previewRunning) return; // Only one job; the newest request follows it.
    m_previewRunning = true;
    m_runningRevision = m_previewRevision;
    disconnect(m_previewWatcher, nullptr, this, nullptr);
    connect(m_previewWatcher, &QFutureWatcher<PrintPreviewImage>::finished, this, [this, key, prefetch]() {
        m_previewRunning = false;
        const auto result = m_previewWatcher->result();
        if (!result.image.isNull())
            m_previewCache.insert(key, new QImage(result.image), qMax(1, int(result.image.sizeInBytes() / 1024)));
        if (m_runningRevision != m_previewRevision) {
            if (!m_previewTimer->isActive()) m_previewTimer->start(0);
            return;
        }
        if (!result.image.isNull()) QTimer::singleShot(0, this, &MengsheePrintDialog::startPreviewRender);
        if (prefetch) return;
        m_previewImage = result.image;
        refreshPreview();
        updatePreviewNavigation();
        if (!result.error.isEmpty()) m_previewDescription->setText(result.error);
    });
    // Values only: closing the dialog disconnects completion without waiting for
    // a render or exposing widgets/the live document to the worker.
    const auto renderer = m_previewRenderer;
    m_previewWatcher->setFuture(QtConcurrent::run([renderer, page, pixels]() { return renderer(page, pixels); }));
}

void MengsheePrintDialog::previewPageChanged(int page)
{
    m_previewSheet = qBound(1, page, m_previewSheetCount);
    ++m_previewRevision;
    m_previewTimer->stop();
    updatePreviewNavigation();
    startPreviewRender();
}

void MengsheePrintDialog::previousPreviewPage()
{
    m_pageSlider->setValue(qMax(1, m_previewSheet - 1));
}

void MengsheePrintDialog::nextPreviewPage()
{
    m_pageSlider->setValue(qMin(m_previewSheetCount, m_previewSheet + 1));
}

void MengsheePrintDialog::updatePreviewNavigation()
{
    m_previewSheet = qBound(1, m_previewSheet, qMax(1, m_previewSheetCount));
    {
        const QSignalBlocker blocker(m_pageSlider);
        m_pageSlider->setRange(1, qMax(1, m_previewSheetCount));
        m_pageSlider->setValue(m_previewSheet);
    }
    m_previousPageButton->setEnabled(m_previewSheet > 1);
    m_nextPageButton->setEnabled(m_previewSheet < m_previewSheetCount);
    m_pageNumberLabel->setText(i18n("Sheet %1 of %2", m_previewSheet, m_previewSheetCount));

    const QPageSize pageSize = selectedPageSize();
    const QString orientation = selectedOrientation() == QPageLayout::Landscape ? i18n("Landscape") : i18n("Portrait");
    m_previewDescription->setText(i18nc("Paper size and page orientation shown above print preview", "%1 · %2", pageSizeDescription(pageSize), orientation));
}

void MengsheePrintDialog::validateSettings()
{
    const bool validRange = !m_rangePagesRadio->isChecked() || pageRangeIsValid();
    m_pageRangeError->setVisible(!validRange);

    const bool hasPrinter = m_printerCombo->currentIndex() >= 0 && !m_printerCombo->currentData().toString().isEmpty();
    // Keep validity separate from the QDialogButtonBox button. On Qt's Win32
    // backend, changing the active QPrinter can invalidate QWidget state used
    // by setEnabled() while the combo-box popup is being dismissed.
    m_settingsValid = validRange && hasPrinter && m_printerReady;
}

void MengsheePrintDialog::accept()
{
    validateSettings();
    if (!m_settingsValid) {
        if (!pageRangeIsValid()) {
            m_pageRangeEdit->setFocus();
        }
        return;
    }

    if (m_printer) configurePrinter(*m_printer);
    syncLayoutProperties(false);
    QDialog::accept();
}

void MengsheePrintDialog::configurePrinter(QPrinter &printer)
{
    if (auto *job = dynamic_cast<PrintJobPrinter *>(&printer))
        job->automaticPageOrientation = m_autoOrientationRadio->isChecked();
    if (!m_printer || m_nativeOutput) {
        printer.setOutputFormat(QPrinter::NativeFormat);
        printer.setPrinterName(m_printerCombo->currentData().toString());
    }
    applyCommonSettings(printer);
    applyPrintRange(printer, false);
}

void MengsheePrintDialog::refreshPreview()
{
    if (!m_previewCanvas) {
        if (m_previewWidget) m_previewWidget->updatePreview();
        return;
    }
    updatePreviewNavigation();
    auto *canvas = static_cast<PagePreview *>(m_previewCanvas);
    canvas->image = m_grayscaleCheck->isChecked() ? m_previewImage.convertToFormat(QImage::Format_Grayscale8) : m_previewImage;
    canvas->layout = QPageLayout(selectedPageSize(), selectedOrientation(), m_capabilities.margins, QPageLayout::Millimeter);
    if (auto options = qobject_cast<PrintOptionsWidget *>(m_printOptionsWidget); options && options->ignorePrintMargins())
        canvas->layout.setMode(QPageLayout::FullPageMode);
    canvas->update();
}

void MengsheePrintDialog::applyCommonSettings(QPrinter &printer)
{
    const QPageSize pageSize = selectedPageSize();
    if (pageSize.isValid()) {
        printer.setPageSize(pageSize);
    }
    printer.setPageOrientation(selectedOrientation());
    printer.setCopyCount(m_copiesSpin->value());
    printer.setColorMode(m_grayscaleCheck->isChecked() ? QPrinter::GrayScale : QPrinter::Color);

    printer.setDuplex(m_duplexCheck->isChecked() ? selectedDuplexMode() : QPrinter::DuplexNone);

    if (auto options = qobject_cast<PrintOptionsWidget *>(m_printOptionsWidget)) {
        printer.setFullPage(options->ignorePrintMargins());
    }
}

void MengsheePrintDialog::applyPrintRange(QPrinter &printer, bool preview)
{
    Q_UNUSED(preview)
    QPageRanges ranges;
    if (m_currentPageRadio->isChecked()) {
        ranges.addPage(m_currentDocumentPage);
        printer.setPrintRange(QPrinter::PageRange);
        printer.setFromTo(m_currentDocumentPage, m_currentDocumentPage);
        printer.setPageRanges(ranges);
    } else if (m_rangePagesRadio->isChecked()) {
        ranges = QPageRanges::fromString(m_pageRangeEdit->text());
        printer.setPrintRange(QPrinter::PageRange);
        printer.setFromTo(ranges.firstPage(), ranges.lastPage());
        printer.setPageRanges(ranges);
    } else if (m_selectedPagesRadio->isChecked()) {
        printer.setPageRanges(QPageRanges());
        printer.setPrintRange(QPrinter::Selection);
    } else {
        printer.setPageRanges(QPageRanges());
        printer.setPrintRange(QPrinter::AllPages);
        printer.setFromTo(0, 0);
    }
}

void MengsheePrintDialog::syncLayoutProperties(bool preview)
{
    if (!m_printOptionsWidget) {
        return;
    }

    const int mode = m_layoutModeGroup && m_layoutModeGroup->checkedId() >= 0 ? m_layoutModeGroup->checkedId() : PrintLayout::Size;
    m_printOptionsWidget->setProperty(PrintLayout::modeProperty, mode);
    m_printOptionsWidget->setProperty(PrintLayout::pagesPerSheetProperty, m_pagesPerSheetCombo->currentData());
    m_printOptionsWidget->setProperty(PrintLayout::pageOrderProperty, m_pageOrderCombo->currentData());
    m_printOptionsWidget->setProperty(PrintLayout::pageBorderProperty, m_pageBorderCheck->isChecked());
    m_printOptionsWidget->setProperty(PrintLayout::posterScaleProperty, m_posterScaleSpin->value());
    m_printOptionsWidget->setProperty(PrintLayout::posterOverlapProperty, m_posterOverlapSpin->value());
    m_printOptionsWidget->setProperty(PrintLayout::posterCutMarksProperty, m_posterCutMarksCheck->isChecked());
    m_printOptionsWidget->setProperty(PrintLayout::bookletSubsetProperty, m_bookletSubsetCombo->currentData());
    m_printOptionsWidget->setProperty(PrintLayout::bookletBindingProperty, m_bookletBindingCombo->currentData());
    m_printOptionsWidget->setProperty(PrintLayout::previewProperty, preview);
    m_printOptionsWidget->setProperty(PrintLayout::previewSheetProperty, qMax(0, m_previewSheet - 1));
    QSize previewViewport;
    if (m_previewWidget) {
        // Leave room for QPrintPreviewWidget's gray surround and page shadow.
        // The generator uses this to prepare a raster close to the final screen
        // size instead of asking the view to shrink a 300-DPI page in one step.
        const qreal deviceScale = m_previewWidget->devicePixelRatioF();
        previewViewport = QSize(qMax(64, qRound((m_previewWidget->width() - 40) * deviceScale)),
                                qMax(64, qRound((m_previewWidget->height() - 60) * deviceScale)));
    }
    m_printOptionsWidget->setProperty(PrintLayout::previewViewportProperty, previewViewport);
}

void MengsheePrintDialog::resetPreviewSheet()
{
    m_previewSheet = 1;
    m_previewSheetCount = 1;
    if (m_pageSlider) {
        const QSignalBlocker blocker(m_pageSlider);
        m_pageSlider->setRange(1, 1);
        m_pageSlider->setValue(1);
    }
}

bool MengsheePrintDialog::pageRangeIsValid() const
{
    const QPageRanges ranges = QPageRanges::fromString(m_pageRangeEdit->text().trimmed());
    if (ranges.isEmpty()) {
        return false;
    }
    for (const QPageRanges::Range &range : ranges.toRangeList()) {
        if (range.from < 1 || range.to > m_documentPageCount || range.from > range.to) {
            return false;
        }
    }
    return true;
}

QPageLayout::Orientation MengsheePrintDialog::selectedOrientation() const
{
    if (m_landscapeRadio->isChecked()) {
        return QPageLayout::Landscape;
    }
    if (m_portraitRadio->isChecked()) {
        return QPageLayout::Portrait;
    }
    return m_previewImage.isNull() ? m_documentOrientation : paperOrientationForPage(m_previewImage.size());
}

QPrinter::DuplexMode MengsheePrintDialog::selectedDuplexMode() const
{
    if (!m_duplexBindingCombo) {
        return QPrinter::DuplexLongSide;
    }

    const QPrinter::DuplexMode mode = m_duplexBindingCombo->currentData().value<QPrinter::DuplexMode>();
    return mode == QPrinter::DuplexShortSide ? QPrinter::DuplexShortSide : QPrinter::DuplexLongSide;
}

QPageSize MengsheePrintDialog::selectedPageSize() const
{
    const int index = m_pageSizeCombo ? m_pageSizeCombo->currentIndex() : -1;
    return index >= 0 && index < m_pageSizes.size() ? m_pageSizes.at(index) : QPageSize();
}

QString MengsheePrintDialog::pageSizeDescription(const QPageSize &pageSize) const
{
    if (!pageSize.isValid()) {
        return i18n("Unknown paper size");
    }
    const QSizeF millimeters = pageSize.size(QPageSize::Millimeter);
    return i18nc("Paper name followed by width and height in millimeters", "%1 (%2 × %3 mm)", pageSize.name(), QString::number(millimeters.width(), 'f', 0), QString::number(millimeters.height(), 'f', 0));
}
