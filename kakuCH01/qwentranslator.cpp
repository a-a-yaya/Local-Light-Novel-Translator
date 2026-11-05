#include "qwentranslator.h"
#include "cjdlexporter.h"
#include "novelpaths.h"
#include "txtexporter.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>

namespace {
// 分块与 History 预算：Qwen 上下文 16384，块放得比 Sakura/Murasaki 大一些，
// 减少请求次数（更少的上下文断裂），同时给 system + 术语表 + 历史留足余量。
constexpr int kTargetChunkChars = 1500;
constexpr int kMaxHistoryChunks = 2;
constexpr int kMaxHistoryChars = 2000;
constexpr int kMaxQualityRetries = 2;

// 术语表提取：块更大（输出只有几个术语），History 不需要。
constexpr int kGlossChunkChars = 2600;

// 术语表提取：审核阶段每批候选词的条数（候选太多时模型会截断 JSON，
// 导致审核结果无法解析、过滤失效）。
constexpr int kGlossReviewBatch = 30;

// ---- 术语表机械过滤 ----
// 小模型经常把普通名词、助词短语，甚至「自己都注明不收录」的词塞进结果里，
// 所以在写入术语表之前统一做一层与模型无关的校验：只允许「人名 / 地名 /
// 技能名 / 专名」四类、必须是正文里真实出现的短词，且不能是助词短语、
// 日常词或纯假名的普通词。

// 词形：术语必须是 2–20 字的短词，不含空格与标点；单字一类的词直接丢弃
// （「凪」「蝶」这种单字既不好判也不好用）。
bool termShapeOk(const QString &source)
{
    if (source.size() < 2 || source.size() > 20)
        return false;
    static const QRegularExpression bad(
        QStringLiteral("[\\s。、，,．.！？!?！？「」『』（）()\\[\\]【】｛｝{}:：;；\"'’“”‘’~〜*…—\\-]"));
    return !source.contains(bad);
}

// 日常词 / 亲属称呼 / 自称 / 泛称 / 一般外来语黑名单：
// 这是唯一保留的机械判定——确定不是专有名词的词直接挡掉，其余全交给模型。
bool isCommonDailyWord(const QString &source)
{
    static const QSet<QString> stop = {
        QStringLiteral("父親"), QStringLiteral("母親"), QStringLiteral("両親"),
        QStringLiteral("父さん"), QStringLiteral("母さん"),
        QStringLiteral("お父さん"), QStringLiteral("お母さん"), QStringLiteral("おばあちゃん"),
        QStringLiteral("おじいさん"), QStringLiteral("父"), QStringLiteral("母"),
        QStringLiteral("兄"), QStringLiteral("弟"), QStringLiteral("姉"), QStringLiteral("妹"),
        QStringLiteral("息子"), QStringLiteral("娘"), QStringLiteral("祖父"), QStringLiteral("祖母"),
        QStringLiteral("吾輩"), QStringLiteral("俺"), QStringLiteral("僕"), QStringLiteral("私"),
        QStringLiteral("あなた"), QStringLiteral("あんた"), QStringLiteral("彼"), QStringLiteral("彼女"),
        QStringLiteral("自分"), QStringLiteral("親御さん"), QStringLiteral("ご主人様"),
        QStringLiteral("学校"), QStringLiteral("先生"), QStringLiteral("病院"), QStringLiteral("会社"),
        QStringLiteral("仕事"), QStringLiteral("料理"), QStringLiteral("部屋"), QStringLiteral("時間"),
        QStringLiteral("今日"), QStringLiteral("明日"), QStringLiteral("昨日"), QStringLiteral("猫"),
        QStringLiteral("犬"), QStringLiteral("蝶"), QStringLiteral("羽虫"), QStringLiteral("人間"),
        QStringLiteral("少女"), QStringLiteral("少年"), QStringLiteral("大人"), QStringLiteral("子供"),
        QStringLiteral("友達"), QStringLiteral("世界"), QStringLiteral("気持ち"), QStringLiteral("気分"),
        QStringLiteral("電話"), QStringLiteral("駅"), QStringLiteral("店"), QStringLiteral("家"),
        QStringLiteral("車"), QStringLiteral("本"), QStringLiteral("水"), QStringLiteral("雨"),
        QStringLiteral("天気"), QStringLiteral("ご飯"), QStringLiteral("名前"), QStringLiteral("言葉"),
        QStringLiteral("声"), QStringLiteral("顔"), QStringLiteral("手"), QStringLiteral("目"),
        QStringLiteral("頭"), QStringLiteral("心"), QStringLiteral("唾液"), QStringLiteral("発作"),
        QStringLiteral("理屈"), QStringLiteral("居候"), QStringLiteral("売人"),
        QStringLiteral("化け物"), QStringLiteral("家庭"), QStringLiteral("大学"),
        QStringLiteral("中学"), QStringLiteral("高校"), QStringLiteral("小学生"),
        QStringLiteral("医師"), QStringLiteral("看護師"), QStringLiteral("外科医"),
        QStringLiteral("法律家"), QStringLiteral("主任"),
        QStringLiteral("ホテル"), QStringLiteral("フロント"), QStringLiteral("スタンガン"),
        QStringLiteral("アニメ"), QStringLiteral("ドラマ"), QStringLiteral("スマホ"),
        QStringLiteral("テレビ"), QStringLiteral("コーヒー"), QStringLiteral("カラオケ"),
        QStringLiteral("レストラン"), QStringLiteral("コンビニ"), QStringLiteral("エレベーター"),
        QStringLiteral("トイレ"), QStringLiteral("メニュー"), QStringLiteral("パソコン"),
        QStringLiteral("ゲーム"), QStringLiteral("バイト"), QStringLiteral("アルバイト"),
        QStringLiteral("サービス"), QStringLiteral("システム"), QStringLiteral("グループ"),
        QStringLiteral("チーム"), QStringLiteral("ニュース"), QStringLiteral("スーパー"),
        QStringLiteral("マンション"), QStringLiteral("アパート"), QStringLiteral("タクシー"),
        QStringLiteral("バス"), QStringLiteral("カメラ"), QStringLiteral("パン"),
        QStringLiteral("ケーキ"), QStringLiteral("カレー"), QStringLiteral("ラーメン"),
        QStringLiteral("オムライス"), QStringLiteral("炊飯器"), QStringLiteral("綿棒"),
        QStringLiteral("脱脂綿"), QStringLiteral("電子タバコ"), QStringLiteral("ホタン"),
        QStringLiteral("メントス"), QStringLiteral("ソフトキャンディー"), QStringLiteral("ロコモコ"),
        QStringLiteral("こたつ"), QStringLiteral("ヤドカリ"), QStringLiteral("中二病"),
        QStringLiteral("間接キス"), QStringLiteral("料理魂"), QStringLiteral("御の字"),
        QStringLiteral("ベージュ"), QStringLiteral("コンクール"), QStringLiteral("ピアス"),
    };
    return stop.contains(source);
}

// 译名里还有假名，说明模型没真正翻译这个专名，需要补一次中文译名。
bool targetNeedsChinese(const QString &target)
{
    for (const QChar c : target) {
        const ushort u = c.unicode();
        if ((u >= 0x3041 && u <= 0x3096) || (u >= 0x30A1 && u <= 0x30F6))
            return true;
    }
    return false;
}

bool glossaryNeedsFixup(const QList<GlossaryTerm> &terms)
{
    for (const GlossaryTerm &t : terms) {
        if (targetNeedsChinese(t.target))
            return true;
    }
    return false;
}

QStringList splitAtBoundaries(const QString &text, const QRegularExpression &boundary,
                              int maxChars)
{
    QStringList result;
    int start = 0;
    auto matches = boundary.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const int end = match.capturedEnd();
        if (end <= start)
            continue;
        QString part = text.mid(start, end - start);
        while (part.size() > maxChars) {
            result.append(part.left(maxChars));
            part.remove(0, maxChars);
        }
        if (!part.isEmpty())
            result.append(part);
        start = end;
    }
    QString tail = text.mid(start);
    while (tail.size() > maxChars) {
        result.append(tail.left(maxChars));
        tail.remove(0, maxChars);
    }
    if (!tail.isEmpty())
        result.append(tail);
    return result;
}

