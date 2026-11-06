#ifndef SERVEREXECUTABLE_H
#define SERVEREXECUTABLE_H

#include <QString>
#include <QStringList>

namespace ServerExecutable
{
QStringList candidatePaths(const QString &launcherDir, const QString &modelDir = QString());
QString locate(const QString &launcherDir, const QString &modelDir = QString());

// Prefer an explicitly configured launcher directory, but recover from an
// old setting when the runtime was selected through the model-directory UI.
QString preferredLauncherDir(const QString &preferredDir, const QString &modelDir);
}

#endif // SERVEREXECUTABLE_H
