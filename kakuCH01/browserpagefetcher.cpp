#include "browserpagefetcher.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

BrowserPageFetcher::BrowserPageFetcher(QObject *parent)
    : QObject(parent)
{
    m_profileDir = new QTemporaryDir(QDir::tempPath()
                                     + QStringLiteral("/kaku-browser-XXXXXX"));
}

BrowserPageFetcher::~BrowserPageFetcher()
{
    abort();
    delete m_profileDir;
}

bool BrowserPageFetcher::isBusy() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

QString BrowserPageFetcher::browserExecutable() const
{
    const QStringList candidates = {
        QStringLiteral("C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"),
        QStringLiteral("C:/Program Files/Microsoft/Edge/Application/msedge.exe"),
        QStringLiteral("C:/Program Files/Google/Chrome/Application/chrome.exe"),
        QStringLiteral("C:/Program Files (x86)/Google/Chrome/Application/chrome.exe"),
    };
    for (const QString &path : candidates) {
        if (QFileInfo::exists(path))
            return path;
    }
    const QString edge = QStandardPaths::findExecutable(QStringLiteral("msedge.exe"));
    if (!edge.isEmpty())
        return edge;
    return QStandardPaths::findExecutable(QStringLiteral("chrome.exe"));
}

void BrowserPageFetcher::get(const QString &url, const QVariant &context)
{
    if (isBusy()) {
        emit requestFinished(url, context, QByteArray(), false,
                             QStringLiteral("浏览器页面读取任务仍在进行"));
        return;
    }

    const QString browser = browserExecutable();
    if (browser.isEmpty()) {
        emit requestFinished(url, context, QByteArray(), false,
                             QStringLiteral("未找到 Microsoft Edge 或 Google Chrome"));
        return;
    }
    if (!m_profileDir || !m_profileDir->isValid()) {
        emit requestFinished(url, context, QByteArray(), false,
                             QStringLiteral("无法创建浏览器临时配置目录"));
        return;
    }

    m_pendingUrl = url;
    m_pendingContext = context;
    m_aborted = false;
    m_timedOut = false;

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->setProperty("url", url);
    m_process->setProperty("context", context);
    QProcess *process = m_process;

    connect(m_process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError error) {
        if (m_process != process || m_aborted || m_timedOut)
            return;
        if (error == QProcess::FailedToStart)
            finishRequest(false, QStringLiteral("浏览器启动失败: %1")
                                     .arg(m_process->errorString()));
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process](int exitCode, QProcess::ExitStatus status) {
        if (m_process != process || m_aborted)
            return;
        const QByteArray html = m_process->readAllStandardOutput();
        const QString browserError =
            QString::fromLocal8Bit(m_process->readAllStandardError()).trimmed();
        if (m_timedOut) {
            finishRequest(false, QStringLiteral("浏览器读取页面超时"));
            return;
        }
        if (status != QProcess::NormalExit || exitCode != 0) {
            finishRequest(false, QStringLiteral("浏览器读取失败（退出码 %1）%2")
                                     .arg(exitCode)
                                     .arg(browserError.isEmpty()
                                              ? QString()
                                              : QStringLiteral(": ") + browserError.left(600)));
            return;
        }
        if (html.trimmed().isEmpty()) {
            finishRequest(false, QStringLiteral("浏览器未返回页面内容"));
            return;
        }
        finishRequest(true, QString(), html);
    });

    QTimer::singleShot(75000, process, [this, process]() {
        if (m_process != process || process->state() == QProcess::NotRunning)
            return;
        m_timedOut = true;
        m_process->kill();
    });

    const QStringList args = {
        QStringLiteral("--headless=new"),
        QStringLiteral("--disable-gpu"),
        QStringLiteral("--no-first-run"),
        QStringLiteral("--no-default-browser-check"),
        QStringLiteral("--disable-default-apps"),
        QStringLiteral("--disable-sync"),
        QStringLiteral("--lang=ja-JP"),
        QStringLiteral("--user-data-dir=%1").arg(m_profileDir->path()),
        QStringLiteral("--virtual-time-budget=15000"),
        QStringLiteral("--dump-dom"),
        url,
    };
    m_process->start(browser, args);
}

void BrowserPageFetcher::abort()
{
    if (!m_process)
        return;
    m_aborted = true;
    QProcess *process = m_process;
    m_process = nullptr;
    disconnect(process, nullptr, this, nullptr);
    if (process->state() != QProcess::NotRunning) {
        process->kill();
        process->waitForFinished(1000);
    }
    process->deleteLater();
}

void BrowserPageFetcher::finishRequest(bool ok, const QString &errorString,
                                       const QByteArray &data)
{
    if (!m_process)
        return;

    QProcess *process = m_process;
    m_process = nullptr;
    const QByteArray html = ok ? data : QByteArray();
    const QString url = m_pendingUrl;
    const QVariant context = m_pendingContext;
    process->deleteLater();
    emit requestFinished(url, context, html, ok, errorString);
}
