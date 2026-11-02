// SPDX-License-Identifier: GPL-2.0-or-later
#include "multifile.h"
#include "core/multifileipc.h"
#include <QApplication>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QLibrary>
#include <QLocalServer>
#include <QPointer>
#include <QSet>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrentRun>
#include <QFile>
#include <cstdio>
#include <atomic>
#ifdef Q_OS_WIN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

namespace MengsheeMultifile {
int runRendererHost()
{
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    QLibrary library(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("plugins/kf6/parts/okularpart.dll")));
    using Render = QByteArray (*)(const QByteArray &);
    auto render = reinterpret_cast<Render>(library.resolve("mengsheeStemtexService"));
    if (!render) return 1;
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) return 1;
    while (true) {
        const QByteArray line = input.readLine(2 * 1024 * 1024 + 1);
        if (line.isEmpty()) return 0;
        if (!line.endsWith('\n')) return 1;
        const QByteArray result = render(line);
        if (std::fwrite(result.constData(), 1, size_t(result.size()), stdout) != size_t(result.size())) return 1;
        std::fflush(stdout);
    }
}

// Accessed only on the coordinator's persistent render thread. There is one
// host for the application's current global typesetting configuration.
class RendererHost {
public:
    QProcess process;
    bool initialized = false;
#ifdef Q_OS_WIN
    HANDLE job = nullptr;
#endif
    ~RendererHost()
    {
        if (process.state() != QProcess::NotRunning) {
            process.write(encode({{QStringLiteral("op"), QStringLiteral("shutdown")}}));
            process.closeWriteChannel();
            process.waitForFinished(3000);
        }
#ifdef Q_OS_WIN
        if (job) { CloseHandle(job); job = nullptr; }
#endif
        if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(5000); }
    }
    bool start()
    {
        process.setProgram(QCoreApplication::applicationFilePath());
        process.setArguments({QStringLiteral("--stemtex-service")});
        auto env = QProcessEnvironment::systemEnvironment();
        env.remove(QStringLiteral("MENGSHEE_DOCUMENT_PROCESS"));
        process.setProcessEnvironment(env);
        process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        process.start();
        if (!process.waitForStarted(5000)) return false;
#ifdef Q_OS_WIN
        // The host cannot spawn workers before receiving its first request.
        // Assign it now; the job never includes document processes.
        job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(process.processId()));
        const bool ok = job && child
            && SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))
            && AssignProcessToJobObject(job, child);
        if (child) CloseHandle(child);
        if (!ok) return false;
#endif
        return true;
    }
    void abort()
    {
#ifdef Q_OS_WIN
        if (job) { CloseHandle(job); job = nullptr; }
#endif
        if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(1000); }
    }
    QByteArray request(const QByteArray &payload, const std::atomic<quint64> &generation, quint64 expectedGeneration, int timeout)
    {
        process.write(payload);
        QElapsedTimer elapsed; elapsed.start();
        QByteArray result;
        while (elapsed.elapsed() < timeout) {
            if (generation.load() != expectedGeneration) { abort(); return {}; }
            result += process.readAllStandardOutput();
            if (result.size() > maxMessage) break;
            if (result.endsWith('\n')) return result;
            if (process.state() == QProcess::NotRunning) break;
            process.waitForReadyRead(qMin(1000, timeout - int(elapsed.elapsed())));
        }
        abort();
        return {};
    }
};

bool openDocuments(const QStringList &paths, const QString &options, QString *error)
{
    const auto reply = request({{QStringLiteral("op"), QStringLiteral("open")},
        {QStringLiteral("paths"), QJsonArray::fromStringList(paths)}, {QStringLiteral("options"), options}}, 15000);
    if (error) *error = reply.value(QStringLiteral("error")).toString();
    return reply.value(QStringLiteral("ok")).toBool();
}

void keepDocumentLease()
{
    // A coordinator restart must never close a document. Re-adopt surviving
    // children over a new lifetime connection, without changing their state.
    auto *socket = new QLocalSocket(qApp);
    auto *retry = new QTimer(socket);
    retry->setInterval(1500);
    const auto attach = [socket] {
        if (socket->state() == QLocalSocket::UnconnectedState) socket->connectToServer(endpoint());
    };
    QObject::connect(socket, &QLocalSocket::connected, socket, [socket] {
        socket->write(encode({{QStringLiteral("op"), QStringLiteral("lease")},
            {QStringLiteral("pid"), double(QCoreApplication::applicationPid())}}));
    });
    QObject::connect(socket, &QLocalSocket::errorOccurred, socket, [socket] {
        if (socket->property("restarting").toBool()) return;
        socket->setProperty("restarting", true);
        auto *watcher = new QFutureWatcher<QJsonObject>(socket);
        QObject::connect(watcher, &QFutureWatcher<QJsonObject>::finished, socket, [socket, watcher] {
            socket->setProperty("restarting", false); watcher->deleteLater();
        });
        watcher->setFuture(QtConcurrent::run([] { return request({{QStringLiteral("op"), QStringLiteral("status")}}, 15000); }));
    });
    QObject::connect(retry, &QTimer::timeout, socket, attach);
    QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket] { socket->readAll(); });
    retry->start(); attach();
}

