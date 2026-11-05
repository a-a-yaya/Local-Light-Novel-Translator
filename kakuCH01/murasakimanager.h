#ifndef MURASAKIMANAGER_H
#define MURASAKIMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

// 管理本地 Murasaki 推理模型的启动与关闭。
// 与 SakuraManager 完全独立：使用独立端口 8081，不写入 Sakura Launcher 配置，
// 只扫描 Murasaki 模型并直接启动 llama-server.exe。
class MurasakiManager : public QObject
{
    Q_OBJECT
public:
    explicit MurasakiManager(QObject *parent = nullptr);
    ~MurasakiManager();

    // llama-server.exe 所在目录（默认复用 Sakura Launcher 目录）
    void setLauncherDir(const QString &dir);
    QString launcherDir() const { return m_launcherDir; }

    // 模型目录管理
    void setModelDir(const QString &dir);
    QString modelDir() const { return m_modelDir; }
    QStringList availableModels() const;  // 递归扫描文件名含 murasaki 的 GGUF，忽略大小写
    QString bestModelPath() const;        // 自动选择最优的 Murasaki 模型
    void setModelPath(const QString &path);
    QString modelPath() const { return m_modelPath; }
    QString modelAlias() const;           // llama-server 的模型别名（用于 API 请求）

    bool isRunning() const;
    QString serverUrl() const { return m_serverUrl; }

public slots:
    void start();  // 启动 Murasaki 模型服务
    void stop();   // 关闭模型服务

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
    bool locateServerExe();  // 定位 llama-server.exe
    void setupKillOnClose(); // 将 llama-server 放入 KILL_ON_JOB_CLOSE 的 Job
    void closeJob();         // 关闭 Job 句柄（触发对 job 内进程的终止）

    QProcess *m_process = nullptr;
    QString m_launcherDir;   // llama-server.exe 所在目录
    QString m_modelDir;      // 模型目录
    QString m_serverExe;     // llama-server.exe 绝对路径
    QString m_modelPath;     // 当前选中的 .gguf 模型绝对路径
    QString m_serverUrl;     // 形如 http://127.0.0.1:8081
    QString m_logBuffer;     // 累计的标准输出，用于判断是否就绪
    bool m_started = false;
    void *m_job = nullptr;   // Windows Job Object 句柄（应用退出时自动杀死子进程）
};

#endif // MURASAKIMANAGER_H
