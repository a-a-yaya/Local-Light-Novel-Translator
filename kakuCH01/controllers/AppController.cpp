#include "controllers/AppController.h"
#include "modelgpuconfig.h"
#include <QThread>
#include <memory>
#include "novelpaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QSettings>
#include <QRegularExpression>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QDateTime>
#include <QSet>
#include <QDesktopServices>
#include <QUrl>
#include <QUuid>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextTable>

#include "models/TranslationModel.h"
#include "models/LibraryModel.h"
#include "models/ReaderModel.h"
#include "cjdlexporter.h"
#include "novelstore.h"
#include "txtexporter.h"

static const QString kStatusStopped = QStringLiteral("未运行");
static const QString kStatusStarting = QStringLiteral("启动中");
static const QString kStatusRunning = QStringLiteral("运行中");
static const QString kStatusFailed = QStringLiteral("启动失败");

namespace {
bool removeDocumentWithReaderEdits(const QString &path)
{
    const QFileInfo document(path);
    const QString edited = document.dir().filePath(
        QStringLiteral(".reader-edits/") + document.fileName());
    const bool hasEdit = QFileInfo::exists(edited);
    const QString pending = edited + QStringLiteral(".deleting-")
                            + QUuid::createUuid().toString(QUuid::Id128);
    // 暂存编辑稿，原版删除失败时恢复，避免失败操作丢失手动修改。
    if (hasEdit && (!QFileInfo(edited).isFile() || !QFile::rename(edited, pending))) {
        qWarning() << "[Reader] 无法移除编辑版:" << edited;
        return false;
    }
    if (QFileInfo::exists(path) && !QFile::remove(path)) {
        if (hasEdit && !QFile::rename(pending, edited))
            qWarning() << "[Reader] 无法恢复编辑版，保留在:" << pending;
        qWarning() << "[Reader] 无法删除原版:" << path;
        return false;
    }
    if (hasEdit && !QFile::remove(pending)) {
        qWarning() << "[Reader] 无法清理已移除的编辑版:" << pending;
        return false;
    }
    return true;
}

QVariantMap glossaryTermData(const GlossaryTerm &term)
{
    return {{QStringLiteral("source"), term.source},
            {QStringLiteral("target"), term.target},
            {QStringLiteral("type"), term.type},
            {QStringLiteral("note"), term.note},
            {QStringLiteral("enabled"), term.enabled},
            {QStringLiteral("origin"), term.origin}};
}

QVariantList glossaryTermsData(const QList<GlossaryTerm> &terms)
{
    QVariantList result;
    result.reserve(terms.size());
    for (const GlossaryTerm &term : terms)
        result.append(glossaryTermData(term));
    return result;
}

QStringList readParagraphsForCjdl(const QString &path)
{
    QStringList result;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return result;
    QByteArray raw = f.readAll();
    f.close();
    if (raw.startsWith("\xEF\xBB\xBF"))
        raw.remove(0, 3);
    QString body;
    TxtExporter::splitChapterDocument(QString::fromUtf8(raw), nullptr, &body);
    const QStringList lines = body.split(QLatin1Char('\n'));

    // 模型可能合并或拆分行；这里只读取完整正文，不假定两侧行数相同。
    for (const QString &line : lines) {
        const QString p = line.trimmed();
        if (!p.isEmpty())
            result << p;
    }
    return result;
}
} // namespace

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_sakura(new SakuraManager(this))
    , m_murasaki(new MurasakiManager(this))
    , m_qwen(new QwenManager(this))
    , m_service(new KakuyomuService(this))
    , m_updateService(new KakuyomuService(this))
    , m_translator(new SakuraTranslator(this))
    , m_murasakiTranslator(new MurasakiTranslator(this))
    , m_qwenTranslator(new QwenTranslator(this))
    , m_siteBrowser(new SiteBrowser(this))
    , m_chapterModel(new ChapterModel(this))
    , m_translationModel(new TranslationModel(this))
    , m_libraryModel(new LibraryModel(this))
    , m_readerModel(new ReaderModel(this))
{
    QSettings settings;
    const QString root = workspaceRootDir();
    m_saveRoot = settings.value(QStringLiteral("paths/saveRoot")).toString();
    if (m_saveRoot.isEmpty())
        m_saveRoot = defaultSaveRoot();
    m_generateBilingual = settings.value(QStringLiteral("paths/generateBilingual"), false).toBool();
    m_sakuraDir = settings.value(QStringLiteral("paths/sakuraDir"),
                                 root + QStringLiteral("/Sakura_Launcher_GUI_v1.2.0")).toString();
    m_modelDir = settings.value(QStringLiteral("paths/modelDir"), m_sakuraDir).toString();
    m_murasakiDir = settings.value(QStringLiteral("paths/murasakiDir"), m_sakuraDir).toString();
    m_qwenDir = settings.value(QStringLiteral("paths/qwenDir"), m_sakuraDir).toString();

    m_sakura->setSakuraDir(m_sakuraDir);
    m_sakura->setModelDir(m_modelDir);
    // 上次选中的模型：存在且有效就沿用
    const QString sakuraModel = settings.value(QStringLiteral("paths/sakuraModel")).toString();
    if (!sakuraModel.isEmpty() && QFileInfo::exists(sakuraModel))
        m_sakura->setModelPath(sakuraModel);
    m_murasaki->setLauncherDir(m_murasakiDir);
    m_murasaki->setModelDir(m_modelDir);
    const QString murasakiModel = settings.value(QStringLiteral("paths/murasakiModel")).toString();
    if (!murasakiModel.isEmpty() && QFileInfo::exists(murasakiModel))
        m_murasaki->setModelPath(murasakiModel);
    else if (m_murasaki->modelPath().isEmpty() || !QFileInfo::exists(m_murasaki->modelPath()))
        m_murasaki->setModelPath(m_murasaki->bestModelPath());
    m_qwen->setLauncherDir(m_qwenDir);
    m_qwen->setModelDir(m_modelDir);
    const QString qwenModel = settings.value(QStringLiteral("paths/qwenModel")).toString();
    if (!qwenModel.isEmpty() && QFileInfo::exists(qwenModel))
        m_qwen->setModelPath(qwenModel);
    else if (m_qwen->modelPath().isEmpty() || !QFileInfo::exists(m_qwen->modelPath()))
        m_qwen->setModelPath(m_qwen->bestModelPath());
    applyDerivedDirs();
    // 目录布局升级：把旧的 DOWNLOAD/<workId> 搬到 DOWNLOAD/<siteKey>/<workId>
    migrateLegacyLayouts();
    refreshLibrary();

    connect(m_sakura, &SakuraManager::started, this, &AppController::onSakuraStarted);
    connect(m_sakura, &SakuraManager::stopped, this, &AppController::onSakuraStopped);
    connect(m_sakura, &SakuraManager::failed, this, &AppController::onSakuraFailed);
    connect(m_sakura, &SakuraManager::logMessage, this, &AppController::onSakuraLog);

    connect(m_murasaki, &MurasakiManager::started, this, &AppController::onMurasakiStarted);
    connect(m_murasaki, &MurasakiManager::stopped, this, &AppController::onMurasakiStopped);
    connect(m_murasaki, &MurasakiManager::failed, this, &AppController::onMurasakiFailed);
    connect(m_murasaki, &MurasakiManager::logMessage, this, &AppController::onMurasakiLog);

    connect(m_qwen, &QwenManager::started, this, &AppController::onQwenStarted);
    connect(m_qwen, &QwenManager::stopped, this, &AppController::onQwenStopped);
    connect(m_qwen, &QwenManager::failed, this, &AppController::onQwenFailed);
    connect(m_qwen, &QwenManager::logMessage, this, &AppController::onQwenLog);

    connect(m_service, &KakuyomuService::workReady, this, &AppController::onWorkReady);
    connect(m_service, &KakuyomuService::workFailed, this, &AppController::onWorkFailed);
    connect(m_service, &KakuyomuService::progressChanged, this, &AppController::onDownloadProgress);
    connect(m_service, &KakuyomuService::downloadFinished, this, &AppController::onDownloadFinished);

    // 「检查更新 / 更新章节」走独立 service，避免影响下载页状态
    connect(m_updateService, &KakuyomuService::workReady, this, &AppController::onUpdateWorkReady);
    connect(m_updateService, &KakuyomuService::workFailed, this, &AppController::onUpdateWorkFailed);
    connect(m_updateService, &KakuyomuService::progressChanged, this, &AppController::onUpdateProgress);
    connect(m_updateService, &KakuyomuService::downloadFinished,
            this, &AppController::onUpdateDownloadFinished);

    connect(m_translator, &SakuraTranslator::progressChanged, this, &AppController::onTranslateProgress);
    connect(m_translator, &SakuraTranslator::fileStatusChanged, this, &AppController::onTranslateFileStatus);
    connect(m_translator, &SakuraTranslator::translationFinished, this, &AppController::onTranslateFinished);

    connect(m_murasakiTranslator, &MurasakiTranslator::progressChanged,
            this, &AppController::onMurasakiTranslateProgress);
    connect(m_murasakiTranslator, &MurasakiTranslator::fileStatusChanged,
            this, &AppController::onMurasakiTranslateFileStatus);
    connect(m_murasakiTranslator, &MurasakiTranslator::translationFinished,
            this, &AppController::onMurasakiTranslateFinished);

    connect(m_qwenTranslator, &QwenTranslator::progressChanged,
            this, &AppController::onQwenTranslateProgress);
    connect(m_qwenTranslator, &QwenTranslator::fileStatusChanged,
            this, &AppController::onQwenTranslateFileStatus);
    connect(m_qwenTranslator, &QwenTranslator::translationFinished,
            this, &AppController::onQwenTranslateFinished);
    connect(m_qwenTranslator, &QwenTranslator::glossaryProgressChanged,
            this, &AppController::onQwenGlossaryProgress);
    connect(m_qwenTranslator, &QwenTranslator::glossaryStageChanged,
            this, [this](const QString &label) {
                m_qwenExtractLabel = label;
                emit qwenExtractProgressChanged();
            });
    connect(m_qwenTranslator, &QwenTranslator::glossaryFinished,
            this, &AppController::onQwenGlossaryFinished);

    connect(m_siteBrowser, &SiteBrowser::workOpened, this, &AppController::onSiteWorkOpened);
    connect(m_siteBrowser, &SiteBrowser::workStatsReady, this, &AppController::onSiteWorkStats);
    connect(m_siteBrowser, &SiteBrowser::workOpenFailed, this, &AppController::onSiteWorkFailed);
    connect(m_siteBrowser, &SiteBrowser::cardsReady, this, &AppController::onSiteCardsReady);
    connect(m_siteBrowser, &SiteBrowser::cardsFailed, this, &AppController::onSiteCardsFailed);
    connect(m_siteBrowser, &SiteBrowser::searchPageReady, this, &AppController::onSitePageReady);
    connect(m_siteBrowser, &SiteBrowser::commentsReady, this, &AppController::onSiteCommentsReady);
    connect(m_siteBrowser, &SiteBrowser::commentsFailed, this, &AppController::onSiteCommentsFailed);
    connect(m_siteBrowser, &SiteBrowser::authorResolved, this, &AppController::onSiteAuthorResolved);

    // Sakura 默认由翻译器按所选模型使用官方 Prompt，不再自动覆盖成旧预设。

    updateModelUi();
    emit murasakiModelNameChanged();
    emit qwenModelNameChanged();
    connect(m_chapterModel, &QAbstractItemModel::dataChanged, this, &AppController::updateSelectedCount);
    connect(m_chapterModel, &QAbstractItemModel::modelReset, this, &AppController::updateSelectedCount);
}

AppController::~AppController()
{
    shutdown();
}

QString AppController::workspaceRootDir() const
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        if (dir.exists(QStringLiteral("CMakeLists.txt"))) {
            QDir parent = dir;
            if (parent.cdUp())
                return parent.absolutePath();
        }
        if (!dir.cdUp())
            break;
    }
    return QCoreApplication::applicationDirPath();
}

// ---------------- Sakura ----------------

bool AppController::sakuraRunning() const { return m_sakuraRunning; }
bool AppController::sakuraStarting() const { return m_sakuraStarting; }
QString AppController::sakuraStatus() const { return m_sakuraStatus; }
QString AppController::modelName() const { return QFileInfo(m_sakura->modelPath()).fileName(); }
QString AppController::serverUrl() const { return m_sakura->serverUrl(); }

ChapterModel *AppController::chapterModel() const { return m_chapterModel; }
TranslationModel *AppController::translationModel() const { return m_translationModel; }
LibraryModel *AppController::libraryModel() const { return m_libraryModel; }
ReaderModel *AppController::readerModel() const { return m_readerModel; }

void AppController::startSakura()
{
    if (anyTranslateBusy()) return;
    if (m_sakuraRunning || m_sakuraStarting)
        return;
    // 三个模型共用显存，启动任何一个前先关掉其它模型。
    if (m_murasakiRunning || m_murasakiStarting)
        stopMurasaki();
    if (m_qwenRunning || m_qwenStarting)
        stopQwen();
    m_sakuraStarting = true;
    m_sakuraStatus = kStatusStarting;
    emit sakuraStartingChanged();
    emit sakuraStatusChanged();
    m_sakura->start();
}

void AppController::stopSakura()
{
    if (anyTranslateBusy()) return;
    m_sakura->stop();
}

void AppController::browseModelDir(const QString &dir)
{
    setModelDir(dir);
}

void AppController::refreshModels()
{
    if (m_sakura->modelPath().isEmpty() || !QFileInfo::exists(m_sakura->modelPath()))
        m_sakura->setModelPath(m_sakura->bestModelPath());
    updateModelUi();
}

void AppController::setModelPath(const QString &path)
{
    m_sakura->setModelPath(path);
    QSettings settings;
    settings.setValue(QStringLiteral("paths/sakuraModel"), path);
    updateModelUi();
}

void AppController::setModelDir(const QString &dir)
{
    if (dir.trimmed().isEmpty())
        return;
    m_modelDir = dir;
    QSettings settings;
    settings.setValue(QStringLiteral("paths/modelDir"), dir);
    m_sakura->setModelDir(dir);
    m_sakura->setModelPath(m_sakura->bestModelPath());
    m_murasaki->setModelDir(dir);
    m_murasaki->setModelPath(m_murasaki->bestModelPath());
    m_qwen->setModelDir(dir);
    m_qwen->setModelPath(m_qwen->bestModelPath());
    // 新目录选出的模型同时写回，避免重启后恢复仍存在的旧目录模型。
    settings.setValue(QStringLiteral("paths/sakuraModel"), m_sakura->modelPath());
    settings.setValue(QStringLiteral("paths/murasakiModel"), m_murasaki->modelPath());
    settings.setValue(QStringLiteral("paths/qwenModel"), m_qwen->modelPath());
    updateModelUi();
    emit murasakiModelNameChanged();
    emit murasakiServerUrlChanged();
    emit qwenModelNameChanged();
    emit qwenServerUrlChanged();
}

void AppController::setSakuraDir(const QString &dir)
{
    if (dir.trimmed().isEmpty())
        return;
    m_sakuraDir = dir;
    QSettings settings;
    settings.setValue(QStringLiteral("paths/sakuraDir"), dir);
    m_sakura->setSakuraDir(dir);
    m_sakura->setModelDir(m_modelDir);
    updateModelUi();
}

void AppController::updateModelUi()
{
    emit modelNameChanged();
    emit serverUrlChanged();
}

void AppController::onSakuraStarted()
{
    m_sakuraRunning = true;
    m_sakuraStarting = false;
    m_sakuraStatus = kStatusRunning;
    emit sakuraRunningChanged();
    emit sakuraStartingChanged();
    emit sakuraStatusChanged();
    emit serverUrlChanged();
    emit notify(QStringLiteral("Sakura 模型已就绪"));
}

void AppController::onSakuraStopped()
{
    m_sakuraRunning = false;
    m_sakuraStarting = false;
    m_sakuraStatus = kStatusStopped;
    emit sakuraRunningChanged();
    emit sakuraStartingChanged();
    emit sakuraStatusChanged();
    emit notify(QStringLiteral("Sakura 模型已关闭"));
}

void AppController::onSakuraFailed(const QString &error)
{
    m_sakuraRunning = false;
    m_sakuraStarting = false;
    m_sakuraStatus = kStatusFailed;
    emit sakuraRunningChanged();
    emit sakuraStartingChanged();
    emit sakuraStatusChanged();
    emit notify(error);
}

void AppController::onSakuraLog(const QString &message)
{
    if (m_sakuraStarting)
        emit notify(message);
}

// ---------------- Murasaki ----------------

bool AppController::murasakiRunning() const { return m_murasakiRunning; }
bool AppController::murasakiStarting() const { return m_murasakiStarting; }
QString AppController::murasakiStatus() const { return m_murasakiStatus; }
QString AppController::murasakiModelName() const { return QFileInfo(m_murasaki->modelPath()).fileName(); }
QString AppController::murasakiServerUrl() const { return m_murasaki->serverUrl(); }

