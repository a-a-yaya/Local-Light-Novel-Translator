#include "sakuratranslator.h"
#include "txtexporter.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <algorithm>

namespace {
constexpr int kChunkChars = 1600;
constexpr int kChunkLines = 64;
constexpr int kMaxAttempts = 3;
constexpr int kMaxSplitDepth = 4;

int boundaryBefore(const QString &text, int limit)
{
    if (text.size() <= limit) return text.size();
    const int end = qMin(limit, int(text.size()));
    // 优先完整句子 / 短语，无边界才按字符切分；不拆开 UTF-16 代理对。
    for (const QString &marks : {QStringLiteral("。！？!?」』"), QStringLiteral("、，,；;")}) {
        for (int i = end - 1; i >= end / 2; --i)
            if (marks.contains(text.at(i))) return i + 1;
    }
    if (end > 0 && (text.at(end - 1).isHighSurrogate()
        || (text.at(end - 1) == QLatin1Char('\\') && end < text.size()
            && text.at(end) == QLatin1Char('n')))) return end - 1;
    return end;
}

QString singleLine(QString text)
{
    text.replace(QRegularExpression(QStringLiteral("[\r\n]+")), QStringLiteral(" "));
    return text.trimmed();
}

QStringList contentLines(const QString &text)
{
    QStringList lines;
    for (const QString &line : text.split(QLatin1Char('\n')))
        if (!line.trimmed().isEmpty()) lines.append(line.trimmed());
    return lines;
}

// Longest matching span wins. A nickname inside a full name or compound item
// is not an independent occurrence requiring another Chinese name.
QHash<QString, int> occurrenceCounts(const QString &text, QStringList candidates)
{
    std::sort(candidates.begin(), candidates.end(), [](const QString &a, const QString &b) {
        return a.size() != b.size() ? a.size() > b.size() : a < b;
    });
    candidates.removeDuplicates();
    QHash<QString, int> counts;
    for (int position = 0; position < text.size();) {
        int length = 0;
        for (const QString &candidate : candidates) {
            if (!candidate.isEmpty() && text.indexOf(candidate, position) == position) {
                ++counts[candidate];
                length = candidate.size();
                break;
            }
        }
        position += qMax(1, length);
    }
    return counts;
}

QString validateTranslation(const QString &source, QString *translation)
{
    if (translation->trimmed().isEmpty()) return QStringLiteral("模型返回空译文");
    for (const QString &marker : {QStringLiteral("<think>"), QStringLiteral("</think>"),
                                  QStringLiteral("<|im_start|>"), QStringLiteral("<|im_end|>")})
        if (translation->contains(marker)) return QStringLiteral("模型返回思考或对话模板，未采用");
    if (source.count(QStringLiteral("\\n")) != translation->count(QStringLiteral("\\n")))
        return QStringLiteral("行内换行控制符数量不一致");
    const QStringList translatedLines = contentLines(*translation);
    if (contentLines(source).size() != translatedLines.size())
        return QStringLiteral("译文行数与原文不一致");
    // 只按原文还原被模型折叠的空行，不猜测或补写正文。
    QStringList restored;
    int next = 0;
    for (const QString &line : source.split(QLatin1Char('\n')))
        restored.append(line.trimmed().isEmpty() ? line : translatedLines.at(next++));
    *translation = restored.join(QLatin1Char('\n'));
    return {};
}
}

SakuraTranslator::SakuraTranslator(QObject *parent) : QObject(parent)
{
    m_http.setTransferTimeout(180000);
    connect(&m_http, &HttpClient::requestFinished, this, &SakuraTranslator::onRequestFinished);
}

