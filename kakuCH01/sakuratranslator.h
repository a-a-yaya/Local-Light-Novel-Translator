#ifndef SAKURATRANSLATOR_H
#define SAKURATRANSLATOR_H

#include <QObject>
#include <QStringList>
#include <QVector>
#include <QHash>
#include "httpclient.h"
#include "glossarymanager.h"

struct FileJob { QString path; QString outputDir; };

// Sakura 专用流水线，不复用其它引擎的 Prompt、重试或状态。
class SakuraTranslator : public QObject
{
    Q_OBJECT
public:
    explicit SakuraTranslator(QObject *parent = nullptr);
    void setServerUrl(const QString &url);
    void setModelAlias(const QString &alias);
    void setSystemPrompt(const QString &prompt);
    void setCjdlDir(const QString &dir);
    void setGenerateBilingual(bool enabled);
    void translateFiles(const QVector<FileJob> &jobs);
    void abort();
signals:
    void progressChanged(int current, int total, const QString &message);
    void fileStatusChanged(const QString &path, const QString &status);
    void translationFinished(int successCount, int failedCount);
private slots:
    void onRequestFinished(const QString &url, const QVariant &context,
                           const QByteArray &data, bool ok, const QString &error);
private:
    struct ChunkTask {
        int fileIndex = -1;
        QString text;
        QString separator; // 本块前的原始换行；长行拆块之间为空。
        int attempts = 0;
        int depth = 0;
        QString feedback;
        QStringList protectedSources;
    };
    struct FileState {
        QString path;
        QString outputDir;
        QString header;
        QString translated;
        bool started = false;
        bool finished = false;
    };
    struct TermBinding { QString source; QString target; QString marker; };
    void buildTaskQueue();
    QVector<ChunkTask> chunksForText(int fileIndex, const QString &text,
                                    const QString &separator, int depth, int limit) const;
    void loadGlossaries();
    QString novelRoot(const QString &path) const;
    QList<GlossaryTerm> relevantTerms(const ChunkTask &task) const;
    QString termIssue(const ChunkTask &task, const QString &translation, QStringList *missingSources) const;
    QVector<TermBinding> termBindings(const ChunkTask &task) const;
    QString modelSource(const ChunkTask &task) const;
    QString restoreTerms(const ChunkTask &task, QString *translation) const;
    QString userPrompt(const ChunkTask &task) const;
    QString systemPrompt() const;
    void scheduleNext(int delayMs = 0);
    void translateNextChunk();
    bool splitCurrentChunk();
    void failCurrentFile(const QString &error);
    void finishCurrentFile(int fileIndex, const QString &error = QString());
    HttpClient m_http;
    QString m_serverUrl = QStringLiteral("http://127.0.0.1:8080");
    QString m_modelAlias = QStringLiteral("sakura");
    QString m_systemPrompt;
    QVector<FileState> m_files;
    QVector<ChunkTask> m_tasks;
    QHash<QString, GlossaryManager> m_glossaries;
    QHash<QString, QString> m_glossaryErrors;
    quint64 m_runId = 0;
    quint64 m_requestId = 0;
    int m_currentTask = 0;
    int m_success = 0;
    int m_failed = 0;
    bool m_running = false;
};
#endif