bool AppController::murasakiTranslating() const { return m_murasakiTranslating; }

QString AppController::murasakiDir() const { return m_murasakiDir; }
QStringList AppController::murasakiAvailableModels() const { return m_murasaki->availableModels(); }
QString AppController::murasakiQualityPrompt() const
{
    return MurasakiTranslator::defaultPrompt();
}

void AppController::startMurasaki()
{
    if (anyTranslateBusy()) return;
    if (m_murasakiRunning || m_murasakiStarting)
        return;
    // 三个模型共用显存，启动任何一个前先关掉其它模型。
    if (m_sakuraRunning || m_sakuraStarting)
        stopSakura();
    if (m_qwenRunning || m_qwenStarting)
        stopQwen();
    m_murasakiStarting = true;
    m_murasakiStatus = kStatusStarting;
    emit murasakiStartingChanged();
    emit murasakiStatusChanged();
    m_murasaki->start();
}

void AppController::stopMurasaki()
{
    if (anyTranslateBusy()) return;
    m_murasaki->stop();
}

void AppController::refreshMurasakiModels()
{
    if (m_murasaki->modelPath().isEmpty() || !QFileInfo::exists(m_murasaki->modelPath()))
        m_murasaki->setModelPath(m_murasaki->bestModelPath());
    emit murasakiModelNameChanged();
    emit murasakiServerUrlChanged();
}

void AppController::setMurasakiModelPath(const QString &path)
{
    m_murasaki->setModelPath(path);
    QSettings settings;
    settings.setValue(QStringLiteral("paths/murasakiModel"), path);
    emit murasakiModelNameChanged();
    emit murasakiServerUrlChanged();
}

void AppController::setMurasakiDir(const QString &dir)
{
    if (dir.trimmed().isEmpty())
        return;
    m_murasakiDir = dir;
    QSettings settings;
    settings.setValue(QStringLiteral("paths/murasakiDir"), dir);
    m_murasaki->setLauncherDir(dir);
    m_murasaki->setModelDir(m_modelDir);
    emit murasakiModelNameChanged();
    emit murasakiServerUrlChanged();
}

void AppController::onMurasakiStarted()
{
    m_murasakiRunning = true;
    m_murasakiStarting = false;
    m_murasakiStatus = kStatusRunning;
    emit murasakiRunningChanged();
    emit murasakiStartingChanged();
    emit murasakiStatusChanged();
    emit murasakiServerUrlChanged();
    emit notify(QStringLiteral("Murasaki 模型已就绪"));
}

void AppController::onMurasakiStopped()
{
    m_murasakiRunning = false;
    m_murasakiStarting = false;
    m_murasakiStatus = kStatusStopped;
    emit murasakiRunningChanged();
    emit murasakiStartingChanged();
    emit murasakiStatusChanged();
    emit notify(QStringLiteral("Murasaki 模型已关闭"));
}

void AppController::onMurasakiFailed(const QString &error)
{
    m_murasakiRunning = false;
    m_murasakiStarting = false;
    m_murasakiStatus = kStatusFailed;
    emit murasakiRunningChanged();
    emit murasakiStartingChanged();
    emit murasakiStatusChanged();
    emit notify(error);
}

void AppController::onMurasakiLog(const QString &message)
{
    if (m_murasakiStarting)
        emit notify(message);
}

// ---------------- Qwen ----------------

bool AppController::qwenRunning() const { return m_qwenRunning; }
bool AppController::qwenStarting() const { return m_qwenStarting; }
QString AppController::qwenStatus() const { return m_qwenStatus; }
QString AppController::qwenModelName() const { return QFileInfo(m_qwen->modelPath()).fileName(); }
QString AppController::qwenServerUrl() const { return m_qwen->serverUrl(); }
QString AppController::qwenDir() const { return m_qwenDir; }
QStringList AppController::qwenAvailableModels() const { return m_qwen->availableModels(); }
QString AppController::qwenQualityPrompt() const { return QwenTranslator::defaultPrompt(); }
QString AppController::qwenGlossaryPrompt() const { return QwenTranslator::glossaryPrompt(); }

bool AppController::qwenTranslating() const { return m_qwenTranslating; }
bool AppController::qwenExtracting() const { return m_qwenExtracting; }

void AppController::startQwen()
{
    if (anyTranslateBusy()) return;
    if (m_qwenRunning || m_qwenStarting)
        return;
    // 三个模型服务共用同一块显存，一次只运行一个。
    if (m_sakuraRunning || m_sakuraStarting)
        stopSakura();
    if (m_murasakiRunning || m_murasakiStarting)
        stopMurasaki();

    m_qwenStarting = true;
    m_qwenStatus = kStatusStarting;
    emit qwenStartingChanged();
    emit qwenStatusChanged();
    m_qwen->start();
}

void AppController::stopQwen()
{
    if (anyTranslateBusy()) return;
    m_qwen->stop();
}

void AppController::refreshQwenModels()
{
    if (m_qwen->modelPath().isEmpty() || !QFileInfo::exists(m_qwen->modelPath()))
        m_qwen->setModelPath(m_qwen->bestModelPath());
    emit qwenModelNameChanged();
    emit qwenServerUrlChanged();
}

void AppController::setQwenModelPath(const QString &path)
{
    m_qwen->setModelPath(path);
    QSettings settings;
    settings.setValue(QStringLiteral("paths/qwenModel"), path);
    emit qwenModelNameChanged();
    emit qwenServerUrlChanged();
}

void AppController::setQwenDir(const QString &dir)
{
    if (dir.trimmed().isEmpty())
        return;
    m_qwenDir = dir;
    QSettings settings;
    settings.setValue(QStringLiteral("paths/qwenDir"), dir);
    m_qwen->setLauncherDir(dir);
    m_qwen->setModelDir(m_modelDir);
    emit qwenModelNameChanged();
    emit qwenServerUrlChanged();
}

void AppController::onQwenStarted()
{
    m_qwenRunning = true;
    m_qwenStarting = false;
    m_qwenStatus = kStatusRunning;
    emit qwenRunningChanged();
    emit qwenStartingChanged();
    emit qwenStatusChanged();
    emit qwenServerUrlChanged();
    emit notify(QStringLiteral("Qwen 模型已就绪"));
}

void AppController::onQwenStopped()
{
    m_qwenRunning = false;
    m_qwenStarting = false;
    m_qwenStatus = kStatusStopped;
    emit qwenRunningChanged();
    emit qwenStartingChanged();
    emit qwenStatusChanged();
    emit notify(QStringLiteral("Qwen 模型已关闭"));
}

void AppController::onQwenFailed(const QString &error)
{
    m_qwenRunning = false;
    m_qwenStarting = false;
    m_qwenStatus = kStatusFailed;
    emit qwenRunningChanged();
    emit qwenStartingChanged();
    emit qwenStatusChanged();
    emit notify(error);
}

void AppController::onQwenLog(const QString &message)
{
    if (m_qwenStarting)
        emit notify(message);
}

void AppController::startQwenTranslate(const QString &outputDir)
{
    refreshTranslationQueueStatuses();
    const QStringList files = m_translationModel->selectedPaths();
    if (files.isEmpty()) {
        emit notify(QStringLiteral("请先添加并勾选要翻译的 txt 文件"));
        return;
    }
    if (anyTranslateBusy()) {
        emit notify(QStringLiteral("当前有其它翻译任务，请稍后再试"));
        return;
    }
    if (!m_qwenRunning) {
        emit notify(m_qwenStarting
                        ? QStringLiteral("Qwen 模型仍在加载，请等状态变为运行中后再开始")
                        : QStringLiteral("Qwen 模型尚未就绪，请先启动模型"));
        return;
    }
    QString dir = outputDir.trimmed();
    if (dir.isEmpty())
        dir = m_chdlDir;
    m_chdlDir = dir;
    QHash<QString, QString> userOutputs;
    if (!prepareUserTranslationOutputs(files, &userOutputs)) return;

    m_qwenTranslator->setServerUrl(m_qwen->serverUrl());
    m_qwenTranslator->setModelAlias(m_qwen->modelAlias());
    m_qwenTranslator->setSystemPrompt(QwenTranslator::defaultPrompt());

    m_qwenTranslateFiles = files;
    m_qwenTranslateOutputDirs.clear();
    QVector<QwenFileJob> jobs;
    for (const QString &path : files) {
        QwenFileJob job;
        job.path = path;
        job.outputDir = translationOutputDir(path, dir, userOutputs);
        jobs.append(job);
        m_qwenTranslateOutputDirs << job.outputDir;
    }

    m_qwenTranslating = true;
    m_qwenTranslateCurrent = 0;
    m_qwenTranslateTotal = 1;
    m_qwenTranslateMessage = QStringLiteral("开始 Qwen 翻译…");
    emit qwenTranslatingChanged();
    emit qwenTranslateProgressChanged();

    m_translationModel->resetStatuses();
    m_qwenTranslator->translateFiles(jobs);
}

void AppController::stopQwenTranslate()
{
    if (!m_qwenTranslating)
        return;
    m_qwenTranslator->abort();
    m_qwenTranslating = false;
    emit qwenTranslatingChanged();
    m_translationModel->resetStatuses();
    emit notify(QStringLiteral("Qwen 翻译已中止"));
}

void AppController::onQwenTranslateProgress(int current, int total, const QString &message)
{
    if (m_siteOverviewTranslating) {
        m_siteOverviewCurrent = current;
        m_siteOverviewTotal = total;
        m_siteOverviewMessage = message;
        emit siteOverviewProgressChanged();
        return;
    }
    m_qwenTranslateCurrent = current;
    m_qwenTranslateTotal = total;
    m_qwenTranslateMessage = message;
    emit qwenTranslateProgressChanged();
}

void AppController::onQwenTranslateFileStatus(const QString &path, const QString &status)
{
    m_translationModel->setFileStatus(path, status);
}

void AppController::onQwenTranslateFinished(int success, int failed)
{
    if (m_siteOverviewTranslating && m_siteOverviewModel == 2) {
        finishSiteOverviewTranslation(success);
        return;
    }
    m_qwenTranslating = false;
    emit qwenTranslatingChanged();

    updateTranslationFileStatuses(m_qwenTranslateFiles, m_qwenTranslateOutputDirs);

    emit notify(QStringLiteral("Qwen 翻译完成：成功 %1 个，失败 %2 个").arg(success).arg(failed));
    refreshLibrary();
}

// ---------------- Qwen 术语表提取 ----------------

void AppController::startQwenGlossaryExtraction()
{
    if (m_qwenExtracting)
        return;
    if (anyTranslateBusy()) {
        emit notify(QStringLiteral("当前有其它翻译任务，请稍后再试"));
        return;
    }
    if (!m_qwenRunning) {
        emit notify(m_qwenStarting
                        ? QStringLiteral("Qwen 模型仍在加载，请等状态变为运行中后再提取")
                        : QStringLiteral("Qwen 模型尚未就绪，请先启动模型"));
        return;
    }
    refreshTranslationQueueStatuses();
    // 术语表提取使用「提取术语」模式下的专用勾选（只读原文，已翻译章节同样有效）。
    const QStringList files = m_translationModel->glossarySelectedPaths();
    if (files.isEmpty()) {
        emit notify(QStringLiteral("请先添加章节，并在「提取术语」模式下勾选要提取的章节"));
        return;
    }

    m_qwenTranslator->setServerUrl(m_qwen->serverUrl());
    m_qwenTranslator->setModelAlias(m_qwen->modelAlias());

    m_qwenExtracting = true;
    m_qwenExtractCurrent = 0;
    m_qwenExtractTotal = 1;
    m_qwenExtractMessage = QStringLiteral("开始提取术语表…");
    m_qwenExtractLabel = QStringLiteral("提取术语");
    emit qwenExtractingChanged();
    emit qwenExtractProgressChanged();

    m_qwenTranslator->extractGlossary(files);
}

void AppController::cancelQwenGlossaryExtraction()
{
    if (!m_qwenExtracting)
        return;
    m_qwenTranslator->abortGlossary();
    m_qwenExtracting = false;
    emit qwenExtractingChanged();
    emit notify(QStringLiteral("术语表提取已取消"));
}

void AppController::onQwenGlossaryProgress(int current, int total, const QString &message)
{
    m_qwenExtractCurrent = current;
    m_qwenExtractTotal = total;
    m_qwenExtractMessage = message;
    emit qwenExtractProgressChanged();
}

void AppController::onQwenGlossaryFinished(int termCount, const QString &message)
{
    Q_UNUSED(termCount);
    m_qwenExtracting = false;
    emit qwenExtractingChanged();
    emit notify(message);
    // 术语表已更新，通知界面刷新（含书库详情页的术语表窗口）。
    emit glossaryChanged();
    refreshLibrary();
}

// ---------------- 下载 ----------------

bool AppController::fetching() const { return m_fetching; }
QString AppController::workTitle() const { return m_workTitle; }
QString AppController::workAuthor() const { return m_workAuthor; }
int AppController::chapterCount() const { return m_chapterModel->chapterCount(); }
int AppController::selectedCount() const { return m_chapterModel->selectedChapters().size(); }
bool AppController::downloading() const { return m_downloading; }

void AppController::fetchWork(const QString &url)
{
    if (m_downloading || m_fetching) return;
    if (url.trimmed().isEmpty()) {
        emit workFailed(QStringLiteral("请输入作品网址"));
        return;
    }
    m_fetching = true;
    emit fetchingChanged();
    m_service->fetchWork(url);
}

void AppController::clearDownloadWork()
{
    if (m_fetching || m_downloading)
        return;
    m_workTitle.clear();
    m_chapterModel->clear();
    m_downloadCurrent = 0;
    m_downloadTotal = 0;
    m_downloadMessage.clear();
    emit workTitleChanged();
    emit chapterCountChanged();
    emit selectedCountChanged();
    emit downloadProgressChanged();
}

void AppController::onWorkReady(const NovelWork &work)
{
    m_fetching = false;
    m_work = work;
    m_workTitle = work.title;
    m_workAuthor = work.author;
    m_chapterModel->setWork(work);

    // 增量更新：DOWNLOAD/<siteKey>/<workId>/chapters/<number>.txt（兼容旧的 <workId> 布局）
    QDir chaptersDir = QDir(novelRootForWork(work)).filePath(QStringLiteral("chapters"));
    int existingCount = 0;
    for (int i = 0; i < m_chapterModel->chapterCount(); ++i) {
        const Chapter ch = m_chapterModel->chapterAt(i);
        int number = ch.number > 0 ? ch.number : (i + 1);
        QString fileName = QString::number(number).rightJustified(3, QLatin1Char('0'))
                           + QStringLiteral(".txt");
        if (QFileInfo::exists(QDir(chaptersDir).filePath(fileName))) {
            m_chapterModel->setChapterStatus(ch.episodeId, QStringLiteral("已下载"));
            m_chapterModel->setData(m_chapterModel->index(i), false, ChapterModel::SelectedRole);
            ++existingCount;
        }
    }
    if (existingCount > 0)
        emit notify(QStringLiteral("本地已有 %1 章，发现 %2 个新章节")
                        .arg(existingCount).arg(work.chapters.size() - existingCount));

    emit fetchingChanged();
    emit workTitleChanged();
    emit workAuthorChanged();
    emit chapterCountChanged();
    updateSelectedCount();
    emit workReady();
}

void AppController::onWorkFailed(const QString &error)
{
    m_fetching = false;
    emit fetchingChanged();
    emit workFailed(error);
}

void AppController::downloadSelected(const QString &dir)
{
    QVector<Chapter> chapters = m_chapterModel->selectedChapters();
    if (chapters.isEmpty()) {
        emit notify(QStringLiteral("请先选择要下载的章节"));
        return;
    }
    QString saveDir = dir.trimmed();
    if (saveDir.isEmpty())
        saveDir = m_downloadDir;
    m_downloadDir = saveDir;

    m_downloading = true;
    m_downloadCurrent = 0;
    m_downloadTotal = chapters.size();
    emit downloadingChanged();
    emit downloadProgressChanged();

    m_chapterModel->resetStatuses();
    m_service->downloadChapters(m_work, chapters, saveDir);
}

void AppController::cancelDownload()
{
    if (!m_downloading)
        return;
    m_service->abort();
    m_downloading = false;
    emit downloadingChanged();
    emit notify(QStringLiteral("下载已中止"));
}

void AppController::setAllSelected(bool selected)
{
    if (anyTranslateBusy()) return;
    m_chapterModel->setAllSelected(selected);
    updateSelectedCount();
}

void AppController::invertSelection()
{
    if (anyTranslateBusy()) return;
    m_chapterModel->invertSelection();
    updateSelectedCount();
}

void AppController::setChapterFilter(const QString &filter)
{
    m_chapterModel->setFilter(filter);
}

