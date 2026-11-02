// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QThread>

namespace MengsheeMultifile {
constexpr qsizetype maxMessage = 96 * 1024 * 1024;
inline QString endpoint()
{
    const auto identity = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath()
        + QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + qEnvironmentVariable("MENGSHEE_MULTIFILE_INSTANCE");
    return QStringLiteral("mengshee-multifile-v1-") + QString::fromLatin1(
        QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
}
inline bool isDocumentProcess() { return qEnvironmentVariableIsSet("MENGSHEE_DOCUMENT_PROCESS"); }
inline QByteArray encode(const QJsonObject &object) { return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n'; }
inline bool connect(QLocalSocket &socket, bool start = true)
{
    socket.connectToServer(endpoint());
    if (socket.waitForConnected(200)) return true;
    if (!start) return false;
    QLockFile launchLock(QDir::temp().filePath(endpoint() + QStringLiteral(".launch.lock")));
    if (!launchLock.tryLock(15000)) return false;
    socket.abort(); socket.connectToServer(endpoint());
    if (socket.waitForConnected(200)) return true;
    QProcess process;
    process.setProgram(QCoreApplication::applicationFilePath());
    process.setArguments({QStringLiteral("--multifile-coordinator")});
    auto env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("MENGSHEE_DOCUMENT_PROCESS"));
    process.setProcessEnvironment(env);
    process.setWorkingDirectory(QCoreApplication::applicationDirPath());
    if (!process.startDetached()) return false;
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < 10000) {
        socket.abort(); socket.connectToServer(endpoint());
        if (socket.waitForConnected(200)) return true;
        QThread::msleep(50);
    }
    return false;
}
inline QJsonObject request(const QJsonObject &object, int timeout = 15000, bool start = true)
{
    const auto failure = [](const QString &message) { return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}}; };
    QLocalSocket socket;
    socket.setReadBufferSize(maxMessage + 1);
    if (!connect(socket, start)) return failure(QStringLiteral("Could not connect to the Mengshee coordinator."));
    const auto data = encode(object);
    if (data.size() > maxMessage) return failure(QStringLiteral("Coordinator request exceeds the size limit."));
    socket.write(data);
    QElapsedTimer timer; timer.start();
    while (socket.bytesToWrite() && timer.elapsed() < timeout)
        if (!socket.waitForBytesWritten(qMax(1, timeout - int(timer.elapsed())))) return failure(socket.errorString());
    QByteArray reply;
    while (timer.elapsed() < timeout) {
        reply += socket.readAll();
        if (reply.size() > maxMessage) return failure(QStringLiteral("Coordinator response exceeds the size limit."));
        while (reply.contains('\n')) {
            const auto end = reply.indexOf('\n');
            const auto message = QJsonDocument::fromJson(reply.left(end)).object();
            reply.remove(0, end + 1);
            if (!message.value(QStringLiteral("pending")).isBool() || !message.value(QStringLiteral("pending")).toBool()) return message;
            timer.restart();
        }
        if (!socket.waitForReadyRead(qMax(1, timeout - int(timer.elapsed())))) return failure(socket.errorString());
    }
    return failure(QStringLiteral("Coordinator request timed out."));
}
}
