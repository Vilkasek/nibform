#pragma once

#include "io/ProjectFile.h"

#include <QDateTime>
#include <QString>

#include <memory>
#include <vector>

class QLockFile;

// Autosave and crash recovery. Each running window writes its unsaved work to
// <dir>/<id>.nibform and holds <dir>/<id>.lock while it runs. A recovery file whose lock
// nobody holds was left by a session that never closed: the app crashed or was killed.
class RecoverySession
{
public:
    explicit RecoverySession(const QString& dir);
    ~RecoverySession(); // unlocks; a written file left uncleared is offered next time
    RecoverySession(const RecoverySession&) = delete;
    RecoverySession& operator=(const RecoverySession&) = delete;

    // Replaces the recovery file; sourcePath is the project's own file (empty if never
    // saved). Returns an error message, empty on success.
    QString write(const ProjectData& project, const QString& sourcePath);
    void clear(); // nothing unsaved any more
    QString file() const { return m_file; }

private:
    QString m_dir;
    QString m_file;
    std::unique_ptr<QLockFile> m_lock;
};

struct RecoveredWork
{
    QString file;       // the recovery file, for discardRecovery()
    QString sourcePath; // where the project was saved before, empty if never
    QDateTime saved;    // when it was autosaved
    ProjectData project;
};

// Work left behind by sessions that ended without closing, newest first. Unreadable
// recovery files are skipped (and kept, in case a newer version can read them).
std::vector<RecoveredWork> findRecoveredWork(const QString& dir);
void discardRecovery(const QString& file);

// Where the app keeps recovery files.
QString defaultRecoveryDir();