QStringList splitLongParagraph(const QString &paragraph)
{
    static const QRegularExpression sentenceBoundary(
        QStringLiteral("[^。！？!?]*[。！？!?]+[」』）】]*"));
    static const QRegularExpression phraseBoundary(
        QStringLiteral("[^、，,；;]*[、，,；;]+"));

    const QStringList sentences = splitAtBoundaries(
        paragraph, sentenceBoundary, qMax(1, paragraph.size()));
    QStringList result;
    for (const QString &sentence : sentences) {
        if (sentence.size() <= kTargetChunkChars) {
            result.append(sentence);
            continue;
        }
        result.append(splitAtBoundaries(sentence, phraseBoundary, kTargetChunkChars));
    }
    return result;
}

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

QString QwenTranslator::defaultPrompt()
{
    return QStringLiteral(
        "你是一位精通二次元文化的资深轻小说翻译家，擅长把日文轻小说译成自然流畅的简体中文。\n"
        "请把用户给出的日文文本翻译成中文。\n\n"
        "要求：\n"
        "1. 信达雅：译文符合中文轻小说的阅读习惯，还原原作的语气、节奏与沉浸感。\n"
        "2. 人称与称谓准确：联系上下文判断说话人，不擅自增删原文没有的代词。\n"
        "3. 严格遵守术语表（正文会以【固定术语】给出）：原文出现其中的词时，必须原样使用\n"
        "   对应的中文译名，不得改字、增删字、换用其它译法或音译；与译文其它部分冲突时，\n"
        "   一律以【固定术语】为准，并保持全文一致（人名、称呼、术语、翻译风格统一）。\n"
        "4. 只输出最终的中文译文，不要输出原文、标题、注释或任何解释。");
}

QString QwenTranslator::glossaryPrompt()
{
    return QStringLiteral(
        "你是术语表编辑。请从下面的日文小说正文中，提取需要在全书保持统一译名的专有名词。\n\n"
        "步骤：先通读全文，把出场的人物（尤其是主角）全部找出来，再找地名 / 技能名 / 专名。\n"
        "主角的名字最容易漏，务必先确认主角叫什么并收录；每个有名字的角色都要收录。\n\n"
        "收录范围（只有这四类，type 必须是其中之一）：\n"
        "1. 人名：角色名、姓氏、昵称、绰号\n"
        "2. 地名：国名、城市、地区、街道、店铺、学校等固有名称\n"
        "3. 技能名：招式、能力、法术等固有名称\n"
        "4. 专名：组织名、种族名、家族名、作品独创的设定名称\n\n"
        "人名写法要求（重要）：\n"
        "- source 一律写「不带敬称的原名」：正文里的「なぎさん」「あこちゃん」「ネコくん」「斎藤先生」"
        "要写成 なぎ / あこ / ネコ / 斎藤。\n"
        "- target 只写名字本身的中文，不要带「小姐 / 先生 / 酱 / 君」这类称呼后缀：\n"
        "  「なぎさん」→ source なぎ / target 凪；「あこちゃん」→ source あこ / target 阿子；"
        "「ネコくん」→ source ネコ / target 猫。\n"
        "- 同一个人只输出一条：如果正文里既有「なぎ」又有「なぎさん」，只输出 なぎ。\n"
        "- 本作登场角色的人名、姓氏、昵称、绰号全部都要收录，不得遗漏"
        "（只出现一两次的角色名也要收）。\n\n"
        "不要收录：日常名词、食物、家具、电器、现实品牌、泛称（少女、猫、便利店）、"
        "亲属称呼（父さん、母さん、お父さん）、自称（吾輩、俺、僕）、形容词、动词、副词、"
        "拟声词、语气词、数字、整句话、含助词的短语（如「私の首」「金のために」）。\n\n"
        "每条给出四个字段：\n"
        "source：日文原文（人名不带敬称，去掉敬称后仍要是正文里出现过的写法）\n"
        "target：中文译名（人名只写名字本身）\n"
        "type：只能是 人名 / 地名 / 技能名 / 专名 之一\n"
        "evidence：从正文里逐字复制的短句（不超过 30 字），必须包含 source\n\n"
        "只输出一个 JSON 数组，不要解释，不要 Markdown 代码块。\n"
        "格式：[{\"source\":\"\",\"target\":\"\",\"type\":\"\",\"evidence\":\"\"}]");
}

QString QwenTranslator::glossaryReviewPrompt()
{
    return QStringLiteral(
        "你是术语表审核员。下面给出若干候选词，请逐个判断：它是不是「某一个具体的谁 / 哪里 / 哪一招 / "
        "哪个组织或种族」的专有名称，并把写法规范化。\n\n"
        "判断原则：宁可多收专有名称，也不要把角色名、地名、招式名漏掉。\n"
        "- 只要它指作品里的某个人、某个地点、某个招式、某个组织 / 种族 / 家族 / 独创设定 → keep=true；"
        "看不出是谁的昵称 / 称呼也按人名 keep=true。\n"
        "- 只有确定它是普通日常词、物品、品牌、亲属称呼、自称、职业身份、症状、身体部位、"
        "普通概念、泛称、含助词的短语才 keep=false。\n\n"
        "必须 keep=true 的例子：カイト、なぎ、麻衣、あこちゃん、ネコくん（人名 / 昵称）、"
        "ルミナリア王国、魔導学院（地名 / 组织）、光刃斬、ねこパンチ（技能名）、ふんがい星人（独创种族）。\n\n"
        "必须 keep=false 的例子：こたつ、蝶、オムライス、炊飯器（日常物品）、メントス（品牌）、"
        "父親、母さん、吾輩（亲属称呼 / 自称）、医師、看護師、大学生（职业身份）、発作、唾液（症状 / 身体部位）、"
        "間接キス、中二病（普通概念）、私の首、金のために（含助词的短语）、少女、化け物（泛称）。\n\n"
        "人名写法要求（重要）：\n"
        "- source 必须与候选词完全一致（原样返回，不要改动）。\n"
        "- normalized 填「去掉敬称后的原名」：候选词是「なぎさん」就填「なぎ」，「あこちゃん」填「あこ」，"
        "「ネコくん」填「ネコ」；候选词本身就是原名（なぎ）就填「なぎ」；"
        "不是人名（地名 / 技能名 / 专名）填空字符串。\n"
        "- target 只写名字本身的中文，不要「小姐 / 先生 / 酱 / 君」：なぎ → 凪、あこ → 阿子。\n"
        "- 同一个名称的不同敬称写法（なぎ / なぎさん / ナギさん）算同一个术语："
        "只保留最先出现的那一条 keep=true，其余一律 keep=false。\n\n"
        "对每一条输出：\n"
        "{\"source\":\"候选词原样\",\"keep\":true 或 false,\"type\":\"人名/地名/技能名/专名\","
        "\"target\":\"中文译名（人名只写名字）\",\"normalized\":\"去掉敬称后的原名\","
        "\"reason\":\"不超过 8 个字说明它是什么，例如「主角名」「登场角色的种族名」\"}\n"
        "keep=false 时 type、target、normalized、reason 都填空字符串。\n"
        "只输出 JSON 数组，不要解释，不要 Markdown 代码块。");
}

QwenTranslator::QwenTranslator(QObject *parent)
    : QObject(parent)
    , m_serverUrl(QStringLiteral("http://127.0.0.1:8082"))
    , m_modelAlias(QStringLiteral("qwen"))
    , m_systemPrompt(defaultPrompt())
{
    connect(&m_http, &HttpClient::requestFinished,
            this, &QwenTranslator::onTranslateReply);
    connect(&m_glossaryHttp, &HttpClient::requestFinished,
            this, &QwenTranslator::onGlossaryReply);
}