void AppController::onDownloadProgress(int current, int total, const QString &message)
{
    m_downloadCurrent = current;
    m_downloadTotal = total;
    m_downloadMessage = message;
    emit downloadProgressChanged();
    emit downloadProgress(current, total, message);
}

void AppController::onDownloadFinished(int success, int failed)
{
    m_downloading = false;
    emit downloadingChanged();

    for (const Chapter &ch : m_chapterModel->selectedChapters())
        m_chapterModel->setChapterStatus(ch.episodeId, QStringLiteral("已下载"));

    emit notify(QStringLiteral("下载完成：成功 %1 个，失败 %2 个").arg(success).arg(failed));
    emit downloadFinished(success, failed);
    refreshLibrary();
}

void AppController::updateSelectedCount()
{
    emit selectedCountChanged();
}

// ---------------- 翻译 ----------------

bool AppController::translating() const { return m_translating; }

void AppController::addFiles(const QStringList &paths)
{
    // 任务进行中不允许改动翻译目录（界面上按钮也已置灰）
    if (anyTranslateBusy())
        return;
    refreshTranslationQueueStatuses();
    m_translationModel->addFiles(paths);
    refreshTranslationQueueStatuses();
}
void AppController::addFolder(const QString &dir)
{
    if (anyTranslateBusy())
        return;
    refreshTranslationQueueStatuses();
    m_translationModel->addFolder(dir);
    refreshTranslationQueueStatuses();
}
void AppController::removeSelectedFiles(bool glossaryMode)
{
    if (anyTranslateBusy())
        return;
    m_translationModel->removeSelected(glossaryMode);
}
void AppController::setTranslationSelection(bool selected, bool glossaryMode)
{
    if (!anyTranslateBusy()) m_translationModel->setSelection(selected, glossaryMode);
}

void AppController::invertTranslationSelection(bool glossaryMode)
{
    if (!anyTranslateBusy()) m_translationModel->setSelection(false, glossaryMode, true);
}

int AppController::addLibraryBookToQueue(int displayRow)
{
    if (anyTranslateBusy()) return 0;
    const int row = m_libraryModel->sourceRow(displayRow);
    if (row < 0) return 0;
    const QString dir = m_libraryModel->novelSourceDir(row);
    QStringList paths;
    for (const QString &name : m_libraryModel->novelChapterNames(row))
        paths.append(QDir(dir).filePath(name + QStringLiteral(".txt")));
    clearFiles();
    addFiles(paths);
    return paths.size();
}

void AppController::clearFiles()
{
    if (anyTranslateBusy())
        return;
    m_translationModel->clear();
}

void AppController::refreshTranslationQueueStatuses()
{
    for (const QString &path : m_translationModel->filePaths()) {
        if (isUserTranslationSource(path)) {
            // User files get a new output directory on every run.
            m_translationModel->setFileTranslated(path, false);
            continue;
        }
        QString outputDir = m_chdlDir;
        const QString rel = relativeKeyForSourceFile(path);
        if (!rel.isEmpty())
            outputDir = NovelPaths::translationChaptersDir(m_chdlDir, rel);
        const QString outputPath = QDir(outputDir).filePath(QFileInfo(path).fileName());
        m_translationModel->setFileTranslated(path, QFileInfo::exists(outputPath));
    }
}

void AppController::updateTranslationFileStatuses(const QStringList &files,
                                                  const QStringList &outputDirs)
{
    for (int i = 0; i < files.size(); ++i) {
        const QString outputDir = outputDirs.value(i, m_chdlDir);
        const QString outputPath = QDir(outputDir).filePath(QFileInfo(files[i]).fileName());
        m_translationModel->setFileStatus(
            files[i], QFileInfo::exists(outputPath) ? QStringLiteral("COMPLETED")
                                                  : QStringLiteral("FAILED"));
    }
}

void AppController::startTranslate(const QString &outputDir)
{
    refreshTranslationQueueStatuses();
    if (anyTranslateBusy()) {
        emit notify(QStringLiteral("当前有其它翻译任务，请稍后再试"));
        return;
    }
    QStringList files = m_translationModel->selectedPaths();
    if (files.isEmpty()) {
        emit notify(QStringLiteral("请先添加并勾选要翻译的 txt 文件"));
        return;
    }
    if (!m_sakuraRunning) {
        emit notify(m_sakuraStarting
                        ? QStringLiteral("Sakura 模型仍在加载，请等状态变为运行中后再开始")
                        : QStringLiteral("Sakura 模型尚未就绪，请先启动模型"));
        return;
    }
    QString dir = outputDir.trimmed();
    if (dir.isEmpty())
        dir = m_chdlDir;
    m_chdlDir = dir;
    QHash<QString, QString> userOutputs;
    if (!prepareUserTranslationOutputs(files, &userOutputs)) return;

    m_translator->setServerUrl(m_sakura->serverUrl());
    m_translator->setModelAlias(m_sakura->modelAlias());
    m_translator->setSystemPrompt(m_systemPrompt);

    // 书库章节保留 CHDL 布局，自添文件使用本次独立的 user 目录。
    m_translateFiles = files;
    m_translateOutputDirs.clear();
    QVector<FileJob> jobs;
    for (const QString &path : files) {
        FileJob job;
        job.path = path;
        job.outputDir = translationOutputDir(path, dir, userOutputs);
        jobs.append(job);
        m_translateOutputDirs << job.outputDir;
    }

    m_translating = true;
    m_translateCurrent = 0;
    m_translateTotal = 1;
    m_translateMessage = QStringLiteral("开始翻译…");
    emit translatingChanged();
    emit translateProgressChanged();

    m_translationModel->resetStatuses();
    m_translator->translateFiles(jobs);
}

void AppController::stopTranslate()
{
    if (!m_translating)
        return;
    m_translator->abort();
    m_translating = false;
    emit translatingChanged();
    m_translationModel->resetStatuses();
    emit notify(QStringLiteral("翻译已中止"));
}

void AppController::onTranslateProgress(int current, int total, const QString &message)
{
    if (m_siteOverviewTranslating) {
        m_siteOverviewCurrent = current;
        m_siteOverviewTotal = total;
        m_siteOverviewMessage = message;
        emit siteOverviewProgressChanged();
        return;
    }
    m_translateCurrent = current;
    m_translateTotal = total;
    m_translateMessage = message;
    emit translateProgressChanged();
}

void AppController::onTranslateFileStatus(const QString &path, const QString &status)
{
    m_translationModel->setFileStatus(path, status);
}

void AppController::onTranslateFinished(int success, int failed)
{
    if (m_siteOverviewTranslating && m_siteOverviewModel == 0) {
        finishSiteOverviewTranslation(success);
        return;
    }
    m_translating = false;
    emit translatingChanged();

    // Sakura 以本轮原子提交的状态为准，保留的旧译文不能把本轮失败改成成功。

    emit notify(QStringLiteral("翻译完成：成功 %1 个，失败 %2 个").arg(success).arg(failed));
    refreshLibrary();
}

// ---------------- Murasaki 翻译 ----------------

void AppController::startMurasakiTranslate(const QString &outputDir)
{
    refreshTranslationQueueStatuses();
    QStringList files = m_translationModel->selectedPaths();
    if (files.isEmpty()) {
        emit notify(QStringLiteral("请先添加并勾选要翻译的 txt 文件"));
        return;
    }
    if (anyTranslateBusy()) {
        emit notify(QStringLiteral("当前有其它翻译任务，请稍后再试"));
        return;
    }
    if (!m_murasakiRunning) {
        emit notify(m_murasakiStarting
                        ? QStringLiteral("Murasaki 模型仍在加载，请等状态变为运行中后再开始")
                        : QStringLiteral("Murasaki 模型尚未就绪，请先启动模型"));
        return;
    }
    QString dir = outputDir.trimmed();
    if (dir.isEmpty())
        dir = m_chdlDir;
    m_chdlDir = dir;
    QHash<QString, QString> userOutputs;
    if (!prepareUserTranslationOutputs(files, &userOutputs)) return;

    m_murasakiTranslator->setServerUrl(m_murasaki->serverUrl());
    m_murasakiTranslator->setModelAlias(m_murasaki->modelAlias());
    m_murasakiTranslator->setModelIdentity(m_murasaki->modelPath());
    m_murasakiTranslator->setDownloadRoot(m_downloadDir);
    m_murasakiTranslator->setSystemPrompt(MurasakiTranslator::defaultPrompt());

    QVector<MurasakiFileJob> jobs;
    for (const QString &path : files) {
        MurasakiFileJob job;
        job.path = path;
        job.outputDir = translationOutputDir(path, dir, userOutputs);
        jobs.append(job);
    }

    m_murasakiTranslating = true;
    m_murasakiTranslateCurrent = 0;
    m_murasakiTranslateTotal = 1;
    m_murasakiTranslateMessage = QStringLiteral("开始 Murasaki 翻译…");
    emit murasakiTranslatingChanged();
    emit murasakiTranslateProgressChanged();

    m_translationModel->resetStatuses();
    m_murasakiTranslator->translateFiles(jobs);
}

void AppController::stopMurasakiTranslate()
{
    if (!m_murasakiTranslating)
        return;
    m_murasakiTranslator->abort();
    m_murasakiTranslating = false;
    emit murasakiTranslatingChanged();
    m_translationModel->resetStatuses();
    emit notify(QStringLiteral("Murasaki 翻译已中止"));
}

void AppController::onMurasakiTranslateProgress(int current, int total, const QString &message)
{
    if (m_siteOverviewTranslating) {
        m_siteOverviewCurrent = current;
        m_siteOverviewTotal = total;
        m_siteOverviewMessage = message;
        emit siteOverviewProgressChanged();
        return;
    }
    m_murasakiTranslateCurrent = current;
    m_murasakiTranslateTotal = total;
    m_murasakiTranslateMessage = message;
    emit murasakiTranslateProgressChanged();
}

void AppController::onMurasakiTranslateFileStatus(const QString &path, const QString &status)
{
    m_translationModel->setFileStatus(path, status);
}

void AppController::onMurasakiTranslateFinished(int success, int failed)
{
    if (m_siteOverviewTranslating && m_siteOverviewModel == 1) {
        finishSiteOverviewTranslation(success);
        return;
    }
    m_murasakiTranslating = false;
    emit murasakiTranslatingChanged();

    // 以本轮翻译器的文件状态为准；已有旧译文不代表本轮失败也应显示成功。
    emit notify(QStringLiteral("Murasaki 翻译完成：成功 %1 个，失败 %2 个").arg(success).arg(failed));
    refreshLibrary();
}

// ---------------- 目录与书库 ----------------

QString AppController::downloadDir() const { return m_downloadDir; }
QString AppController::chdlDir() const { return m_chdlDir; }
QString AppController::cjdlDir() const { return m_cjdlDir; }
QString AppController::saveRoot() const { return m_saveRoot; }
QString AppController::defaultSaveRoot() const { return workspaceRootDir(); }
bool AppController::generateBilingual() const { return m_generateBilingual; }
QString AppController::sakuraDir() const { return m_sakuraDir; }
QString AppController::modelDir() const { return m_modelDir; }
QStringList AppController::availableModels() const { return m_sakura->availableModels(); }

QVariantMap AppController::modelGpuConfiguration(const QString &engine, const QString &modelPath) const
{
    return ModelGpuConfig::configuration(engine, modelPath);
}

bool AppController::saveModelGpuConfiguration(const QString &engine, const QString &modelPath, int layers)
{
    if (anyTranslateBusy()) return false;
    if (!ModelGpuConfig::save(engine, modelPath, layers)) {
        emit notify(QStringLiteral("无法保存模型配置，请检查模型文件及层数"));
        return false;
    }
    emit notify(QStringLiteral("已保存模型配置，下次启动此模型时生效"));
    return true;
}