void SakuraTranslator::setServerUrl(const QString &url)
{
    m_serverUrl = url;
    while (m_serverUrl.endsWith(QLatin1Char('/'))) m_serverUrl.chop(1);
}
void SakuraTranslator::setModelAlias(const QString &alias)
{
    if (!alias.trimmed().isEmpty()) m_modelAlias = alias;
}
void SakuraTranslator::setSystemPrompt(const QString &prompt)
{
    m_systemPrompt = prompt.trimmed(); // 空值恢复对应版本的官方 system。
}
// 保留现有调用接口，双语资源仍由详情页 / 阅读器手动生成。
void SakuraTranslator::setCjdlDir(const QString &dir) { Q_UNUSED(dir); }
void SakuraTranslator::setGenerateBilingual(bool enabled) { Q_UNUSED(enabled); }

QString SakuraTranslator::systemPrompt() const
{
    if (!m_systemPrompt.isEmpty()) return m_systemPrompt;
    if (m_modelAlias.contains(QStringLiteral("qwen3"), Qt::CaseInsensitive))
        return QStringLiteral("你是一个日本二次元领域的日语翻译模型，可以流畅通顺地以日本轻小说/漫画/Galgame的风格将日文翻译成简体中文，并联系上下文正确使用人称代词，不擅自添加原文中没有的代词。");
    return QStringLiteral("你是一个轻小说翻译模型，可以流畅通顺地以日本轻小说的风格将日文翻译成简体中文，并联系上下文正确使用人称代词，不擅自添加原文中没有的代词。");
}

QString SakuraTranslator::novelRoot(const QString &path) const
{
    QDir dir = QFileInfo(path).absoluteDir();
    if (dir.dirName() != QStringLiteral("chapters")) return {};
    dir.cdUp();
    return dir.absolutePath();
}

void SakuraTranslator::loadGlossaries()
{
    m_glossaries.clear();
    m_glossaryErrors.clear();
    for (const auto &file : m_files) {
        const QString root = novelRoot(file.path);
        if (root.isEmpty() || m_glossaries.contains(root)) continue;
        QDir parent(root);
        const QString workId = parent.dirName();
        parent.cdUp();
        // 新版站点 / 作品键优先，兼容旧作品键；缓存按完整小说根目录隔离。
        const QString key = parent.dirName().compare(QStringLiteral("DOWNLOAD"), Qt::CaseInsensitive) == 0
            ? workId : parent.dirName() + QLatin1Char('/') + workId;
        GlossaryManager glossary(root, GlossaryManager::fileNameForKind(
            GlossaryManager::activeKindFor(key, workId)));
        if (!glossary.load() && QFileInfo::exists(glossary.glossaryPath()))
            m_glossaryErrors.insert(root, QStringLiteral("无法读取启用的术语表：%1").arg(glossary.glossaryPath()));
        m_glossaries.insert(root, glossary);
    }
}

QList<GlossaryTerm> SakuraTranslator::relevantTerms(const ChunkTask &task) const
{
    const auto glossary = m_glossaries.constFind(novelRoot(m_files[task.fileIndex].path));
    if (glossary == m_glossaries.constEnd()) return {};
    const auto candidates = glossary->termsForText(task.text);
    QStringList sources;
    for (const auto &term : candidates) sources.append(term.source);
    const auto counts = occurrenceCounts(task.text, sources);
    QList<GlossaryTerm> result;
    for (const auto &term : candidates)
        if (counts.value(term.source) > 0) result.append(term);
    return result;
}

QString SakuraTranslator::termIssue(const ChunkTask &task, const QString &translation, QStringList *missingSources) const
{
    const auto terms = relevantTerms(task);
    QStringList sources, targets;
    for (const auto &term : terms) {
        sources.append(term.source);
        targets.append(singleLine(term.target));
    }
    const auto sourceCounts = occurrenceCounts(task.text, sources);
    const auto targetCounts = occurrenceCounts(translation, targets);
    QHash<QString, int> required;
    for (const auto &term : terms) {
        // Identical CJK strings can be ordinary verbs, e.g. 世界征服. Do not
        // reject a faithful word-order change such as 征服世界 as a name error.
        const QString target = singleLine(term.target);
        if (term.source != target) required[target] += sourceCounts.value(term.source);
    }
    QStringList missing;
    for (const auto &term : terms) {
        const QString target = singleLine(term.target);
        const int expected = required.value(target);
        if (expected > 0 && targetCounts.value(target) < expected) {
            missingSources->append(term.source);
            const QString detail = QStringLiteral("%1->%2（译名至少出现%3次）")
                .arg(singleLine(term.source), singleLine(term.target)).arg(expected);
            if (!missing.contains(detail)) missing.append(detail);
        }
    }
    if (missing.isEmpty()) return {};
    return QStringLiteral("术语译名不一致：") + missing.join(QStringLiteral("；"));
}

