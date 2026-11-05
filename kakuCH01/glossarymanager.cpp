#include "glossarymanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QSettings>
#include <QDebug>
#include <QVector>
#include <QHash>
#include <algorithm>

namespace {
constexpr int kGlossaryVersion = 1;

// QSettings 中保存“翻译时启用哪份术语表”的键。
QString activeKindKey(const QString &workId)
{
    return QStringLiteral("glossary/active/%1").arg(workId);
}

QJsonObject termToJson(const GlossaryTerm &term)
{
    QJsonObject o;
    o.insert(QStringLiteral("source"), term.source);
    o.insert(QStringLiteral("target"), term.target);
    o.insert(QStringLiteral("type"), term.type);
    o.insert(QStringLiteral("note"), term.note);
    o.insert(QStringLiteral("enabled"), term.enabled);
    o.insert(QStringLiteral("origin"), term.origin.isEmpty()
                                      ? QStringLiteral("manual") : term.origin);
    return o;
}

GlossaryTerm termFromJson(const QJsonObject &o)
{
    GlossaryTerm term;
    term.source = o.value(QStringLiteral("source")).toString();
    term.target = o.value(QStringLiteral("target")).toString();
    term.type = o.value(QStringLiteral("type")).toString();
    term.note = o.value(QStringLiteral("note")).toString();
    term.enabled = o.value(QStringLiteral("enabled")).toBool(true);
    term.origin = o.value(QStringLiteral("origin")).toString(QStringLiteral("manual"));
    return term;
}
} // namespace

const QString GlossaryManager::kManualKind = QStringLiteral("manual");
const QString GlossaryManager::kQwenKind = QStringLiteral("qwen");

QString GlossaryManager::fileNameForKind(const QString &kind)
{
    return kind == kQwenKind ? QStringLiteral("glossary_qwen.json")
                             : QStringLiteral("glossary.json");
}

QString GlossaryManager::kindForFileName(const QString &fileName)
{
    return fileName.contains(QStringLiteral("qwen"), Qt::CaseInsensitive) ? kQwenKind
                                                                         : kManualKind;
}

QString GlossaryManager::activeKindFor(const QString &novelKey,
                                       const QString &legacyWorkId)
{
    const QString key = novelKey.trimmed();
    const QString legacy = legacyWorkId.trimmed();
    if (key.isEmpty() && legacy.isEmpty())
        return kManualKind;

    QSettings settings;
    const QVariant value = key.isEmpty() ? QVariant()
                                         : settings.value(activeKindKey(key));
    if (value.isValid()) {
        const QString kind = value.toString();
        return kind == kQwenKind ? kQwenKind : kManualKind;
    }
    if (!legacy.isEmpty()) {
        const QVariant old = settings.value(activeKindKey(legacy));
        if (old.isValid()) {
            const QString kind = old.toString();
            return kind == kQwenKind ? kQwenKind : kManualKind;
        }
    }
    return kManualKind;
}

void GlossaryManager::setActiveKindFor(const QString &novelKey, const QString &kind)
{
    const QString key = novelKey.trimmed();
    if (key.isEmpty())
        return;
    QSettings settings;
    settings.setValue(activeKindKey(key),
                      kind == kQwenKind ? kQwenKind : kManualKind);
}

GlossaryManager::GlossaryManager()
    : m_fileName(QStringLiteral("glossary.json"))
{
}

GlossaryManager::GlossaryManager(const QString &novelRootPath, const QString &fileName)
    : m_novelRootPath(novelRootPath)
    , m_fileName(fileName.isEmpty() ? QStringLiteral("glossary.json") : fileName)
{
}

void GlossaryManager::setNovelRootPath(const QString &novelRootPath)
{
    m_novelRootPath = novelRootPath;
}

QString GlossaryManager::novelRootPath() const
{
    return m_novelRootPath;
}

void GlossaryManager::setFileName(const QString &fileName)
{
    if (!fileName.isEmpty())
        m_fileName = fileName;
}

QString GlossaryManager::fileName() const
{
    return m_fileName;
}

QString GlossaryManager::glossaryPath() const
{
    if (m_novelRootPath.isEmpty())
        return QString();
    return QDir(m_novelRootPath).filePath(m_fileName);
}

int GlossaryManager::indexOf(const QString &source) const
{
    for (int i = 0; i < m_terms.size(); ++i) {
        if (m_terms[i].source == source)
            return i;
    }
    return -1;
}