int AppController::requestModelGpuRecommendation(const QString &engine, const QString &modelPath)
{
    const int requestId = ++m_modelGpuRequestId;
    if (anyTranslateBusy() || m_sakura->isRunning() || m_murasaki->isRunning() || m_qwen->isRunning()
        || m_sakuraStarting || m_murasakiStarting || m_qwenStarting) {
        QTimer::singleShot(0, this, [this, requestId] {
            emit modelGpuRecommendationReady(requestId, {{"success", false},
                {"message", QStringLiteral("请先关闭已启动的模型，再自动配置，以便测量可用显存")}});
        });
        return requestId;
    }
    auto result = std::make_shared<QVariantMap>();
    QThread *worker = QThread::create([engine, modelPath, result] {
        *result = ModelGpuConfig::recommend(engine, modelPath);
    });
    worker->setParent(this);
    connect(worker, &QThread::finished, this, [this, requestId, result] {
        emit modelGpuRecommendationReady(requestId, *result);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
    return requestId;
}

void AppController::applyDerivedDirs()
{
    if (m_siteBrowser->setSaveRoot(m_saveRoot)) {
        m_siteRefreshWorkUrl.clear();
        m_siteBusy = false;
        m_siteSearching = false;
        m_siteCommentsBusy = false;
        m_siteMessage.clear();
        emit siteBusyChanged();
        emit siteSearchingChanged();
        emit siteCommentsChanged();
        emit siteMessageChanged();
    }
    m_downloadDir = QDir(m_saveRoot).filePath(QStringLiteral("DOWNLOAD"));
    m_chdlDir = QDir(m_saveRoot).filePath(QStringLiteral("CHDL"));
    m_cjdlDir = QDir(m_saveRoot).filePath(QStringLiteral("CJDL"));
    // 由调用者在目录派生/迁移完成后统一刷新书库，避免重复扫描。
    m_readerModel->setCjdlDir(m_cjdlDir);
    // 翻译器要用同一个下载根目录来算「站点/作品」相对标识
    m_qwenTranslator->setDownloadRoot(m_downloadDir);
}

void AppController::setSaveRoot(const QString &dir)
{
    if (dir.trimmed().isEmpty())
        return;
    m_saveRoot = dir;
    QSettings settings;
    settings.setValue(QStringLiteral("paths/saveRoot"), dir);
    applyDerivedDirs();
    refreshLibrary();
    emit saveRootChanged();
}

void AppController::resetSaveRoot()
{
    setSaveRoot(defaultSaveRoot());
}

void AppController::setGenerateBilingual(bool enabled)
{
    if (m_generateBilingual == enabled)
        return;
    m_generateBilingual = enabled;
    QSettings settings;
    settings.setValue(QStringLiteral("paths/generateBilingual"), enabled);
    emit generateBilingualChanged();
}

void AppController::setDownloadDir(const QString &dir)
{
    if (dir.trimmed().isEmpty())
        return;
    m_downloadDir = dir;
    QSettings settings;
    settings.setValue(QStringLiteral("paths/downloadDir"), dir);
    refreshLibrary();
}

void AppController::setChdlDir(const QString &dir)
{
    if (dir.trimmed().isEmpty())
        return;
    m_chdlDir = dir;
    QSettings settings;
    settings.setValue(QStringLiteral("paths/chdlDir"), dir);
    refreshLibrary();
}

int AppController::libraryNovelCount() const { return m_libraryModel->totalNovelCount(); }
int AppController::libraryDownloadedCount() const { return m_libraryModel->totalDownloadedCount(); }
int AppController::libraryTranslatedCount() const { return m_libraryModel->totalTranslatedCount(); }

void AppController::refreshLibrary()
{
    const QString selectedWorkId = m_libraryModel->novelWorkId(m_currentNovelRow);
    m_libraryModel->setDirectories(m_downloadDir, m_chdlDir);

    if (!selectedWorkId.isEmpty()) {
        m_currentNovelRow = -1;
        for (int row = 0; row < m_libraryModel->totalNovelCount(); ++row) {
            if (m_libraryModel->novelWorkId(row) == selectedWorkId) {
                m_currentNovelRow = row;
                break;
            }
        }
        emit currentNovelRowChanged();
    }
    emit libraryRefreshed();
}

void AppController::openNovelForReading(int libraryRow)
{
    QString source = m_libraryModel->novelSourceDir(libraryRow);
    QString translation = m_libraryModel->novelTranslationDir(libraryRow);
    QString title = m_libraryModel->novelTitle(libraryRow);
    if (source.isEmpty())
        return;
    QString key = m_libraryModel->novelWorkId(libraryRow);
    if (key.isEmpty())
        key = title;
    {
        // 双语资源目录用 <siteKey>/<workId> 相对标识，阅读进度仍用 workId。
        const QString root = m_libraryModel->novelRootPath(libraryRow);
        m_readerModel->setCjdlKey(root.isEmpty()
                                      ? QString()
                                      : NovelPaths::relativeKey(m_downloadDir, root));
    }
    m_readerModel->openNovel(source, translation, title, key);
    emit openReaderRequested();
}

QString AppController::currentNovelKey() const
{
    QString key = m_libraryModel->novelWorkId(m_currentNovelRow);
    if (key.isEmpty())
        key = m_libraryModel->novelTitle(m_currentNovelRow);
    return key;
}

void AppController::openNovelChapter(int chapterIndex, const QString &language)
{
    if (m_currentNovelRow < 0)
        return;
    QString source = m_libraryModel->novelSourceDir(m_currentNovelRow);
    QString translation = m_libraryModel->novelTranslationDir(m_currentNovelRow);
    QString title = m_libraryModel->novelTitle(m_currentNovelRow);
    if (source.isEmpty())
        return;
    QString lang = (language == QStringLiteral("jp")) ? QStringLiteral("jp") : QStringLiteral("zh");
    m_readerModel->setCjdlKey(currentNovelRelativeKey());
    m_readerModel->openNovelAt(source, translation, title, currentNovelKey(), lang, chapterIndex);
    emit openReaderRequested();
}

void AppController::openNovelChapterFile(const QString &chapterBaseName,
                                         const QString &language)
{
    if (m_currentNovelRow < 0)
        return;

    const QString source = m_libraryModel->novelSourceDir(m_currentNovelRow);
    const QString translation = m_libraryModel->novelTranslationDir(m_currentNovelRow);
    const QString title = m_libraryModel->novelTitle(m_currentNovelRow);
    const QString lang = (language == QStringLiteral("jp"))
                             ? QStringLiteral("jp") : QStringLiteral("zh");
    m_readerModel->setCjdlKey(currentNovelRelativeKey());
    if (source.isEmpty()
        || !m_readerModel->openNovelAtFile(source, translation, title,
                                           currentNovelKey(), lang, chapterBaseName)) {
        emit notify(QStringLiteral("未找到所选章节文件: ") + chapterBaseName);
        return;
    }
    emit openReaderRequested();
}

bool AppController::currentNovelChapterRead(int chapterIndex, const QString &language) const
{
    if (m_currentNovelRow < 0)
        return false;
    return m_readerModel->isChapterRead(currentNovelKey(), language, chapterIndex);
}

QStringList AppController::currentNovelBilingualChapterNames() const
{
    QStringList names;
    if (m_currentNovelRow < 0)
        return names;

    const QString rel = currentNovelRelativeKey();
    if (rel.isEmpty())
        return names;

    const QDir chaptersDir(NovelPaths::bilingualChaptersDir(m_cjdlDir, rel));
    if (!chaptersDir.exists())
        return names;

    const QStringList files = chaptersDir.entryList(QStringList() << QStringLiteral("*.json"),
                                                    QDir::Files, QDir::Name);
    for (const QString &file : files)
        names << QFileInfo(file).completeBaseName();
    return names;
}

bool AppController::currentNovelBilingualChapterAvailable(const QString &chapterBaseName) const
{
    if (chapterBaseName.trimmed().isEmpty())
        return false;
    return currentNovelBilingualChapterNames().contains(chapterBaseName);
}

void AppController::openNovelBilingualChapterFile(const QString &chapterBaseName)
{
    if (m_currentNovelRow < 0)
        return;

    const QString source = m_libraryModel->novelSourceDir(m_currentNovelRow);
    const QString translation = m_libraryModel->novelTranslationDir(m_currentNovelRow);
    const QString title = m_libraryModel->novelTitle(m_currentNovelRow);
    if (source.isEmpty()) {
        emit notify(QStringLiteral("未找到该小说的原文目录"));
        return;
    }

    m_readerModel->setCjdlKey(currentNovelRelativeKey());
    m_readerModel->openNovelBilingualFile(source, translation, title,
                                          currentNovelKey(), chapterBaseName);
    emit openReaderRequested();
}

void AppController::clearCurrentNovelReadingProgress(const QString &mode)
{
    if (m_currentNovelRow < 0)
        return;
    if (mode != "original" && mode != "translation" && mode != "bilingual") return;
    m_readerModel->clearNovelProgress(currentNovelKey(), mode);
    refreshLibrary();
    emit notify(QStringLiteral("已删除当前目录的阅读记录与书签"));
}

void AppController::shutdown()
{
    m_readerModel->saveProgress();
    m_service->abort();
    m_translator->abort();
    m_murasakiTranslator->abort();
    m_qwenTranslator->abort();
    m_qwenTranslator->abortGlossary();
    m_siteBrowser->abort();
    m_sakura->stop();
    m_murasaki->stop();
    m_qwen->stop();
}

// ---------------- 小说详情 / 阅读 ----------------

QString AppController::currentNovelTitle() const { return m_libraryModel->novelTitle(m_currentNovelRow); }
QString AppController::currentNovelAuthor() const { return m_libraryModel->novelAuthor(m_currentNovelRow); }
QString AppController::currentNovelWorkId() const { return m_libraryModel->novelWorkId(m_currentNovelRow); }
QString AppController::currentNovelUrl() const
{
    if (m_currentNovelRow < 0)
        return QString();
    // 优先用 novel.json 里记录的原始作品地址
    const QString root = m_libraryModel->novelRootPath(m_currentNovelRow);
    if (!root.isEmpty()) {
        const NovelWork meta = NovelStore::loadNovel(root);
        if (!meta.workUrl.isEmpty())
            return meta.workUrl;
    }
    return NovelPaths::workUrlFor(m_libraryModel->novelSite(m_currentNovelRow),
                                  m_libraryModel->novelWorkId(m_currentNovelRow));
}

void AppController::checkCurrentNovelUpdate()
{
    if (m_novelUpdateChecking || m_novelUpdating)
        return;
    if (m_currentNovelRow < 0) {
        emit novelUpdateChecked(QStringLiteral("failed"), 0);
        return;
    }
    const QString url = currentNovelUrl();
    if (url.isEmpty()) {
        emit notify(QStringLiteral("无法获取该作品网址，检查更新失败"));
        emit novelUpdateChecked(QStringLiteral("failed"), 0);
        return;
    }
    m_novelUpdateChecking = true;
    emit novelUpdateCheckingChanged();
    emit notify(QStringLiteral("正在检查更新…"));
    m_updateService->fetchWork(url);
}

void AppController::onUpdateWorkReady(const NovelWork &work)
{
    m_novelUpdateChecking = false;
    emit novelUpdateCheckingChanged();

    m_updateWork = work;
    m_updateMissing.clear();

    const QString chaptersDir = m_libraryModel->novelSourceDir(m_currentNovelRow);
    for (int i = 0; i < work.chapters.size(); ++i) {
        const QString file = QDir(chaptersDir).filePath(
            QStringLiteral("%1.txt").arg(i + 1, 3, 10, QLatin1Char('0')));
        if (!QFileInfo::exists(file))
            m_updateMissing.append(work.chapters.at(i));
    }

    if (m_updateMissing.isEmpty()) {
        emit novelUpdateChecked(QStringLiteral("complete"), 0);
    } else {
        emit novelUpdateChecked(QStringLiteral("incomplete"), m_updateMissing.size());
    }
}

void AppController::onUpdateWorkFailed(const QString &error)
{
    m_novelUpdateChecking = false;
    emit novelUpdateCheckingChanged();
    emit notify(QStringLiteral("检查更新失败：") + error);
    emit novelUpdateChecked(QStringLiteral("failed"), 0);
}

void AppController::updateCurrentNovel()
{
    if (m_novelUpdating || m_novelUpdateChecking)
        return;
    if (m_updateMissing.isEmpty()) {
        emit novelUpdateFinished(false, 0);
        return;
    }
    m_novelUpdating = true;
    m_novelUpdateCurrent = 0;
    m_novelUpdateTotal = m_updateMissing.size();
    m_novelUpdateMessage = QStringLiteral("开始更新章节…");
    emit novelUpdatingChanged();
    emit novelUpdateProgressChanged();
    emit notify(QStringLiteral("正在更新章节…"));
    m_updateService->downloadChapters(m_updateWork, m_updateMissing, m_downloadDir);
}

void AppController::onUpdateProgress(int current, int total, const QString &message)
{
    m_novelUpdateCurrent = current;
    m_novelUpdateTotal = total;
    m_novelUpdateMessage = message;
    emit novelUpdateProgressChanged();
}

void AppController::onUpdateDownloadFinished(int successCount, int failedCount)
{
    Q_UNUSED(failedCount);
    m_novelUpdating = false;
    emit novelUpdatingChanged();
    m_updateMissing.clear();
    refreshLibrary();
    emit novelUpdateFinished(successCount > 0, successCount);
}

void AppController::queueCurrentNovelForTranslation()
{
    if (m_currentNovelRow < 0) {
        emit notify(QStringLiteral("当前没有选中小说"));
        return;
    }
    // 任务进行中不重排翻译目录：正在跑的任务与列表会错位
    if (anyTranslateBusy())
        return;
    const QString sourceDir = m_libraryModel->novelSourceDir(m_currentNovelRow);
    const QStringList names = m_libraryModel->novelChapterNames(m_currentNovelRow);
    QStringList paths;
    for (const QString &name : names) {
        const QString path = QDir(sourceDir).filePath(name + QStringLiteral(".txt"));
        if (QFileInfo::exists(path))
            paths << path;
    }
    if (paths.isEmpty()) {
        emit notify(QStringLiteral("没有可加入翻译队列的章节"));
        return;
    }

    // 先清空翻译目录，再加入本书章节：避免上一次留下的其它小说章节被一起翻译
    m_translationModel->clear();
    refreshTranslationQueueStatuses();
    m_translationModel->addFiles(paths);
    // 全部选中（已翻译的章节在界面上会置灰，但勾选状态仍为选中）
    for (int row = 0; row < m_translationModel->rowCount(); ++row) {
        m_translationModel->setData(m_translationModel->index(row, 0), true,
                                    TranslationModel::SelectedRole);
    }
    refreshTranslationQueueStatuses();
    emit notify(QStringLiteral("已清空翻译目录并加入 %1 个章节").arg(paths.size()));
}

int AppController::currentNovelReadCount() const
{
    if (m_currentNovelRow < 0)
        return 0;
    const QString key = m_libraryModel->novelWorkId(m_currentNovelRow);
    if (key.isEmpty())
        return 0;
    QSettings settings(QStringLiteral("KakuReader"), QStringLiteral("KakuReader"));
    settings.beginGroup(QStringLiteral("reading/") + key);
    const int count = settings.value(QStringLiteral("zhRead")).toStringList().size();
    settings.endGroup();
    return qMin(count, m_libraryModel->novelChapterCount(m_currentNovelRow));
}

QString AppController::currentNovelRootPath() const { return m_libraryModel->novelRootPath(m_currentNovelRow); }
int AppController::currentNovelChapterCount() const { return m_libraryModel->novelChapterCount(m_currentNovelRow); }
int AppController::currentNovelTranslatedCount() const { return m_libraryModel->novelTranslatedCount(m_currentNovelRow); }
int AppController::currentNovelDownloadedCount() const { return m_libraryModel->novelDownloadedCount(m_currentNovelRow); }
QStringList AppController::currentNovelChapterNames() const { return m_libraryModel->novelChapterNames(m_currentNovelRow); }
QString AppController::currentNovelChapterTitle(int chapterIndex) const
{
    return m_libraryModel->novelChapterTitle(m_currentNovelRow, chapterIndex);
}
bool AppController::currentNovelChapterTranslated(int chapterIndex) const
{
    return m_libraryModel->novelChapterTranslated(m_currentNovelRow, chapterIndex);
}

void AppController::selectNovel(int libraryRow)
{
    int real = m_libraryModel->sourceRow(libraryRow);
    if (real < 0)
        return;
    m_currentNovelRow = real;
    emit currentNovelRowChanged();
    emit novelSelected();
}

void AppController::openCurrentNovelForReading()
{
    openNovelForReading(m_currentNovelRow);
}

void AppController::queueChapterForTranslation(int chapterIndex)
{
    if (m_currentNovelRow < 0) {
        emit notify(QStringLiteral("当前没有选中小说"));
        return;
    }

    const QString name = m_libraryModel->novelChapterName(m_currentNovelRow, chapterIndex);
    const QString sourceDir = m_libraryModel->novelSourceDir(m_currentNovelRow);
    if (name.isEmpty() || sourceDir.isEmpty()) {
        emit notify(QStringLiteral("未找到该章节的源文件"));
        return;
    }

    const QString sourcePath = QDir(sourceDir).filePath(name + QStringLiteral(".txt"));
    if (!QFileInfo::exists(sourcePath)) {
        emit notify(QStringLiteral("未找到源文件: ") + sourcePath);
        return;
    }

    // 只加入翻译队列，由 QML 跳转到翻译页；是否开始翻译交给用户决定。
    refreshTranslationQueueStatuses();
    m_translationModel->addFiles(QStringList() << sourcePath);
    refreshTranslationQueueStatuses();
    emit notify(QStringLiteral("已添加: ") + name);
}

void AppController::deleteChapterTranslation(int chapterIndex)
{
    // 仅删除该章的译文，不再加入翻译队列、不跳转翻译页；双语资源保持独立。
    if (m_currentNovelRow < 0) {
        emit notify(QStringLiteral("当前没有选中小说"));
        return;
    }
    const QString name = m_libraryModel->novelChapterName(m_currentNovelRow, chapterIndex);
    if (name.isEmpty()) {
        emit notify(QStringLiteral("未找到该章节"));
        return;
    }

    const QString transDir = m_libraryModel->novelTranslationDir(m_currentNovelRow);
    if (!transDir.isEmpty()) {
        const QString transPath = QDir(transDir).filePath(name + QStringLiteral(".txt"));
        if (!removeDocumentWithReaderEdits(transPath)) {
            qWarning() << "[Translation] 无法删除旧译文:" << transPath;
            emit notify(QStringLiteral("删除译文失败: ") + name);
            return;
        }
    }

    // 刷新书库，让该章立即恢复为“未翻译”状态。
    refreshLibrary();
    emit notify(QStringLiteral("已删除译文: ") + name);
}

// ---------------- 小说级手动术语表 ----------------

GlossaryManager AppController::loadCurrentGlossary() const
{
    return loadGlossaryOfKind(activeGlossaryKind());
}

GlossaryManager AppController::loadGlossaryOfKind(const QString &kind) const
{
    GlossaryManager gm(currentNovelRootPath(),
                       GlossaryManager::fileNameForKind(kind));
    gm.load();
    return gm;
}

bool AppController::saveGlossaryChanges(GlossaryManager &glossary, const QString &message)
{
    if (!glossary.save()) {
        emit notify(QStringLiteral("术语表保存失败"));
        return false;
    }
    emit glossaryChanged();
    emit notify(message);
    return true;
}

QString AppController::activeGlossaryKind() const
{
    // 键用「站点/作品」相对标识（currentNovelRelativeKey 对旧布局会退化成作品目录名），
    // 这样不同站点撞同一个作品 id 的两本书不会共用同一个开关。
    return GlossaryManager::activeKindFor(currentNovelRelativeKey(), currentNovelWorkId());
}

void AppController::setActiveGlossaryKind(const QString &kind)
{
    const QString workId = currentNovelWorkId();
    const QString rel = currentNovelRelativeKey();
    if (workId.isEmpty() && rel.isEmpty()) {
        emit notify(QStringLiteral("当前没有选中小说"));
        return;
    }
    GlossaryManager::setActiveKindFor(rel.isEmpty() ? workId : rel, kind);
    emit glossaryChanged();
    emit notify(kind == GlossaryManager::kQwenKind
                    ? QStringLiteral("翻译将使用 Qwen 术语表")
                    : QStringLiteral("翻译将使用人工术语表"));
}

bool AppController::glossaryExistsIn(const QString &kind) const
{
    return QFileInfo::exists(
        GlossaryManager(currentNovelRootPath(),
                        GlossaryManager::fileNameForKind(kind)).glossaryPath());
}

int AppController::glossaryCountIn(const QString &kind) const
{
    return loadGlossaryOfKind(kind).terms().size();
}

QVariantList AppController::glossaryTermsIn(const QString &kind) const
{
    return glossaryTermsData(loadGlossaryOfKind(kind).displayTerms());
}

QVariantMap AppController::glossaryTermIn(const QString &kind, const QString &source) const
{
    const GlossaryTerm t = loadGlossaryOfKind(kind).findTerm(source);
    if (t.source.isEmpty())
        return QVariantMap();
    return glossaryTermData(t);
}

bool AppController::addGlossaryTermIn(const QString &kind, const QString &source,
                                      const QString &target, const QString &type,
                                      const QString &note, bool enabled)
{
    if (m_currentNovelRow < 0 || source.trimmed().isEmpty()) {
        emit notify(QStringLiteral("无法添加术语：当前没有选中小说或原文为空"));
        return false;
    }
    GlossaryManager gm = loadGlossaryOfKind(kind);
    GlossaryTerm t;
    t.source = source.trimmed();
    t.target = target.trimmed();
    t.type = type;
    t.note = note;
    t.enabled = enabled;
    t.origin = (kind == GlossaryManager::kQwenKind) ? QStringLiteral("qwen")
                                                    : QStringLiteral("manual");
    if (!gm.addTerm(t)) {
        emit notify(QStringLiteral("该术语已经存在：%1").arg(source));
        return false;
    }
    return saveGlossaryChanges(gm, QStringLiteral("已添加术语：%1 → %2").arg(t.source, t.target));
}

bool AppController::updateGlossaryTermIn(const QString &kind, const QString &oldSource,
                                         const QString &source, const QString &target,
                                         const QString &type, const QString &note,
                                         bool enabled)
{
    if (m_currentNovelRow < 0 || oldSource.trimmed().isEmpty() || source.trimmed().isEmpty()) {
        emit notify(QStringLiteral("无法更新术语"));
        return false;
    }
    GlossaryManager gm = loadGlossaryOfKind(kind);
    if (!gm.contains(oldSource)) {
        emit notify(QStringLiteral("未找到该术语：%1").arg(oldSource));
        return false;
    }
    GlossaryTerm t = gm.findTerm(oldSource);
    t.source = source.trimmed();
    t.target = target.trimmed();
    t.type = type;
    t.note = note;
    t.enabled = enabled;
    t.origin = (kind == GlossaryManager::kQwenKind) ? QStringLiteral("qwen")
                                                    : QStringLiteral("manual");
    if (!gm.updateTerm(oldSource, t)) {
        emit notify(QStringLiteral("术语更新失败（目标原文可能已存在）"));
        return false;
    }
    return saveGlossaryChanges(gm, QStringLiteral("已更新术语：%1").arg(t.source));
}

bool AppController::removeGlossaryTermIn(const QString &kind, const QString &source)
{
    if (m_currentNovelRow < 0 || source.trimmed().isEmpty())
        return false;
    GlossaryManager gm = loadGlossaryOfKind(kind);
    if (!gm.removeTerm(source)) {
        emit notify(QStringLiteral("未找到该术语：%1").arg(source));
        return false;
    }
    return saveGlossaryChanges(gm, QStringLiteral("已删除术语：%1").arg(source));
}

bool AppController::clearGlossaryIn(const QString &kind)
{
    if (m_currentNovelRow < 0) {
        emit notify(QStringLiteral("当前没有选中小说"));
        return false;
    }
    const bool qwen = (kind == GlossaryManager::kQwenKind);
    const QString label = qwen ? QStringLiteral("Qwen术语表") : QStringLiteral("人工术语表");

    GlossaryManager gm(currentNovelRootPath(),
                       GlossaryManager::fileNameForKind(kind));
    const QString path = gm.glossaryPath();
    if (!QFileInfo::exists(path)) {
        emit notify(label + QStringLiteral("本来就是空的"));
        return false;
    }
    gm.load();
    const int count = gm.terms().size();
    if (!QFile::remove(path)) {
        emit notify(QStringLiteral("术语表删除失败"));
        return false;
    }
    emit glossaryChanged();
    emit notify(QStringLiteral("已删除%1的全部 %2 条术语").arg(label).arg(count));
    return true;
}

bool AppController::copyGlossaryTermTo(const QString &fromKind, const QString &toKind,
                                        const QString &source, bool overwrite)
{
    if (m_currentNovelRow < 0 || source.trimmed().isEmpty())
        return false;
    if (fromKind == toKind) {
        emit notify(QStringLiteral("不能复制到同一份术语表"));
        return false;
    }
    GlossaryManager from = loadGlossaryOfKind(fromKind);
    if (!from.contains(source)) {
        emit notify(QStringLiteral("未找到该术语：%1").arg(source));
        return false;
    }
    GlossaryManager to = loadGlossaryOfKind(toKind);
    const bool exists = to.contains(source);
    if (exists && !overwrite) {
        emit notify(QStringLiteral("目标术语表里已有：%1").arg(source));
        return false;
    }
    GlossaryTerm t = from.findTerm(source);
    t.origin = (toKind == GlossaryManager::kQwenKind) ? QStringLiteral("qwen")
                                                      : QStringLiteral("manual");
    const bool changed = exists ? to.updateTerm(source, t) : to.addTerm(t);
    if (!changed || !to.save()) {
        emit notify(QStringLiteral("添加到目标术语表失败：%1").arg(source));
        return false;
    }
    emit glossaryChanged();
    emit notify((exists ? QStringLiteral("已覆盖%1：%2") : QStringLiteral("已添加到%1：%2"))
                    .arg(toKind == GlossaryManager::kQwenKind ? QStringLiteral("Qwen术语表")
                                                              : QStringLiteral("人工术语表"),
                         source));
    return true;
}

bool AppController::setGlossaryTermEnabledIn(const QString &kind, const QString &source,
                                             bool enabled)
{
    if (m_currentNovelRow < 0 || source.trimmed().isEmpty())
        return false;
    GlossaryManager gm = loadGlossaryOfKind(kind);
    if (!gm.setEnabled(source, enabled)) {
        emit notify(QStringLiteral("未找到该术语：%1").arg(source));
        return false;
    }
    return saveGlossaryChanges(gm, enabled ? QStringLiteral("已启用术语：%1").arg(source)
                        : QStringLiteral("已禁用术语：%1").arg(source));
}

QVariantList AppController::currentNovelGlossaryTerms() const
{
    return glossaryTermsData(loadCurrentGlossary().terms());
}

bool AppController::currentNovelGlossaryContains(const QString &source) const
{
    return loadCurrentGlossary().contains(source);
}

QVariantMap AppController::currentNovelGlossaryTerm(const QString &source) const
{
    const GlossaryTerm t = loadCurrentGlossary().findTerm(source);
    if (t.source.isEmpty())
        return QVariantMap();
    return glossaryTermData(t);
}

bool AppController::addCurrentNovelGlossaryTerm(const QString &source,
                                                const QString &target,
                                                const QString &type,
                                                const QString &note,
                                                bool enabled)
{
    if (m_currentNovelRow < 0 || source.trimmed().isEmpty()) {
        emit notify(QStringLiteral("无法添加术语：当前没有选中小说或原文为空"));
        return false;
    }
    GlossaryManager gm = loadCurrentGlossary();
    GlossaryTerm t;
    t.source = source.trimmed();
    t.target = target.trimmed();
    t.type = type;
    t.note = note;
    t.enabled = enabled;
    t.origin = QStringLiteral("manual");
    if (!gm.addTerm(t)) {
        emit notify(QStringLiteral("该术语已经存在：%1").arg(source));
        return false;
    }
    return saveGlossaryChanges(gm, QStringLiteral("已添加术语：%1 → %2").arg(t.source, t.target));
}

bool AppController::updateCurrentNovelGlossaryTerm(const QString &oldSource,
                                                   const QString &source,
                                                   const QString &target,
                                                   const QString &type,
                                                   const QString &note,
                                                   bool enabled)
{
    if (m_currentNovelRow < 0 || oldSource.trimmed().isEmpty() || source.trimmed().isEmpty()) {
        emit notify(QStringLiteral("无法更新术语"));
        return false;
    }
    GlossaryManager gm = loadCurrentGlossary();
    if (!gm.contains(oldSource)) {
        emit notify(QStringLiteral("未找到该术语：%1").arg(oldSource));
        return false;
    }
    GlossaryTerm t = gm.findTerm(oldSource);
    t.source = source.trimmed();
    t.target = target.trimmed();
    t.type = type;
    t.note = note;
    t.enabled = enabled;
    t.origin = QStringLiteral("manual");
    if (!gm.updateTerm(oldSource, t)) {
        emit notify(QStringLiteral("术语更新失败（目标原文可能已存在）"));
        return false;
    }
    return saveGlossaryChanges(gm, QStringLiteral("已更新术语：%1").arg(t.source));
}

bool AppController::removeCurrentNovelGlossaryTerm(const QString &source)
{
    if (m_currentNovelRow < 0 || source.trimmed().isEmpty())
        return false;
    GlossaryManager gm = loadCurrentGlossary();
    if (!gm.removeTerm(source)) {
        emit notify(QStringLiteral("未找到该术语：%1").arg(source));
        return false;
    }
    return saveGlossaryChanges(gm, QStringLiteral("已删除术语：%1").arg(source));
}

bool AppController::setCurrentNovelGlossaryEnabled(const QString &source, bool enabled)
{
    if (m_currentNovelRow < 0 || source.trimmed().isEmpty())
        return false;
    GlossaryManager gm = loadCurrentGlossary();
    if (!gm.setEnabled(source, enabled)) {
        emit notify(QStringLiteral("未找到该术语：%1").arg(source));
        return false;
    }
    return saveGlossaryChanges(gm, enabled ? QStringLiteral("已启用术语：%1").arg(source)
                        : QStringLiteral("已禁用术语：%1").arg(source));
}

void AppController::generateBilingualResources()
{
    if (m_currentNovelRow < 0) {
        emit notify(QStringLiteral("当前没有选中小说"));
        return;
    }
    const QString relKey = currentNovelRelativeKey();
    if (relKey.isEmpty()) {
        emit notify(QStringLiteral("旧版扁平目录暂不支持生成双语资源"));
        return;
    }
    const QString sourceDir = m_libraryModel->novelSourceDir(m_currentNovelRow);
    const QString transDir = m_libraryModel->novelTranslationDir(m_currentNovelRow);
    const QStringList names = m_libraryModel->novelChapterNames(m_currentNovelRow);
    int generated = 0;
    for (const QString &name : names) {
        const QString src = QDir(sourceDir).filePath(name + QStringLiteral(".txt"));
        const QString dst = QDir(transDir).filePath(name + QStringLiteral(".txt"));
        if (!QFileInfo::exists(src) || !QFileInfo::exists(dst))
            continue;
        const QStringList sources = readParagraphsForCjdl(src);
        const QStringList translations = readParagraphsForCjdl(dst);
        QList<CjdlBlock> blocks;
        if (sources.size() == translations.size()) {
            for (int i = 0; i < sources.size(); ++i)
                blocks.append({i, sources[i], translations[i]});
        } else {
            // 无法仅凭行数推断合并/拆分的位置，整章对照保留两侧全文。
            // 不截短列表，也不把剩余原文错误地配给某一句译文。
            blocks.append({0, sources.join(QLatin1Char('\n')),
                           translations.join(QLatin1Char('\n'))});
        }
        if (sources.isEmpty() && translations.isEmpty())
            continue;
        if (CjdlExporter::write(m_cjdlDir, relKey, name, name, blocks))
            ++generated;
    }
    emit notify(QStringLiteral("已生成 %1 个双语阅读资源").arg(generated));
    refreshLibrary();
}

void AppController::deleteBilingualResource(const QString &chapterBaseName)
{
    if (m_currentNovelRow < 0) {
        emit notify(QStringLiteral("当前没有选中小说"));
        return;
    }
    const QString name = chapterBaseName.trimmed();
    if (name.isEmpty()) {
        emit notify(QStringLiteral("未指定章节"));
        return;
    }
    const QString relKey = currentNovelRelativeKey();
    if (relKey.isEmpty()) {
        emit notify(QStringLiteral("旧版扁平目录暂不支持删除双语资源"));
        return;
    }

    const QString jsonPath = NovelPaths::bilingualChapterFile(m_cjdlDir, relKey, name);
    if (!removeDocumentWithReaderEdits(jsonPath)) {
        emit notify(QStringLiteral("删除双语资源失败: ") + name);
        return;
    }

    emit notify(QStringLiteral("已删除双语资源: ") + name);
    refreshLibrary();
}

void AppController::clearBilingualResources()
{
    if (m_currentNovelRow < 0) {
        emit notify(QStringLiteral("当前没有选中小说"));
        return;
    }
    const QString relKey = currentNovelRelativeKey();
    if (relKey.isEmpty()) {
        emit notify(QStringLiteral("旧版扁平目录暂不支持清空双语资源"));
        return;
    }

    QDir chaptersDir(NovelPaths::bilingualChaptersDir(m_cjdlDir, relKey));
    if (chaptersDir.exists() && !chaptersDir.removeRecursively()) {
        emit notify(QStringLiteral("清空双语资源失败"));
        return;
    }

    emit notify(QStringLiteral("已清空双语资源"));
    refreshLibrary();
}

// ---------------- 翻译设置 ----------------

void AppController::setSystemPrompt(const QString &prompt)
{
    if (m_systemPrompt == prompt)
        return;
    m_systemPrompt = prompt;
    QSettings settings;
    settings.setValue(QStringLiteral("paths/systemPrompt"), prompt);
    emit systemPromptChanged();
}

void AppController::applyPromptPreset(const QString &preset)
{
    QString prompt;
    if (preset == QStringLiteral("文学"))
        prompt = QStringLiteral("你是专业文学翻译，请用典雅、流畅的中文翻译日文小说，保留原文意境与文学性。\n"
                                "若给出了【固定术语】，必须严格遵守：原文出现其中的词时，一律原样使用给定的中文译名，"
                                "不得改字、增删字或换用其它译法，也不要添加原文没有的敬称。");
    else if (preset == QStringLiteral("直译"))
        prompt = QStringLiteral("请尽量忠实原文、逐句直译成简体中文，不润色、不添加内容。\n"
                                "若给出了【固定术语】，必须严格遵守：原文出现其中的词时，一律原样使用给定的中文译名，"
                                "不得改字、增删字或换用其它译法，也不要添加原文没有的敬称。");
    else if (preset == QStringLiteral("自然中文"))
        prompt = QStringLiteral("请把日文翻译成自然地道、符合中文阅读习惯的简体中文，可适当调整语序。\n"
                                "若给出了【固定术语】，必须严格遵守：原文出现其中的词时，一律原样使用给定的中文译名，"
                                "不得改字、增删字或换用其它译法，也不要添加原文没有的敬称。");
    else
        prompt = QStringLiteral("你是一个轻小说翻译模型，可以流畅通顺地以日本轻小说的风格将日文翻译成简体中文，"
                                "并联系上下文正确使用人称代词，不擅自添加原文中没有的代词。\n"
                                "若给出了【固定术语】，必须严格遵守：原文出现其中的词时，一律原样使用给定的中文译名，"
                                "不得改字、增删字或换用其它译法，也不要添加原文没有的敬称。");
    setSystemPrompt(prompt);
}

// ---------------- 章节 / 小说目录解析 ----------------

QString AppController::novelRootForSourceFile(const QString &path) const
{
    QDir d = QFileInfo(path).absoluteDir();
    if (d.dirName() != QStringLiteral("chapters"))
        return QString();
    QDir parent = d;
    parent.cdUp();
    QString root = parent.absolutePath();
    if (QFileInfo::exists(QDir(root).filePath(QStringLiteral("novel.json"))))
        return root;
    return QString();
}

// ---------------- 目录布局（<siteKey>/<workId>） ----------------

// 小说根目录 -> 在 CHDL / CJDL 下的相对标识；新旧布局都能对上。
QString AppController::relativeKeyForSourceFile(const QString &path) const
{
    const QString root = novelRootForSourceFile(path);
    if (root.isEmpty())
        return QString();
    return NovelPaths::relativeKey(m_downloadDir, root);
}

bool AppController::isUserTranslationSource(const QString &path) const
{
    const QString relative = QDir(m_downloadDir).relativeFilePath(QFileInfo(path).absoluteFilePath());
    return QDir::isAbsolutePath(relative) || relative == QStringLiteral("..")
           || relative.startsWith(QStringLiteral("../"))
           || relative.startsWith(QStringLiteral("..\\"));
}

QString AppController::translationOutputDir(const QString &path, const QString &defaultDir,
                                           const QHash<QString, QString> &userOutputs) const
{
    const auto userOutput = userOutputs.constFind(path);
    if (userOutput != userOutputs.cend())
        return userOutput.value();
    const QString relativeKey = relativeKeyForSourceFile(path);
    return relativeKey.isEmpty() ? defaultDir
                                : NovelPaths::translationChaptersDir(defaultDir, relativeKey);
}

void AppController::openUserTranslationDirectory()
{
    const QString directory = QDir(m_saveRoot).absoluteFilePath(QStringLiteral("user"));
    if (!QDir().mkpath(directory)) {
        emit notify(QStringLiteral("无法创建保存目录：%1").arg(directory));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(directory)))
        emit notify(QStringLiteral("无法打开保存目录：%1").arg(directory));
}

QString AppController::userGuideText() const
{
    QFile guide(QStringLiteral(":/help/USER_GUIDE.md"));
    if (!guide.open(QIODevice::ReadOnly))
        return QStringLiteral("# 使用说明\n\n暂时无法读取使用说明，请检查程序文件是否完整。");
    return QString::fromUtf8(guide.readAll());
}

QString AppController::userGuideHtml(const QColor &linkColor, const QColor &borderColor,
                                    const QFont &font) const
{
    QTextDocument document;
    document.setDefaultFont(font);
    document.setMarkdown(userGuideText());
    // Markdown 的默认蓝色链接在深色背景下不易辨认，随应用配色调整。
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().isAnchor())
                continue;
            QTextCursor cursor(&document);
            cursor.setPosition(fragment.position());
            cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
            QTextCharFormat format;
            format.setForeground(linkColor);
            format.setFontUnderline(true);
            cursor.mergeCharFormat(format);
        }
    }
    for (QTextFrame *frame : document.rootFrame()->childFrames()) {
        if (auto *table = qobject_cast<QTextTable *>(frame)) {
            QTextTableFormat format = table->format();
            format.setCellPadding(5);
            format.setCellSpacing(0);
            format.setBorderCollapse(true);
            format.setBorder(0.5);
            format.setBorderBrush(borderColor);
            format.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
            table->setFormat(format);
        }
    }
    return document.toHtml();
}