QVector<SakuraTranslator::TermBinding> SakuraTranslator::termBindings(const ChunkTask &task) const
{
    if (task.protectedSources.isEmpty()) return {};
    auto terms = relevantTerms(task);
    std::sort(terms.begin(), terms.end(), [](const GlossaryTerm &a, const GlossaryTerm &b) {
        return a.source.size() != b.source.size() ? a.source.size() > b.source.size() : a.source < b.source;
    });
    QString prefix = QStringLiteral("ZXQTERM");
    QString occupied = task.text;
    for (const auto &term : terms) occupied += term.target;
    while (occupied.contains(prefix)) prefix += QLatin1Char('Z');
    QVector<TermBinding> bindings;
    for (const auto &term : terms) {
        const QString target = singleLine(term.target);
        if (!task.protectedSources.contains(term.source) || term.source == target
            || term.source.contains(QLatin1Char('\n')) || term.source.contains(QLatin1Char('\r'))
            || term.source.contains(QStringLiteral("\\n")) || target.contains(QStringLiteral("\\n"))) continue;
        bindings.append({term.source, target, prefix + QString::number(bindings.size()).rightJustified(3, QLatin1Char('0')) + QStringLiteral("ZXQ")});
    }
    return bindings;
}

QString SakuraTranslator::modelSource(const ChunkTask &task) const
{
    const auto bindings = termBindings(task);
    if (bindings.isEmpty()) return task.text;
    const auto terms = relevantTerms(task);
    QString result;
    for (int position = 0; position < task.text.size();) {
        int consumed = 0;
        for (const auto &term : terms) {
            if (task.text.indexOf(term.source, position) != position) continue;
            QString value = term.source;
            for (const auto &binding : bindings)
                if (binding.source == term.source) { value = binding.marker; break; }
            result += value;
            consumed = term.source.size();
            break;
        }
        if (consumed == 0) result += task.text.at(position);
        position += qMax(1, consumed);
    }
    return result;
}

QString SakuraTranslator::restoreTerms(const ChunkTask &task, QString *translation) const
{
    const auto bindings = termBindings(task);
    if (bindings.isEmpty()) return {};
    const auto sourceLines = modelSource(task).split(QLatin1Char('\n'));
    const auto outputLines = translation->split(QLatin1Char('\n'));
    if (sourceLines.size() != outputLines.size()) return QStringLiteral("术语占位符行数不一致");
    for (const auto &binding : bindings) {
        for (int i = 0; i < sourceLines.size(); ++i)
            if (sourceLines[i].count(binding.marker) != outputLines[i].count(binding.marker))
                return QStringLiteral("术语占位符缺失、重复或跨行：%1").arg(binding.marker);
    }
    // Restore only reserved markers created from exact Japanese spans. No
    // generated Chinese name or prose is searched for or guessed at here.
    QString restored = *translation;
    for (const auto &binding : bindings) restored.replace(binding.marker, binding.target);
    const QString prefix = bindings.first().marker.left(bindings.first().marker.size() - 6);
    if (restored.contains(prefix)) return QStringLiteral("译文包含未知术语占位符");
    *translation = restored;
    return {};
}

