#ifndef QWENMANAGER_H
#define QWENMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;



// 与 SakuraManager / MurasakiManager 完全独立。
class QwenManager : public QObject
{
    Q_OBJECT
public:
    explicit QwenManager(QObject *parent = nullptr);
    ~QwenManager();

    void setLauncherDir(const QString &dir);
    QString launcherDir() const { return m_launcherDir; }

    void setModelDir(const QString &dir);
    QString modelDir() const { return m_modelDir; }
    QStringList availableModels() const;
    QString bestModelPath() const;
    void setModelPath(const QString &path);
    QString modelPath() const { return m_modelPath; }
    QString modelAlias() const;

    bool isRunning() const;
    QString serverUrl() const { return m_serverUrl; }

public slots:
    void start();
    void stop();

signals:
    void started();
    void stopped();
    void failed(const QString &error);
    void logMessage(const QString &message);

private slots:
    void onProcessError();
    void onProcessFinished(int exitCode);
    void onReadyReadStandardOutput();

private:
    bool locateServerExe();
    void setupKillOnClose();
    void closeJob();

    QProcess *m_process = nullptr;
    QString m_launcherDir;
    QString m_modelDir;
    QString m_serverExe;
    QString m_modelPath;
    QString m_serverUrl;
    QString m_logBuffer;
    bool m_started = false;
    void *m_job = nullptr;
};

#endif // QWENMANAGER_H
