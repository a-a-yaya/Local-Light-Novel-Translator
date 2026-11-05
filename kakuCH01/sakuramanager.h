#ifndef SAKURAMANAGER_H
#define SAKURAMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

// 管理本地 Sakura 模型的启动与关闭：
// 1) 将 Sakura Launcher 的配置改为最适合轻小说翻译的设置
// 2) 直接通过 QProcess 启动 llama-server.exe 提供本地翻译服务
// 3) 扫描模型目录，自动选择最优的 sakura 模型，并支持用户自选
class SakuraManager : public QObject
{
    Q_OBJECT
public:
    explicit SakuraManager(QObject *parent = nullptr);
    ~SakuraManager();

    // 设置 Sakura_Launcher_GUI_v1.2.0 所在目录（同时作为默认模型目录）
    void setSakuraDir(const QString &dir);

    // 模型目录管理
    void setModelDir(const QString &dir);
    QString modelDir() const { return m_modelDir; }
    QStringList availableModels() const;  // 递归扫描文件名含 sakura 的 GGUF，忽略大小写
    QString bestModelPath() const;        // 自动选择最优的 sakura 模型
    void setModelPath(const QString &path);
    QString modelPath() const { return m_modelPath; }
    QString modelAlias() const;           // llama-server 的模型别名（用于 API 请求）

    bool isRunning() const;
    QString serverUrl() const { return m_serverUrl; }

public slots:
    void start();  // 写入推荐配置并启动模型服务
    void stop();   // 关闭模型服务

signals:
    void started();                 // 服务已监听（模型加载完成）
    void stopped();
    void failed(const QString &error);
    void logMessage(const QString &message);

private slots:
    void onProcessError();
    void onProcessFinished(int exitCode);
    void onReadyReadStandardOutput();

private:
    bool locateServerExe();         // 定位 llama-server.exe
    bool writeRecommendedConfig();  // 将最优设置写回 sakura-launcher_config.json

    QProcess *m_process = nullptr;
    QString m_sakuraDir;
    QString m_modelDir;    // 模型目录
    QString m_serverExe;   // llama-server.exe 绝对路径
    QString m_modelPath;   // 当前选中的 .gguf 模型绝对路径
    QString m_configPath;  // 配置文件绝对路径
    QString m_serverUrl;   // 形如 http://127.0.0.1:8080
    QString m_logBuffer;   // 累计的标准输出，用于判断是否就绪
    bool m_started = false;
};

#endif // SAKURAMANAGER_H
