#include "serverexecutable.h"

#include <QDir>
#include <QFileInfo>

namespace
{
QString cleanDir(const QString &path)
{
    const QString trimmed = path.trimmed();
    return trimmed.isEmpty() ? QString() : QDir::cleanPath(trimmed);
}

void appendRoot(QStringList &roots, const QString &path)
{
    const QString clean = cleanDir(path);
    if (!clean.isEmpty() && !roots.contains(clean, Qt::CaseInsensitive))
        roots << clean;
}
}

QStringList ServerExecutable::candidatePaths(const QString &launcherDir,
                                             const QString &modelDir)
{
    QStringList roots;
    appendRoot(roots, launcherDir);
    appendRoot(roots, modelDir);

    QStringList candidates;
    for (const QString &root : roots) {
        candidates << QDir(root).filePath(QStringLiteral("llama/llama-server.exe"));
        candidates << QDir(root).filePath(QStringLiteral("llama-server.exe"));
    }
    return candidates;
}

QString ServerExecutable::locate(const QString &launcherDir, const QString &modelDir)
{
    const QStringList candidates = candidatePaths(launcherDir, modelDir);
    for (const QString &candidate : candidates) {
        const QFileInfo file(candidate);
        if (file.isFile())
            return file.absoluteFilePath();
    }
    return QString();
}

QString ServerExecutable::preferredLauncherDir(const QString &preferredDir,
                                               const QString &modelDir)
{
    if (!locate(preferredDir).isEmpty())
        return cleanDir(preferredDir);
    if (!locate(modelDir).isEmpty())
        return cleanDir(modelDir);
    return cleanDir(preferredDir);
}