void QwenTranslator::setServerUrl(const QString &url)
{
    m_serverUrl = url;
    while (m_serverUrl.endsWith(QLatin1Char('/')))
        m_serverUrl.chop(1);
}

void QwenTranslator::setModelAlias(const QString &alias)
{
    if (!alias.trimmed().isEmpty())
        m_modelAlias = alias;
}

void QwenTranslator::setSystemPrompt(const QString &prompt)
{
    if (!prompt.trimmed().isEmpty())
        m_systemPrompt = prompt;
}

void QwenTranslator::setCjdlDir(const QString &dir)
{
    m_cjdlDir = dir;
}

void QwenTranslator::setDownloadRoot(const QString &dir)
{
    m_downloadRoot = dir;
}

// ---------------------------------------------------------------- 翻译

void QwenTranslator::translateFiles(const QVector<QwenFileJob> &jobs)
{
    m_filePaths.clear();
    m_outputDirs.clear();
    for (const QwenFileJob &job : jobs) {
        m_filePaths << job.path;
        m_outputDirs << job.outputDir;
    }

    m_success = 0;
    m_failed = 0;
    m_aborted = false;
    m_attempts = 0;
    m_translated.fill(QStringList(), m_filePaths.size());
    m_sourceChunks.fill(QStringList(), m_filePaths.size());
    m_fileFailed.fill(false, m_filePaths.size());
    m_fileStarted.fill(false, m_filePaths.size());
    m_fileHeaders.fill(QString(), m_filePaths.size());

    m_history.clear();
    m_historyFileIndex = -1;
    m_pendingHistoryText.clear();

    loadGlossaries();
    buildTaskQueue();
    m_taskQualityRetries.fill(0, m_tasks.size());
    m_taskFeedback.clear();
    for (int i = 0; i < m_filePaths.size(); ++i)
        emit fileStatusChanged(m_filePaths[i], m_fileFailed[i]
                               ? QStringLiteral("FAILED") : QStringLiteral("WAITING"));
    m_currentTask = 0;
    emit progressChanged(0, m_tasks.size(), QStringLiteral("开始 Qwen 翻译…"));
    translateNextChunk();
}

void QwenTranslator::abort()
{
    if (m_aborted)
        return;
    m_aborted = true;
    m_history.clear();
    m_http.abortAll();
    emit progressChanged(m_currentTask, m_tasks.size(), QStringLiteral("Qwen 翻译已中止"));
}

