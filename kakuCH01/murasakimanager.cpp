#include "murasakimanager.h"
#include "modelgpuconfig.h"

#include <QProcess>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <algorithm>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Murasaki 推理模型推荐参数：
//  - 上下文需要同时容纳 system prompt + 术语表 + 历史 + 正文，并给思考过程留余量；
//  - 串行翻译（-np 1）避免推理模型显存峰值叠加。
static const int kContextLength = 8192;
static const int kParallel = 1;
static const QString kHost = QStringLiteral("127.0.0.1");
static const QString kPort = QStringLiteral("8081");

// 根据 Murasaki 模型文件名评估“翻译质量”分数，用于挑选最优模型。
// 量化精度优先，其次参数规模。
static int murasakiQualityScore(const QString &fileName)
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

    if (n.contains(QStringLiteral("xxs")))
        score -= 10;

    if (n.contains(QStringLiteral("8b")))
        score += 60;
    else if (n.contains(QStringLiteral("7b")))
        score += 50;

    return score;
}

MurasakiManager::MurasakiManager(QObject *parent)
    : QObject(parent)
    , m_serverUrl(QStringLiteral("http://127.0.0.1:8081"))
{
}

MurasakiManager::~MurasakiManager()
{
    stop();
}

void MurasakiManager::setLauncherDir(const QString &dir)
{
    m_launcherDir = dir;
    while (m_launcherDir.endsWith(QLatin1Char('/')) || m_launcherDir.endsWith(QLatin1Char('\\')))
        m_launcherDir.chop(1);
}

void MurasakiManager::setModelDir(const QString &dir)
{
    m_modelDir = dir;
    while (m_modelDir.endsWith(QLatin1Char('/')) || m_modelDir.endsWith(QLatin1Char('\\')))
        m_modelDir.chop(1);
}

QStringList MurasakiManager::availableModels() const
{
    QStringList models;
    if (m_modelDir.isEmpty())
        return models;

    QDirIterator files(m_modelDir, QDir::Files | QDir::Readable,
                       QDirIterator::Subdirectories);
    while (files.hasNext()) {
        files.next();
        const QFileInfo file = files.fileInfo();
        if (file.suffix().compare(QStringLiteral("gguf"), Qt::CaseInsensitive) != 0
            || !file.fileName().contains(QStringLiteral("murasaki"), Qt::CaseInsensitive))
            continue;
        models << file.absoluteFilePath();
    }
    models.sort(Qt::CaseInsensitive);
    return models;
}

QString MurasakiManager::bestModelPath() const
{
    QStringList candidates = availableModels();
    if (candidates.isEmpty())
        return QString();

    std::sort(candidates.begin(), candidates.end(), [](const QString &a, const QString &b) {
        return murasakiQualityScore(QFileInfo(a).fileName())
               > murasakiQualityScore(QFileInfo(b).fileName());
    });
    return candidates.first();
}

void MurasakiManager::setModelPath(const QString &path)
{
    m_modelPath = path;
}

QString MurasakiManager::modelAlias() const
{
    return QFileInfo(m_modelPath).completeBaseName();
}

bool MurasakiManager::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void MurasakiManager::start()
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
        emit failed(QStringLiteral("模型目录中没有 Murasaki .gguf 模型文件"));
        return;
    }

    m_process = new QProcess(this);
    m_process->setWorkingDirectory(m_launcherDir);
    connect(m_process, &QProcess::errorOccurred, this, &MurasakiManager::onProcessError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) { onProcessFinished(code); });
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &MurasakiManager::onReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &MurasakiManager::onReadyReadStandardOutput);

    QStringList args;
    args << QStringLiteral("--model") << m_modelPath
         << QStringLiteral("-c") << QString::number(kContextLength)
         << QStringLiteral("-ngl") << QString::number(ModelGpuConfig::layersFor("murasaki", m_modelPath))
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
    emit logMessage(QStringLiteral("正在启动 Murasaki 模型: %1…")
                        .arg(QFileInfo(m_modelPath).fileName()));
}

void MurasakiManager::stop()
{
    if (!m_process) {
        // 进程对象已不在，但仍需清理 Job 句柄，避免残留 job 内进程。
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
    // 兜底：即使 QProcess 状态失联，关闭 Job 句柄也会触发 KILL_ON_JOB_CLOSE，
    // 强制终止 Job 内的 llama-server。
    closeJob();
    emit stopped();
}

void MurasakiManager::onProcessError()
{
    if (!m_process)
        return;
    emit failed(QStringLiteral("启动失败: %1").arg(m_process->errorString()));
    stop();
}

void MurasakiManager::onProcessFinished(int exitCode)
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
        // 进程在“就绪(listening)”前自行退出：把 llama-server 的报错回传给界面，
        // 避免用户只看到“已关闭”却不知道启动失败原因。
        QString msg = QStringLiteral("Murasaki 模型启动失败（退出码 %1）").arg(exitCode);
        if (!tail.isEmpty())
            msg += QStringLiteral("\n%1").arg(tail);
        emit failed(msg);
    } else {
        emit stopped();
    }
}

void MurasakiManager::onReadyReadStandardOutput()
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

bool MurasakiManager::locateServerExe()
{
    if (m_launcherDir.isEmpty())
        return false;
    m_serverExe = m_launcherDir + QStringLiteral("/llama/llama-server.exe");
    return QFileInfo::exists(m_serverExe);
}

void MurasakiManager::setupKillOnClose()
{
    if (!m_process || !m_process->waitForStarted(5000))
        return;
    const qint64 pid = m_process->processId();
    if (pid <= 0)
        return;

    // 创建带 KILL_ON_JOB_CLOSE 的 Job：应用进程退出（含崩溃）时，系统会杀死 Job 内的 llama-server，
    // 避免留下占用显存/端口的孤儿进程。
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
            m_job = job;   // 分配成功，由 manager 持有句柄
        CloseHandle(proc);
    }
    if (m_job != job)
        CloseHandle(job);
}

void MurasakiManager::closeJob()
{
    if (m_job) {
        CloseHandle(static_cast<HANDLE>(m_job));
        m_job = nullptr;
    }
}
