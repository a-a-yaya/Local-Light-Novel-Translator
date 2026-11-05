#include "murasakitranslator.h"
#include "cjdlexporter.h"
#include "txtexporter.h"
#include "novelpaths.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDateTime>
#include <QUrl>
#include <algorithm>

namespace {
// 小说以连续场景而非逐句请求；字符数只是分块目标，实际容量由 tokenizer 决定。
// 每章独立，不注入历史译文，以免把历史当作本次待翻正文。
constexpr int kTargetChunkChars = 2400;
// 思考模式要给「思考 + 译文」留足预算，块变大后同步提高上限（不截断就不会拖慢）。
constexpr int kMaxTokensThinking = 5120;
// 一块最多重发几次（拿不到有效译文/被截断/请求失败都算）
constexpr int kMaxRequestRetries = 3;
constexpr int kMaxChunkAttempts = kMaxRequestRetries + 1;
constexpr int kContextMargin = 128;

QString digest(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool hiragana(QChar ch) { return ch.unicode() >= 0x3041 && ch.unicode() <= 0x3096; }
bool katakana(QChar ch)
{
    return (ch.unicode() >= 0x30A1 && ch.unicode() <= 0x30FA) || ch == QChar(0x30FC);
}
int honorificLength(const QString &text, int offset);

bool insideWord(const GlossaryTerm &term, const QString &text, int start)
{
    const int end = start + term.source.size();
    const QChar before = start > 0 ? text.at(start - 1) : QChar();
    const QChar after = end < text.size() ? text.at(end) : QChar();
    const auto latin = [](QChar c) {
        return (c.unicode() < 128 && c.isLetterOrNumber()) || c == QLatin1Char('_');
    };
    if (std::all_of(term.source.cbegin(), term.source.cend(), latin))
        return latin(before) || latin(after);
    if (!term.type.contains(QStringLiteral("人"))) return false;
    if (std::all_of(term.source.cbegin(), term.source.cend(), katakana))
        return katakana(before) || katakana(after);
    if (term.source.size() > 4
        || !std::all_of(term.source.cbegin(), term.source.cend(), hiragana)) return false;
    // 完整敬称是比左侧假名更强的边界证据：「もしなぎさん」「そしてあこちゃん」
    // 都是独立人名。先判敬称，避免漏计后将正确译文误判为擅自加敬称。
    // 从敬称内部截出的长名已由 termOccurrences 的 insideHonorific 排除。
    if (honorificLength(text, end) > 0) return false;
    // 短平假名人名不能从更长的词中截取；助词后的独立人名仍可命中。
    if (hiragana(before) && !QStringLiteral("はがをにへともので").contains(before)) return true;
    static const QRegularExpression particle(QStringLiteral(
        "^(?:は|が|を|に|へ|と|も|の|で|って|から|まで|より|だけ|しか|こそ|さえ|です|だった|だ[。！？」』、\\s])"));
    return hiragana(after) && honorificLength(text, end) == 0
        && !particle.match(text.mid(end, 12)).hasMatch();
}

int honorificLength(const QString &text, int offset)
{
    // 包括口语拉长的「さーん」「ちゃーん」，不把「なぎさん」误当「なぎさ」。
    static const QRegularExpression suffix(QStringLiteral(
        "^(?:さ[ー〜～]*ん|さ[ー〜～]*ま|ち[ー〜～]*ゃ[ー〜～]*ん|く[ー〜～]*ん|様|君|殿|嬢)"));
    const auto match = suffix.match(text.mid(offset, 16));
    return match.hasMatch() ? match.capturedLength() : 0;
}

bool ambiguousEverydayUse(const GlossaryTerm &term, const QString &text, int offset)
{
    // 这些平假名既可能是人名，也可能是普通副词。这类普通短语不注入人名译名，
    // 避免提示本身诱导模型添加人物（例如「私はさきに…」不是「咲」）。
    static const QStringList phrases = {QStringLiteral("さきに"), QStringLiteral("さきほど"),
        QStringLiteral("さきから"), QStringLiteral("あとで"), QStringLiteral("あとに"),
        QStringLiteral("まえに")};
    if (!term.type.contains(QStringLiteral("人")))
        return false;
    for (const QString &phrase : phrases) {
        if (phrase.startsWith(term.source) && phrase.size() > term.source.size()
            && text.mid(offset, phrase.size()) == phrase)
            return true;
    }
    return false;
}

struct TermOccurrence {
    int start = 0;
    GlossaryTerm term;
    bool ambiguous = false;
};

QList<TermOccurrence> termOccurrences(const GlossaryManager &glossary, const QString &text)
{
    const auto terms = glossary.termsForText(text);
    QList<TermOccurrence> candidates;
    for (const auto &term : terms) {
        for (int from = 0;;) {
            const int start = text.indexOf(term.source, from);
            if (start < 0) break;
            from = start + term.source.size();
            bool insideHonorific = false;
            for (const auto &shorter : terms) {
                if (shorter.source.size() >= term.source.size()
                    || !term.source.startsWith(shorter.source)) continue;
                const int honorific = honorificLength(text, start + shorter.source.size());
                if (honorific > 0 && term.source.size() < shorter.source.size() + honorific) {
                    insideHonorific = true;
                    break;
                }
            }
            if (!insideHonorific && !insideWord(term, text, start))
                candidates.append({start, term, ambiguousEverydayUse(term, text, start)});
        }
    }
    // 同一处全名优先，不要求短名的另一个译名也同时出现；独立短名仍单独约束。
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
        return a.start != b.start ? a.start < b.start : a.term.source.size() > b.term.source.size();
    });
    QList<TermOccurrence> result;
    int end = -1;
    for (const auto &candidate : candidates) {
        if (candidate.start < end) continue;
        result.append(candidate);
        end = candidate.start + candidate.term.source.size();
    }
    return result;
}

// 优先保留完整段落、行和句子；硬切也不截断 UTF-16 字符或已确认的专名。
int sourceCut(const QString &text, int limit, const QList<GlossaryTerm> &terms)
{
    int cut = qMin(limit, int(text.size()));
    if (cut >= text.size()) return text.size();
    const int lower = qMax(1, cut / 2);
    int bestRank = -1;
    int quoteDepth = 0;
    for (int i = 1; i <= limit && i < text.size(); ++i) {
        const QChar ch = text.at(i - 1);
        if (ch == QChar(u'「') || ch == QChar(u'『')) ++quoteDepth;
        if (ch == QChar(u'」') || ch == QChar(u'』')) quoteDepth = qMax(0, quoteDepth - 1);
        if (i < lower || quoteDepth > 0) continue;
        int rank = -1;
        if (ch == QLatin1Char('\n'))
            rank = (i >= 2 && text.at(i - 2) == QLatin1Char('\n')) ? 3 : 2;
        else if (QStringLiteral("。！？!?").contains(ch)) rank = 1;
        else if (QStringLiteral("、，,；;").contains(ch)) rank = 0;
        else if ((ch == QChar(u'」') || ch == QChar(u'』')) && i >= 2
                 && QStringLiteral("。！？!?").contains(text.at(i - 2))) rank = 1;
        if (rank >= bestRank && rank >= 0) { cut = i; bestRank = rank; }
    }
    // 句末后的闭引号属于前一句。
    while (cut < text.size() && cut < limit + 8
           && QStringLiteral("」』）】\"'").contains(text.at(cut))) ++cut;
    for (const auto &term : terms) {
        if (term.source.isEmpty()) continue;
        const int start = text.lastIndexOf(term.source, cut - 1);
        const int end = start + term.source.size();
        const int honorificEnd = start >= 0 ? end + honorificLength(text, end) : -1;
        if (start >= 0 && start < cut && honorificEnd > cut) {
            if (start > 0) cut = start;
            else cut = honorificEnd; // 超长专名作为一整块交给预算检查。
        }
    }
    if (cut < text.size() && text.at(cut).isLowSurrogate()
        && cut > 0 && text.at(cut - 1).isHighSurrogate()) --cut;
    return qMax(1, cut);
}