void QwenTranslator::buildTaskQueue()
{
    m_tasks.clear();

    for (int i = 0; i < m_filePaths.size(); ++i) {
        QFile f(m_filePaths[i]);
        if (!f.open(QIODevice::ReadOnly)) {
            m_fileFailed[i] = true;
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

        const QStringList paragraphs = text.split(
            QRegularExpression(QStringLiteral("\\n\\s*\\n")));
        QString current;
        for (const QString &para : paragraphs) {
            const QString p = para.trimmed();
            if (p.isEmpty())
                continue;

            const QStringList segments = p.size() > kTargetChunkChars
                ? splitLongParagraph(p) : QStringList{p};
            for (int segmentIndex = 0; segmentIndex < segments.size(); ++segmentIndex) {
                const QString &segment = segments.at(segmentIndex);
                const QString separator = segmentIndex == 0 && !current.isEmpty()
                    ? QStringLiteral("\n") : QString();
                if (!current.isEmpty()
                    && current.size() + separator.size() + segment.size() > kTargetChunkChars) {
                    m_tasks.append({i, current});
                    current = segment;
                } else {
                    current += separator + segment;
                }
            }
        }
        if (!current.isEmpty())
            m_tasks.append({i, current});
    }
}

void QwenTranslator::clearHistory()
{
    m_history.clear();
}

void QwenTranslator::appendHistoryEntry(const QString &sourceText,
                                        const QString &translatedText)
{
    QwenHistoryEntry entry;
    entry.sourceText = sourceText;
    entry.translatedText = translatedText;
    m_history.append(entry);
    while (m_history.size() > kMaxHistoryChunks)
        m_history.removeFirst();
}

QString QwenTranslator::serializeHistory(const QList<QwenHistoryEntry> &history) const
{
    QString text;
    for (int i = 0; i < history.size(); ++i) {
        text += QStringLiteral("[%1]\n原文：%2\n译文：%3\n")
                    .arg(i + 1)
                    .arg(history[i].sourceText, history[i].translatedText);
    }
    return text;
}

QList<QwenHistoryEntry> QwenTranslator::effectiveHistory() const
{
    QList<QwenHistoryEntry> result;
    if (m_history.isEmpty())
        return result;

    int used = 0;
    for (int i = m_history.size() - 1; i >= 0; --i) {
        const int size = serializeHistory(QList<QwenHistoryEntry>{ m_history[i] }).size();
        if (!result.isEmpty() && used + size > kMaxHistoryChars)
            break;
        result.prepend(m_history[i]);
        used += size;
    }
    if (result.isEmpty()) {
        QwenHistoryEntry last = m_history.last();
        const int half = qMax(1, kMaxHistoryChars / 2);
        if (last.sourceText.size() > half)
            last.sourceText = last.sourceText.right(half);
        if (last.translatedText.size() > half)
            last.translatedText = last.translatedText.right(half);
        result.append(last);
    }
    return result;
}

void QwenTranslator::resolveSourceIdentity(const QString &filePath,
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

// 章节文件 -> 小说根目录（".../chapters/" 的上层）；非规范结构返回空
QString QwenTranslator::novelRootForSourceFile(const QString &filePath) const
{
    const QFileInfo fi(filePath);
    QDir dir = fi.absoluteDir();
    if (dir.dirName() != QStringLiteral("chapters"))
        return QString();
    dir.cdUp();
    return dir.absolutePath();
}

void QwenTranslator::loadGlossaries()
{
    m_glossaries.clear();

    for (const QString &path : m_filePaths) {
        // 术语表按「小说根目录的完整路径」索引：不同站点可能撞上同一个 workId
        // 目录名，只按目录名索引会让后一本小说用到前一本的术语表。
        const QString root = novelRootForSourceFile(path);
        if (root.isEmpty() || m_glossaries.contains(root))
            continue;

        QString workId;
        QString chapterId;
        resolveSourceIdentity(path, workId, chapterId);

        // 「启用哪份术语表」的键必须和 AppController 用的一致：优先「站点/作品」
        // 相对标识（不同站点撞同一个作品 id 时才不会串），旧键作为回退。
        QString novelKey = m_downloadRoot.isEmpty()
                               ? QString()
                               : NovelPaths::relativeKey(m_downloadRoot, root);
        if (novelKey.isEmpty())
            novelKey = workId;

        // 翻译时只加载「当前启用」的那一份术语表（人工 / Qwen 二选一）。
        const QString kind = GlossaryManager::activeKindFor(novelKey, workId);
        GlossaryManager gm(root, GlossaryManager::fileNameForKind(kind));
        gm.load();
        if (kind == GlossaryManager::kQwenKind) {
            // Qwen 术语表是机器生成的：老文件里可能残留普通词与助词短语，
            // 注入 Prompt 之前先按同一套机械规则过滤一遍（只改内存，不回写文件）。
            GlossaryManager cleaned(root, gm.fileName());
            for (const GlossaryTerm &t : gm.terms()) {
                if (isCommonDailyWord(t.source) || !termShapeOk(t.source))
                    continue; // 机械部分：黑名单 + 词形
                cleaned.addTerm(t);
            }
            m_glossaries.insert(root, cleaned);
        } else {
            m_glossaries.insert(root, gm);
        }
    }
}

QString QwenTranslator::glossaryPromptForChunk(int fileIndex, const QString &text) const
{
    if (fileIndex < 0 || fileIndex >= m_filePaths.size())
        return QString();
    const QString root = novelRootForSourceFile(m_filePaths[fileIndex]);
    if (root.isEmpty())
        return QString();
    const auto it = m_glossaries.constFind(root);
    if (it == m_glossaries.constEnd())
        return QString();
    return it->buildPromptText(text);
}

QList<GlossaryTerm> QwenTranslator::relevantTermsForChunk(int fileIndex,
                                                          const QString &text) const
{
    if (fileIndex < 0 || fileIndex >= m_filePaths.size())
        return {};
    const QString root = novelRootForSourceFile(m_filePaths[fileIndex]);
    if (root.isEmpty())
        return {};
    const auto it = m_glossaries.constFind(root);
    if (it == m_glossaries.constEnd())
        return {};
    return it->termsForText(text);
}

QString QwenTranslator::validateChunk(const ChunkTask &task, const QString &translated,
                                      QStringList &missingTerms,
                                      bool &japaneseResidue) const
{
    missingTerms.clear();
    japaneseResidue = false;

    const QList<GlossaryTerm> relevant = relevantTermsForChunk(task.fileIndex, task.text);
    for (const GlossaryTerm &term : relevant) {
        if (!translated.contains(term.target))
            missingTerms << QStringLiteral("%1 → %2").arg(term.source, term.target);
    }

    int kanaCount = 0;
    for (const QChar ch : translated) {
        const ushort u = ch.unicode();
        if ((u >= 0x3040 && u <= 0x309F) || (u >= 0x30A0 && u <= 0x30FF) || u == 0x30FC)
            ++kanaCount;
    }
    if (kanaCount >= 4 && kanaCount * 2 > translated.size())
        japaneseResidue = true;

    QStringList glossarySources;
    for (const GlossaryTerm &term : relevant)
        glossarySources << term.source;
    QStringList untranslatedAnchors;
    const QStringList anchors = katakanaAnchors(task.text);
    for (const QString &anchor : anchors) {
        if (glossarySources.contains(anchor))
            continue;
        if (translated.contains(anchor))
            untranslatedAnchors << anchor;
    }

    QStringList problems;
    if (!missingTerms.isEmpty())
        problems << QStringLiteral("以下术语必须使用指定译名：%1")
                        .arg(missingTerms.join(QStringLiteral("、")));
    if (japaneseResidue)
        problems << QStringLiteral("译文仍含大量日文假名，请输出纯简体中文");
    if (!untranslatedAnchors.isEmpty())
        problems << QStringLiteral("以下专名仍未转换（残留片假名）：%1")
                        .arg(untranslatedAnchors.join(QStringLiteral("、")));

    return problems.join(QStringLiteral("；"));
}

void QwenTranslator::translateNextChunk()
{
    if (m_aborted)
        return;
    if (m_currentTask >= m_tasks.size()) {
        emit progressChanged(m_tasks.size(), m_tasks.size(), QStringLiteral("Qwen 翻译完成"));
        emit translationFinished(m_success, m_failed);
        return;
    }

    const ChunkTask &task = m_tasks[m_currentTask];

    if (task.fileIndex != m_historyFileIndex) {
        m_historyFileIndex = task.fileIndex;
        m_history.clear();
    }

    if (task.fileIndex >= 0 && task.fileIndex < m_fileStarted.size()
        && !m_fileStarted[task.fileIndex]) {
        m_fileStarted[task.fileIndex] = true;
        emit fileStatusChanged(m_filePaths[task.fileIndex], QStringLiteral("TRANSLATING"));
    }

    const QList<QwenHistoryEntry> history = effectiveHistory();
    m_pendingHistoryText = serializeHistory(history);

    emit progressChanged(m_currentTask, m_tasks.size(),
                         QStringLiteral("正在翻译: %1 (%2/%3)")
                             .arg(QFileInfo(m_filePaths[task.fileIndex]).fileName())
                             .arg(m_currentTask + 1).arg(m_tasks.size()));

    QJsonArray messages;
    QJsonObject systemMsg;
    systemMsg.insert(QStringLiteral("role"), QStringLiteral("system"));
    systemMsg.insert(QStringLiteral("content"), m_systemPrompt);
    messages.append(systemMsg);

    QString userContent;
    const auto feedbackIt = m_taskFeedback.constFind(m_currentTask);
    if (feedbackIt != m_taskFeedback.constEnd())
        userContent += QStringLiteral("上一版译文不符合要求，请修正后重新翻译：\n%1\n\n")
                           .arg(feedbackIt.value());
    const QString glossaryPrompt = glossaryPromptForChunk(task.fileIndex, task.text);
    if (!glossaryPrompt.isEmpty())
        userContent += glossaryPrompt + QStringLiteral("\n\n");
    if (!m_pendingHistoryText.isEmpty())
        userContent += QStringLiteral("前文译文参考（用于保持人名与风格一致，勿重复翻译）：\n%1\n\n")
                           .arg(m_pendingHistoryText);
    userContent += QStringLiteral("请将以下日文翻译成中文：\n%1").arg(task.text);

    QJsonObject userMsg;
    userMsg.insert(QStringLiteral("role"), QStringLiteral("user"));
    userMsg.insert(QStringLiteral("content"), userContent);
    messages.append(userMsg);

    QJsonObject body;
    body.insert(QStringLiteral("model"), m_modelAlias);
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("temperature"), 0.30);
    body.insert(QStringLiteral("top_p"), 0.90);
    body.insert(QStringLiteral("top_k"), 20);
    body.insert(QStringLiteral("min_p"), 0.0);
    body.insert(QStringLiteral("repeat_penalty"), 1.05);
    body.insert(QStringLiteral("repeat_last_n"), 256);
    body.insert(QStringLiteral("max_tokens"), 2048);
    // Qwen3.5 默认开启思考；思考模式在本任务上收益低且耗时长，固定关闭。
    QJsonObject templateKwargs;
    templateKwargs.insert(QStringLiteral("enable_thinking"), false);
    body.insert(QStringLiteral("chat_template_kwargs"), templateKwargs);

    m_http.postJson(m_serverUrl + QStringLiteral("/v1/chat/completions"),
                    QJsonDocument(body).toJson(QJsonDocument::Compact), m_currentTask);
}

QString QwenTranslator::stripThink(const QString &text)
{
    QString s = text;
    static const QRegularExpression thinkBlock(
        QStringLiteral("<think[^>]*>.*?</think>"),
        QRegularExpression::CaseInsensitiveOption
            | QRegularExpression::DotMatchesEverythingOption);
    s.remove(thinkBlock);
    static const QRegularExpression thinkOpen(QStringLiteral("<think[^>]*>"),
                                              QRegularExpression::CaseInsensitiveOption);
    s.remove(thinkOpen);
    return s;
}

QString QwenTranslator::stripPromptEcho(const QString &text)
{
    static const QStringList markers = {
        QStringLiteral("【重试反馈"),
        QStringLiteral("【固定术语"),
        QStringLiteral("【上文历史"),
        QStringLiteral("【前文译文参考"),
        QStringLiteral("【当前正文"),
        QStringLiteral("【正文开始")
    };
    int cut = text.size();
    for (const QString &marker : markers) {
        const int idx = text.indexOf(marker);
        if (idx >= 0 && idx < cut)
            cut = idx;
    }
    return text.left(cut).trimmed();
}

QString QwenTranslator::extractTranslation(const QByteArray &data) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return QString();
    const QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return QString();
    const QJsonObject message =
        choices.first().toObject().value(QStringLiteral("message")).toObject();

    QString text = message.value(QStringLiteral("content")).toString();
    if (text.trimmed().isEmpty())
        text = message.value(QStringLiteral("reasoning_content")).toString();
    return stripPromptEcho(stripThink(text).trimmed());
}

