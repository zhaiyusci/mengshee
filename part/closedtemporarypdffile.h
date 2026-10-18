// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QFile>
#include <QTemporaryFile>
#include <memory>

// Own the pathname, not an open QTemporaryFile engine. On Windows even a
// QTemporaryFile closed through QIODevice can prevent atomic replacement.
class ClosedTemporaryPdfFile
{
public:
    static std::unique_ptr<ClosedTemporaryPdfFile> create(const QString &fileTemplate, bool autoRemove = true)
    {
        QTemporaryFile reservation(fileTemplate);
        if (!reservation.open())
            return {};
        auto result = std::unique_ptr<ClosedTemporaryPdfFile>(new ClosedTemporaryPdfFile(reservation.fileName(), autoRemove));
        reservation.setAutoRemove(false);
        // The reservation's destructor releases its native handle before return.
        return result;
    }

    ~ClosedTemporaryPdfFile()
    {
        if (m_autoRemove)
            QFile::remove(m_path);
    }

    QString fileName() const
    {
        return m_path;
    }

private:
    ClosedTemporaryPdfFile(const QString &path, bool autoRemove)
        : m_path(path)
        , m_autoRemove(autoRemove)
    {
    }
    Q_DISABLE_COPY_MOVE(ClosedTemporaryPdfFile)
    QString m_path;
    bool m_autoRemove;
};