QStringList sourceChunks(const QString &text, const QList<GlossaryTerm> &terms)
{
    QStringList result;
    int from = 0;
    while (from < text.size()) {
        const QString tail = text.mid(from);
        // 避免章末只剩孤立的一两句话：把不足 600 字的尾部与上一块平衡。
        const int target = tail.size() > kTargetChunkChars
                && tail.size() < kTargetChunkChars + 600 ? int(tail.size() / 2) : kTargetChunkChars;
        const int cut = sourceCut(tail, target, terms);
        result.append(tail.left(cut));
        from += cut;
    }
    return result;
}

// 提取原文中的片假名专名片段（人名/外来语等），作为锚点。
QStringList katakanaAnchors(const QString &text)
{
    QStringList anchors;
    QString current;
    auto flush = [&]() {
        int letters = 0;
        for (const QChar ch : current) {
            const ushort u = ch.unicode();
            if (u >= 0x30A0 && u <= 0x30FF)
                ++letters;
        }
        if (letters >= 2 && !anchors.contains(current))
            anchors << current;
        current.clear();
    };
    for (const QChar ch : text) {
        const ushort u = ch.unicode();
        const bool katakana = (u >= 0x30A0 && u <= 0x30FF) || u == 0x30FC || u == 0x30FB;
        if (katakana)
            current.append(ch);
        else
            flush();
    }
    flush();
    return anchors;
}
} // namespace

// 首次请求严格使用模型卡中的 Novel Mode；纠错约束仅在失败重试时附加。
QString MurasakiTranslator::defaultPrompt()
{
    return QStringLiteral(
        "你是一位精通二次元文化的资深轻小说翻译家。\n"
        "请将日文文本翻译成流畅、优美的中文。\n\n"
        "**核心要求：**\n"
        "1. **深度思考：** 在翻译前，先在 <think> 标签中分析文风、补全主语并梳理逻辑。\n"
        "2. **信达雅：** 译文需符合中文轻小说阅读习惯，还原原作的沉浸感与文学性。");
}

MurasakiTranslator::MurasakiTranslator(QObject *parent)
    : QObject(parent)
    , m_serverUrl(QStringLiteral("http://127.0.0.1:8081"))
    , m_modelAlias(QStringLiteral("murasaki"))
    , m_systemPrompt(defaultPrompt())
{
    // 单块翻译通常几十秒；给一个宽松的看门狗，避免模型进程假死时任务永久卡住。
    m_http.setTransferTimeout(300000);
    connect(&m_http, &HttpClient::requestFinished,
            this, &MurasakiTranslator::onRequestFinished);
    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, &MurasakiTranslator::translateNextChunk);
}

MurasakiTranslator::~MurasakiTranslator()
{
    m_aborted = true;
    m_running = false;
    ++m_runId;
    m_retryTimer.stop();
    if (m_preflightReply) m_preflightReply->abort();
    m_http.abortAll();
}

void MurasakiTranslator::setServerUrl(const QString &url)
{
    m_serverUrl = url;
    while (m_serverUrl.endsWith(QLatin1Char('/')))
        m_serverUrl.chop(1);
}

void MurasakiTranslator::setModelAlias(const QString &alias)
{
    if (!alias.trimmed().isEmpty())
        m_modelAlias = alias;
}

void MurasakiTranslator::setModelIdentity(const QString &modelPath)
{
    const QFileInfo model(modelPath);
    m_modelIdentity = model.absoluteFilePath() + QLatin1Char('|') + QString::number(model.size())
        + QLatin1Char('|') + QString::number(model.lastModified().toMSecsSinceEpoch());
}

void MurasakiTranslator::setSystemPrompt(const QString &prompt)
{
    if (!prompt.trimmed().isEmpty())
        m_systemPrompt = prompt;
}

void MurasakiTranslator::setCjdlDir(const QString &dir)
{
    m_cjdlDir = dir;
}

void MurasakiTranslator::setDownloadRoot(const QString &dir)
{
    m_downloadRoot = dir;
}

void MurasakiTranslator::setGenerateBilingual(bool enabled)
{
    m_generateBilingual = enabled;
}

void MurasakiTranslator::translateFiles(const QVector<MurasakiFileJob> &jobs)
{
    if (m_running) abort();
    ++m_runId;
    m_running = true;
    m_retryTimer.stop();
    m_requestCount = m_retryCount = m_restoredChunks = 0;
    m_contextTokens = 8192;
    m_tokenizerAvailable = true;
    m_serverIdentity.clear();
    m_elapsed.start();
    m_filePaths.clear();
    m_outputDirs.clear();
    for (const MurasakiFileJob &job : jobs) {
        m_filePaths << job.path;
        m_outputDirs << job.outputDir;
    }

    m_success = 0;
    m_failed = 0;
    m_aborted = false;
    m_translated.fill(QStringList(), m_filePaths.size());
    m_sourceChunks.fill(QStringList(), m_filePaths.size());
    m_fileFailed.fill(false, m_filePaths.size());
    m_fileStarted.fill(false, m_filePaths.size());
    m_fileTexts.fill(QString(), m_filePaths.size());
    m_fileHeaders.fill(QString(), m_filePaths.size());
    m_fileQualityWarnings.fill(false, m_filePaths.size());
    m_fileGlossaryCompatibility.fill(false, m_filePaths.size());
    m_currentTask = 0;
    m_tasks.clear();
    for (const auto &path : m_filePaths) emit fileErrorChanged(path, QString());

    loadGlossaries();
    buildTaskQueue();
    for (int i = 0; i < m_filePaths.size(); ++i)
        emit fileStatusChanged(m_filePaths[i], m_fileFailed[i]
                               ? QStringLiteral("FAILED") : QStringLiteral("WAITING"));
    m_currentTask = 0;
    emit progressChanged(0, m_tasks.size(), QStringLiteral("开始 Murasaki 翻译…"));
    probeServer();
}

void MurasakiTranslator::abort()
{
    if (m_aborted)
        return;
    m_aborted = true;
    m_running = false;
    ++m_runId;
    m_retryTimer.stop();
    if (m_preflightReply) m_preflightReply->abort();
    m_http.abortAll();
    emit progressChanged(m_currentTask, m_tasks.size(), QStringLiteral("Murasaki 翻译已中止"));
}

void MurasakiTranslator::buildTaskQueue()
{
    m_tasks.clear();

    for (int i = 0; i < m_filePaths.size(); ++i) {
        QFile f(m_filePaths[i]);
        if (!f.open(QIODevice::ReadOnly)) {
            m_fileFailed[i] = true;
            reportFileError(i, QStringLiteral("无法读取原文：%1").arg(f.errorString()));
            continue;
        }
        QByteArray raw = f.readAll();
        f.close();

        if (raw.startsWith("\xEF\xBB\xBF"))
            raw.remove(0, 3);
        QString header;
        QString text;
        TxtExporter::splitChapterDocument(QString::fromUtf8(raw), &header, &text);
        m_fileHeaders[i] = header;
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        text = text.trimmed();
        m_fileTexts[i] = text;
        const QString glossaryError = m_glossaryErrors.value(novelRootForSourceFile(m_filePaths[i]));
        if (text.isEmpty() || !glossaryError.isEmpty()) {
            m_fileFailed[i] = true;
            const QString reason = text.isEmpty() ? QStringLiteral("原文为空：%1").arg(m_filePaths[i])
                                                  : glossaryError;
            reportFileError(i, reason);
            emit progressChanged(0, 0, reason);
            continue;
        }
        const auto terms = relevantTermsForChunk(i, text);
        for (const auto &chunk : sourceChunks(text, terms)) m_tasks.append({i, chunk});
    }
    // 读不到 / 空正文 / 配置冲突的章节没有任务，也必须计入失败。
    for (const bool failed : m_fileFailed) if (failed) ++m_failed;
}

