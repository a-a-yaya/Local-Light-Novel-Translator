#ifndef GLOSSARYMANAGER_H
#define GLOSSARYMANAGER_H

#include "glossary.h"

#include <QString>
#include <QList>

// 负责读写 DOWNLOAD/<workId>/glossary.json，并提供术语增删改查、
// 相关术语筛选、Prompt 文本构造与稳定 Hash 计算。
// 不包含任何“自动候选发现”逻辑，术语只由用户手动确认写入。
class GlossaryManager
{
public:
    // 一本小说可以有两份术语表：人工术语表（用户维护）与 Qwen 术语表（模型生成）。
    // 翻译时只会使用其中一份，由 activeKindFor() 决定。
    static const QString kManualKind;   // "manual"
    static const QString kQwenKind;     // "qwen"

    static QString fileNameForKind(const QString &kind);
    static QString kindForFileName(const QString &fileName);
    // 翻译时启用的术语表种类，持久化在 QSettings 里，默认 "manual"。
    // novelKey 用「站点/作品」相对标识（旧版扁平目录就是作品目录名）；不同站点的
    // 作品 id 可能撞号，所以键里必须带站点。legacyWorkId 用来兼容早期只按
    // 作品目录名存的键：新键没有值时回退读旧键，用户原来的选择不会丢。
    static QString activeKindFor(const QString &novelKey,
                                 const QString &legacyWorkId = QString());
    static void setActiveKindFor(const QString &novelKey, const QString &kind);

    GlossaryManager();
    explicit GlossaryManager(const QString &novelRootPath,
                             const QString &fileName = QStringLiteral("glossary.json"));

    void setNovelRootPath(const QString &novelRootPath);
    QString novelRootPath() const;
    void setFileName(const QString &fileName);
    QString fileName() const;
    QString glossaryPath() const;

    bool load();
    bool save() const;

    QList<GlossaryTerm> terms() const;
    // Presentation only: type groups and related original names stay together.
    QList<GlossaryTerm> displayTerms() const;
    bool contains(const QString &source) const;
    GlossaryTerm findTerm(const QString &source) const;

    // 已存在同名 source 时返回 false，防止重复添加。
    bool addTerm(const GlossaryTerm &term);
    // 允许修改 source；若 source 改变则按“删旧加新”处理。
    bool updateTerm(const QString &source, const GlossaryTerm &term);
    bool removeTerm(const QString &source);
    bool setEnabled(const QString &source, bool enabled);

    QList<GlossaryTerm> termsForText(const QString &text) const;
    QString buildPromptText(const QString &text) const;
    QString calculateHash() const;

private:
    int indexOf(const QString &source) const;

    QString m_novelRootPath;
    QString m_fileName;
    QList<GlossaryTerm> m_terms;
};

#endif // GLOSSARYMANAGER_H
