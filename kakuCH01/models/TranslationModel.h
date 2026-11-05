#ifndef TRANSLATIONMODEL_H
#define TRANSLATIONMODEL_H

#include <QAbstractListModel>
#include <QStringList>

// 翻译队列模型：展示待翻译文件及其状态
class TranslationModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int fileCount READ fileCount NOTIFY fileCountChanged)
public:
    enum Roles {
        FileNameRole = Qt::UserRole + 1,
        PathRole,
        StatusRole,       // WAITING / TRANSLATING / COMPLETED / FAILED / CANCELLED
        ProgressRole,     // 0-100
        SelectedRole,
        ErrorRole,
        TranslatedRole,
        // 术语表提取模式下的独立勾选状态（与翻译勾选互不影响）
        GlossarySelectedRole
    };
    Q_ENUM(Roles)

    explicit TranslationModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    QHash<int, QByteArray> roleNames() const override;

    void addFiles(const QStringList &paths);
    void addFolder(const QString &dir);
    void removeSelected(bool glossaryMode = false);
    void setSelection(bool selected, bool glossaryMode = false, bool invert = false);
    void clear();
    QStringList selectedPaths() const;
    QStringList filePaths() const;
    int fileCount() const { return m_entries.size(); }

    // 由 AppController 驱动状态
    void setFileStatus(const QString &path, const QString &status);
    void setFileTranslated(const QString &path, bool translated);
    void setFileProgress(const QString &path, int progress);
    // 术语表提取专用勾选：不受 translated 限制（提取只读原文）
    void setGlossarySelected(const QString &path, bool selected);
    QStringList glossarySelectedPaths() const;
    void resetStatuses();

signals:
    void fileCountChanged();

private:
    struct Entry
    {
        QString path;
        QString status = QStringLiteral("WAITING");
        int progress = 0;
        bool selected = true;
        bool translated = false;
        bool glossarySelected = true;
    };

    QVector<Entry> m_entries;
    int indexOf(const QString &path) const;
};

#endif // TRANSLATIONMODEL_H