bool AppController::prepareUserTranslationOutputs(const QStringList &files,
                                                 QHash<QString, QString> *outputs)
{
    outputs->clear();
    QStringList userFiles;
    for (const QString &path : files)
        if (isUserTranslationSource(path)) userFiles.append(path);
    if (userFiles.isEmpty()) return true;

    QString name = QFileInfo(userFiles.first()).completeBaseName();
    name.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]")), QStringLiteral("_"));
    name = name.left(80).trimmed();
    if (name.isEmpty()) name = QStringLiteral("files");
    const QString prefix = name + QLatin1Char('_')
                           + QDateTime::currentDateTime().toString(QStringLiteral("yyyy.MM.dd_HH.mm.ss"));
    QDir userRoot(QDir(m_saveRoot).filePath(QStringLiteral("user")));
    if (!userRoot.mkpath(QStringLiteral("."))) {
        emit notify(QStringLiteral("无法创建自添文件的译文保存目录：%1").arg(userRoot.absolutePath()));
        return false;
    }
    QString folder;
    for (int attempt = 1; attempt <= 10000; ++attempt) {
        const QString candidate = prefix + (attempt == 1 ? QString() : QStringLiteral("_%1").arg(attempt));
        if (userRoot.mkdir(candidate)) { folder = userRoot.filePath(candidate); break; }
        if (!userRoot.exists(candidate)) break;
    }
    if (folder.isEmpty()) {
        emit notify(QStringLiteral("无法创建本次翻译文件夹：%1").arg(userRoot.absolutePath()));
        return false;
    }
    QSet<QString> names;
    int duplicate = 1;
    for (const QString &path : userFiles) {
        const QString key = QFileInfo(path).fileName().toCaseFolded();
        QString destination = folder;
        if (names.contains(key)) {
            // Same-named files in one batch must never overwrite each other.
            destination = QDir(folder).filePath(QStringLiteral("重名文件_%1").arg(++duplicate));
            if (!QDir().mkpath(destination)) {
                emit notify(QStringLiteral("无法创建重名文件的译文保存目录"));
                return false;
            }
        }
        names.insert(key);
        outputs->insert(path, destination);
    }
    return true;
}