void MurasakiTranslator::resolveSourceIdentity(const QString &filePath,
                                               QString &workId, QString &chapterId) const
{
    workId.clear();
    chapterId.clear();
    const QFileInfo fi(filePath);
    QDir dir = fi.absoluteDir();
    if (dir.dirName() == QStringLiteral("chapters")) {
        dir.cdUp();
        workId = dir.dirName();
        chapterId = fi.completeBaseName();
    } else {
        chapterId = fi.absoluteFilePath();
    }
}

void MurasakiTranslator::loadGlossaries()
{
    m_glossaries.clear();
    m_glossaryErrors.clear();

    for (const QString &path : m_filePaths) {
        const QString root = novelRootForSourceFile(path);
        if (root.isEmpty() || m_glossaries.contains(root))
            continue;
        const QString workId = QDir(root).dirName();
        QString downloadRoot = m_downloadRoot;
        if (downloadRoot.isEmpty()) {
            // 独立调用时也识别 DOWNLOAD/<site>/<work>/chapters；兼容旧版平铺目录。
            QDir parent(root);
            parent.cdUp();
            if (parent.dirName().compare(QStringLiteral("DOWNLOAD"), Qt::CaseInsensitive) == 0)
                downloadRoot = parent.absolutePath();
            else if (parent.cdUp()
                     && parent.dirName().compare(QStringLiteral("DOWNLOAD"), Qt::CaseInsensitive) == 0)
                downloadRoot = parent.absolutePath();
        }
        const QString novelKey = NovelPaths::relativeKey(downloadRoot, root);

        // 翻译时只加载「当前启用」的那一份术语表（人工 / Qwen 二选一）。
        GlossaryManager gm(root, GlossaryManager::fileNameForKind(
                                     GlossaryManager::activeKindFor(novelKey, workId)));
        const bool loaded = gm.load();
        if (!loaded && QFileInfo::exists(gm.glossaryPath()))
            m_glossaryErrors.insert(root, QStringLiteral("无法读取启用的专名表，请修复后重试：%1")
                                            .arg(gm.glossaryPath()));
        QHash<QString, QString> targets;
        for (const auto &term : gm.terms()) {
            if (!term.enabled) continue;
            if (term.target.trimmed().isEmpty()
                || (targets.contains(term.source) && targets.value(term.source) != term.target)) {
                m_glossaryErrors.insert(root,
                    QStringLiteral("专名表存在空译名或同原文的冲突译名，请先修正：%1（%2）")
                        .arg(term.source, gm.glossaryPath()));
                break;
            }
            targets.insert(term.source, term.target);
        }
        m_glossaries.insert(root, gm);
    }
}

QString MurasakiTranslator::novelRootForSourceFile(const QString &filePath) const
{
    QDir dir = QFileInfo(filePath).absoluteDir();
    if (dir.dirName() != QStringLiteral("chapters")) return {};
    dir.cdUp();
    return dir.absolutePath();
}

QString MurasakiTranslator::glossaryPromptForChunk(int fileIndex, const QString &text) const
{
    if (fileIndex < 0 || fileIndex >= m_filePaths.size())
        return QString();
    // 术语表块：表头 + 每行「原文: 译名」。
    // 刻意不复用 GlossaryManager::buildPromptText()（Sakura / Qwen 用的那份是
    // 「【固定术语】+ 原文 = 译名」），三套流程互不影响。
    const QList<GlossaryTerm> relevant = relevantTermsForChunk(fileIndex, text);
    if (relevant.isEmpty()) return {};

    QStringList lines;
    // 官方标题优先；本章出现 thinking-only 才启用本地 GGUF 的标题兼容。
    lines << (m_fileGlossaryCompatibility.value(fileIndex)
                  ? QStringLiteral("【专名对照】") : QStringLiteral("【术语表】"));
    for (const GlossaryTerm &term : relevant)
        lines << QStringLiteral("%1: %2").arg(term.source, term.target);
    return lines.join(QLatin1Char('\n'));
}

QList<GlossaryTerm> MurasakiTranslator::relevantTermsForChunk(int fileIndex, const QString &text) const
{
    if (fileIndex < 0 || fileIndex >= m_filePaths.size())
        return {};
    const auto it = m_glossaries.constFind(novelRootForSourceFile(m_filePaths[fileIndex]));
    if (it == m_glossaries.constEnd())
        return {};
    QList<GlossaryTerm> relevant;
    for (const auto &occurrence : termOccurrences(it.value(), text)) {
        if (occurrence.ambiguous) continue;
        const auto exists = std::any_of(relevant.cbegin(), relevant.cend(), [&](const auto &term) {
            return term.source == occurrence.term.source;
        });
        if (!exists) relevant.append(occurrence.term);
    }
    return relevant;
}

QString MurasakiTranslator::validateChunk(const ChunkTask &task, const QString &translated,
                                          QStringList &missingTerms, QStringList &honorificIssues,
                                          bool &japaneseResidue, QStringList *issueKeys) const
{
    missingTerms.clear();
    honorificIssues.clear();
    japaneseResidue = false;

    // 术语覆盖率：本 chunk 原文中出现的启用术语，其指定译名必须出现在译文里。
    const QList<GlossaryTerm> relevant = relevantTermsForChunk(task.fileIndex, task.text);
    for (const GlossaryTerm &term : relevant) {
        if (!translated.contains(term.target))
            missingTerms << QStringLiteral("%1 → %2").arg(term.source, term.target);
    }

    // 敬称检查：原文该术语后面没有敬称，译文却给固定译名加了敬称 / 性别称谓
    //（例如把「ノエル」写成「诺埃尔小姐」「诺埃尔先生」）→ 视为没严格执行术语表。
    static const QStringList zhHonorifics = { QStringLiteral("小姐"), QStringLiteral("先生"),
                                              QStringLiteral("女士"), QStringLiteral("大人"),
                                              QStringLiteral("少爷"), QStringLiteral("酱"),
                                              QStringLiteral("桑"), QStringLiteral("様"),
                                              QStringLiteral("君") };
    QHash<QString, int> allowedHonorifics;
    const auto glossary = m_glossaries.constFind(novelRootForSourceFile(m_filePaths.at(task.fileIndex)));
    if (glossary != m_glossaries.cend()) {
        for (const auto &occurrence : termOccurrences(glossary.value(), task.text)) {
            if (!occurrence.ambiguous
                && honorificLength(task.text, occurrence.start + occurrence.term.source.size()) > 0)
                ++allowedHonorifics[occurrence.term.source];
        }
    }
    for (const GlossaryTerm &term : relevant) {
        if (term.source.isEmpty() || term.target.isEmpty())
            continue;
        int translatedHonorifics = 0;
        for (int t = 0;;) {
            const int idx = translated.indexOf(term.target, t);
            if (idx < 0)
                break;
            const QString tail = translated.mid(idx + term.target.size(), 2);
            for (const QString &h : zhHonorifics) {
                if (tail.startsWith(h)) {
                    if (++translatedHonorifics > allowedHonorifics.value(term.source))
                        honorificIssues << QStringLiteral("%1（应写作「%2」，不要添加敬称）")
                                               .arg(term.target + h, term.target);
                    break;
                }
            }
            t = idx + term.target.size();
        }
    }

    // 日文残留：译文仍以假名为主时判为未翻译/漏翻。
    int kanaCount = 0;
    for (const QChar ch : translated) {
        const ushort u = ch.unicode();
        if ((u >= 0x3040 && u <= 0x309F) || (u >= 0x30A0 && u <= 0x30FF) || u == 0x30FC)
            ++kanaCount;
    }
    if (kanaCount >= 4 && kanaCount * 2 > translated.size())
        japaneseResidue = true;

    // 锚点一致：原文中出现的片假名专名（人名/外来语等），不应原样残留在译文里。
    // 例外：固定术语的译名本身就是假名（例如保留片假名的专名），此时不算残留。
    QStringList glossaryTargets;
    for (const GlossaryTerm &term : relevant)
        glossaryTargets << term.target;
    QStringList untranslatedAnchors;
    const QStringList anchors = katakanaAnchors(task.text);
    for (const QString &anchor : anchors) {
        if (!translated.contains(anchor))
            continue;
        bool explainedByTarget = false;
        for (const QString &target : glossaryTargets) {
            if (target.contains(anchor)) {
                explainedByTarget = true;
                break;
            }
        }
        if (!explainedByTarget)
            untranslatedAnchors << anchor;
    }

    QStringList problems;
    if (issueKeys) issueKeys->clear();
    const auto addProblem = [&](const QString &key, const QString &message) {
        problems << message;
        if (issueKeys) issueKeys->append(key);
    };
    if (!missingTerms.isEmpty())
        addProblem(QStringLiteral("missing_term"), QStringLiteral("以下术语必须使用指定译名：%1")
                        .arg(missingTerms.join(QStringLiteral("、"))));
    if (!honorificIssues.isEmpty())
        addProblem(QStringLiteral("honorific"), QStringLiteral("固定译名不要添加敬称或性别称谓（原文没有）：%1")
                        .arg(honorificIssues.join(QStringLiteral("、"))));
    if (japaneseResidue)
        addProblem(QStringLiteral("kana"), QStringLiteral("译文仍含大量日文假名，请输出纯简体中文"));
    if (!untranslatedAnchors.isEmpty())
        addProblem(QStringLiteral("untranslated_name"), QStringLiteral("以下专名仍未转换（残留片假名）：%1")
                        .arg(untranslatedAnchors.join(QStringLiteral("、"))));

    const auto contentLength = [](const QString &text) {
        int count = 0;
        for (const QChar ch : text) if (ch.isLetterOrNumber()) ++count;
        return count;
    };
    const int sourceLength = contentLength(task.text);
    // 只拦截极端缩写，避免把正常日中长度差或代词省略误判为漏译。
    if (sourceLength >= 200 && contentLength(translated) * 5 < sourceLength)
        addProblem(QStringLiteral("too_short"), QStringLiteral("译文异常短，疑似概括或大段漏译；请逐段完整翻译，不要概括"));
    if (sourceLength >= 100 && contentLength(translated) > sourceLength * 4)
        addProblem(QStringLiteral("too_long"), QStringLiteral("译文异常膨胀，疑似重复或增写；仅翻译本次正文"));
    QHash<QString, int> sourceParagraphs, targetParagraphs;
    int sourceRepetitions = 1;
    for (const auto &line : task.text.split(QLatin1Char('\n'))) {
        const QString p = line.trimmed();
        if (p.size() >= 30) sourceRepetitions = qMax(sourceRepetitions, ++sourceParagraphs[p]);
    }
    for (const auto &line : translated.split(QLatin1Char('\n'))) {
        const QString p = line.trimmed();
        if (p.size() >= 30 && ++targetParagraphs[p] > qMax(2, sourceRepetitions + 1)) {
            addProblem(QStringLiteral("repetition"), QStringLiteral("译文出现原文没有的长段复读，请保持原文顺序且每段只翻译一次"));
            break;
        }
    }

    return problems.join(QStringLiteral("；"));
}

