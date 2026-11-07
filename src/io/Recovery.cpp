#include "io/Recovery.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace {

const QString kSuffix = QStringLiteral(".nibform");

QString lockPath(const QString& file)
{
    return file.left(file.size() - kSuffix.size()) + QStringLiteral(".lock");
}

} // namespace

RecoverySession::RecoverySession(const QString& dir)
    : m_dir(dir)
{
    QDir().mkpath(dir);
    // The pid alone could repeat after a reboot; the time makes the name unique.
    const QString id = QStringLiteral("%1-%2").arg(QCoreApplication::applicationPid())
                           .arg(QDateTime::currentMSecsSinceEpoch());
    m_file = QDir(dir).filePath(id + kSuffix);
    m_lock = std::make_unique<QLockFile>(lockPath(m_file));
    m_lock->setStaleLockTime(0); // only a dead owner makes it stale, never its age
    m_lock->tryLock(0);
}

RecoverySession::~RecoverySession()
{
    // The file stays unless clear() was called: quitting without the window's close
    // (logout, a fatal error) keeps the work, to be offered at the next start.
    m_lock->unlock();
}

QString RecoverySession::write(const ProjectData& project, const QString& sourcePath)
{
    QJsonObject root = QJsonDocument::fromJson(projectToJson(project)).object();
    root.insert(QStringLiteral("recovery"),
                QJsonObject{{QStringLiteral("source"), sourcePath},
                            {QStringLiteral("saved"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
    QSaveFile file(m_file);
    if (!file.open(QIODevice::WriteOnly))
        return file.errorString();
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit())
        return file.errorString();
    return {};
}

void RecoverySession::clear()
{
    QFile::remove(m_file);
}

std::vector<RecoveredWork> findRecoveredWork(const QString& dir)
{
    std::vector<RecoveredWork> found;
    const QDir folder(dir);
    for (const QString& name : folder.entryList({QStringLiteral("*") + kSuffix}, QDir::Files)) {
        const QString path = folder.filePath(name);
        // Taking the lock succeeds only when its owner is gone (or never had one).
        QLockFile lock(lockPath(path));
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0))
            continue; // a running session's work
        lock.unlock();

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QByteArray json = file.readAll();
        auto project = projectFromJson(json);
        if (!project)
            continue;
        const QJsonObject info = QJsonDocument::fromJson(json).object().value(QStringLiteral("recovery")).toObject();
        QDateTime saved = QDateTime::fromString(info.value(QStringLiteral("saved")).toString(), Qt::ISODate);
        if (!saved.isValid())
            saved = QFileInfo(path).lastModified();
        found.push_back({path, info.value(QStringLiteral("source")).toString(), saved.toLocalTime(), std::move(*project)});
    }
    std::sort(found.begin(), found.end(), [](const RecoveredWork& a, const RecoveredWork& b) { return a.saved > b.saved; });
    return found;
}

void discardRecovery(const QString& file)
{
    QFile::remove(file);
    QFile::remove(lockPath(file));
}

QString defaultRecoveryDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("recovery"));
}