bool GlossaryManager::load()
{
    m_terms.clear();
    const QString path = glossaryPath();
    if (path.isEmpty())
        return false;

    QFile f(path);
    if (!f.exists()) {
        // 首次使用：术语表不存在，等价于空术语表。
        return false;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "[Glossary] 无法读取术语表:" << path;
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    f.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[Glossary] 术语表 JSON 损坏:" << path << parseError.errorString();
        return false;
    }

    const QJsonArray arr = doc.object().value(QStringLiteral("terms")).toArray();
    for (const QJsonValue &v : arr) {
        if (!v.isObject())
            continue;
        const GlossaryTerm term = termFromJson(v.toObject());
        if (term.source.trimmed().isEmpty())
            continue; // 跳过错误条目
        m_terms.append(term);
    }
    return true;
}

bool GlossaryManager::save() const
{
    const QString path = glossaryPath();
    if (path.isEmpty())
        return false;

    QDir dir(m_novelRootPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return false;

    QJsonArray arr;
    for (const GlossaryTerm &term : m_terms)
        arr.append(termToJson(term));

    QJsonObject root;
    root.insert(QStringLiteral("version"), kGlossaryVersion);
    root.insert(QStringLiteral("terms"), arr);

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.commit();
}

QList<GlossaryTerm> GlossaryManager::terms() const
{
    return m_terms;
}

QList<GlossaryTerm> GlossaryManager::displayTerms() const
{
    auto normalized = [](const QString &text) {
        QString key;
        for (QChar ch : text.normalized(QString::NormalizationForm_KC).toCaseFolded()) {
            if (ch.unicode() >= 0x3041 && ch.unicode() <= 0x3096)
                ch = QChar(ch.unicode() + 0x60); // Hiragana and katakana compare alike.
            if (ch.isLetterOrNumber()) key += ch;
        }
        return key;
    };
    auto category = [](QString type) {
        type = type.trimmed();
        if (type == QStringLiteral("人名") || type == QStringLiteral("角色")) type = QStringLiteral("人物");
        if (type == QStringLiteral("地点") || type == QStringLiteral("場所")) type = QStringLiteral("地名");
        if (type == QStringLiteral("技能名")) type = QStringLiteral("技能");
        if (type == QStringLiteral("称呼")) type = QStringLiteral("称号");
        return type.isEmpty() ? QStringLiteral("其他") : type;
    };
    auto related = [](const QString &a, const QString &b) {
        if (a.isEmpty() || b.isEmpty()) return false;
        if (a == b) return true;
        if (qMin(a.size(), b.size()) >= 2 && (a.contains(b) || b.contains(a))) return true;
        // A single inserted, removed or changed character also keeps spelling variants nearby.
        if (qMin(a.size(), b.size()) < 3 || qAbs(a.size() - b.size()) > 1) return false;
        int i = 0, j = 0, edits = 0;
        while (i < a.size() && j < b.size()) {
            if (a[i] == b[j]) { ++i; ++j; continue; }
            if (++edits > 1) return false;
            if (a.size() >= b.size()) ++i;
            if (b.size() >= a.size()) ++j;
        }
        return edits + (i < a.size() || j < b.size() ? 1 : 0) <= 1;
    };
    struct Entry { GlossaryTerm term; QString type, key, group, anchor; int parent; };
    QVector<Entry> entries;
    for (const auto &term : m_terms)
        entries.append({term, category(term.type), normalized(term.source), {}, {}, int(entries.size())});
    auto root = [&](int i) {
        while (entries[i].parent != i) {
            entries[i].parent = entries[entries[i].parent].parent;
            i = entries[i].parent;
        }
        return i;
    };
    for (int i = 0; i < entries.size(); ++i)
        for (int j = 0; j < i; ++j)
            if (entries[i].type == entries[j].type && related(entries[i].key, entries[j].key))
                entries[root(i)].parent = root(j);
    QHash<int, QString> groupKeys;
    for (int i = 0; i < entries.size(); ++i) {
        const int group = root(i);
        if (!groupKeys.contains(group) || entries[i].key < groupKeys[group]) groupKeys[group] = entries[i].key;
        entries[i].anchor = entries[i].key;
        for (const auto &other : entries)
            if (other.type == entries[i].type && other.key.size() >= 2
                && other.key.size() < entries[i].anchor.size() && entries[i].key.contains(other.key))
                entries[i].anchor = other.key;
    }
    for (int i = 0; i < entries.size(); ++i) entries[i].group = groupKeys[root(i)];
    const QStringList types{QStringLiteral("人物"), QStringLiteral("地名"), QStringLiteral("组织"),
        QStringLiteral("称号"), QStringLiteral("技能"), QStringLiteral("物品"), QStringLiteral("其他")};
    auto typeOrder = [&](const QString &type) { const int i = types.indexOf(type); return i < 0 ? types.size() : i; };
    std::stable_sort(entries.begin(), entries.end(), [&](const Entry &a, const Entry &b) {
        if (typeOrder(a.type) != typeOrder(b.type)) return typeOrder(a.type) < typeOrder(b.type);
        if (a.type != b.type) return a.type < b.type;
        if (a.group != b.group) return a.group < b.group;
        if (a.anchor != b.anchor) return a.anchor < b.anchor;
        if (a.key.size() != b.key.size()) return a.key.size() < b.key.size();
        if (a.key != b.key) return a.key < b.key;
        return a.term.source < b.term.source;
    });
    QList<GlossaryTerm> sorted;
    for (const auto &entry : entries) sorted.append(entry.term);
    return sorted;
}

bool GlossaryManager::contains(const QString &source) const
{
    return indexOf(source) >= 0;
}

GlossaryTerm GlossaryManager::findTerm(const QString &source) const
{
    const int i = indexOf(source);
    if (i < 0)
        return GlossaryTerm();
    return m_terms[i];
}

bool GlossaryManager::addTerm(const GlossaryTerm &term)
{
    if (term.source.trimmed().isEmpty())
        return false;
    if (indexOf(term.source) >= 0)
        return false;
    m_terms.append(term);
    return true;
}

bool GlossaryManager::updateTerm(const QString &source, const GlossaryTerm &term)
{
    if (source.trimmed().isEmpty() || term.source.trimmed().isEmpty())
        return false;

    const int oldIndex = indexOf(source);
    if (oldIndex < 0)
        return false;

    if (term.source == source) {
        m_terms[oldIndex] = term;
        return true;
    }

    const int existing = indexOf(term.source);
    if (existing >= 0 && existing != oldIndex)
        return false; // 新 source 已存在，避免重复

    m_terms.removeAt(oldIndex);
    m_terms.append(term);
    return true;
}

bool GlossaryManager::removeTerm(const QString &source)
{
    const int i = indexOf(source);
    if (i < 0)
        return false;
    m_terms.removeAt(i);
    return true;
}

bool GlossaryManager::setEnabled(const QString &source, bool enabled)
{
    const int i = indexOf(source);
    if (i < 0)
        return false;
    m_terms[i].enabled = enabled;
    return true;
}

QList<GlossaryTerm> GlossaryManager::termsForText(const QString &text) const
{
    QList<GlossaryTerm> result;
    if (text.isEmpty())
        return result;

    for (const GlossaryTerm &term : m_terms) {
        if (!term.enabled || term.source.trimmed().isEmpty())
            continue;
        if (term.target.trimmed().isEmpty())
            continue;
        if (text.contains(term.source))
            result.append(term);
    }

    // 按 source 长度从长到短排序，减少短词干扰。
    std::sort(result.begin(), result.end(), [](const GlossaryTerm &a, const GlossaryTerm &b) {
        return a.source.size() > b.source.size();
    });
    return result;
}

QString GlossaryManager::buildPromptText(const QString &text) const
{
    const QList<GlossaryTerm> relevant = termsForText(text);
    if (relevant.isEmpty())
        return QString();

    QStringList lines;
    // 这是 Sakura / Qwen 共用的术语表块格式（Murasaki 有自己的一份官方格式，
    // 见 MurasakiTranslator::glossaryPromptForChunk）。
    lines << QStringLiteral("【固定术语】以下术语为本书固定译名。若原文出现这些术语，"
                            "必须优先使用对应中文译名，不得自行修改，并保持全文一致。");
    for (const GlossaryTerm &term : relevant)
        lines << QStringLiteral("%1 = %2").arg(term.source, term.target);
    return lines.join(QLatin1Char('\n'));
}

QString GlossaryManager::calculateHash() const
{
    QList<GlossaryTerm> sorted = m_terms;
    std::sort(sorted.begin(), sorted.end(), [](const GlossaryTerm &a, const GlossaryTerm &b) {
        return a.source < b.source;
    });

    QJsonArray arr;
    for (const GlossaryTerm &term : sorted) {
        QJsonObject o;
        o.insert(QStringLiteral("source"), term.source);
        o.insert(QStringLiteral("target"), term.target);
        o.insert(QStringLiteral("type"), term.type);
        o.insert(QStringLiteral("enabled"), term.enabled);
        arr.append(o);
    }

    const QByteArray json = QJsonDocument(arr).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(
        QCryptographicHash::hash(json, QCryptographicHash::Sha256).toHex());
}