void MurasakiTranslator::probeServer()
{
    if (m_tasks.isEmpty()) { translateNextChunk(); return; }
    QNetworkRequest request(QUrl(m_serverUrl + QStringLiteral("/props")));
    request.setTransferTimeout(3000);
    auto *reply = m_preflight.get(request);
    m_preflightReply = reply;
    const auto run = m_runId;
    connect(reply, &QNetworkReply::finished, this, [this, reply, run]() {
        const bool ok = reply->error() == QNetworkReply::NoError;
        const auto props = ok ? QJsonDocument::fromJson(reply->readAll()).object() : QJsonObject();
        const bool unavailable = reply->error() == QNetworkReply::ConnectionRefusedError
            || reply->error() == QNetworkReply::HostNotFoundError
            || reply->error() == QNetworkReply::TimeoutError
            || reply->error() == QNetworkReply::OperationCanceledError;
        const QString networkError = reply->errorString();
        reply->deleteLater();
        if (run != m_runId || m_aborted) return;
        if (unavailable) {
            for (int i = 0; i < m_filePaths.size(); ++i) {
                if (m_fileFailed[i]) continue;
                m_fileFailed[i] = true;
                ++m_failed;
                reportFileError(i, QStringLiteral("Murasaki 服务无法连接：%1").arg(networkError));
                emit fileStatusChanged(m_filePaths.at(i), QStringLiteral("FAILED"));
            }
            m_currentTask = m_tasks.size();
            emit progressChanged(m_currentTask, m_tasks.size(),
                QStringLiteral("Murasaki 服务无法连接，请重新启动模型后重试；保留旧译文和断点：%1").arg(networkError));
            translateNextChunk();
            return;
        }
        if (ok) {
            const auto settings = props.value(QStringLiteral("default_generation_settings")).toObject();
            const int context = settings.value(QStringLiteral("n_ctx")).toInt();
            if (context >= 1024) m_contextTokens = context;
            const QString path = props.value(QStringLiteral("model_path")).toString();
            if (!path.isEmpty()) {
                const QFileInfo model(path);
                m_serverIdentity = path + QLatin1Char('|') + QString::number(model.size())
                    + QLatin1Char('|') + QString::number(model.lastModified().toMSecsSinceEpoch())
                    + QLatin1Char('|') + props.value(QStringLiteral("build_info")).toString()
                    + QLatin1Char('|') + digest(props.value(QStringLiteral("chat_template")).toString().toUtf8());
            }
        }
        translateNextChunk();
    });
}

void MurasakiTranslator::translateNextChunk()
{
    if (m_aborted || !m_running)
        return;
    if (m_currentTask >= m_tasks.size()) {
        m_running = false;
        emit progressChanged(m_tasks.size(), m_tasks.size(),
            QStringLiteral("Murasaki 翻译完成：%1 次推理，%2 次重试，恢复 %3 块，用时 %4 秒")
                .arg(m_requestCount).arg(m_retryCount).arg(m_restoredChunks)
                .arg(m_elapsed.elapsed() / 1000.0, 0, 'f', 1));
        emit translationFinished(m_success, m_failed);
        return;
    }

    const int fileIndex = m_tasks.at(m_currentTask).fileIndex;
    if (!m_fileStarted[fileIndex]) {
        m_fileStarted[fileIndex] = true;
        emit fileStatusChanged(m_filePaths[fileIndex], QStringLiteral("TRANSLATING"));
        restoreCheckpoint(fileIndex);
        if (m_aborted || !m_running) return;
        if (m_currentTask >= m_tasks.size() || m_tasks.at(m_currentTask).fileIndex != fileIndex) {
            finishCurrentFile(fileIndex);
            retryCurrentChunk();
            return;
        }
    }

    emit progressChanged(m_currentTask, m_tasks.size(),
                         QStringLiteral("正在翻译: %1 (%2/%3)")
                             .arg(QFileInfo(m_filePaths[fileIndex]).fileName())
                             .arg(m_currentTask + 1).arg(m_tasks.size()));

    if (!m_aborted && m_running) checkTokenBudget(requestBody());
}