QString SakuraTranslator::userPrompt(const ChunkTask &task) const
{
    QStringList dictionary;
    const auto bindings = termBindings(task);
    for (const auto &term : relevantTerms(task)) {
        QString source = singleLine(term.source), target = singleLine(term.target);
        QString protectionNote;
        for (const auto &binding : bindings) {
            if (binding.source != term.source) continue;
            // Keep dictionary semantics attached to the opaque marker, so
            // gender / relationship notes still apply during recovery.
            protectionNote = QStringLiteral("受保护术语，原文：") + source;
            if (!term.type.trimmed().isEmpty()) protectionNote += QStringLiteral("；类型：") + singleLine(term.type);
            source = target = binding.marker;
            break;
        }
        QString line = source + QStringLiteral("->") + target;
        if (!protectionNote.isEmpty()) line += QStringLiteral(" #") + protectionNote;
        if (!term.note.trimmed().isEmpty()) line += (protectionNote.isEmpty() ? QStringLiteral(" #") : QStringLiteral("；")) + singleLine(term.note);
        dictionary.append(line);
    }
    QString prompt;
    if (!dictionary.isEmpty())
        prompt = QStringLiteral("根据以下术语表（可以为空）：\n") + dictionary.join(QLatin1Char('\n')) + QLatin1Char('\n');
    prompt += QStringLiteral("翻译要求：作者后记与读者互动也须完整翻译；每个非空原文行对应一个译文行，保留符号和行内\\n。"
        "术语译名逐字使用，不另行音译。联系上下文确认人物、指代、否定、条件和数量。"
        "原文省略主语时译文也优先省略，不猜测人物性别或擅自补入他、她等代词。准确区分动作发出者、接受者及动作方向，不增加原文未发生的后续动作。"
        "未说完的短对白要结合相邻对白理解，不孤立猜同音字；抽象指代须结合话题理解，不一律译为地点。"
        "遇到惯用语或比喻，按上下文把实际意思直白说出来，不套用中文成语。只输出当前原文的译文，不解释。\n");
    if (!task.feedback.isEmpty()) prompt += QStringLiteral("本次校正要求：") + task.feedback + QLatin1Char('\n');
    if (!task.protectedSources.isEmpty()) prompt += QStringLiteral("正文中的ZXQTERM数字ZXQ（或ZXQTERM后带Z的同类标记）是不可改写的占位符，请逐字保留，每次出现都保留，不翻译、不省略、不替换成人名；其它日文正常翻译。只输出译文。\n");
    prompt += dictionary.isEmpty() ? QStringLiteral("将下面的日文文本翻译成中文：")
                                  : QStringLiteral("将下面的日文文本根据对应关系和备注翻译成中文：");
    return prompt + modelSource(task);
}

