#include "qwenmanager.h"
#include "modelgpuconfig.h"

#include <QProcess>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <algorithm>
#include <functional>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Qwen 通用模型的运行参数：
//  - 上下文 16384：长章节分块后可容纳「system + 术语表 + 历史 + 正文 + 译文」；
//  - 串行（-np 1）避免显存峰值叠加；
//  - 关闭思考模式由请求侧的 chat_template_kwargs 控制（Qwen3.5 的思考过程
//    容易陷入反复自我怀疑的长循环，实测单次可达 10000+ token，故默认关闭）。
static const int kContextLength = 16384;
static const int kParallel = 1;
static const QString kHost = QStringLiteral("127.0.0.1");
static const QString kPort = QStringLiteral("8082");

// 按量化精度优先、参数规模其次，挑出目录里质量最好的 Qwen 模型。
static int qwenQualityScore(const QString &fileName)
{
    QString n = fileName.toLower();
    int score = 0;

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

    // 参数规模：从文件名里抓 "9b" 这类标记，越大越好。
    static const QRegularExpression sizeRe(QStringLiteral("(\\d+)b"));
    const auto m = sizeRe.match(n);
    if (m.hasMatch())
        score += m.captured(1).toInt() * 5;

    return score;
}

QwenManager::QwenManager(QObject *parent)
    : QObject(parent)
    , m_serverUrl(QStringLiteral("http://127.0.0.1:8082"))
{
}

QwenManager::~QwenManager()
{
    stop();
}

void QwenManager::setLauncherDir(const QString &dir)
{
    m_launcherDir = dir;
    while (m_launcherDir.endsWith(QLatin1Char('/')) || m_launcherDir.endsWith(QLatin1Char('\\')))
        m_launcherDir.chop(1);
}

void QwenManager::setModelDir(const QString &dir)
{
    m_modelDir = dir;
    while (m_modelDir.endsWith(QLatin1Char('/')) || m_modelDir.endsWith(QLatin1Char('\\')))
        m_modelDir.chop(1);
}

QStringList QwenManager::availableModels() const
{
    QStringList models;
    if (m_modelDir.isEmpty())
        return models;

    // 只挑文件名里带 qwen 的 .gguf（模型目录下含子目录，最多 3 层）。
    QDir root(m_modelDir);
    std::function<void(const QDir &, int)> walk = [&](const QDir &d, int depth) {
        const QStringList files = d.entryList(QStringList() << QStringLiteral("*.gguf"),
                                              QDir::Files, QDir::Name);
        for (const QString &f : files) {
            if (!f.contains(QStringLiteral("qwen"), Qt::CaseInsensitive))
                continue;
            models << d.absoluteFilePath(f);
        }
        if (depth >= 3)
            return;
        const QStringList subs = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &s : subs)
            walk(QDir(d.absoluteFilePath(s)), depth + 1);
    };
    walk(root, 1);
    return models;
}

QString QwenManager::bestModelPath() const
{
    QStringList candidates = availableModels();
    if (candidates.isEmpty())
        return QString();
    std::sort(candidates.begin(), candidates.end(), [](const QString &a, const QString &b) {
        return qwenQualityScore(QFileInfo(a).fileName())
               > qwenQualityScore(QFileInfo(b).fileName());
    });
    return candidates.first();
}

void QwenManager::setModelPath(const QString &path)
{
    m_modelPath = path;
}

QString QwenManager::modelAlias() const
{
    return QFileInfo(m_modelPath).completeBaseName();
}