void QwenTranslator::onTranslateReply(const QString &url, const QVariant &context,
                                      const QByteArray &data, bool ok,
                                      const QString &error)
{
    Q_UNUSED(url);
    if (m_aborted)
        return;

    const int taskIndex = context.toInt();
    if (taskIndex < 0 || taskIndex >= m_tasks.size())
        return;

    const ChunkTask task = m_tasks[taskIndex];
    const QString translated = ok ? extractTranslation(data) : QString();

    if (!ok || translated.trimmed().isEmpty()) {
        if (m_attempts < 3) {
            ++m_attempts;
            emit progressChanged(m_currentTask, m_tasks.size(),
                                 QStringLiteral("Qwen 翻译失败，正在重试 (%1/3): %2")
                                     .arg(m_attempts)
                                     .arg(QFileInfo(m_filePaths[task.fileIndex]).fileName()));
            translateNextChunk();
            return;
        }
        m_attempts = 0;
        m_fileFailed[task.fileIndex] = true;
        emit progressChanged(m_currentTask, m_tasks.size(),
                             QStringLiteral("Qwen 翻译失败: %1")
                                 .arg(ok ? QStringLiteral("模型未返回有效译文") : error));
    } else {
        m_attempts = 0;
        const QString text = translated.trimmed();
        QStringList missingTerms;
        bool japaneseResidue = false;
        const QString feedback = validateChunk(task, text, missingTerms, japaneseResidue);
        if (!feedback.isEmpty()) {
            const int retries = (taskIndex < m_taskQualityRetries.size())
                                    ? m_taskQualityRetries[taskIndex] : 0;
            if (retries < kMaxQualityRetries) {
                m_taskQualityRetries[taskIndex] = retries + 1;
                m_taskFeedback.insert(taskIndex, feedback);
                emit progressChanged(m_currentTask, m_tasks.size(),
                                     QStringLiteral("质量校验未通过，正在重试 (%1/%2): %3")
                                         .arg(m_taskQualityRetries[taskIndex])
                                         .arg(kMaxQualityRetries)
                                         .arg(QFileInfo(m_filePaths[task.fileIndex]).fileName()));
                translateNextChunk();
                return;
            }
            emit progressChanged(m_currentTask, m_tasks.size(),
                                 QStringLiteral("已重试仍不达标，采用当前译文（%1）").arg(feedback));
        }
        m_taskFeedback.remove(taskIndex);
        m_translated[task.fileIndex].append(text);
        m_sourceChunks[task.fileIndex].append(task.text);
        appendHistoryEntry(task.text, text);
    }

    ++m_currentTask;

    const bool fileDone = (m_currentTask >= m_tasks.size())
                          || (m_tasks[m_currentTask].fileIndex != task.fileIndex);
    if (fileDone)
        finishCurrentFile(task.fileIndex);

    translateNextChunk();
}

void QwenTranslator::finishCurrentFile(int fileIndex)
{
    if (fileIndex < 0 || fileIndex >= m_filePaths.size())
        return;

    if (m_fileFailed[fileIndex]) {
        ++m_failed;
        emit fileStatusChanged(m_filePaths[fileIndex], QStringLiteral("FAILED"));
        emit progressChanged(m_currentTask, m_tasks.size(),
                             QStringLiteral("Qwen 翻译失败: %1")
                                 .arg(QFileInfo(m_filePaths[fileIndex]).fileName()));
        return;
    }

    const QString outDir = (fileIndex < m_outputDirs.size() && !m_outputDirs[fileIndex].isEmpty())
                               ? m_outputDirs[fileIndex] : QString();
    QDir dir(outDir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        ++m_failed;
        emit fileStatusChanged(m_filePaths[fileIndex], QStringLiteral("FAILED"));
        emit progressChanged(m_currentTask, m_tasks.size(),
                             QStringLiteral("无法创建目录: %1").arg(outDir));
        return;
    }

    const QString body = m_translated[fileIndex].join(QStringLiteral("\n\n"));
    const QString content = TxtExporter::mergeChapterDocument(
        fileIndex < m_fileHeaders.size() ? m_fileHeaders.at(fileIndex) : QString(), body);
    const QString outPath = dir.filePath(QFileInfo(m_filePaths[fileIndex]).fileName());

    QFile f(outPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        ++m_failed;
        emit fileStatusChanged(m_filePaths[fileIndex], QStringLiteral("FAILED"));
        emit progressChanged(m_currentTask, m_tasks.size(),
                             QStringLiteral("无法写入文件: %1").arg(outPath));
        return;
    }
    f.write("\xEF\xBB\xBF");
    f.write(content.toUtf8());
    f.close();
    ++m_success;
    emit fileStatusChanged(m_filePaths[fileIndex], QStringLiteral("COMPLETED"));
    emit progressChanged(m_currentTask, m_tasks.size(),
                         QStringLiteral("已保存 Qwen 译文: %1").arg(outPath));

    if (!m_cjdlDir.isEmpty()) {
        QString workId;
        QString chapterId;
        resolveSourceIdentity(m_filePaths[fileIndex], workId, chapterId);
        if (!workId.isEmpty() && !chapterId.isEmpty()) {
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
    }
}

// ---------------------------------------------------------------- 术语表提取

QString QwenTranslator::parseJsonArrayText(const QString &raw)
{
    QString t = raw.trimmed();
    t.remove(QRegularExpression(QStringLiteral("^```(?:json)?\\s*")));
    t.remove(QRegularExpression(QStringLiteral("\\s*```$")));
    const int start = t.indexOf(QLatin1Char('['));
    const int end = t.lastIndexOf(QLatin1Char(']'));
    if (start < 0 || end <= start)
        return QString();
    return t.mid(start, end - start + 1);
}

void QwenTranslator::extractGlossary(const QStringList &files)
{
    m_glossPhase = GlossPhase::Extracting;
    m_glossTasks.clear();
    m_glossRoots.clear();
    m_glossTerms.clear();
    m_glossText.clear();
    m_glossCurrent = 0;
    m_glossRootIndex = 0;
    m_glossRetries = 0;
    m_glossReviewBatch = 0;
    m_glossFinal.clear();
    m_glossReviewCurrent = 0;
    m_glossReviewTotal = 0;
    m_glossFixed.clear();
    m_glossFixupFixed = 0;
    m_glossExtractDropped = 0;
    m_glossReviewDropped = 0;

    buildGlossaryTasks(files);

    if (m_glossTasks.isEmpty()) {
        m_glossPhase = GlossPhase::Idle;
        emit glossaryFinished(0, QStringLiteral("没有可提取的章节正文"));
        return;
    }

    emit glossaryStageChanged(QStringLiteral("提取术语"));
    emit glossaryProgressChanged(0, m_glossTasks.size(),
                                 QStringLiteral("开始提取术语表…"));
    runNextGlossaryTask();
}

void QwenTranslator::abortGlossary()
{
    if (m_glossPhase == GlossPhase::Idle)
        return;
    m_glossPhase = GlossPhase::Idle;
    m_glossaryHttp.abortAll();
    emit glossaryProgressChanged(m_glossCurrent, m_glossTasks.size(),
                                 QStringLiteral("术语表提取已中止"));
}

void QwenTranslator::buildGlossaryTasks(const QStringList &files)
{
    for (const QString &path : files) {
        const QFileInfo fi(path);
        QDir dir = fi.absoluteDir();
        if (dir.dirName() != QStringLiteral("chapters"))
            continue; // 只对规范的小说章节结构提取术语表
        dir.cdUp();
        const QString root = dir.absolutePath();
        const QString workId = QFileInfo(root).fileName();

        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        QByteArray raw = f.readAll();
        f.close();
        if (raw.startsWith("\xEF\xBB\xBF"))
            raw.remove(0, 3);
        QString ignoredHeader;
        QString body;
        TxtExporter::splitChapterDocument(QString::fromUtf8(raw), &ignoredHeader, &body);
        body = body.trimmed();
        if (body.isEmpty())
            continue;

        if (!m_glossRoots.contains(root)) {
            m_glossRoots << root;
            m_glossText.insert(root, QString());
        }
        m_glossText[root] += body + QStringLiteral("\n");

        const QStringList paragraphs = body.split(
            QRegularExpression(QStringLiteral("\\n\\s*\\n")));
        QString current;
        for (const QString &para : paragraphs) {
            const QString p = para.trimmed();
            if (p.isEmpty())
                continue;
            const QStringList segments = p.size() > kGlossChunkChars
                ? splitAtBoundaries(p,
                                    QRegularExpression(QStringLiteral("[^。！？!?]*[。！？!?]+[」』）】]*")),
                                    kGlossChunkChars)
                : QStringList{p};
            for (const QString &segment : segments) {
                if (!current.isEmpty()
                    && current.size() + segment.size() > kGlossChunkChars) {
                    m_glossTasks.append({root, fi.fileName(), current});
                    current = segment;
                } else {
                    current += segment;
                }
            }
        }
        if (!current.isEmpty())
            m_glossTasks.append({root, fi.fileName(), current});
        Q_UNUSED(workId);
    }
}

void QwenTranslator::runNextGlossaryTask()
{
    if (m_glossPhase != GlossPhase::Extracting)
        return;
    if (m_glossCurrent >= m_glossTasks.size()) {
        m_glossRootIndex = 0;
        m_glossPhase = GlossPhase::Reviewing;
        for (const QString &root : m_glossRoots)
            m_glossReviewTotal += (m_glossTerms.value(root).size() + kGlossReviewBatch - 1)
                                 / kGlossReviewBatch;
        runNextGlossaryReview();
        return;
    }

    const GlossaryChunk &task = m_glossTasks[m_glossCurrent];
    emit glossaryProgressChanged(m_glossCurrent, m_glossTasks.size(),
                                 QStringLiteral("正在提取术语 (%1/%2): %3")
                                     .arg(m_glossCurrent + 1)
                                     .arg(m_glossTasks.size())
                                     .arg(task.fileName));

    QJsonArray messages;
    QJsonObject systemMsg;
    systemMsg.insert(QStringLiteral("role"), QStringLiteral("system"));
    systemMsg.insert(QStringLiteral("content"), glossaryPrompt());
    messages.append(systemMsg);
    QJsonObject userMsg;
    userMsg.insert(QStringLiteral("role"), QStringLiteral("user"));
    userMsg.insert(QStringLiteral("content"), QStringLiteral("正文：\n%1").arg(task.text));
    messages.append(userMsg);

    QJsonObject body;
    body.insert(QStringLiteral("model"), m_modelAlias);
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("temperature"), 0.20);
    body.insert(QStringLiteral("top_p"), 0.90);
    body.insert(QStringLiteral("top_k"), 20);
    body.insert(QStringLiteral("min_p"), 0.0);
    body.insert(QStringLiteral("repeat_penalty"), 1.05);
    body.insert(QStringLiteral("repeat_last_n"), 256);
    body.insert(QStringLiteral("max_tokens"), 1200);
    QJsonObject templateKwargs;
    templateKwargs.insert(QStringLiteral("enable_thinking"), false);
    body.insert(QStringLiteral("chat_template_kwargs"), templateKwargs);

    m_glossaryHttp.postJson(m_serverUrl + QStringLiteral("/v1/chat/completions"),
                            QJsonDocument(body).toJson(QJsonDocument::Compact),
                            m_glossCurrent);
}

