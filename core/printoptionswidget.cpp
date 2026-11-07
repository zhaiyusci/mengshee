/*
    SPDX-FileCopyrightText: 2019 Michael Weghorn <m.weghorn@posteo.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "printoptionswidget.h"

#include <QComboBox>
#include <QFormLayout>

#include <KLocalizedString>

namespace Okular
{
DefaultPrintOptionsWidget::DefaultPrintOptionsWidget(QWidget *parent)
    : PrintOptionsWidget(parent)
{
    setWindowTitle(i18n("Print Options"));
    QFormLayout *layout = new QFormLayout(this);
    m_scaleModeCombo = new QComboBox;
    m_scaleModeCombo->setObjectName(QStringLiteral("scaleModeCombo"));
    m_scaleModeCombo->insertItem(0, i18n("Fit to printable area"), int(PrintScaleMode::FitToPrintableArea));
    m_scaleModeCombo->insertItem(1, i18n("Fit to full page"), int(PrintScaleMode::FitToFullPage));
    // Same wording as the PDF backend's print options, so the choice reads the
    // same whichever backend provides it.
    m_scaleModeCombo->insertItem(2, i18n("None; print original size"), int(PrintScaleMode::OriginalSize));
    layout->addRow(i18n("Scale mode:"), m_scaleModeCombo);
}

PrintScaleMode DefaultPrintOptionsWidget::scaleMode() const
{
    return static_cast<PrintScaleMode>(m_scaleModeCombo->currentData().toInt());
}

bool DefaultPrintOptionsWidget::ignorePrintMargins() const
{
    return scaleMode() == PrintScaleMode::FitToFullPage;
}

bool DefaultPrintOptionsWidget::originalPageSize() const
{
    return scaleMode() == PrintScaleMode::OriginalSize;
}

}

#include "moc_printoptionswidget.cpp"