QJsonObject MurasakiTranslator::requestBody() const
{
    const auto &task = m_tasks.at(m_currentTask);

    QJsonArray messages;
    const QString glossaryPrompt = glossaryPromptForChunk(task.fileIndex, task.text);
    QString systemContent = m_systemPrompt;
    if (!glossaryPrompt.isEmpty())
        systemContent += QStringLiteral("\n\n") + glossaryPrompt;
    // 纠错属于翻译指令，不混入待翻正文，也不携带上次的失败答案。
    if (!task.feedback.isEmpty())
        systemContent += QStringLiteral("\n\n【本次校正要求】\n%1").arg(task.feedback.left(800));

    QJsonObject systemMsg;
    systemMsg.insert(QStringLiteral("role"), QStringLiteral("system"));
    systemMsg.insert(QStringLiteral("content"), systemContent);
    messages.append(systemMsg);

    QJsonObject userMsg;
    userMsg.insert(QStringLiteral("role"), QStringLiteral("user"));
    // 单轮翻译：当前连贯正文 + 当前命中的专名；不重放已译段落。
    userMsg.insert(QStringLiteral("content"), QStringLiteral("请翻译：\n%1").arg(task.text));
    messages.append(userMsg);

    QJsonObject body;
    body.insert(QStringLiteral("model"), m_modelAlias);
    body.insert(QStringLiteral("messages"), messages);
    // Murasaki 的 GGUF 模板与 llama-server 自动推理解析不完全匹配：
    // 服务端可能把整个回答（含译文）都判成 reasoning_content。
    // 保留原始 <think> 标签，在本翻译器内分离思考；这不会关闭模型思考。
    body.insert(QStringLiteral("reasoning_format"), QStringLiteral("none"));
    body.insert(QStringLiteral("temperature"), 0.30);
    body.insert(QStringLiteral("top_p"), 0.90);
    body.insert(QStringLiteral("top_k"), 40);
    body.insert(QStringLiteral("min_p"), 0.05);
    // 官方推荐的温和复读惩罚，避免过强惩罚影响专名与长段翻译。
    body.insert(QStringLiteral("repeat_penalty"), 1.05);
    body.insert(QStringLiteral("repeat_last_n"), 256);
    // 固定保留模型思考，不传关闭思考的模板参数；预算同时覆盖思考与译文。
    body.insert(QStringLiteral("max_tokens"), kMaxTokensThinking);

    body.insert(QStringLiteral("cache_prompt"), true);
    return body;
}

void MurasakiTranslator::checkTokenBudget(const QJsonObject &body)
{
    if (!m_tokenizerAvailable) { sendChunk(body, 0, false); return; }
    QNetworkRequest request(QUrl(m_serverUrl + QStringLiteral("/apply-template")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(3000);
    QJsonObject chat = body;
    chat.insert(QStringLiteral("add_generation_prompt"), true);
    auto *reply = m_preflight.post(request, QJsonDocument(chat).toJson(QJsonDocument::Compact));
    m_preflightReply = reply;
    const auto run = m_runId;
    connect(reply, &QNetworkReply::finished, this, [this, reply, run, body]() {
        const bool ok = reply->error() == QNetworkReply::NoError;
        const auto response = ok ? QJsonDocument::fromJson(reply->readAll()).object() : QJsonObject();
        reply->deleteLater();
        if (run != m_runId || m_aborted) return;
        const QString prompt = response.value(QStringLiteral("prompt")).toString();
        if (!ok || prompt.isEmpty()) {
            m_tokenizerAvailable = false;
            emit progressChanged(m_currentTask, m_tasks.size(),
                QStringLiteral("服务端不支持提示词分词，改用保守估算检查上下文预算"));
            sendChunk(body, 0, false);
            return;
        }
        QNetworkRequest request(QUrl(m_serverUrl + QStringLiteral("/tokenize")));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setTransferTimeout(3000);
        const QJsonObject input{{QStringLiteral("content"), prompt},
            {QStringLiteral("add_special"), true}, {QStringLiteral("parse_special"), true}};
        auto *tokensReply = m_preflight.post(request, QJsonDocument(input).toJson(QJsonDocument::Compact));
        m_preflightReply = tokensReply;
        connect(tokensReply, &QNetworkReply::finished, this, [this, tokensReply, run, body]() {
            const bool networkOk = tokensReply->error() == QNetworkReply::NoError;
            const auto response = networkOk ? QJsonDocument::fromJson(tokensReply->readAll()).object() : QJsonObject();
            const int tokens = response.value(QStringLiteral("tokens")).toArray().size();
            const bool ok = networkOk && tokens > 0;
            tokensReply->deleteLater();
            if (run != m_runId || m_aborted) return;
            if (!ok) {
                m_tokenizerAvailable = false;
                emit progressChanged(m_currentTask, m_tasks.size(),
                    QStringLiteral("服务端分词不可用，改用保守估算检查上下文预算"));
            }
            sendChunk(body, tokens, ok);
        });
    });
}

void MurasakiTranslator::sendChunk(const QJsonObject &body, int promptTokens, bool measured)
{
    if (m_aborted || !m_running) return;
    if (!measured) {
        // 老服务端没有分词接口时显式使用保守估算，不把估算宣称为精确 token。
        int bytes = 0;
        for (const auto &message : body.value(QStringLiteral("messages")).toArray())
            bytes += message.toObject().value(QStringLiteral("content")).toString().toUtf8().size();
        promptTokens = (bytes + 1) / 2 + 64;
    }
    const int outputTokens = body.value(QStringLiteral("max_tokens")).toInt();
    if (outputTokens + kContextMargin >= m_contextTokens) {
        failCurrentChunk(QStringLiteral("模型上下文 %1 小于当前模式的输出预留 %2，请增加上下文；未改变思考模式")
            .arg(m_contextTokens).arg(outputTokens));
        return;
    }
    if (promptTokens + outputTokens + kContextMargin > m_contextTokens) {
        const QString reason = QStringLiteral("%1提示占用 %2 token，预留输出 %3，上下文 %4")
            .arg(measured ? QStringLiteral("实测") : QStringLiteral("估算"))
            .arg(promptTokens).arg(outputTokens).arg(m_contextTokens);
        if (splitTruncatedTask(m_currentTask, reason, 128)) retryCurrentChunk();
        else failCurrentChunk(QStringLiteral("上下文预算不足，请增加模型上下文或精简启用表；") + reason);
        return;
    }
    ++m_requestCount;
    if (m_tasks.at(m_currentTask).attempts > 0) ++m_retryCount;
    ++m_tasks[m_currentTask].attempts;
    const QVariantMap context{{QStringLiteral("run"), QVariant::fromValue(m_runId)},
                              {QStringLiteral("task"), m_currentTask}};
    m_http.postJson(m_serverUrl + QStringLiteral("/v1/chat/completions"),
                   QJsonDocument(body).toJson(QJsonDocument::Compact), context);
}

void MurasakiTranslator::retryCurrentChunk(int delayMs)
{
    if (!m_aborted && m_running) m_retryTimer.start(delayMs);
}

bool MurasakiTranslator::splitTruncatedTask(int taskIndex, const QString &reason, int minimum)
{
    if (taskIndex < 0 || taskIndex >= m_tasks.size()) return false;
    const ChunkTask task = m_tasks.at(taskIndex);
    // 只有明确的输出 / 上下文超限才缩块；不改变思考模式，也不把半截译文写入文件。
    if (task.text.size() <= minimum * 2) return false;
    const int cut = sourceCut(task.text, task.text.size() / 2,
                              relevantTermsForChunk(task.fileIndex, task.text));
    const QString left = task.text.left(cut);
    const QString right = task.text.mid(cut);
    if (left.trimmed().isEmpty() || right.trimmed().isEmpty()) return false;
    m_tasks[taskIndex].text = left;
    m_tasks.insert(taskIndex + 1, {task.fileIndex, right});
    m_tasks[taskIndex].attempts = 0;
    m_tasks[taskIndex].qualityRetries = 0;
    m_tasks[taskIndex].correctedIssues.clear();
    m_tasks[taskIndex].feedback.clear();
    emit progressChanged(m_currentTask, m_tasks.size(),
                         QStringLiteral("%1，按正文边界缩块后继续翻译: %2")
                             .arg(reason.isEmpty() ? QStringLiteral("输出 / 上下文超限") : reason,
                                  QFileInfo(m_filePaths[task.fileIndex]).fileName()));
    return true;
}

QString MurasakiTranslator::checkpointPath(int fileIndex) const
{
    if (m_outputDirs.value(fileIndex).isEmpty()) return {};
    return QDir(m_outputDirs.at(fileIndex)).filePath(QStringLiteral(".murasaki-progress/"))
        + QFileInfo(m_filePaths.at(fileIndex)).fileName() + QStringLiteral(".json");
}

QString MurasakiTranslator::failurePath(int fileIndex) const
{
    const QString checkpoint = checkpointPath(fileIndex);
    return checkpoint.isEmpty() ? QString() : checkpoint.chopped(5) + QStringLiteral(".error.json");
}

void MurasakiTranslator::reportFileError(int fileIndex, const QString &reason, bool accepted)
{
    if (fileIndex < 0 || fileIndex >= m_filePaths.size()) return;
    if (accepted) m_fileQualityWarnings[fileIndex] = true;
    else emit fileErrorChanged(m_filePaths.at(fileIndex), reason);
    const QString checkpoint = checkpointPath(fileIndex);
    const QString path = accepted
        ? (checkpoint.isEmpty() ? QString() : checkpoint.chopped(5) + QStringLiteral(".warning.json"))
        : failurePath(fileIndex);
    if (path.isEmpty()) return;
    QJsonObject failure{{QStringLiteral("version"), 1},
        {QStringLiteral("time_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("source_file"), QFileInfo(m_filePaths.at(fileIndex)).absoluteFilePath()},
        {QStringLiteral("reason"), reason}, {QStringLiteral("model_alias"), m_modelAlias},
        {QStringLiteral("thinking"), true}};
    if (accepted) failure.insert(QStringLiteral("accepted_after_quality_retries"), true);
    if (m_currentTask < m_tasks.size() && m_tasks.at(m_currentTask).fileIndex == fileIndex) {
        int offset = 0;
        for (const auto &source : m_sourceChunks.at(fileIndex)) offset += source.size();
        failure.insert(QStringLiteral("source_offset"), offset);
        failure.insert(QStringLiteral("chunk_chars"), m_tasks.at(m_currentTask).text.size());
        failure.insert(QStringLiteral("attempts"), m_tasks.at(m_currentTask).attempts);
    }
    // 只记录定位信息，不把未通过校验的译文、思考过程或整章正文写进诊断文件。
    const auto bytes = QJsonDocument(failure).toJson(QJsonDocument::Indented);
    QSaveFile file(path);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !file.open(QIODevice::WriteOnly)
        || file.write(bytes) != bytes.size() || !file.commit())
        qWarning() << "[Murasaki] 无法保存失败原因:" << path << file.errorString();
}

QString MurasakiTranslator::checkpointFingerprint(int fileIndex) const
{
    // 无法识别服务端模型时不恢复断点，防止同一 alias 已切换模型。
    if (m_modelIdentity.isEmpty() && m_serverIdentity.isEmpty()) return {};
    QJsonArray terms;
    const auto glossary = m_glossaries.value(novelRootForSourceFile(m_filePaths.at(fileIndex)));
    for (const auto &term : glossary.terms()) {
        if (term.enabled) terms.append(QJsonObject{{QStringLiteral("source"), term.source},
            {QStringLiteral("target"), term.target}, {QStringLiteral("type"), term.type},
            {QStringLiteral("note"), term.note}});
    }
    const QJsonObject identity{{QStringLiteral("revision"), QStringLiteral("murasaki-novel-pipeline-2")},
        {QStringLiteral("sourcePath"), QFileInfo(m_filePaths.at(fileIndex)).absoluteFilePath()},
        {QStringLiteral("source"), m_fileTexts.at(fileIndex)},
        {QStringLiteral("model"), m_modelIdentity}, {QStringLiteral("serverModel"), m_serverIdentity},
        {QStringLiteral("url"), m_serverUrl}, {QStringLiteral("alias"), m_modelAlias},
        {QStringLiteral("thinking"), true}, {QStringLiteral("prompt"), m_systemPrompt},
        {QStringLiteral("context"), m_contextTokens},
        {QStringLiteral("generation"), QJsonObject{{QStringLiteral("temperature"), 0.30},
            {QStringLiteral("top_p"), 0.90}, {QStringLiteral("top_k"), 40},
            {QStringLiteral("min_p"), 0.05}, {QStringLiteral("repeat_penalty"), 1.05},
            {QStringLiteral("repeat_last_n"), 256},
            {QStringLiteral("max_tokens"), kMaxTokensThinking}}},
        {QStringLiteral("glossaryKind"), glossary.fileName()}, {QStringLiteral("terms"), terms}};
    return digest(QJsonDocument(identity).toJson(QJsonDocument::Compact));
}

void MurasakiTranslator::saveCheckpoint(int fileIndex)
{
    const QString path = checkpointPath(fileIndex);
    const QString fingerprint = checkpointFingerprint(fileIndex);
    if (path.isEmpty() || fingerprint.isEmpty()) return;
    QJsonArray chunks;
    for (int i = 0; i < m_sourceChunks.at(fileIndex).size(); ++i) {
        const QString source = m_sourceChunks.at(fileIndex).at(i);
        const QString translated = m_translated.at(fileIndex).at(i);
        chunks.append(QJsonObject{{QStringLiteral("source"), source},
            {QStringLiteral("translated"), translated},
            {QStringLiteral("digest"), digest(source.toUtf8() + '\0' + translated.toUtf8())}});
    }
    const QByteArray bytes = QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
        {QStringLiteral("fingerprint"), fingerprint}, {QStringLiteral("chunks"), chunks},
        {QStringLiteral("glossaryCompatibility"), m_fileGlossaryCompatibility.value(fileIndex)}})
        .toJson(QJsonDocument::Compact);
    QSaveFile file(path);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !file.open(QIODevice::WriteOnly)
        || file.write(bytes) != bytes.size() || !file.commit()) {
        emit progressChanged(m_currentTask, m_tasks.size(),
            QStringLiteral("断点保存失败，本轮仍继续翻译：%1").arg(file.errorString()));
    }
}

