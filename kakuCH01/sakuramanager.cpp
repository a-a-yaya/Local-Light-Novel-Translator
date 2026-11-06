#include "sakuramanager.h"
#include "modelgpuconfig.h"
#include "serverexecutable.h"

#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>

// 轻小说翻译推荐参数
static const int kContextLength = 6144;   // 为官方术语提示、正文及完整译文预留空间
static const int kParallel = 1;           // 并发数，串行翻译够用且省显存
static const QString kHost = QStringLiteral("127.0.0.1");
static const QString kPort = QStringLiteral("8080");

// 根据模型文件名评估“翻译质量”分数，用于挑选最优模型
// 分数越高越优先：量化精度 > 参数规模
static int modelQualityScore(const QString &fileName)
{
    QString n = fileName.toLower();
    int score = 0;

    // 量化精度（数字越大量化越精细，质量越高）
    if (n.contains(QStringLiteral("q8")))
        score = 800;
    else if (n.contains(QStringLiteral("q6")))
        score = 600;
    else if (n.contains(QStringLiteral("q5")))
        score = 500;
    else if (n.contains(QStringLiteral("iq4")) || n.contains(QStringLiteral("q4")))
        score = 400;
    else if (n.contains(QStringLiteral("iq3")) || n.contains(QStringLiteral("q3")))
        score = 300;
    else if (n.contains(QStringLiteral("iq2")) || n.contains(QStringLiteral("q2")))
        score = 200;

    // XXS 相比 XS 略降一级
    if (n.contains(QStringLiteral("xxs")))
        score -= 10;

    // 参数规模（越大通常质量越好）
    if (n.contains(QStringLiteral("14b")))
        score += 100;
    else if (n.contains(QStringLiteral("13b")))
        score += 90;
    else if (n.contains(QStringLiteral("12b")))
        score += 80;
    else if (n.contains(QStringLiteral("7b")))
        score += 50;

    return score;
}

SakuraManager::SakuraManager(QObject *parent)
    : QObject(parent)
    , m_serverUrl(QStringLiteral("http://127.0.0.1:8080"))
{
}

SakuraManager::~SakuraManager()
{
    stop();
}

void SakuraManager::setSakuraDir(const QString &dir)
{
    m_sakuraDir = dir;
    while (m_sakuraDir.endsWith(QLatin1Char('/')) || m_sakuraDir.endsWith(QLatin1Char('\\')))
        m_sakuraDir.chop(1);
    // 默认模型目录与 Sakura 目录相同
    m_modelDir = m_sakuraDir;
}

void SakuraManager::setModelDir(const QString &dir)
{
    m_modelDir = dir;
    while (m_modelDir.endsWith(QLatin1Char('/')) || m_modelDir.endsWith(QLatin1Char('\\')))
        m_modelDir.chop(1);
}

QStringList SakuraManager::availableModels() const
{
    QStringList result;
    if (m_modelDir.isEmpty())
        return result;
    QDirIterator files(m_modelDir, QDir::Files | QDir::Readable,
                       QDirIterator::Subdirectories);
    while (files.hasNext()) {
        files.next();
        const QFileInfo file = files.fileInfo();
        if (file.suffix().compare(QStringLiteral("gguf"), Qt::CaseInsensitive) != 0
            || !file.fileName().contains(QStringLiteral("sakura"), Qt::CaseInsensitive))
            continue;
        result << file.absoluteFilePath();
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QString SakuraManager::bestModelPath() const
{
    // 候选只要求 GGUF 文件名包含 sakura，不限制其它命名。
    QStringList candidates = availableModels();
    if (candidates.isEmpty())
        return QString();

    // 按质量分数降序，取最优
    std::sort(candidates.begin(), candidates.end(), [](const QString &a, const QString &b) {
        return modelQualityScore(QFileInfo(a).fileName()) > modelQualityScore(QFileInfo(b).fileName());
    });
    return candidates.first();
}

void SakuraManager::setModelPath(const QString &path)
{
    m_modelPath = path;
}

QString SakuraManager::modelAlias() const
{
    // 用文件名（不含 .gguf 后缀）作为别名
    return QFileInfo(m_modelPath).completeBaseName();
}

bool SakuraManager::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void SakuraManager::start()
{
    if (isRunning())
        return;

    if (!locateServerExe()) {
        emit failed(QStringLiteral("未找到 llama-server.exe，请确认 Sakura 目录正确"));
        return;
    }
    // 未显式指定模型时，自动选最优
    if (m_modelPath.isEmpty() || !QFileInfo::exists(m_modelPath))
        m_modelPath = bestModelPath();
    if (m_modelPath.isEmpty()) {
        emit failed(QStringLiteral("模型目录中没有 .gguf 模型文件"));
        return;
    }
    if (!writeRecommendedConfig()) {
        emit failed(QStringLiteral("写入推荐配置失败"));
        return;
    }

    m_process = new QProcess(this);
    m_process->setWorkingDirectory(QFileInfo(m_serverExe).absolutePath());
    connect(m_process, &QProcess::errorOccurred, this, &SakuraManager::onProcessError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) { onProcessFinished(code); });
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &SakuraManager::onReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &SakuraManager::onReadyReadStandardOutput);

    QString alias = modelAlias();

    QStringList args;
    args << QStringLiteral("--model") << m_modelPath
         << QStringLiteral("-c") << QString::number(kContextLength)
         << QStringLiteral("-ngl") << QString::number(ModelGpuConfig::layersFor("sakura", m_modelPath))
         << QStringLiteral("-a") << alias
         << QStringLiteral("--host") << kHost
         << QStringLiteral("--port") << kPort
         << QStringLiteral("-np") << QString::number(kParallel)
         << QStringLiteral("--no-mmap");

    m_started = false;
    m_logBuffer.clear();
    m_process->start(m_serverExe, args);
    emit logMessage(QStringLiteral("正在启动模型: %1…").arg(QFileInfo(m_modelPath).fileName()));
}