bool AppController::deleteCurrentNovel()
{
    if (m_currentNovelRow < 0) {
        emit notify(QStringLiteral("当前没有选中小说"));
        return false;
    }
    const QString title = m_libraryModel->novelTitle(m_currentNovelRow);
    const QString workId = m_libraryModel->novelWorkId(m_currentNovelRow);
    const QString siteKey = m_libraryModel->novelSite(m_currentNovelRow);
    const QString rel = currentNovelRelativeKey();

    bool removed = false;

    // 1) 原文：新布局整目录删除；旧扁平结构按章节名逐个删
    const QString root = m_libraryModel->novelRootPath(m_currentNovelRow);
    if (!root.isEmpty()) {
        removed = QDir(root).removeRecursively();
    } else {
        const QString sourceDir = m_libraryModel->novelSourceDir(m_currentNovelRow);
        const QStringList names = m_libraryModel->novelChapterNames(m_currentNovelRow);
        for (const QString &n : names) {
            const QString p = QDir(sourceDir).filePath(n + QStringLiteral(".txt"));
            if (QFileInfo::exists(p) && QFile::remove(p))
                removed = true;
        }
    }

    // 2) 译文目录（CHDL/<relativeKey>）
    if (!rel.isEmpty()) {
        const QString chdl = QDir(m_chdlDir).filePath(rel);
        if (QFileInfo::exists(chdl))
            QDir(chdl).removeRecursively();
        // 3) 双语资源目录（CJDL/<relativeKey>）
        const QString cjdl = QDir(m_cjdlDir).filePath(rel);
        if (QFileInfo::exists(cjdl))
            QDir(cjdl).removeRecursively();
    }

    // 4) 概要翻译（OVERVIEW/<siteKey>/<workId>.txt）
    if (!siteKey.isEmpty() && !workId.isEmpty()) {
        const QString overview = QDir(siteOverviewDir()).filePath(
            siteKey + QLatin1Char('/') + workId + QStringLiteral(".txt"));
        if (QFileInfo::exists(overview))
            QFile::remove(overview);
        const QString comments = siteCommentsPathFor(siteKey, workId);
        if (!comments.isEmpty() && QFileInfo::exists(comments))
            QFile::remove(comments);
    }

    // 5) 阅读进度 / 术语表启用状态
    if (!workId.isEmpty()) {
        m_readerModel->clearNovelProgress(workId);
        QSettings settings(QStringLiteral("KakuReader"), QStringLiteral("KakuReader"));
        settings.remove(QStringLiteral("glossary/active/") + workId);
    }

    m_currentNovelRow = -1;
    emit currentNovelRowChanged();
    refreshLibrary();
    emit notify(removed ? QStringLiteral("已删除《") + title + QStringLiteral("》（含译文/双语/术语表）")
                        : QStringLiteral("《") + title + QStringLiteral("》的文件已不存在"));
    return removed;
}

QString AppController::currentNovelRelativeKey() const
{
    if (m_currentNovelRow < 0)
        return QString();
    const QString root = m_libraryModel->novelRootPath(m_currentNovelRow);
    if (root.isEmpty())
        return QString(); // 旧扁平结构：没有小说根目录
    const QString rel = NovelPaths::relativeKey(m_downloadDir, root);
    if (!rel.isEmpty())
        return rel;
    return m_libraryModel->novelWorkId(m_currentNovelRow);
}

// 这本书应该落在哪个目录：优先复用已存在的目录（兼容旧布局），否则用新布局。
QString AppController::novelRootForWork(const NovelWork &work) const
{
    const QString fresh = NovelPaths::novelRoot(m_downloadDir, work.siteKey, work.workId);
    if (QFileInfo::exists(fresh))
        return fresh;
    const QString legacy = NovelPaths::legacyNovelRoot(m_downloadDir, work.workId);
    if (QFileInfo::exists(legacy))
        return legacy;
    return fresh;
}

namespace {
// 把 <base>/<from> 移动到 <base>/<siteKey>/<workId>；目标已存在或源不存在时跳过。
void moveLayoutDir(const QString &base, const QString &from, const QString &siteKey,
                   const QString &workId, QStringList *movedLog)
{
    if (base.isEmpty() || from.isEmpty() || siteKey.isEmpty() || workId.isEmpty())
        return;
    QDir baseDir(base);
    const QString src = baseDir.filePath(from);
    if (!QDir(src).exists())
        return;
    const QString dst = NovelPaths::novelRoot(base, siteKey, workId);
    if (QFileInfo::exists(dst))
        return;
    baseDir.mkpath(siteKey);
    if (QDir().rename(src, dst) && movedLog)
        movedLog->append(QDir(dst).dirName());
}
} // namespace

// 一次性把旧布局 DOWNLOAD/<workId>（以及 CHDL / CJDL 下同名目录）
// 迁移到 DOWNLOAD/<siteKey>/<workId>。只在目标不存在时移动，失败也不会丢数据。
void AppController::migrateLegacyLayouts()
{
    QDir dl(m_downloadDir);
    if (!dl.exists())
        return;

    const QStringList dirs = dl.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    int migrated = 0;
    for (const QString &name : dirs) {
        const QString legacyRoot = dl.filePath(name);
        // 旧布局的小说根：自己就带 chapters/
        if (!QDir(QDir(legacyRoot).filePath(QStringLiteral("chapters"))).exists())
            continue;

        const NovelWork meta = NovelStore::loadNovel(legacyRoot);
        QString siteKey = meta.siteKey;
        if (siteKey.isEmpty())
            siteKey = NovelPaths::siteKeyFromUrl(meta.workUrl);
        if (siteKey.isEmpty())
            siteKey = QStringLiteral("kakuyomujp"); // 旧版本只支持カクヨム
        const QString workId = meta.workId.isEmpty() ? name : meta.workId;

        const QString target = NovelPaths::novelRoot(m_downloadDir, siteKey, workId);
        if (QFileInfo::exists(target))
            continue;

        moveLayoutDir(m_downloadDir, name, siteKey, workId, nullptr);
        moveLayoutDir(m_chdlDir, name, siteKey, workId, nullptr);
        moveLayoutDir(m_cjdlDir, name, siteKey, workId, nullptr);
        if (QFileInfo::exists(target))
            ++migrated;
    }
    if (migrated > 0) {
        qInfo() << "[Layout] 已迁移" << migrated << "本小说到 <siteKey>/<workId> 目录布局";
        emit notify(QStringLiteral("已按新目录布局整理 %1 本小说").arg(migrated));
    }
}

// ---------------- 「网站」在线浏览 ----------------

QString AppController::siteName() const
{
    return m_siteWork.siteKey.isEmpty()
               ? QString()
               : NovelPaths::siteDisplayName(m_siteWork.siteKey);
}

// Only expose sites with a verified public catalogue -> saved text path.
QVariantList AppController::supportedSites() const
{
    struct Def { const char *key; const char *url; const char *sample; };
    static const Def defs[] = {
        { "kakuyomujp", "https://kakuyomu.jp/", "https://kakuyomu.jp/works/16817330669718495903" },
        { "pixivnet", "https://www.pixiv.net/novel/", "https://www.pixiv.net/novel/show.php?id=28638469" },
        { "estarjp", "https://estar.jp/", "https://estar.jp/novels/26100006" },
        { "noveldays", "https://novel.daysneo.com/", "https://novel.daysneo.com/works/9a5769c1bd6af21f9a3458095eb912a3.html" },
        { "novelba", "https://novelba.com/", "https://novelba.com/indies/works/908458" },
        { "solispia", "https://solispia.com/", "https://solispia.com/title/7239" },
    };
    QVariantList list;
    for (const Def &d : defs) {
        QVariantMap m;
        m.insert(QStringLiteral("key"), QString::fromUtf8(d.key));
        m.insert(QStringLiteral("name"), NovelPaths::siteDisplayName(QString::fromUtf8(d.key)));
        m.insert(QStringLiteral("url"), QString::fromUtf8(d.url));
        m.insert(QStringLiteral("sample"), QString::fromUtf8(d.sample));
        list.append(m);
    }
    return list;
}

// kakuyomu 的排行榜分类（已验证可用的 slug）
QVariantList AppController::siteGenres() const
{
    struct Def { const char *slug; const char *name; };
    static const Def defs[] = {
        { "all",         "综合" },
        { "fantasy",     "奇幻" },
        { "action",      "动作" },
        { "romance",     "恋爱" },
        { "drama",       "现代剧情" },
        { "mystery",     "悬疑" },
        { "horror",      "恐怖" },
        { "sf",          "科幻" },
        { "history",     "历史·时代" },
        { "criticism",   "创作论·评论" },
        { "nonfiction",  "随笔·纪实" },
    };
    QVariantList list;
    for (const Def &d : defs) {
        QVariantMap m;
        m.insert(QStringLiteral("slug"), QString::fromUtf8(d.slug));
        m.insert(QStringLiteral("name"), QString::fromUtf8(d.name));
        list.append(m);
    }
    return list;
}

// 排行周期
QVariantList AppController::sitePeriods() const
{
    struct Def { const char *slug; const char *name; };
    static const Def defs[] = {
        { "daily",   "日榜" },
        { "weekly",  "周榜" },
        { "monthly", "月榜" },
        { "yearly",  "年榜" },
    };
    QVariantList list;
    for (const Def &d : defs) {
        QVariantMap m;
        m.insert(QStringLiteral("slug"), QString::fromUtf8(d.slug));
        m.insert(QStringLiteral("name"), QString::fromUtf8(d.name));
        list.append(m);
    }
    return list;
}

// 小说长度（本地按字数筛选）
QVariantList AppController::siteLengths() const
{
    QVariantList list;
    const char *names[] = { "全部", "长篇", "短篇" };
    for (int i = 0; i < 3; ++i) {
        QVariantMap m;
        m.insert(QStringLiteral("value"), i);
        m.insert(QStringLiteral("name"), QString::fromUtf8(names[i]));
        list.append(m);
    }
    return list;
}

void AppController::siteSetLengthFilter(int value)
{
    const int v = qBound(0, value, 2);
    int &filter = m_siteMode == QStringLiteral("ranking") ? m_siteRankLengthFilter : m_siteLengthFilter;
    if (v == filter)
        return;
    filter = v;
    emit siteCardsChanged();
}

// 结果列表：按「小说长度」做本地筛选（10 万字为界）
QVariantList AppController::siteCards() const
{
    QVariantList out;
    const bool search = m_siteMode == QStringLiteral("search");
    const int length = siteLengthFilterValue();
    const int status = siteStatusFilter();
    const QString inc = search ? m_siteInclusions.trimmed() : QString();
    const QString exc = search ? m_siteExclusions.trimmed() : QString();
    for (const QVariant &v : m_siteCards) {
        const QVariantMap c = v.toMap();

        // 小说长度（本地，10 万字为界）
        if (length != 0) {
            QString chars = c.value(QStringLiteral("chars")).toString();
            chars.remove(QLatin1Char(','));
            const bool isLong = chars.toInt() >= 100000;
            if (length == 1 && !isLong)
                continue;
            if (length == 2 && isLong)
                continue;
        }
        // 连载状态
        if (status != 0) {
            const QString st = c.value(QStringLiteral("status")).toString();
            if (status == 1 && st != QStringLiteral("連載中"))
                continue;
            if (status == 2 && st != QStringLiteral("完結済"))
                continue;
        }
        // ★ 数量下限
        if (search && m_siteMinStars > 0) {
            QString f = c.value(QStringLiteral("follow")).toString();
            f.remove(QLatin1Char(','));
            if (f.toInt() < m_siteMinStars)
                continue;
        }
        const QString hay = c.value(QStringLiteral("title")).toString()
                            + QLatin1Char(' ') + c.value(QStringLiteral("author")).toString()
                            + QLatin1Char(' ') + c.value(QStringLiteral("genre")).toString();
        if (!inc.isEmpty() && !hay.contains(inc, Qt::CaseInsensitive))
            continue;
        if (!exc.isEmpty() && hay.contains(exc, Qt::CaseInsensitive))
            continue;
        out.append(v);
    }
    return out;
}

void AppController::siteRefresh()
{
    if (m_siteOverviewTranslating) {
        emit notify(QStringLiteral("请等待当前概要/评论翻译完成后刷新"));
        return;
    }
    m_siteRefreshWorkUrl = m_siteWork.workUrl;
    m_siteBrowser->clearSessionCache();
    m_siteWork = {};
    m_siteStats.clear();
    m_siteChaptersList.clear();
    m_siteComments.clear();
    m_siteCommentTranslations.clear();
    m_siteCommentsTranslated = false;
    m_siteCommentsBusy = false;
    ++m_siteChapterRevision;
    m_siteCards.clear();
    m_siteBusy = true;
    m_siteSearching = m_siteMode == QStringLiteral("search");
    m_siteMessage = QStringLiteral("正在刷新…");
    emit siteWorkChanged();
    emit siteCommentsChanged();
    emit siteCardsChanged();
    emit siteBusyChanged();
    emit siteSearchingChanged();
    emit siteMessageChanged();
    if (m_siteMode == QStringLiteral("ranking"))
        m_siteBrowser->fetchKakuyomuRanking(m_siteRankGenre, m_siteRankPeriod, m_sitePage);
    else if (!m_siteSubmittedKeyword.isEmpty()) {
        if (m_siteSearchKind == QStringLiteral("authors"))
            m_siteBrowser->searchKakuyomuAuthorWorks(m_siteSubmittedKeyword);
        else
            m_siteBrowser->searchKakuyomu(m_siteSubmittedKeyword, m_sitePage, m_siteSearchOrder);
    } else {
        onSiteCardsFailed(QStringLiteral("缓存已清除，请输入检索条件"));
    }
}

void AppController::siteSearch(const QString &keyword)
{
    siteSetMode(QStringLiteral("search"));
    siteSetSearchKind(QStringLiteral("works"));
    siteSetKeyword(keyword);
    siteSearchNow();
}