void MurasakiTranslator::restoreCheckpoint(int fileIndex)
{
    QFile file(checkpointPath(fileIndex));
    if (!file.exists()) return;
    const QString fingerprint = checkpointFingerprint(fileIndex);
    if (fingerprint.isEmpty() || !file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024) return;
    const auto saved = QJsonDocument::fromJson(file.readAll()).object();
    if (saved.value(QStringLiteral("version")).toInt() != 1
        || saved.value(QStringLiteral("fingerprint")).toString() != fingerprint) {
        emit progressChanged(m_currentTask, m_tasks.size(),
            QStringLiteral("原文、模型或专名配置已变更，重新翻译：%1")
                .arg(QFileInfo(m_filePaths.at(fileIndex)).fileName()));
        return;
    }
    QStringList sources, translations;
    QString prefix;
    bool qualityWarning = false;
    for (const auto &entry : saved.value(QStringLiteral("chunks")).toArray()) {
        const auto chunk = entry.toObject();
        const QString source = chunk.value(QStringLiteral("source")).toString();
        const QString translated = chunk.value(QStringLiteral("translated")).toString();
        QStringList missing, honorifics;
        bool residue = false;
        if (source.isEmpty() || translated.isEmpty()
            || digest(source.toUtf8() + '\0' + translated.toUtf8())
                != chunk.value(QStringLiteral("digest")).toString()) return;
        // 质量提示不再否定已采用的分块；仍校验指纹、摘要和原文连续性。
        qualityWarning |= !validateChunk({fileIndex, source}, translated, missing, honorifics, residue).isEmpty();
        prefix += source;
        if (!m_fileTexts.at(fileIndex).startsWith(prefix)) return;
        sources.append(source);
        translations.append(translated);
    }
    if (sources.isEmpty()) return;
    const QString tail = m_fileTexts.at(fileIndex).mid(prefix.size());
    const auto chunks = sourceChunks(tail, relevantTermsForChunk(fileIndex, tail));
    int end = m_currentTask;
    while (end < m_tasks.size() && m_tasks.at(end).fileIndex == fileIndex) ++end;
    const int removed = end - m_currentTask;
    m_tasks.remove(m_currentTask, removed);
    for (int i = 0; i < chunks.size(); ++i) {
        m_tasks.insert(m_currentTask + i, {fileIndex, chunks.at(i)});
    }
    m_fileGlossaryCompatibility[fileIndex] = saved.value(QStringLiteral("glossaryCompatibility")).toBool();
    m_sourceChunks[fileIndex] = sources;
    m_translated[fileIndex] = translations;
    m_fileQualityWarnings[fileIndex] = qualityWarning;
    m_restoredChunks += sources.size();
    emit progressChanged(m_currentTask, m_tasks.size(),
        QStringLiteral("已恢复 %1 个已保存的分块，继续未完成部分：%2")
            .arg(sources.size()).arg(QFileInfo(m_filePaths.at(fileIndex)).fileName()));
}