void SakuraManager::stop()
{
    if (!m_process)
        return;

    QProcess *p = m_process;
    m_process = nullptr;
    m_started = false;

    // 先断开信号，避免 kill 触发的 errorOccurred(Crashed) 被误报为“启动失败”
    p->disconnect(this);
    if (p->state() != QProcess::NotRunning) {
        p->kill();
        p->waitForFinished(2000);
    }
    p->deleteLater();
    emit stopped();
}

void SakuraManager::onProcessError()
{
    if (!m_process)
        return;
    emit failed(QStringLiteral("启动失败: %1").arg(m_process->errorString()));
    stop();
}

void SakuraManager::onProcessFinished(int exitCode)
{
    Q_UNUSED(exitCode);
    if (!m_process)
        return;
    m_started = false;
    // 进程自行退出（非用户点击关闭），释放并通知
    m_process->disconnect(this);
    m_process->deleteLater();
    m_process = nullptr;
    emit stopped();
}

void SakuraManager::onReadyReadStandardOutput()
{
    if (!m_process)
        return;
    m_logBuffer += QString::fromUtf8(m_process->readAllStandardOutput());
    m_logBuffer += QString::fromUtf8(m_process->readAllStandardError());
    // llama-server 加载完模型后输出 "server is listening"
    if (!m_started && m_logBuffer.contains(QStringLiteral("listening"))) {
        m_started = true;
        emit started();
    }
}

bool SakuraManager::locateServerExe()
{
    m_serverExe = ServerExecutable::locate(m_sakuraDir, m_modelDir);
    if (m_serverExe.isEmpty())
        return false;

    QDir launcherRoot = QFileInfo(m_serverExe).absoluteDir();
    if (launcherRoot.dirName().compare(QStringLiteral("llama"), Qt::CaseInsensitive) == 0)
        launcherRoot.cdUp();
    m_configPath = launcherRoot.filePath(QStringLiteral("sakura-launcher_config.json"));
    return true;
}

bool SakuraManager::writeRecommendedConfig()
{
    QJsonObject root;
    QFile f(m_configPath);
    if (f.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isObject())
            root = doc.object();
        f.close();
    }

    // 更新所有运行预设为推荐设置，保留 GPU 等用户特定字段
    QJsonArray presets = root.value(QStringLiteral("运行")).toArray();
    for (int i = 0; i < presets.size(); ++i) {
        QJsonObject preset = presets[i].toObject();
        QJsonObject config = preset.value(QStringLiteral("config")).toObject();
        config.insert(QStringLiteral("gpu_layers"), ModelGpuConfig::layersFor("sakura", m_modelPath));
        config.insert(QStringLiteral("flash_attention"), false);
        config.insert(QStringLiteral("no_mmap"), true);
        config.insert(QStringLiteral("context_length"), kContextLength);
        config.insert(QStringLiteral("n_parallel"), kParallel);
        config.insert(QStringLiteral("host"), kHost);
        config.insert(QStringLiteral("port"), kPort);
        // 若未指定模型则补上当前选中的模型文件名
        if (config.value(QStringLiteral("model_path")).toString().isEmpty())
            config.insert(QStringLiteral("model_path"), QFileInfo(m_modelPath).fileName());
        preset.insert(QStringLiteral("config"), config);
        presets[i] = preset;
    }
    root.insert(QStringLiteral("运行"), presets);

    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}