void AppController::siteRanking(const QString &genre, const QString &period)
{
    m_siteRefreshWorkUrl.clear();
    m_siteRankGenre = genre;
    m_siteRankPeriod = period;
    m_sitePage = 1;
    m_sitePageCount = 1;
    m_siteCards.clear();
    m_siteSearching = false;
    m_siteBusy = true;
    QString label = genre;
    for (const QVariant &v : siteGenres()) {
        const QVariantMap g = v.toMap();
        if (g.value(QStringLiteral("slug")).toString() == genre) {
            label = g.value(QStringLiteral("name")).toString();
            break;
        }
    }
    QString periodLabel = period;
    for (const QVariant &v : sitePeriods()) {
        const QVariantMap g = v.toMap();
        if (g.value(QStringLiteral("slug")).toString() == period) {
            periodLabel = g.value(QStringLiteral("name")).toString();
            break;
        }
    }
    m_siteListTitle = QStringLiteral("%1 · %2").arg(label, periodLabel);
    m_siteMessage = QStringLiteral("正在获取排行榜…");
    emit siteBusyChanged();
    emit siteCardsChanged();
    emit siteMessageChanged();
    emit sitePageChanged();
    emit siteSearchingChanged();
    m_siteBrowser->fetchKakuyomuRanking(genre, period, m_sitePage);
}

void AppController::onSiteCardsReady(const QVariantList &cards)
{
    m_siteCards = cards;
    m_siteBusy = false;
    m_siteSearching = false;
    m_siteMessage = QStringLiteral("共 %1 条").arg(cards.size());
    emit siteBusyChanged();
    emit siteSearchingChanged();
    emit siteCardsChanged();
    emit siteMessageChanged();
    if (!m_siteRefreshWorkUrl.isEmpty()) {
        const QString url = m_siteRefreshWorkUrl;
        m_siteRefreshWorkUrl.clear();
        siteOpenWork(url);
    }
}

void AppController::onSiteCardsFailed(const QString &error)
{
    m_siteCards.clear();
    m_siteBusy = false;
    m_siteSearching = false;
    m_siteMessage = error;
    emit siteBusyChanged();
    emit siteSearchingChanged();
    emit siteCardsChanged();
    emit siteMessageChanged();
    if (!m_siteRefreshWorkUrl.isEmpty()) {
        const QString url = m_siteRefreshWorkUrl;
        m_siteRefreshWorkUrl.clear();
        siteOpenWork(url);
    }
}

void AppController::siteOpenWork(const QString &url)
{
    m_siteRefreshWorkUrl.clear();
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty()) {
        m_siteMessage = QStringLiteral("请先选择或输入作品网址");
        emit siteMessageChanged();
        return;
    }
    m_siteBusy = true;
    m_siteMessage = QStringLiteral("正在打开作品…");
    emit siteBusyChanged();
    emit siteMessageChanged();
    m_siteBrowser->openWork(trimmed);
}

void AppController::onSiteWorkOpened(const NovelWork &work)
{
    m_siteWork = work;
    m_siteBusy = false;
    ++m_siteChapterRevision;
    m_siteChaptersList = buildSiteChapterList();
    m_siteComments.clear();
    m_siteCommentTranslations.clear();
    m_siteCommentsTranslated = false;
    m_siteMessage = QStringLiteral("共 %1 章").arg(work.chapters.size());
    // 读取本地收藏 / 评分
    QSettings settings;
    const QString key = work.workId;
    m_siteFavorited = settings.value(QStringLiteral("site/fav/") + key, false).toBool();
    m_siteRating = settings.value(QStringLiteral("site/rating/") + key, 0).toInt();
    emit siteBusyChanged();
    emit siteMessageChanged();
    emit siteWorkChanged();
    emit sitePrefsChanged();
    refreshSiteOverviewTranslation();
    // 自动载入评论区
    siteLoadComments();
}

void AppController::onSiteWorkStats(const QVariantMap &stats)
{
    m_siteStats = stats;
    emit siteWorkChanged();
}

void AppController::onSiteWorkFailed(const QString &error)
{
    m_siteBusy = false;
    m_siteMessage = error;
    emit siteBusyChanged();
    emit siteMessageChanged();
}

// 章节列表 + 「是否已下载到本地」（直接查磁盘）
QVariantList AppController::buildSiteChapterList() const
{
    QVariantList list;
    if (m_siteWork.chapters.isEmpty())
        return list;
    const QString root = NovelPaths::novelRoot(m_downloadDir, m_siteWork.siteKey,
                                               m_siteWork.workId);
    const QDir chaptersDir(QDir(root).filePath(QStringLiteral("chapters")));
    for (int i = 0; i < m_siteWork.chapters.size(); ++i) {
        const Chapter &ch = m_siteWork.chapters.at(i);
        const int number = ch.number > 0 ? ch.number : (i + 1);
        const QString fileName = QString::number(number).rightJustified(3, QLatin1Char('0'))
                                 + QStringLiteral(".txt");
        QVariantMap m;
        m.insert(QStringLiteral("index"), i);
        m.insert(QStringLiteral("number"), number);
        m.insert(QStringLiteral("title"), ch.title);
        m.insert(QStringLiteral("downloaded"),
                 QFileInfo::exists(chaptersDir.filePath(fileName)));
        list.append(m);
    }
    return list;
}

void AppController::siteLoadComments()
{
    if (m_siteWork.workId.isEmpty())
        return;
    m_siteCommentsBusy = true;
    emit siteCommentsChanged();
    m_siteBrowser->fetchKakuyomuComments(m_siteWork.workId);
}

void AppController::onSiteCommentsReady(const QVariantList &comments)
{
    m_siteComments = comments;
    m_siteCommentsBusy = false;
    refreshSiteCommentsTranslation();
}

void AppController::onSiteCommentsFailed(const QString &error)
{
    m_siteComments.clear();
    m_siteCommentsBusy = false;
    m_siteMessage = error;
    emit siteCommentsChanged();
    emit siteMessageChanged();
}

void AppController::onSiteAuthorResolved(const QString &authorName)
{
    if (m_siteMode != QStringLiteral("search")
        || m_siteSearchKind != QStringLiteral("authors"))
        return;
    m_siteListTitle = QStringLiteral("作者：%1 · %2")
                          .arg(authorName, m_siteSubmittedKeyword);
    emit siteCardsChanged();
}

// 本地收藏（站点上的收藏需要登录，这里只做软件内的标记）
void AppController::siteToggleFavorite()
{
    if (m_siteWork.workId.isEmpty())
        return;
    m_siteFavorited = !m_siteFavorited;
    QSettings settings;
    settings.setValue(QStringLiteral("site/fav/") + m_siteWork.workId, m_siteFavorited);
    emit sitePrefsChanged();
    emit notify(m_siteFavorited ? QStringLiteral("已加入本地收藏")
                                : QStringLiteral("已取消本地收藏"));
}

void AppController::siteSetRating(int stars)
{
    if (m_siteWork.workId.isEmpty())
        return;
    m_siteRating = qBound(0, stars, 5);
    QSettings settings;
    settings.setValue(QStringLiteral("site/rating/") + m_siteWork.workId, m_siteRating);
    emit sitePrefsChanged();
}

// 用既有下载流程把这本书的全部章节存到 DOWNLOAD/<siteKey>/<workId>/
void AppController::siteDownloadWork()
{
    if (m_siteWork.workId.isEmpty() || m_siteWork.chapters.isEmpty()) {
        m_siteMessage = QStringLiteral("请先打开一本作品");
        emit siteMessageChanged();
        return;
    }
    if (m_downloading) {
        m_siteMessage = QStringLiteral("已有下载任务在进行");
        emit siteMessageChanged();
        return;
    }
    m_downloading = true;
    emit downloadingChanged();
    m_siteMessage = QStringLiteral("开始下载《%1》…").arg(m_siteWork.title);
    emit siteMessageChanged();
    m_service->downloadChapters(m_siteWork, m_siteWork.chapters, m_downloadDir);
}

// 把当前作品的网址交给「下载」页
void AppController::queueSiteWorkForDownload()
{
    if (m_siteWork.workUrl.isEmpty()) {
        m_siteMessage = QStringLiteral("请先打开一本作品");
        emit siteMessageChanged();
        return;
    }
    m_pendingDownloadUrl = m_siteWork.workUrl;
    emit pendingDownloadUrlChanged();
    emit notify(QStringLiteral("网址已送到「下载」页"));
}

void AppController::clearPendingDownloadUrl()
{
    if (m_pendingDownloadUrl.isEmpty())
        return;
    m_pendingDownloadUrl.clear();
    emit pendingDownloadUrlChanged();
}

// ---------------- 「网站」检索模式 ----------------

void AppController::siteSetMode(const QString &mode)
{
    const QString m = (mode == QStringLiteral("search")) ? mode : QStringLiteral("ranking");
    if (m == m_siteMode)
        return;
    m_siteMode = m;
    m_siteBrowser->invalidateCards();
    m_siteRefreshWorkUrl.clear();
    m_siteCards.clear();
    m_siteListTitle.clear();
    m_siteMessage.clear();
    m_siteBusy = false;
    m_siteSearching = false;
    m_sitePage = 1;
    m_sitePageCount = 1;
    emit siteModeChanged();
    emit sitePageChanged();
    emit siteCardsChanged();
    emit siteSearchStateChanged();
    emit siteBusyChanged();
    emit siteSearchingChanged();
    emit siteMessageChanged();
    if (m == QStringLiteral("ranking"))
        siteRanking(m_siteRankGenre, m_siteRankPeriod);
}

// kakuyomu 搜索页的ジャンル（显示中文，勾选后把原版日文放进搜索框）
QVariantList AppController::siteSearchGenres() const
{
    struct Def { const char *jp; const char *zh; };
    static const Def defs[] = {
        { "異世界ファンタジー",      "异世界奇幻" },
        { "現代ファンタジー",        "现代奇幻" },
        { "SF",                      "科幻" },
        { "恋愛",                    "恋爱" },
        { "ラブコメ",                "恋爱喜剧" },
        { "現代ドラマ",              "现代剧情" },
        { "ホラー",                  "恐怖" },
        { "ミステリー",              "悬疑" },
        { "エッセイ・ノンフィクション", "随笔·纪实" },
        { "歴史・時代・伝奇",        "历史·时代·传奇" },
        { "創作論・評論",            "创作论·评论" },
        { "詩・童話・その他",        "诗歌·童话·其他" },
        { "魔法のiらんど",           "魔法i乐园" },
        { "二次創作",                "二次创作" },
    };
    QVariantList list;
    for (const Def &d : defs) {
        QVariantMap m;
        m.insert(QStringLiteral("jp"), QString::fromUtf8(d.jp));
        m.insert(QStringLiteral("zh"), QString::fromUtf8(d.zh));
        list.append(m);
    }
    return list;
}

void AppController::siteSetKeyword(const QString &text)
{
    if (m_siteKeyword == text)
        return;
    m_siteKeyword = text;
    emit siteSearchStateChanged();
}

void AppController::siteSetSearchKind(const QString &kind)
{
    const QString value = kind == QStringLiteral("authors")
                              ? QStringLiteral("authors") : QStringLiteral("works");
    if (value == m_siteSearchKind)
        return;
    m_siteSearchKind = value;
    m_siteBrowser->invalidateCards();
    m_siteCards.clear();
    m_siteListTitle.clear();
    m_siteMessage.clear();
    m_sitePage = 1;
    m_sitePageCount = 1;
    m_siteBusy = false;
    m_siteSearching = false;
    emit siteSearchStateChanged();
    emit siteCardsChanged();
    emit sitePageChanged();
    emit siteBusyChanged();
    emit siteSearchingChanged();
    emit siteMessageChanged();
}

void AppController::siteSetInclusions(const QString &text)
{
    if (m_siteInclusions == text)
        return;
    m_siteInclusions = text;
    emit siteSearchStateChanged();
    emit siteCardsChanged();
}

void AppController::siteSetExclusions(const QString &text)
{
    if (m_siteExclusions == text)
        return;
    m_siteExclusions = text;
    emit siteSearchStateChanged();
    emit siteCardsChanged();
}

void AppController::siteSetStatusFilter(int value)
{
    const int v = qBound(0, value, 2);
    int &filter = m_siteMode == QStringLiteral("ranking") ? m_siteRankStatusFilter : m_siteStatusFilter;
    if (v == filter)
        return;
    filter = v;
    emit siteSearchStateChanged();
    emit siteCardsChanged();
}

void AppController::siteSetMinStars(int value)
{
    if (value == m_siteMinStars)
        return;
    m_siteMinStars = value;
    emit siteSearchStateChanged();
    emit siteCardsChanged();
}

void AppController::siteSetSearchOrder(const QString &order)
{
    const QStringList orders = {"popular", "weekly_ranking", "published_at", "last_episode_published_at"};
    if (!orders.contains(order) || order == m_siteSearchOrder)
        return;
    m_siteSearchOrder = order;
    emit siteSearchStateChanged();
    if (m_siteMode == QStringLiteral("search")
        && m_siteSearchKind == QStringLiteral("works")
        && (!m_siteKeyword.trimmed().isEmpty() || !m_siteSearchGenresSelected.isEmpty()))
        siteSearchNow();
}

// 分类和推荐词作为独立筛选条件保存，不再污染搜索框里的可见文本。
void AppController::siteToggleSearchGenre(const QString &jp, bool on)
{
    if (jp.trimmed().isEmpty())
        return;
    const bool changed = on ? !m_siteSearchGenresSelected.contains(jp)
                            : m_siteSearchGenresSelected.contains(jp);
    if (!changed)
        return;
    if (on)
        m_siteSearchGenresSelected << jp;
    else
        m_siteSearchGenresSelected.removeAll(jp);
    emit siteSearchStateChanged();
}

void AppController::siteToggleSearchKeyword(const QString &jp, bool on)
{
    if (jp.trimmed().isEmpty())
        return;
    QStringList parts = m_siteKeyword.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const bool changed = on ? !parts.contains(jp) : parts.contains(jp);
    if (!changed)
        return;
    if (on)
        parts << jp;
    else
        parts.removeAll(jp);
    m_siteKeyword = parts.join(QLatin1Char(' '));
    emit siteSearchStateChanged();
}

void AppController::siteSearchNow()
{
    m_siteRefreshWorkUrl.clear();
    if (m_siteSearchKind == QStringLiteral("authors")) {
        const QString keyword = m_siteKeyword.trimmed();
        if (keyword.isEmpty()) {
            m_siteMessage = QStringLiteral("请输入作者 ID 或笔名");
            emit siteMessageChanged();
            return;
        }
        m_siteBusy = true;
        m_siteSearching = true;
        m_siteListTitle = QStringLiteral("作者检索：%1").arg(keyword);
        m_siteMessage = QStringLiteral("正在搜索作者…");
        m_sitePage = 1;
        m_sitePageCount = 1;
        m_siteCards.clear();
        m_siteSubmittedKeyword = keyword;
        emit siteBusyChanged();
        emit siteSearchingChanged();
        emit siteCardsChanged();
        emit siteMessageChanged();
        emit sitePageChanged();
        m_siteBrowser->searchKakuyomuAuthorWorks(keyword);
        return;
    }

    QStringList effectiveParts =
        m_siteKeyword.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &jp : m_siteSearchGenresSelected) {
        if (!effectiveParts.contains(jp))
            effectiveParts << jp;
    }
    if (effectiveParts.isEmpty()) {
        m_siteMessage = QStringLiteral("请输入关键词或勾选分类");
        emit siteMessageChanged();
        return;
    }

    QStringList titleParts;
    if (!m_siteKeyword.trimmed().isEmpty())
        titleParts << m_siteKeyword.trimmed();
    const QVariantList genres = siteSearchGenres();
    for (const QString &jp : m_siteSearchGenresSelected) {
        QString zh = jp;
        for (const QVariant &value : genres) {
            const QVariantMap genre = value.toMap();
            if (genre.value(QStringLiteral("jp")).toString() == jp) {
                zh = genre.value(QStringLiteral("zh")).toString();
                break;
            }
        }
        titleParts << zh;
    }

    m_siteBusy = true;
    m_siteSearching = true;
    m_siteListTitle = QStringLiteral("搜索：%1").arg(titleParts.join(QStringLiteral(" · ")));
    m_siteMessage = QStringLiteral("正在搜索…");
    m_sitePage = 1;
    m_sitePageCount = 1;
    m_siteCards.clear();
    m_siteSubmittedKeyword = effectiveParts.join(QLatin1Char(' '));
    emit siteBusyChanged();
    emit siteSearchingChanged();
    emit siteCardsChanged();
    emit siteMessageChanged();
    emit sitePageChanged();
    m_siteBrowser->searchKakuyomu(m_siteSubmittedKeyword, m_sitePage, m_siteSearchOrder);
}