void QwenTranslator::collectCandidates(const QString &novelRoot, const QByteArray &data)
{
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return;
    const QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return;
    const QJsonObject message =
        choices.first().toObject().value(QStringLiteral("message")).toObject();
    QString text = message.value(QStringLiteral("content")).toString();
    if (text.trimmed().isEmpty())
        text = message.value(QStringLiteral("reasoning_content")).toString();

    const QString json = parseJsonArrayText(stripThink(text));
    if (json.isEmpty())
        return;
    const QJsonDocument arrDoc = QJsonDocument::fromJson(json.toUtf8());
    if (!arrDoc.isArray())
        return;

    const QString novelText = m_glossText.value(novelRoot);
    QList<GlossaryTerm> &candidates = m_glossTerms[novelRoot];

    for (const QJsonValue &v : arrDoc.array()) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        const QString source = o.value(QStringLiteral("source")).toString().trimmed();
        const QString target = o.value(QStringLiteral("target")).toString().trimmed();
        const QString type = o.value(QStringLiteral("type")).toString().trimmed();
        const QString evidence = o.value(QStringLiteral("evidence")).toString().trimmed();

        // 机械检查只有两条：不是日常词黑名单、且确实在正文里出现过（防止编词）。
        // 类型、敬称、译名等全部交给审核模型判断。
        if (source.isEmpty() || !termShapeOk(source) || isCommonDailyWord(source)) {
            ++m_glossExtractDropped;
            continue;
        }
        if (!novelText.contains(source)) {
            ++m_glossExtractDropped;
            continue;
        }
        bool duplicate = false;
        for (const GlossaryTerm &existing : candidates) {
            if (existing.source == source) {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;

        GlossaryTerm t;
        t.source = source;
        t.target = target;
        t.type = type;
        t.note = evidence;
        t.enabled = true;
        t.origin = QStringLiteral("qwen");
        candidates.append(t);
    }
}

void QwenTranslator::runNextGlossaryReview()
{
    if (m_glossPhase != GlossPhase::Reviewing)
        return;
    // 逐本小说、逐批审核；本批完成后回到这里继续下一批。
    while (m_glossRootIndex < m_glossRoots.size()) {
        const QString root = m_glossRoots[m_glossRootIndex];
        const int count = m_glossTerms.value(root).size();
        const int batches = (count + kGlossReviewBatch - 1) / kGlossReviewBatch;
        if (count > 0 && m_glossReviewBatch < batches) {
            requestGlossaryReview(m_glossRootIndex);
            return;
        }
        // 审核结束：如果还有「译名是日文（含假名）」的条目，补一次中文译名。
        if (!m_glossFixed.contains(root) && glossaryNeedsFixup(m_glossFinal.value(root))) {
            m_glossFixed.insert(root);
            requestGlossaryFixup(m_glossRootIndex);
            return;
        }
        ++m_glossRootIndex;
        m_glossReviewBatch = 0;
    }
    m_glossPhase = GlossPhase::Saving;
    saveGlossaryResults();
}

void QwenTranslator::requestGlossaryReview(int rootIndex)
{
    const QString root = m_glossRoots[rootIndex];
    const QList<GlossaryTerm> candidates = m_glossTerms.value(root);
    const int begin = m_glossReviewBatch * kGlossReviewBatch;
    if (begin >= candidates.size()) {
        ++m_glossRootIndex;
        m_glossReviewBatch = 0;
        runNextGlossaryReview();
        return;
    }
    const int end = qMin(begin + kGlossReviewBatch, candidates.size());
    const int batches = (candidates.size() + kGlossReviewBatch - 1) / kGlossReviewBatch;

    emit glossaryStageChanged(QStringLiteral("审核术语"));
    emit glossaryProgressChanged(m_glossReviewCurrent, m_glossReviewTotal,
                                 QStringLiteral("正在审核术语（第 %1/%2 批）…")
                                     .arg(m_glossReviewBatch + 1)
                                     .arg(batches));

    QStringList lines;
    for (int i = begin; i < end; ++i) {
        // 把抽取阶段抄回的原文依据一起给审核模型：看得到上下文，
        // 才不会把「昵称 / 招式」误判成普通词。
        QString evidence = candidates.at(i).note.trimmed();
        if (evidence.size() > 40)
            evidence = evidence.left(40) + QStringLiteral("…");
        lines << QStringLiteral("- %1（现译名：%2，当前归类：%3，正文依据：%4）")
                     .arg(candidates.at(i).source, candidates.at(i).target,
                          candidates.at(i).type, evidence);
    }

    QJsonArray messages;
    QJsonObject systemMsg;
    systemMsg.insert(QStringLiteral("role"), QStringLiteral("system"));
    systemMsg.insert(QStringLiteral("content"), glossaryReviewPrompt());
    messages.append(systemMsg);
    QJsonObject userMsg;
    userMsg.insert(QStringLiteral("role"), QStringLiteral("user"));
    userMsg.insert(QStringLiteral("content"),
                   QStringLiteral("候选词：\n%1").arg(lines.join(QLatin1Char('\n'))));
    messages.append(userMsg);

    QJsonObject body;
    body.insert(QStringLiteral("model"), m_modelAlias);
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("temperature"), 0.10);
    body.insert(QStringLiteral("top_p"), 0.90);
    body.insert(QStringLiteral("top_k"), 20);
    body.insert(QStringLiteral("min_p"), 0.0);
    body.insert(QStringLiteral("repeat_penalty"), 1.05);
    body.insert(QStringLiteral("repeat_last_n"), 256);
    body.insert(QStringLiteral("max_tokens"), 3000);
    QJsonObject templateKwargs;
    templateKwargs.insert(QStringLiteral("enable_thinking"), false);
    body.insert(QStringLiteral("chat_template_kwargs"), templateKwargs);

    m_glossaryHttp.postJson(m_serverUrl + QStringLiteral("/v1/chat/completions"),
                            QJsonDocument(body).toJson(QJsonDocument::Compact),
                            100000 + rootIndex * 1000 + m_glossReviewBatch);
}