QVector<SakuraTranslator::ChunkTask> SakuraTranslator::chunksForText(
    int fileIndex, const QString &text, const QString &separator, int depth, int limit) const
{
    QVector<ChunkTask> result;
    QString current;
    QString before = separator;
    QString pending;
    const auto flush = [&]() {
        if (current.isEmpty()) return;
        result.append({fileIndex, current, before, 0, depth});
        current.clear();
    };
    const auto keepBriefDialogue = [&](const QString &nextPart) {
        // A small unfinished reply can depend on the next exchange. Move the
        // last brief reply and its following lines together, without overlap,
        // rather than isolating it at a size / recovery boundary.
        int end = current.size(), seen = 0, moveFrom = -1;
        while (end > 0 && seen < 3) {
            const int start = current.lastIndexOf(QLatin1Char('\n'), end - 1) + 1;
            const QString line = current.mid(start, end - start).trimmed();
            if (!line.isEmpty()) {
                ++seen;
                if (line.size() <= 14 && ((line.startsWith(QChar(0x300C)) && line.endsWith(QChar(0x300D)))
                    || (line.startsWith(QChar(0x300E)) && line.endsWith(QChar(0x300F))))) moveFrom = start;
            }
            end = start - 1;
        }
        if (moveFrom <= 0) return false;
        const QString tail = current.mid(moveFrom);
        if (tail.size() + pending.size() + nextPart.size() > limit
            || tail.count(QLatin1Char('\n')) + pending.count(QLatin1Char('\n')) >= kChunkLines) return false;
        int prefixEnd = moveFrom;
        while (prefixEnd > 0 && current.at(prefixEnd - 1).isSpace()) --prefixEnd;
        if (prefixEnd == 0) return false;
        const QString between = current.mid(prefixEnd, moveFrom - prefixEnd);
        current.truncate(prefixEnd);
        flush();
        before = between;
        current = tail;
        return true;
    };
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        if (lineIndex) pending += QLatin1Char('\n');
        QString line = lines.at(lineIndex);
        if (line.trimmed().isEmpty()) { pending += line; continue; }
        while (!line.isEmpty()) {
            int cut = boundaryBefore(line, limit);
            const auto glossary = m_glossaries.constFind(novelRoot(m_files[fileIndex].path));
            if (cut < line.size() && glossary != m_glossaries.constEnd()) {
                for (const auto &term : glossary->termsForText(line)) {
                    for (int start = line.indexOf(term.source); start >= 0 && start < cut;
                         start = line.indexOf(term.source, start + term.source.size())) {
                        const int end = start + term.source.size();
                        if (end > cut) cut = start > 0 ? start : end;
                    }
                }
            }
            const QString part = line.left(cut);
            line.remove(0, cut);
            if (!current.isEmpty() && (current.size() + pending.size() + part.size() > limit
                || current.count(QLatin1Char('\n')) + pending.count(QLatin1Char('\n')) >= kChunkLines)) {
                if (!keepBriefDialogue(part)) flush();
            }
            if (current.isEmpty()) {
                if (!result.isEmpty()) before = pending;
                current = part;
            } else current += pending + part;
            pending.clear();
        }
    }
    flush();
    return result;
}

void SakuraTranslator::buildTaskQueue()
{
    m_tasks.clear();
    for (int i = 0; i < m_files.size(); ++i) {
        QFile file(m_files[i].path);
        QString error = m_glossaryErrors.value(novelRoot(file.fileName()));
        if (error.isEmpty() && !file.open(QIODevice::ReadOnly))
            error = QStringLiteral("无法读取原文：%1").arg(file.errorString());
        QString text;
        if (error.isEmpty()) {
            TxtExporter::splitChapterDocument(QString::fromUtf8(file.readAll()), &m_files[i].header, &text);
            text = text.trimmed();
            if (text.isEmpty()) error = QStringLiteral("原文正文为空");
        }
        if (!error.isEmpty()) { finishCurrentFile(i, error); continue; }
        m_tasks += chunksForText(i, text, {}, 0, kChunkChars);
    }
}

void SakuraTranslator::translateFiles(const QVector<FileJob> &jobs)
{
    ++m_runId;
    m_running = false;
    m_http.abortAll();
    m_files.clear();
    for (const auto &job : jobs) m_files.append({job.path, job.outputDir, {}, {}, false, false});
    m_success = m_failed = m_currentTask = 0;
    m_running = true;
    loadGlossaries();
    buildTaskQueue();
    for (const auto &file : m_files)
        if (!file.finished) emit fileStatusChanged(file.path, QStringLiteral("WAITING"));
    emit progressChanged(0, m_tasks.size(), QStringLiteral("开始 Sakura 翻译…"));
    scheduleNext();
}

void SakuraTranslator::abort()
{
    if (!m_running) return;
    m_running = false;
    ++m_runId;
    m_http.abortAll();
    emit progressChanged(m_currentTask, m_tasks.size(), QStringLiteral("翻译已中止"));
}

void SakuraTranslator::scheduleNext(int delayMs)
{
    const auto run = m_runId;
    QTimer::singleShot(delayMs, this, [this, run]() {
        if (m_running && run == m_runId) translateNextChunk();
    });
}

