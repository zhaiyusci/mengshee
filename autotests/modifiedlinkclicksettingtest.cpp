/*
    SPDX-FileCopyrightText: 2026 Mengshee contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QTemporaryDir>
#include <QTest>

#include <KConfig>
#include <KSharedConfig>

#include "../settings.h"

class ModifiedLinkClickSettingTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testDefaultAndSelectableModes();
};

void ModifiedLinkClickSettingTest::testDefaultAndSelectableModes()
{
    QTemporaryDir configDirectory;
    QVERIFY(configDirectory.isValid());

    const KSharedConfig::Ptr config = KSharedConfig::openConfig(configDirectory.filePath(QStringLiteral("mengsheerc")), KConfig::SimpleConfig);
    Okular::Settings::instance(config);

    QCOMPARE(Okular::Settings::modifiedLinkClickAction(), static_cast<int>(Okular::Settings::EnumModifiedLinkClickAction::AuxiliaryFrame));

    Okular::Settings::setModifiedLinkClickAction(Okular::Settings::EnumModifiedLinkClickAction::FloatingPreview);
    QCOMPARE(Okular::Settings::modifiedLinkClickAction(), static_cast<int>(Okular::Settings::EnumModifiedLinkClickAction::FloatingPreview));

    Okular::Settings::setModifiedLinkClickAction(Okular::Settings::EnumModifiedLinkClickAction::AuxiliaryFrame);
    QCOMPARE(Okular::Settings::modifiedLinkClickAction(), static_cast<int>(Okular::Settings::EnumModifiedLinkClickAction::AuxiliaryFrame));
}

QTEST_GUILESS_MAIN(ModifiedLinkClickSettingTest)

#include "modifiedlinkclicksettingtest.moc"