// 审核通过一条术语：同一原文只保留一条（其余判断交给模型）。
void QwenTranslator::addReviewedTerm(const QString &novelRoot, const GlossaryTerm &term)
{
    QList<GlossaryTerm> &final = m_glossFinal[novelRoot];
    for (const GlossaryTerm &existing : final) {
        if (existing.source == term.source)
            return;
    }
    final.append(term);
}

// 应用审核结果：只认模型的 keep 判定（默认拒绝：审核没提到的候选丢弃）。
// 机械部分只剩「日常词黑名单」与「原文确实出现在正文里」两条。
// 返回 false 表示这次响应无法解析。
bool QwenTranslator::applyReviewResponse(const QString &novelRoot,
                                         const QList<GlossaryTerm> &batch,
                                         const QByteArray &data,
                                         QList<GlossaryTerm> &accepted)
{
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return false;
    const QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return false;
    const QJsonObject message =
        choices.first().toObject().value(QStringLiteral("message")).toObject();
    QString text = message.value(QStringLiteral("content")).toString();
    if (text.trimmed().isEmpty())
        text = message.value(QStringLiteral("reasoning_content")).toString();

    const QString json = parseJsonArrayText(stripThink(text));
    if (json.isEmpty())
        return false;
    const QJsonDocument arrDoc = QJsonDocument::fromJson(json.toUtf8());
    if (!arrDoc.isArray())
        return false;
    if (arrDoc.array().isEmpty())
        return true; // 明确的「本批都不合格」：保持默认拒绝，而不是当作失败

    QHash<QString, QJsonObject> verdicts;
    for (const QJsonValue &v : arrDoc.array()) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        const QString source = o.value(QStringLiteral("source")).toString().trimmed();
        if (!source.isEmpty())
            verdicts.insert(source, o);
    }
    if (verdicts.isEmpty())
        return false;

    const QString novelText = m_glossText.value(novelRoot);
    for (const GlossaryTerm &candidate : batch) {
        const auto it = verdicts.constFind(candidate.source);
        if (it == verdicts.constEnd()) {
            ++m_glossReviewDropped; // 审核没提到：默认拒绝
            continue;
        }
        const QJsonObject o = it.value();
        if (!o.value(QStringLiteral("keep")).toBool(false)) {
            ++m_glossReviewDropped;
            continue;
        }
        QString target = o.value(QStringLiteral("target")).toString().trimmed();
        if (target.isEmpty())
            target = candidate.target;
        if (target.isEmpty() || !termShapeOk(candidate.source)
            || isCommonDailyWord(candidate.source)
            || !novelText.contains(candidate.source)) {
            ++m_glossReviewDropped;
            continue;
        }
        // 人名去敬称：模型给出 normalized（なぎさん → なぎ）时用它做术语原文，
        // 但必须也在正文里出现过，否则保留原写法。
        QString finalSource = o.value(QStringLiteral("normalized")).toString().trimmed();
        if (finalSource.isEmpty() || !termShapeOk(finalSource)
            || !novelText.contains(finalSource)) {
            finalSource = candidate.source;
        }

        GlossaryTerm t;
        t.source = finalSource;
        t.target = target;
        t.type = o.value(QStringLiteral("type")).toString().trimmed();
        const QString reason = o.value(QStringLiteral("reason")).toString().trimmed();
        t.note = reason.isEmpty() ? candidate.note : reason;
        t.enabled = true;
        t.origin = QStringLiteral("qwen");
        accepted.append(t);
    }
    return true;
}

void QwenTranslator::requestGlossaryFixup(int rootIndex)
{
    const QString root = m_glossRoots.value(rootIndex);
    QStringList lines;
    const QList<GlossaryTerm> terms = m_glossFinal.value(root);
    for (const GlossaryTerm &t : terms) {
        if (targetNeedsChinese(t.target)) {
            lines << QStringLiteral("- %1（当前译名：%2，类型：%3）")
                         .arg(t.source, t.target, t.type);
        }
    }
    if (lines.isEmpty()) {
        ++m_glossRootIndex;
        m_glossReviewBatch = 0;
        runNextGlossaryReview();
        return;
    }

    emit glossaryStageChanged(QStringLiteral("补翻译名"));
    emit glossaryProgressChanged(0, 1,
                                 QStringLiteral("正在补翻术语译名（%1 条）…").arg(lines.size()));

    QJsonArray messages;
    QJsonObject systemMsg;
    systemMsg.insert(QStringLiteral("role"), QStringLiteral("system"));
    systemMsg.insert(QStringLiteral("content"), QStringLiteral(
        "你是轻小说译名译审。下面给出若干日文专有名称（人名 / 地名 / 技能名 / 专名），"
        "请为每一个给出自然、符合中文轻小说习惯的中文译名。\n"
        "要求：\n"
        "1. 人名按日文读音或通行译法写成中文汉字（如「斎藤」→「斋藤」）。\n"
        "2. 只写名字本身，不要「小姐 / 先生 / 酱 / 君」这类称呼后缀"
        "（如「あこ」→「阿子」、「かちゃん」→「佳」）。\n"
        "3. 译名里不能出现假名或罗马字，必须只有中文。\n"
        "4. 只输出 JSON 数组，格式：[{\"source\":\"原文\",\"target\":\"中文译名\"}]，"
        "source 必须与输入完全一致。\n"
        "5. 不要解释，不要 Markdown 代码块。"));
    messages.append(systemMsg);
    QJsonObject userMsg;
    userMsg.insert(QStringLiteral("role"), QStringLiteral("user"));
    userMsg.insert(QStringLiteral("content"),
                   QStringLiteral("名称：\n%1").arg(lines.join(QLatin1Char('\n'))));
    messages.append(userMsg);

    QJsonObject body;
    body.insert(QStringLiteral("model"), m_modelAlias);
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("temperature"), 0.20);
    body.insert(QStringLiteral("top_p"), 0.90);
    body.insert(QStringLiteral("top_k"), 20);
    body.insert(QStringLiteral("min_p"), 0.0);
    body.insert(QStringLiteral("repeat_penalty"), 1.05);
    body.insert(QStringLiteral("repeat_last_n"), 256);
    body.insert(QStringLiteral("max_tokens"), 1024);
    QJsonObject templateKwargs;
    templateKwargs.insert(QStringLiteral("enable_thinking"), false);
    body.insert(QStringLiteral("chat_template_kwargs"), templateKwargs);

    m_glossaryHttp.postJson(m_serverUrl + QStringLiteral("/v1/chat/completions"),
                            QJsonDocument(body).toJson(QJsonDocument::Compact),
                            200000 + rootIndex);
}