void AppController::siteGoPage(int page)
{
    const int p = qBound(1, page, qMax(1, m_sitePageCount));
    if (p == m_sitePage || m_siteBusy)
        return;
    m_sitePage = p;
    emit sitePageChanged();
    m_siteBusy = true;
    m_siteSearching = m_siteMode == QStringLiteral("search");
    m_siteCards.clear();
    m_siteMessage = m_siteSearching ? QStringLiteral("正在搜索…") : QStringLiteral("正在获取排行榜…");
    emit siteBusyChanged();
    emit siteSearchingChanged();
    emit siteCardsChanged();
    emit siteMessageChanged();
    if (m_siteSearching)
        m_siteBrowser->searchKakuyomu(m_siteSubmittedKeyword, m_sitePage, m_siteSearchOrder);
    else
        m_siteBrowser->fetchKakuyomuRanking(m_siteRankGenre, m_siteRankPeriod, m_sitePage);
}

void AppController::onSitePageReady(int totalPages)
{
    m_sitePageCount = m_siteMode == QStringLiteral("ranking")
                          ? qMax(m_sitePageCount, qBound(1, totalPages, 5))
                          : qMax(1, totalPages);
    emit sitePageChanged();
}

// ---------------------------------------------------------------- 概要翻译

// 是否有其它翻译任务在进行（下载不算）
bool AppController::anyTranslateBusy() const
{
    return m_translating || m_murasakiTranslating || m_qwenTranslating
           || m_qwenExtracting || m_siteOverviewTranslating;
}

QString AppController::siteOverviewDir() const
{
    return QDir(m_saveRoot).filePath(QStringLiteral("OVERVIEW"));
}

QString AppController::siteOverviewFilePath() const
{
    return siteOverviewPathFor(m_siteWork.siteKey, m_siteWork.workId);
}

QString AppController::siteOverviewPathFor(const QString &siteKey, const QString &workId) const
{
    if (siteKey.isEmpty() || workId.isEmpty())
        return QString();
    return QDir(siteOverviewDir())
        .filePath(siteKey + QLatin1Char('/') + workId + QStringLiteral(".txt"));
}

QString AppController::siteCommentsDir() const
{
    return QDir(m_saveRoot).filePath(QStringLiteral("COMMENTS"));
}

QString AppController::siteCommentsPathFor(const QString &siteKey, const QString &workId) const
{
    if (siteKey.isEmpty() || workId.isEmpty())
        return QString();
    return QDir(siteCommentsDir())
        .filePath(siteKey + QLatin1Char('/') + workId + QStringLiteral(".json"));
}

void AppController::refreshSiteOverviewTranslation()
{
    const QString path = siteOverviewFilePath();
    QString text;
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            text = QString::fromUtf8(f.readAll());
            f.close();
            if (text.startsWith(QChar(0xFEFF)))
                text.remove(0, 1);
        }
    }
    m_siteOverviewTranslated = !text.trimmed().isEmpty();
    m_siteOverviewTranslation = text;
    emit siteOverviewTranslatedChanged();
}

void AppController::refreshSiteCommentsTranslation()
{
    m_siteCommentTranslations.clear();
    for (int i = 0; i < m_siteComments.size(); ++i)
        m_siteCommentTranslations << QString();

    QHash<QString, QString> saved;
    const QString path = siteCommentsPathFor(m_siteWork.siteKey, m_siteWork.workId);
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonArray entries =
                QJsonDocument::fromJson(f.readAll()).array();
            for (const QJsonValue &value : entries) {
                const QJsonObject entry = value.toObject();
                const QString source = entry.value(QStringLiteral("source")).toString();
                const QString translation = entry.value(QStringLiteral("translation")).toString();
                if (!source.isEmpty() && !translation.trimmed().isEmpty())
                    saved.insert(source, translation);
            }
        }
    }

    bool hasTranslation = false;
    for (int i = 0; i < m_siteComments.size(); ++i) {
        const QString source =
            m_siteComments.at(i).toMap().value(QStringLiteral("body")).toString();
        const QString translation = saved.value(source);
        m_siteCommentTranslations[i] = translation;
        hasTranslation = hasTranslation || !translation.trimmed().isEmpty();
    }
    m_siteCommentsTranslated = hasTranslation;
    emit siteCommentsChanged();
}

void AppController::startSiteTextTranslation(const QString &kind, const QStringList &sources)
{
    if (m_siteOverviewTranslating)
        return;
    if (anyTranslateBusy()) {
        emit notify(QStringLiteral("当前有其它翻译任务，请稍后再试"));
        return;
    }
    if (m_siteWork.workId.isEmpty()) {
        emit notify(QStringLiteral("请先打开一本作品"));
        return;
    }
    if (sources.isEmpty()) {
        emit notify(kind == QStringLiteral("comments")
                        ? QStringLiteral("该作品没有可翻译的评论")
                        : QStringLiteral("该作品没有概要可翻译"));
        return;
    }

    int model = -1;
    if (m_qwenRunning)
        model = 2;
    else if (m_murasakiRunning)
        model = 1;
    else if (m_sakuraRunning)
        model = 0;
    if (model < 0) {
        emit notify(QStringLiteral("请先启动一个翻译模型（Sakura / Murasaki / Qwen）"));
        return;
    }

    // 锁定本次翻译归属的作品：翻译期间用户切换小说时，结果只落给这本书。
    m_siteOverviewSiteKey = m_siteWork.siteKey;
    m_siteOverviewWorkId = m_siteWork.workId;
    m_siteTranslationKind = kind;
    m_siteTranslationSources = sources;
    m_siteTranslationFiles.clear();

    const QString base = QDir::tempPath() + QStringLiteral("/kakuCH_site_")
                         + kind + QLatin1Char('_') + m_siteOverviewWorkId;
    m_siteOverviewSourceDir = base + QStringLiteral("_src");
    m_siteOverviewOutputDir = base + QStringLiteral("_out");
    QDir(m_siteOverviewSourceDir).removeRecursively();
    QDir(m_siteOverviewOutputDir).removeRecursively();
    QDir().mkpath(m_siteOverviewSourceDir);
    QDir().mkpath(m_siteOverviewOutputDir);

    for (int i = 0; i < sources.size(); ++i) {
        const QString fileName = kind == QStringLiteral("overview")
            ? QStringLiteral("overview.txt")
            : QStringLiteral("%1.txt").arg(i + 1, 3, 10, QLatin1Char('0'));
        const QString srcPath = QDir(m_siteOverviewSourceDir).filePath(fileName);
        QFile f(srcPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit notify(kind == QStringLiteral("comments")
                            ? QStringLiteral("无法创建评论临时文件")
                            : QStringLiteral("无法创建概要临时文件"));
            QDir(m_siteOverviewSourceDir).removeRecursively();
            QDir(m_siteOverviewOutputDir).removeRecursively();
            return;
        }
        f.write(sources.at(i).toUtf8());
        f.close();
        m_siteTranslationFiles << srcPath;
    }

    m_siteOverviewModel = model;
    m_siteOverviewTranslating = true;
    m_siteOverviewCurrent = 0;
    m_siteOverviewTotal = sources.size();
    m_siteOverviewMessage = kind == QStringLiteral("comments")
        ? QStringLiteral("开始翻译评论…")
        : QStringLiteral("开始翻译概要…");
    emit siteOverviewTranslatingChanged();
    emit siteOverviewProgressChanged();

    if (model == 0) {
        m_translator->setCjdlDir(QString());
        m_translator->setGenerateBilingual(false);
        m_translator->setServerUrl(m_sakura->serverUrl());
        m_translator->setModelAlias(m_sakura->modelAlias());
        m_translator->setSystemPrompt(m_systemPrompt);
        QVector<FileJob> jobs;
        for (const QString &path : m_siteTranslationFiles) {
            FileJob job;
            job.path = path;
            job.outputDir = m_siteOverviewOutputDir;
            jobs.append(job);
        }
        m_translator->translateFiles(jobs);
    } else if (model == 1) {
        m_murasakiTranslator->setCjdlDir(QString());
        m_murasakiTranslator->setGenerateBilingual(false);
        m_murasakiTranslator->setServerUrl(m_murasaki->serverUrl());
        m_murasakiTranslator->setModelAlias(m_murasaki->modelAlias());
        m_murasakiTranslator->setModelIdentity(m_murasaki->modelPath());
        m_murasakiTranslator->setDownloadRoot(m_downloadDir);
        m_murasakiTranslator->setSystemPrompt(MurasakiTranslator::defaultPrompt());
        QVector<MurasakiFileJob> jobs;
        for (const QString &path : m_siteTranslationFiles) {
            MurasakiFileJob job;
            job.path = path;
            job.outputDir = m_siteOverviewOutputDir;
            jobs.append(job);
        }
        m_murasakiTranslator->translateFiles(jobs);
    } else {
        m_qwenTranslator->setCjdlDir(QString());
        m_qwenTranslator->setServerUrl(m_qwen->serverUrl());
        m_qwenTranslator->setModelAlias(m_qwen->modelAlias());
        m_qwenTranslator->setSystemPrompt(QwenTranslator::defaultPrompt());
        QVector<QwenFileJob> jobs;
        for (const QString &path : m_siteTranslationFiles) {
            QwenFileJob job;
            job.path = path;
            job.outputDir = m_siteOverviewOutputDir;
            jobs.append(job);
        }
        m_qwenTranslator->translateFiles(jobs);
    }
}

void AppController::siteTranslateOverview()
{
    const QString catchphrase = m_siteStats.value(QStringLiteral("catchphrase")).toString().trimmed();
    const QString introduction = m_siteStats.value(QStringLiteral("introduction")).toString().trimmed();
    QString source = introduction;
    if (!catchphrase.isEmpty())
        source = catchphrase + QStringLiteral("\n\n") + introduction;
    if (source.trimmed().isEmpty()) {
        emit notify(QStringLiteral("该作品没有概要可翻译"));
        return;
    }
    startSiteTextTranslation(QStringLiteral("overview"), {source});
}

void AppController::siteTranslateComments()
{
    QStringList sources;
    for (const QVariant &value : m_siteComments) {
        const QString body =
            value.toMap().value(QStringLiteral("body")).toString().trimmed();
        if (!body.isEmpty())
            sources << body;
    }
    startSiteTextTranslation(QStringLiteral("comments"), sources);
}

void AppController::finishSiteOverviewTranslation(int success)
{
    const bool comments = m_siteTranslationKind == QStringLiteral("comments");
    QString overviewTranslation;
    QHash<QString, QString> commentTranslations;

    if (comments) {
        for (int i = 0; i < m_siteTranslationSources.size(); ++i) {
            const QString fileName =
                QStringLiteral("%1.txt").arg(i + 1, 3, 10, QLatin1Char('0'));
            const QString outPath = QDir(m_siteOverviewOutputDir).filePath(fileName);
            if (!QFileInfo::exists(outPath))
                continue;
            QFile f(outPath);
            if (f.open(QIODevice::ReadOnly)) {
                QString text = QString::fromUtf8(f.readAll());
                f.close();
                if (text.startsWith(QChar(0xFEFF)))
                    text.remove(0, 1);
                text = text.trimmed();
                if (!text.isEmpty())
                    commentTranslations.insert(m_siteTranslationSources.at(i), text);
            }
        }
    } else {
        const QString outPath =
            QDir(m_siteOverviewOutputDir).filePath(QStringLiteral("overview.txt"));
        if (success > 0 && QFileInfo::exists(outPath)) {
            QFile f(outPath);
            if (f.open(QIODevice::ReadOnly)) {
                overviewTranslation = QString::fromUtf8(f.readAll());
                f.close();
                if (overviewTranslation.startsWith(QChar(0xFEFF)))
                    overviewTranslation.remove(0, 1);
            }
        }
    }

    QDir(m_siteOverviewSourceDir).removeRecursively();
    QDir(m_siteOverviewOutputDir).removeRecursively();

    m_siteOverviewTranslating = false;
    m_siteOverviewModel = -1;

    // 结果落给「发起翻译时」的那本书，而不是当前打开的小说：
    // 否则在 A 书中点「翻译概要」、随后切到 B 书等它跑完，概要会写进 B 书。
    const QString targetPath = siteOverviewPathFor(m_siteOverviewSiteKey, m_siteOverviewWorkId);
    const bool sameWork = (m_siteWork.siteKey == m_siteOverviewSiteKey
                           && m_siteWork.workId == m_siteOverviewWorkId);

    if (!comments) {
        if (!overviewTranslation.trimmed().isEmpty()) {
            if (!targetPath.isEmpty()) {
                QDir().mkpath(QFileInfo(targetPath).absolutePath());
                QFile f(targetPath);
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    f.write(overviewTranslation.toUtf8());
                    f.close();
                }
            }
            if (sameWork) {
                m_siteOverviewTranslated = true;
                m_siteOverviewTranslation = overviewTranslation;
            }
            emit notify(QStringLiteral("概要翻译完成"));
        } else {
            emit notify(QStringLiteral("概要翻译失败"));
        }
    } else if (!commentTranslations.isEmpty()) {
        const QString path = siteCommentsPathFor(m_siteOverviewSiteKey, m_siteOverviewWorkId);
        QHash<QString, QString> merged = commentTranslations;
        if (!path.isEmpty() && QFileInfo::exists(path)) {
            QFile existing(path);
            if (existing.open(QIODevice::ReadOnly)) {
                const QJsonArray entries =
                    QJsonDocument::fromJson(existing.readAll()).array();
                for (const QJsonValue &value : entries) {
                    const QJsonObject entry = value.toObject();
                    const QString source = entry.value(QStringLiteral("source")).toString();
                    const QString translation =
                        entry.value(QStringLiteral("translation")).toString();
                    if (!source.isEmpty() && !translation.isEmpty()
                        && !merged.contains(source)) {
                        merged.insert(source, translation);
                    }
                }
            }
        }
        if (!path.isEmpty() && QDir().mkpath(QFileInfo(path).absolutePath())) {
            QJsonArray entries;
            for (auto it = merged.constBegin(); it != merged.constEnd(); ++it) {
                entries.append(QJsonObject{
                    {QStringLiteral("source"), it.key()},
                    {QStringLiteral("translation"), it.value()},
                });
            }
            QSaveFile file(path);
            const QByteArray bytes =
                QJsonDocument(entries).toJson(QJsonDocument::Indented);
            if (file.open(QIODevice::WriteOnly)
                && file.write(bytes) == bytes.size()
                && file.commit()) {
                if (sameWork)
                    refreshSiteCommentsTranslation();
            }
        }
        emit notify(QStringLiteral("评论翻译完成"));
    } else {
        emit notify(QStringLiteral("评论翻译失败"));
    }

    m_siteOverviewSiteKey.clear();
    m_siteOverviewWorkId.clear();
    m_siteTranslationKind.clear();
    m_siteTranslationSources.clear();
    m_siteTranslationFiles.clear();
    emit siteOverviewTranslatingChanged();
    emit siteOverviewProgressChanged();
    emit siteOverviewTranslatedChanged();
    if (comments)
        emit siteCommentsChanged();
}

void AppController::siteDeleteOverviewTranslation()
{
    const QString path = siteOverviewFilePath();
    if (!path.isEmpty() && QFileInfo::exists(path))
        QFile::remove(path);
    m_siteOverviewTranslated = false;
    m_siteOverviewTranslation.clear();
    emit siteOverviewTranslatedChanged();
    emit notify(QStringLiteral("已删除当前概要翻译"));
}

void AppController::siteDeleteCommentsTranslation()
{
    const QString path = siteCommentsPathFor(m_siteWork.siteKey, m_siteWork.workId);
    if (!path.isEmpty() && QFileInfo::exists(path))
        QFile::remove(path);
    m_siteCommentsTranslated = false;
    m_siteCommentTranslations.clear();
    for (int i = 0; i < m_siteComments.size(); ++i)
        m_siteCommentTranslations << QString();
    emit siteCommentsChanged();
    emit notify(QStringLiteral("已删除当前评论翻译"));
}

void AppController::clearAllOverviewTranslations()
{
    if (m_siteOverviewTranslating) {
        emit notify(QStringLiteral("请等待概要/评论翻译完成后再清除"));
        return;
    }
    QDir directory(siteOverviewDir());
    const bool removed = !directory.exists() || directory.removeRecursively();
    if (!removed) {
        refreshSiteOverviewTranslation();
        emit notify(QStringLiteral("部分概要翻译清除失败，请检查文件权限"));
        return;
    }
    m_siteOverviewTranslated = false;
    m_siteOverviewTranslation.clear();
    emit siteOverviewTranslatedChanged();
    emit notify(QStringLiteral("已清除所有概要翻译"));
}

void AppController::clearAllCommentsTranslations()
{
    if (m_siteOverviewTranslating) {
        emit notify(QStringLiteral("请等待概要/评论翻译完成后再清除"));
        return;
    }
    QDir directory(siteCommentsDir());
    const bool removed = !directory.exists() || directory.removeRecursively();
    refreshSiteCommentsTranslation();
    emit notify(removed ? QStringLiteral("已清除所有评论翻译")
                        : QStringLiteral("部分评论翻译清除失败，请检查文件权限"));
}