MurasakiTranslator::TranslationResponse MurasakiTranslator::parseResponse(
    const QByteArray &data, const QString &source) const
{
    const auto root = QJsonDocument::fromJson(data).object();
    const auto choices = root.value(QStringLiteral("choices")).toArray();
    if (root.contains(QStringLiteral("error")) || choices.size() != 1)
        return {ResponseStatus::Invalid, {}, QStringLiteral("响应不是单条有效翻译结果")};
    const auto choice = choices.first().toObject();
    const QString finish = choice.value(QStringLiteral("finish_reason")).toString();
    if (finish == QStringLiteral("length"))
        return {ResponseStatus::Truncated, {}, QStringLiteral("输出被截断（思考/译文超出上限）")};
    // tool_calls / content_filter / 缺少结束原因都不能冒充完整成稿。
    if (finish != QStringLiteral("stop"))
        return {ResponseStatus::Invalid, {}, QStringLiteral("响应未正常完成：%1").arg(finish)};
    const auto message = choice.value(QStringLiteral("message")).toObject();
    if (!message.value(QStringLiteral("refusal")).toString().isEmpty()
        || !message.value(QStringLiteral("tool_calls")).toArray().isEmpty())
        return {ResponseStatus::Invalid, {}, QStringLiteral("响应拒绝翻译或返回了工具调用")};

    QString raw = message.value(QStringLiteral("content")).toString();
    const QString reasoning = message.value(QStringLiteral("reasoning_content")).toString();
    if (raw.trimmed().isEmpty()) {
        if (reasoning.trimmed().isEmpty())
            return {ResponseStatus::Invalid, {}, QStringLiteral("模型返回空响应")};
        // 兼容旧服务端错把整个回答放进 reasoning_content；必须有明确的结束边界。
        static const QRegularExpression end(QStringLiteral("</think\\s*>"),
                                             QRegularExpression::CaseInsensitiveOption);
        if (!end.match(reasoning).hasMatch())
            return {ResponseStatus::ThinkingOnly, {}, QStringLiteral("只有思考，没有最终译文")};
        raw = reasoning;
    }
    const QString translated = stripThink(raw).trimmed();
    if (translated.isEmpty())
        return {ResponseStatus::ThinkingOnly, {}, QStringLiteral("思考未结束或未生成最终译文")};
    static const QRegularExpression unfinishedTag(QStringLiteral("</?think\\b"),
                                                  QRegularExpression::CaseInsensitiveOption);
    if (unfinishedTag.match(translated).hasMatch())
        return {ResponseStatus::Invalid, {}, QStringLiteral("响应含损坏的思考标签")};
    // 不通过删行/截断来修补答案。脚手架混入正文意味着整份结果需要重生成。
    static const QStringList markers = {
        QStringLiteral("【术语表】"), QStringLiteral("【专名对照】"),
        QStringLiteral("【本次校正要求】"), QStringLiteral("【前情提要"),
        QStringLiteral("【正文开始"), QStringLiteral("【正文结束"),
        QStringLiteral("[术语表检查]"), QStringLiteral("**核心要求：**"),
        QStringLiteral("[Style & Persona]"), QStringLiteral("[Structure & Logic]"),
        QStringLiteral("[Nuance & Culture]"), QStringLiteral("[Comparative Decision]"),
        QStringLiteral("请将以下日文翻译成中文"), QStringLiteral("请翻译："),
        QStringLiteral("上一版译文不符合要求"), QStringLiteral("<|im_start|>"),
        QStringLiteral("<|im_end|>")};
    for (const auto &marker : markers) {
        if (translated.contains(marker) && !source.contains(marker))
            return {ResponseStatus::Contaminated, {}, QStringLiteral("译文混入提示词或模板标记：%1").arg(marker)};
    }
    return {ResponseStatus::Complete, translated, {}};
}

QString MurasakiTranslator::stripThink(const QString &text) const
{
    static const QRegularExpression thinkTag(
        QStringLiteral("<(\\/?)think\\b[^>]*>"),
        QRegularExpression::CaseInsensitiveOption);
    QString result;
    int depth = 0;
    int start = 0;
    bool closed = false;
    auto tags = thinkTag.globalMatch(text);
    while (tags.hasNext()) {
        const auto tag = tags.next();
        if (depth == 0)
            result += text.mid(start, tag.capturedStart() - start);
        if (tag.captured(1).isEmpty()) {
            // 思考只能作为回答前缀；不能删除夹在译文中间的思考再拼接两段。
            if (depth == 0 && (closed || !result.trimmed().isEmpty())) return {};
            ++depth;
        } else if (depth > 0) {
            --depth;
            if (depth == 0) closed = true;
        } else {
            // 模板可能已预填 <think>，响应只含分析及 </think>：
            // 闭合标签之前的内容仍是思考，必须全部丢弃。
            if (closed) return {};
            result.clear();
            closed = true;
        }
        start = tag.capturedEnd();
    }
    // 未闭合的思考，即使 finish_reason=stop 也不能作为译文落盘。
    if (depth != 0)
        return QString();
    return result + text.mid(start);
}