void SakuraTranslator::translateNextChunk()
{
    if (!m_running) return;
    if (m_currentTask >= m_tasks.size()) {
        m_running = false;
        emit progressChanged(m_tasks.size(), m_tasks.size(), QStringLiteral("翻译完成"));
        emit translationFinished(m_success, m_failed);
        return;
    }
    auto &task = m_tasks[m_currentTask];
    auto &file = m_files[task.fileIndex];
    if (!file.started) {
        file.started = true;
        emit fileStatusChanged(file.path, QStringLiteral("TRANSLATING"));
    }
    if (!m_running) return;
    ++task.attempts;
    emit progressChanged(m_currentTask, m_tasks.size(), QStringLiteral("正在翻译: %1 (%2/%3)")
        .arg(QFileInfo(file.path).fileName()).arg(m_currentTask + 1).arg(m_tasks.size()));
    if (!m_running) return;
    const QJsonArray messages{
        QJsonObject{{"role", "system"}, {"content", systemPrompt()}},
        QJsonObject{{"role", "user"}, {"content", userPrompt(task)}}};
    // GGUF chat template 负责特殊符号；不在正文重复嵌套 ChatML。
    const QJsonObject body{{"model", m_modelAlias}, {"messages", messages},
        {"temperature", 0.1}, {"top_p", 0.3}, {"repeat_penalty", 1.0},
        {"max_tokens", 3072}, {"stream", false}, {"reasoning_format", "none"}};
    const QVariantMap context{{"run", QVariant::fromValue(m_runId)}, {"task", m_currentTask},
                              {"request", QVariant::fromValue(++m_requestId)}};
    m_http.postJson(m_serverUrl + QStringLiteral("/v1/chat/completions"),
                    QJsonDocument(body).toJson(QJsonDocument::Compact), context);
}

bool SakuraTranslator::splitCurrentChunk()
{
    const auto task = m_tasks.at(m_currentTask);
    if (task.depth >= kMaxSplitDepth || task.text.size() < 80) return false;
    auto parts = chunksForText(task.fileIndex, task.text, task.separator, task.depth + 1,
                              qMax(1, int(task.text.size()) / 2));
    if (parts.size() < 2) return false;
    for (auto &part : parts) {
        part.protectedSources = task.protectedSources;
    }
    m_tasks.removeAt(m_currentTask);
    for (int i = 0; i < parts.size(); ++i) m_tasks.insert(m_currentTask + i, parts.at(i));
    emit progressChanged(m_currentTask, m_tasks.size(), QStringLiteral("当前片段超限或格式不完整，缩小分块后重译"));
    scheduleNext();
    return true;
}

