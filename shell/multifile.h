// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QStringList>
namespace MengsheeMultifile {
int runRendererHost();
int runCoordinator();
void keepDocumentLease();
bool openDocuments(const QStringList &paths, const QString &options, QString *error = nullptr);
}