void QwenTranslator::applyGlossaryFixup(const QString &novelRoot, const QByteArray &data)
{
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return;
    const QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return;
    const QJsonObject message =
        choices.first().toObject().value(QStringLiteral("message")).toObject();
    QString text = message.value(QStringLiteral("content")).toString();
    if (text.trimmed().isEmpty())
        text = message.value(QStringLiteral("reasoning_content")).toString();
    const QString json = parseJsonArrayText(stripThink(text));
    if (json.isEmpty())
        return;
    const QJsonDocument arrDoc = QJsonDocument::fromJson(json.toUtf8());
    if (!arrDoc.isArray())
        return;

    QHash<QString, QString> translations;
    for (const QJsonValue &v : arrDoc.array()) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        const QString source = o.value(QStringLiteral("source")).toString().trimmed();
        const QString target = o.value(QStringLiteral("target")).toString().trimmed();
        if (!source.isEmpty() && !target.isEmpty() && !targetNeedsChinese(target))
            translations.insert(source, target);
    }
    if (translations.isEmpty())
        return;

    QList<GlossaryTerm> &final = m_glossFinal[novelRoot];
    for (GlossaryTerm &t : final) {
        const auto it = translations.constFind(t.source);
        if (it == translations.constEnd() || !targetNeedsChinese(t.target))
            continue;
        t.target = it.value();
        ++m_glossFixupFixed;
    }
}

void QwenTranslator::saveGlossaryResults()
{
    emit glossaryStageChanged(QStringLiteral("保存术语"));
    emit glossaryProgressChanged(0, 1, QStringLiteral("正在保存术语表…"));
    int total = 0;
    int purged = 0;
    QStringList savedRoots;

    for (const QString &root : m_glossRoots) {
        const QString workId = QFileInfo(root).fileName();
        const QString fileName = GlossaryManager::fileNameForKind(GlossaryManager::kQwenKind);

        const QList<GlossaryTerm> fresh = m_glossFinal.value(root);

        GlossaryManager existing(root, fileName);
        existing.load();

        // Qwen 术语表完全由模型生成，重新提取时以本次审核结果为准：
        // 只有本次一条都没通过时才保留旧文件（避免一次跑偏把术语表清空）。
        // 之前版本「只追加不替换」会把旧版本收进来的普通词一直留在表里。
        GlossaryManager out(root, fileName);
        out.setNovelRootPath(root);
        if (fresh.isEmpty()) {
            for (const GlossaryTerm &oldTerm : existing.terms()) {
                if (isCommonDailyWord(oldTerm.source) || !termShapeOk(oldTerm.source)) {
                    ++purged;
                    continue;
                }
                out.addTerm(oldTerm);
            }
        } else {
            // 统计被本次结果替换掉的旧条目（用于提示「清理了多少条」）。
            for (const GlossaryTerm &oldTerm : existing.terms()) {
                bool stillUsed = false;
                for (const GlossaryTerm &t : fresh) {
                    if (t.source == oldTerm.source) {
                        stillUsed = true;
                        break;
                    }
                }
                if (!stillUsed)
                    ++purged;
            }
            for (const GlossaryTerm &t : fresh)
                out.addTerm(t);
        }

        if (!out.save())
            continue;
        savedRoots << workId;
        total += out.terms().size();
    }

    m_glossPhase = GlossPhase::Idle;
    emit glossaryProgressChanged(1, 1,
                                 QStringLiteral("术语表提取完成"));

    if (savedRoots.isEmpty()) {
        emit glossaryFinished(0, QStringLiteral("未找到可提取的小说章节"));
        return;
    }

    int added = 0;
    for (const QString &root : m_glossRoots)
        added += m_glossFinal.value(root).size();

    QString detail;
    const int filtered = m_glossExtractDropped + m_glossReviewDropped + purged;
    if (filtered > 0)
        detail += QStringLiteral("；已过滤 %1 条非专有名词").arg(filtered);
    if (m_glossFixupFixed > 0)
        detail += QStringLiteral("；补翻译名 %1 条").arg(m_glossFixupFixed);

    if (added == 0) {
        emit glossaryFinished(0, purged > 0
            ? QStringLiteral("本次没有新增术语，已清理 %1 条旧的非专有名词").arg(purged)
            : QStringLiteral("本次没有发现符合条件的人名／地名／技能名等术语，Qwen 术语表保持原样"));
    } else {
        emit glossaryFinished(added, QStringLiteral("已生成 Qwen 术语表：新增 %1 条（共 %2 条）%3")
                                        .arg(added).arg(total).arg(detail));
    }
}

void QwenTranslator::onGlossaryReply(const QString &url, const QVariant &context,
                                     const QByteArray &data, bool ok,
                                     const QString &error)
{
    Q_UNUSED(url);
    const int ctx = context.toInt();

    if (ctx >= 200000) {
        // 译名补翻阶段：context = 200000 + rootIndex
        if (m_glossPhase != GlossPhase::Reviewing)
            return;
        const int rootIndex = ctx - 200000;
        if (rootIndex < 0 || rootIndex >= m_glossRoots.size()
            || rootIndex != m_glossRootIndex) {
            return;
        }
        if (ok) {
            applyGlossaryFixup(m_glossRoots.at(rootIndex), data);
        } else {
            emit glossaryProgressChanged(0, 1,
                                         QStringLiteral("术语译名补翻失败，保留原译名：%1")
                                             .arg(error));
        }
        emit glossaryProgressChanged(1, 1, QStringLiteral("术语译名补翻结束"));
        ++m_glossRootIndex;
        m_glossReviewBatch = 0;
        runNextGlossaryReview();
        return;
    }

    if (ctx >= 100000) {
        // 审核阶段：context = 100000 + rootIndex * 1000 + 批次序号
        if (m_glossPhase != GlossPhase::Reviewing)
            return;
        const int tag = ctx - 100000;
        const int rootIndex = tag / 1000;
        const int batchIndex = tag % 1000;
        if (rootIndex < 0 || rootIndex >= m_glossRoots.size()
            || rootIndex != m_glossRootIndex || batchIndex != m_glossReviewBatch) {
            return;
        }
        const QString root = m_glossRoots[rootIndex];
        const QList<GlossaryTerm> all = m_glossTerms.value(root);
        const int begin = batchIndex * kGlossReviewBatch;
        if (begin >= all.size()) {
            ++m_glossReviewBatch;
            runNextGlossaryReview();
            return;
        }
        const int end = qMin(begin + kGlossReviewBatch, all.size());
        QList<GlossaryTerm> batch;
        for (int k = begin; k < end; ++k)
            batch.append(all.at(k));

        QList<GlossaryTerm> accepted;
        bool usable = false;
        if (ok)
            usable = applyReviewResponse(root, batch, data, accepted);
        if (!usable) {
            // 审核结果不可用（网络错误，或模型把 JSON 截断/跑偏）：
            // 退回这一批「已通过机械过滤、且带正文依据」的候选，避免整轮白跑。
            accepted = batch;
            emit glossaryProgressChanged(m_glossReviewCurrent, m_glossReviewTotal,
                                         ok ? QStringLiteral("术语审核结果不可用，沿用已过滤的候选")
                                            : QStringLiteral("术语审核失败，沿用已过滤的候选：%1")
                                                  .arg(error));
        }
        for (const GlossaryTerm &t : accepted)
            addReviewedTerm(root, t);

        m_glossReviewBatch = batchIndex + 1;
        ++m_glossReviewCurrent;
        emit glossaryProgressChanged(m_glossReviewCurrent, m_glossReviewTotal,
                                     QStringLiteral("术语审核进度 %1/%2")
                                         .arg(m_glossReviewCurrent).arg(m_glossReviewTotal));
        runNextGlossaryReview();
        return;
    }

    // 提取阶段
    if (m_glossPhase != GlossPhase::Extracting)
        return;
    if (ctx < 0 || ctx >= m_glossTasks.size())
        return;

    if (!ok) {
        if (m_glossRetries < 2) {
            ++m_glossRetries;
            emit glossaryProgressChanged(m_glossCurrent, m_glossTasks.size(),
                                         QStringLiteral("术语提取失败，正在重试 (%1/2)…")
                                             .arg(m_glossRetries));
            runNextGlossaryTask();
            return;
        }
        m_glossRetries = 0;
    } else {
        m_glossRetries = 0;
        collectCandidates(m_glossTasks[ctx].novelRoot, data);
    }

    ++m_glossCurrent;
    runNextGlossaryTask();
}