void SakuraTranslator::onRequestFinished(const QString &url, const QVariant &context,
    const QByteArray &data, bool ok, const QString &error)
{
    Q_UNUSED(url);
    const auto request = context.toMap();
    if (!m_running || request.value("run").toULongLong() != m_runId
        || request.value("request").toULongLong() != m_requestId
        || request.value("task").toInt() != m_currentTask) return;
    const auto task = m_tasks.at(m_currentTask);
    QString translated;
    QStringList missingSources;
    QString issue = ok ? QString() : error;
    bool capacity = !ok && (error.contains(QStringLiteral("context"), Qt::CaseInsensitive)
                            || error.contains(QStringLiteral("too large"), Qt::CaseInsensitive));
    if (ok) {
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(data, &parseError);
        const auto choices = document.object().value("choices").toArray();
        if (parseError.error != QJsonParseError::NoError || choices.isEmpty()) {
            issue = QStringLiteral("服务端响应缺少有效 choices");
        } else {
            const auto choice = choices.first().toObject();
            const auto message = choice.value("message").toObject();
            const QString reason = choice.value("finish_reason").toString();
            capacity = reason == QStringLiteral("length");
            if (reason != QStringLiteral("stop")) issue = QStringLiteral("响应未完整结束：%1").arg(reason);
            else if (!message.value("refusal").toString().isEmpty() || !message.value("tool_calls").toArray().isEmpty())
                issue = QStringLiteral("模型未返回正文译文");
            else {
                translated = message.value("content").toString().trimmed();
                issue = validateTranslation(task.text, &translated);
                if (issue.isEmpty()) issue = restoreTerms(task, &translated);
                if (issue.isEmpty()) issue = termIssue(task, translated, &missingSources);
            }
        }
    }
    if (!issue.isEmpty()) {
        auto &retry = m_tasks[m_currentTask];
        retry.protectedSources.append(missingSources);
        retry.protectedSources.removeDuplicates();
        const bool brokenMarker = issue.startsWith(QStringLiteral("术语占位符"))
            || issue == QStringLiteral("译文包含未知术语占位符");
        // A broken protected span needs a smaller source window. Repeating
        // the same large marker request wastes the remaining retry budget.
        if ((capacity || brokenMarker) && splitCurrentChunk()) return;
        if (task.attempts < kMaxAttempts) {
            retry.feedback = (missingSources.isEmpty() ? issue : QStringLiteral("术语译名不一致，本次以不可改写的占位符保护相关术语"))
                + QStringLiteral("。请重新完整翻译当前全部原文；不要省略后记或对白，不要输出空内容。"
                "保持原文行数，给定译名必须逐字使用，每次出现该人名都保留正确译名，不用代词或其它音译代替。只输出修正后的译文。");
            emit progressChanged(m_currentTask, m_tasks.size(), QStringLiteral("Sakura 重试 %1/%2：%3")
                .arg(task.attempts).arg(kMaxAttempts - 1).arg(issue.left(240)));
            scheduleNext(ok ? 0 : task.attempts * 500);
        } else if (ok && splitCurrentChunk()) return;
        else failCurrentFile(issue);
        return;
    }
    m_files[task.fileIndex].translated += task.separator + translated;
    ++m_currentTask;
    if (m_currentTask == m_tasks.size() || m_tasks[m_currentTask].fileIndex != task.fileIndex)
        finishCurrentFile(task.fileIndex);
    scheduleNext();
}

void SakuraTranslator::failCurrentFile(const QString &error)
{
    const int fileIndex = m_tasks[m_currentTask].fileIndex;
    while (m_currentTask < m_tasks.size() && m_tasks[m_currentTask].fileIndex == fileIndex) ++m_currentTask;
    finishCurrentFile(fileIndex, error);
    scheduleNext();
}

void SakuraTranslator::finishCurrentFile(int fileIndex, const QString &error)
{
    auto &file = m_files[fileIndex];
    if (file.finished) return;
    QString issue = error;
    const QString outputPath = QDir(file.outputDir).filePath(QFileInfo(file.path).fileName());
    if (issue.isEmpty() && file.outputDir.trimmed().isEmpty()) issue = QStringLiteral("未指定输出目录");
    if (issue.isEmpty() && QFileInfo(outputPath).absoluteFilePath() == QFileInfo(file.path).absoluteFilePath())
        issue = QStringLiteral("输出路径与原文相同，拒绝覆盖原文");
    if (issue.isEmpty() && !QDir().mkpath(file.outputDir)) issue = QStringLiteral("无法创建输出目录");
    if (issue.isEmpty()) {
        QSaveFile output(outputPath);
        const QByteArray content = QByteArray("\xEF\xBB\xBF")
            + TxtExporter::mergeChapterDocument(file.header, file.translated).toUtf8();
        if (!output.open(QIODevice::WriteOnly) || output.write(content) != content.size() || !output.commit())
            issue = QStringLiteral("无法完整保存译文：%1").arg(output.errorString());
    }
    file.finished = true;
    if (issue.isEmpty()) ++m_success; else ++m_failed;
    emit fileStatusChanged(file.path, issue.isEmpty() ? QStringLiteral("COMPLETED") : QStringLiteral("FAILED"));
    emit progressChanged(m_currentTask, m_tasks.size(), issue.isEmpty()
        ? QStringLiteral("已保存译文: %1").arg(outputPath)
        : QStringLiteral("翻译失败: %1：%2").arg(QFileInfo(file.path).fileName(), issue));
}