bool QwenManager::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void QwenManager::start()
{
    if (isRunning())
        return;

    if (!locateServerExe()) {
        emit failed(QStringLiteral("未找到 llama-server.exe，请确认模型目录正确"));
        return;
    }
    if (m_modelPath.isEmpty() || !QFileInfo::exists(m_modelPath))
        m_modelPath = bestModelPath();
    if (m_modelPath.isEmpty()) {
        emit failed(QStringLiteral("模型目录中没有 Qwen .gguf 模型文件"));
        return;
    }

    m_process = new QProcess(this);
    m_process->setWorkingDirectory(m_launcherDir);
    connect(m_process, &QProcess::errorOccurred, this, &QwenManager::onProcessError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) { onProcessFinished(code); });
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &QwenManager::onReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &QwenManager::onReadyReadStandardOutput);

    QStringList args;
    args << QStringLiteral("--model") << m_modelPath
         << QStringLiteral("-c") << QString::number(kContextLength)
         << QStringLiteral("-ngl") << QString::number(ModelGpuConfig::layersFor("qwen", m_modelPath))
         << QStringLiteral("--batch-size") << QStringLiteral("256")
         << QStringLiteral("-a") << modelAlias()
         << QStringLiteral("--host") << kHost
         << QStringLiteral("--port") << kPort
         << QStringLiteral("-np") << QString::number(kParallel)
         << QStringLiteral("--cache-type-k") << QStringLiteral("f16")
         << QStringLiteral("--no-mmap");

    m_started = false;
    m_logBuffer.clear();
    m_process->start(m_serverExe, args);
    setupKillOnClose();
    emit logMessage(QStringLiteral("正在启动 Qwen 模型: %1…")
                        .arg(QFileInfo(m_modelPath).fileName()));
}

void QwenManager::stop()
{
    if (!m_process) {
        closeJob();
        return;
    }
    QProcess *p = m_process;
    m_process = nullptr;
    m_started = false;

    p->disconnect(this);
    if (p->state() != QProcess::NotRunning) {
        p->kill();
        p->waitForFinished(2000);
    }
    p->deleteLater();
    closeJob();
    emit stopped();
}

void QwenManager::onProcessError()
{
    if (!m_process)
        return;
    emit failed(QStringLiteral("启动失败: %1").arg(m_process->errorString()));
    stop();
}

void QwenManager::onProcessFinished(int exitCode)
{
    if (!m_process)
        return;
    const bool wasStarted = m_started;
    const QString tail = m_logBuffer.right(800).trimmed();
    m_started = false;
    m_process->disconnect(this);
    m_process->deleteLater();
    m_process = nullptr;
    closeJob();

    if (!wasStarted) {
        QString msg = QStringLiteral("Qwen 模型启动失败（退出码 %1）").arg(exitCode);
        if (!tail.isEmpty())
            msg += QStringLiteral("\n%1").arg(tail);
        emit failed(msg);
    } else {
        emit stopped();
    }
}

void QwenManager::onReadyReadStandardOutput()
{
    if (!m_process)
        return;
    m_logBuffer += QString::fromUtf8(m_process->readAllStandardOutput());
    m_logBuffer += QString::fromUtf8(m_process->readAllStandardError());
    if (!m_started && m_logBuffer.contains(QStringLiteral("listening"))) {
        m_started = true;
        emit started();
    }
}

bool QwenManager::locateServerExe()
{
    if (m_launcherDir.isEmpty())
        return false;
    m_serverExe = m_launcherDir + QStringLiteral("/llama/llama-server.exe");
    return QFileInfo::exists(m_serverExe);
}

void QwenManager::setupKillOnClose()
{
    if (!m_process || !m_process->waitForStarted(5000))
        return;
    const qint64 pid = m_process->processId();
    if (pid <= 0)
        return;

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job)
        return;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
    ZeroMemory(&info, sizeof(info));
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
        CloseHandle(job);
        return;
    }

    HANDLE proc = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                              static_cast<DWORD>(pid));
    if (proc) {
        if (AssignProcessToJobObject(job, proc))
            m_job = job;
        CloseHandle(proc);
    }
    if (m_job != job)
        CloseHandle(job);
}

void QwenManager::closeJob()
{
    if (m_job) {
        CloseHandle(static_cast<HANDLE>(m_job));
        m_job = nullptr;
    }
}
