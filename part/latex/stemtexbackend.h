/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MENGSHEE_STEMTEXBACKEND_H
#define MENGSHEE_STEMTEXBACKEND_H
#include "../latexbackend.h"
#include <QList>

namespace GuiUtils
{
struct StemTeXStatus {
    bool supported = false;
    bool initializing = false;
    bool ready = false;
    bool primaryReady = false;
    int spareReady = 0;
    int spareTarget = 0;
    bool spareRebuilding = false;
    int rendererStatus = 0;
    int renderStage = 0;
    bool asyncRunning = false;
    bool asyncPending = false;
    quint64 runningJobId = 0;
    quint64 pendingJobId = 0;
    int lastError = 0;
    QString note;
};

struct StemTeXProfile {
    QString id;
    QString name;
    QString path;
    bool userManaged = false;
};

class StemTeXBackend final : public LatexBackend
{
public:
    QString id() const override;
    LatexBackendResult render(const LatexRenderRequest &request) override;
    void prewarm() override;
    void restart() override;

    // Only the StemTeX configuration UI uses these backend-specific controls.
    static QList<StemTeXProfile> profiles();
    static QString runtimeRoot();
    static QString userProfilesRoot();
    static QString profileCreatorExecutable();
    static QString defaultTexmfRoot();
    static StemTeXStatus status();
};
}
#endif