void MurasakiTranslator::onRequestFinished(const QString &url, const QVariant &context,
                                           const QByteArray &data, bool ok,
                                           const QString &error)
{
    Q_UNUSED(url);
    if (m_aborted)
        return;

    const auto request = context.toMap();
    if (request.value(QStringLiteral("run")).toULongLong() != m_runId || !m_running) return;
    int taskIndex = request.value(QStringLiteral("task")).toInt();
    if (taskIndex != m_currentTask || taskIndex < 0 || taskIndex >= m_tasks.size())
        return;

    ChunkTask task = m_tasks[taskIndex];
    const auto response = ok ? parseResponse(data, task.text) : TranslationResponse{};
    const bool truncated = response.status == ResponseStatus::Truncated;
    const bool contextExceeded = !ok && error.contains(QStringLiteral("context"), Qt::CaseInsensitive)
        && (error.contains(QStringLiteral("exceed"), Qt::CaseInsensitive)
            || error.contains(QStringLiteral("too long"), Qt::CaseInsensitive));
    if ((truncated || contextExceeded) && splitTruncatedTask(taskIndex)) {
        retryCurrentChunk();
        return;
    }

    if (!ok || response.status != ResponseStatus::Complete) {
        const QString reasonText = ok ? response.reason : error;
        const int attempts = task.attempts;
        if (ok && response.status == ResponseStatus::ThinkingOnly
            && !glossaryPromptForChunk(task.fileIndex, task.text).isEmpty()
            && !m_fileGlossaryCompatibility.value(task.fileIndex)) {
            m_fileGlossaryCompatibility[task.fileIndex] = true;
            emit progressChanged(m_currentTask, m_tasks.size(),
                QStringLiteral("官方术语模板只返回思考，本章启用专名标题兼容；保留 Novel 思考模式"));
        }
        if (ok) {
            m_tasks[taskIndex].feedback = QStringLiteral(
                "请完成正文翻译，必须在 </think> 之后输出完整中文译文。"
                "只翻译本次日文，保持段落顺序，不要复述指令、专名清单或增写解释。\n%1").arg(reasonText);
        }
        const int code = QJsonDocument::fromJson(data).object().value(QStringLiteral("error"))
                             .toObject().value(QStringLiteral("code")).toInt();
        const bool permanent = !ok && (contextExceeded || code == 400 || code == 401
            || code == 403 || code == 404 || code == 422);
        if (!permanent && attempts < kMaxChunkAttempts) {
            // 不改思考模式；下一次请求补充「必须完成正文翻译」的约束。
            emit progressChanged(m_currentTask, m_tasks.size(),
                                 QStringLiteral("Murasaki 翻译未完成（%1），正在重试 (%2/3): %3")
                                     .arg(reasonText)
                                     .arg(attempts)
                                     .arg(QFileInfo(m_filePaths[task.fileIndex]).fileName()));
            // 网络 / 服务繁忙采用有限退避；格式或质量反馈立即重试。
            retryCurrentChunk(ok ? 0 : qMin(4000, 500 * (1 << qMax(0, attempts - 1))));
            return;
        }
        m_fileFailed[task.fileIndex] = true;
        reportFileError(task.fileIndex, reasonText);
        emit progressChanged(m_currentTask, m_tasks.size(),
                             QStringLiteral("Murasaki 翻译失败: %1").arg(reasonText));
    } else {
        const QString text = response.translation;
        QStringList missingTerms;
        QStringList honorificIssues;
        bool japaneseResidue = false;
        QStringList issueKeys;
        const QString feedback =
            validateChunk(task, text, missingTerms, honorificIssues, japaneseResidue, &issueKeys);
        if (!feedback.isEmpty()) {
            const int retries = task.qualityRetries;
            const bool repeatedIssue = std::any_of(issueKeys.cbegin(), issueKeys.cend(),
                [&](const QString &key) { return task.correctedIssues.contains(key); });
            // 同一块内按问题类型记账，不依赖反馈文本或本次遗漏的具体译名。
            // 任何已纠正问题再次出现就采用当前有效译文，不因同时出现新问题继续打回。
            if (!repeatedIssue && m_tasks.at(taskIndex).attempts < kMaxChunkAttempts) {
                for (const auto &key : issueKeys) m_tasks[taskIndex].correctedIssues.insert(key);
                m_tasks[taskIndex].qualityRetries = retries + 1;
                m_tasks[taskIndex].feedback = QStringLiteral("请修正后重新翻译：\n") + feedback;
                emit progressChanged(m_currentTask, m_tasks.size(),
                                     QStringLiteral("质量校验未通过，正在重试 (%1/%2): %3；%4")
                                         .arg(m_tasks.at(taskIndex).qualityRetries)
                                         .arg(kMaxChunkAttempts - 1)
                                         .arg(QFileInfo(m_filePaths[task.fileIndex]).fileName(), feedback.left(240)));
                retryCurrentChunk();
                return;
            }
            // 同类问题已纠正过一次或请求预算耗尽，采用当前有效译文。
            // 空回复、请求失败和截断仍由上面的基础有效性检查处理。
            reportFileError(task.fileIndex, QStringLiteral("术语 / 质量检查仍有提示，已采用本次译文：%1").arg(feedback), true);
            emit progressChanged(m_currentTask, m_tasks.size(),
                                 repeatedIssue ? QStringLiteral("同一质量问题已纠正一次，采用本次译文继续翻译")
                                               : QStringLiteral("重试预算已用完，采用本次译文继续翻译"));
        }
        m_tasks[taskIndex].feedback.clear();
        if (!m_fileFailed[task.fileIndex]) {
            m_translated[task.fileIndex].append(text);
            m_sourceChunks[task.fileIndex].append(task.text);
            saveCheckpoint(task.fileIndex);
        }
    }

    advanceChunk(task.fileIndex);
}

void MurasakiTranslator::failCurrentChunk(const QString &reason)
{
    const int fileIndex = m_tasks.at(m_currentTask).fileIndex;
    m_fileFailed[fileIndex] = true;
    reportFileError(fileIndex, reason);
    emit progressChanged(m_currentTask, m_tasks.size(), QStringLiteral("Murasaki 翻译失败：%1").arg(reason));
    advanceChunk(fileIndex);
}

void MurasakiTranslator::advanceChunk(int fileIndex)
{
    if (m_aborted || !m_running) return;
    m_tasks[m_currentTask].feedback.clear();
    ++m_currentTask;
    // 整章已经无法保存时，跳过它的剩余块，避免继续消耗几轮无意义推理。
    if (m_fileFailed[fileIndex]) {
        while (m_currentTask < m_tasks.size()
               && m_tasks.at(m_currentTask).fileIndex == fileIndex)
            ++m_currentTask;
    }

    bool fileDone = (m_currentTask >= m_tasks.size())
                    || (m_tasks[m_currentTask].fileIndex != fileIndex);
    if (fileDone)
        finishCurrentFile(fileIndex);

    retryCurrentChunk();
}

void MurasakiTranslator::finishCurrentFile(int fileIndex)
{
    if (fileIndex < 0 || fileIndex >= m_filePaths.size())
        return;

    if (m_fileFailed[fileIndex]) {
        ++m_failed;
        emit fileStatusChanged(m_filePaths[fileIndex], QStringLiteral("FAILED"));
        emit progressChanged(m_currentTask, m_tasks.size(),
                             QStringLiteral("Murasaki 翻译失败: %1")
                                 .arg(QFileInfo(m_filePaths[fileIndex]).fileName()));
        return;
    }

    QString outDir = (fileIndex < m_outputDirs.size() && !m_outputDirs[fileIndex].isEmpty())
                         ? m_outputDirs[fileIndex] : QString();
    QDir dir(outDir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        ++m_failed;
        reportFileError(fileIndex, QStringLiteral("无法创建译文输出目录：%1").arg(outDir));
        emit fileStatusChanged(m_filePaths[fileIndex], QStringLiteral("FAILED"));
        emit progressChanged(m_currentTask, m_tasks.size(),
                             QStringLiteral("无法创建目录: %1").arg(outDir));
        return;
    }

    const QString body = m_translated[fileIndex].join(QStringLiteral("\n\n"));
    const QString content = TxtExporter::mergeChapterDocument(
        fileIndex < m_fileHeaders.size() ? m_fileHeaders.at(fileIndex) : QString(), body);
    QString outPath = dir.filePath(QFileInfo(m_filePaths[fileIndex]).fileName());

    QSaveFile f(outPath);
    const QByteArray bytes = QByteArray("\xEF\xBB\xBF") + content.toUtf8();
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        ++m_failed;
        reportFileError(fileIndex, QStringLiteral("无法保存译文：%1（%2）").arg(outPath, f.errorString()));
        emit fileStatusChanged(m_filePaths[fileIndex], QStringLiteral("FAILED"));
        emit progressChanged(m_currentTask, m_tasks.size(),
                             QStringLiteral("无法写入文件: %1").arg(outPath));
        return;
    }
    QFile::remove(checkpointPath(fileIndex));
    QFile::remove(failurePath(fileIndex));
    if (!m_fileQualityWarnings.value(fileIndex)) {
        const QString checkpoint = checkpointPath(fileIndex);
        if (!checkpoint.isEmpty()) QFile::remove(checkpoint.chopped(5) + QStringLiteral(".warning.json"));
    }
    emit fileErrorChanged(m_filePaths.at(fileIndex), QString());
    ++m_success;
    emit fileStatusChanged(m_filePaths[fileIndex], QStringLiteral("COMPLETED"));
    emit progressChanged(m_currentTask, m_tasks.size(),
                         QStringLiteral("已保存 Murasaki 译文: %1").arg(outPath));
}

void MurasakiTranslator::writeCjdl(int fileIndex)
{
    if (fileIndex < 0 || fileIndex >= m_filePaths.size())
        return;
    if (m_cjdlDir.isEmpty())
        return;

    QString workId;
    QString chapterId;
    resolveSourceIdentity(m_filePaths[fileIndex], workId, chapterId);
    if (workId.isEmpty() || chapterId.isEmpty())
        return;

    QList<CjdlBlock> blocks;
    const int count = qMin(m_sourceChunks[fileIndex].size(),
                           m_translated[fileIndex].size());
    for (int i = 0; i < count; ++i) {
        CjdlBlock block;
        block.index = i;
        block.source = m_sourceChunks[fileIndex][i];
        block.translation = m_translated[fileIndex][i];
        blocks.append(block);
    }

    CjdlExporter::write(m_cjdlDir, workId, chapterId, chapterId, blocks);
}