int runCoordinator()
{
    qApp->setQuitOnLastWindowClosed(false);
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(endpoint())) return 1;
    QSet<QLocalSocket *> leases;
    QSet<QProcess *> children;
    QThreadPool renderPool;
    renderPool.setMaxThreadCount(1);
    renderPool.setExpiryTimeout(-1);
    int pending = 0;
    QHash<QString, bool> ready;
    QString lastError;
    std::unique_ptr<RendererHost> host;
    QString hostKey;
    std::atomic<quint64> resetGeneration{0};
    const auto render = [&host, &hostKey, &resetGeneration](const QByteArray &payload, quint64 generation) {
        auto message = QJsonDocument::fromJson(payload).object();
        const auto key = message.value(QStringLiteral("key")).toString();
        const auto fail = [](const QString &text) { return encode({{QStringLiteral("ok"), false}, {QStringLiteral("error"), text}}); };
        const auto interrupted = [&] { return generation != resetGeneration.load(); };
        if (interrupted()) return fail(QStringLiteral("Typesetting was interrupted by a restart."));
        if (key.isEmpty()) return fail(QStringLiteral("Missing renderer configuration identity."));
        QElapsedTimer startupDeadline; startupDeadline.start();
        const bool wantsRender = message.value(QStringLiteral("op")) == QLatin1String("render");
        if (message.value(QStringLiteral("op")) == QLatin1String("reset")) {
            host.reset();
            message.insert(QStringLiteral("op"), QStringLiteral("warmup"));
        }
        if (hostKey != key) host.reset();
        if (!host) {
            host = std::make_unique<RendererHost>();
            if (!host->start()) {
                host.reset();
                return fail(QStringLiteral("Could not start an isolated StemTeX host."));
            }
            hostKey = key;
        }
        QByteArray result;
        if (!host->initialized || !wantsRender) {
            auto warmup = message;
            warmup.insert(QStringLiteral("op"), QStringLiteral("warmup"));
            result = host->request(encode(warmup), resetGeneration, generation, qMax(0, 60000 - int(startupDeadline.elapsed())));
            host->initialized = QJsonDocument::fromJson(result).object().value(QStringLiteral("ok")).toBool();
            if (!host->initialized) {
                host.reset();
                if (interrupted()) return fail(QStringLiteral("Typesetting was interrupted by a restart."));
                return result.isEmpty() ? fail(QStringLiteral("StemTeX startup failed or exceeded 60 seconds.")) : result;
            }
        }
        if (wantsRender) result = host->request(encode(message), resetGeneration, generation, 10000);
        if (result.isEmpty()) {
            host.reset();
            if (interrupted()) return fail(QStringLiteral("Typesetting was interrupted by a restart."));
            return fail(QStringLiteral("StemTeX typesetting failed or exceeded 10 seconds, including error recovery."));
        }
        return result;
    };
    QTimer idle;
    idle.setInterval(10000);
    QObject::connect(&idle, &QTimer::timeout, &server, [&] {
        if (leases.isEmpty() && children.isEmpty() && pending == 0) qApp->quit();
    });
    idle.start();
    const auto send = [](QLocalSocket *socket, const QJsonObject &reply) {
        if (socket->state() != QLocalSocket::ConnectedState) return;
        socket->write(encode(reply)); socket->disconnectFromServer();
    };
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&] {
        while (auto *socket = server.nextPendingConnection()) {
            constexpr qsizetype requestLimit = 2 * 1024 * 1024;
            socket->setReadBufferSize(requestLimit + 1);
            auto buffer = std::make_shared<QByteArray>();
            auto *deadline = new QTimer(socket);
            deadline->setSingleShot(true); deadline->start(15000);
            QObject::connect(deadline, &QTimer::timeout, socket, &QLocalSocket::abort);
            QObject::connect(socket, &QLocalSocket::disconnected, &server, [&, socket] {
                leases.remove(socket); socket->deleteLater();
            });
            QObject::connect(socket, &QLocalSocket::readyRead, &server, [&, socket, buffer, deadline] {
                if (socket->property("dispatched").toBool()) { socket->readAll(); return; }
                *buffer += socket->readAll();
                if (buffer->size() > 2 * 1024 * 1024) { socket->abort(); return; }
                if (!buffer->endsWith('\n')) return;
                socket->setProperty("dispatched", true); deadline->stop();
                const auto request = QJsonDocument::fromJson(*buffer).object();
                const auto op = request.value(QStringLiteral("op")).toString();
                if (op == QLatin1String("lease")) {
                    leases.insert(socket); socket->setProperty("pid", request.value(QStringLiteral("pid")).toDouble());
                    socket->write(encode({{QStringLiteral("ok"), true}})); return;
                }
                if (op == QLatin1String("status")) {
                    QJsonArray pids;
                    for (auto *lease : leases) pids.append(lease->property("pid").toDouble());
                    QJsonArray profiles;
                    for (auto it = ready.cbegin(); it != ready.cend(); ++it) if (it.value()) profiles.append(it.key());
                    send(socket, {{QStringLiteral("ok"), true}, {QStringLiteral("pid"), double(QCoreApplication::applicationPid())},
                        {QStringLiteral("documents"), pids}, {QStringLiteral("pending"), pending},
                        {QStringLiteral("profiles"), profiles},
                        {QStringLiteral("error"), lastError},
                        {QStringLiteral("ready"), ready.value(request.value(QStringLiteral("key")).toString())}}); return;
                }
                if (op == QLatin1String("open")) {
                    const auto paths = request.value(QStringLiteral("paths")).toArray();
                    if (paths.size() > 64) { send(socket, {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Too many documents in one request.")}}); return; }
                    QJsonArray pids;
                    bool ok = true;
                    for (int i = 0; i < qMax(1, int(paths.size())); ++i) {
                        auto *child = new QProcess(&server);
                        child->setProgram(QCoreApplication::applicationFilePath());
                        QStringList args{QStringLiteral("--document-process"), QStringLiteral("--document-options"), request.value(QStringLiteral("options")).toString()};
                        if (!paths.isEmpty()) args << QStringLiteral("--") << paths[i].toString();
                        child->setArguments(args);
                        auto env = QProcessEnvironment::systemEnvironment();
                        env.insert(QStringLiteral("MENGSHEE_DOCUMENT_PROCESS"), QStringLiteral("1"));
                        child->setProcessEnvironment(env);
                        child->setWorkingDirectory(QCoreApplication::applicationDirPath());
                        child->setProcessChannelMode(QProcess::ForwardedChannels);
                        children.insert(child);
                        QObject::connect(child, &QProcess::finished, &server, [&, child] { children.remove(child); child->deleteLater(); });
                        child->start();
                        if (!child->waitForStarted(5000)) { children.remove(child); child->deleteLater(); ok = false; }
                        else pids.append(double(child->processId()));
                    }
                    send(socket, {{QStringLiteral("ok"), ok}, {QStringLiteral("children"), pids},
                        {QStringLiteral("error"), ok ? QString() : QStringLiteral("Could not start a document process.")}}); return;
                }
                if (op != QLatin1String("render") && op != QLatin1String("warmup") && op != QLatin1String("reset")) {
                    send(socket, {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Unknown coordinator request.")}}); return;
                }
                if (pending >= 64) { send(socket, {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Render queue is full.")}}); return; }
                ++pending;
                if (op == QLatin1String("reset")) { ready.clear(); ++resetGeneration; }
                const quint64 generation = resetGeneration.load();
                auto *watcher = new QFutureWatcher<QByteArray>(&server);
                const QPointer<QLocalSocket> client(socket);
                // Transport liveness is separate from queue/startup/render deadlines.
                auto *heartbeat = new QTimer(socket);
                heartbeat->setInterval(1000);
                QObject::connect(heartbeat, &QTimer::timeout, socket, [socket] {
                    socket->write(encode({{QStringLiteral("pending"), true}}));
                });
                heartbeat->start();
                const QPointer<QTimer> progressTimer(heartbeat);
                const auto key = request.value(QStringLiteral("key")).toString();
                QObject::connect(watcher, &QFutureWatcher<QByteArray>::finished, &server, [&, watcher, client, key, progressTimer] {
                    if (progressTimer) progressTimer->stop();
                    --pending;
                    const auto result = QJsonDocument::fromJson(watcher->result()).object();
                    ready.clear();
                    ready[key] = result.value(QStringLiteral("ready")).toBool();
                    lastError = result.value(QStringLiteral("error")).toString();
                    if (client) send(client, result);
                    watcher->deleteLater();
                });
                const auto payload = *buffer;
                watcher->setFuture(QtConcurrent::run(&renderPool, [render, payload, generation] { return render(payload, generation); }));
            });
        }
    });
    const int result = qApp->exec();
    renderPool.waitForDone();
    QtConcurrent::run(&renderPool, [&host] { host.reset(); }).waitForFinished();
    return result;
}
}
