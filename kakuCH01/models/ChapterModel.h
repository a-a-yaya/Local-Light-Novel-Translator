#ifndef CHAPTERMODEL_H
#define CHAPTERMODEL_H

#include <QAbstractListModel>
#include "novelwork.h"

// 章节列表模型：把 NovelWork 的章节数据提供给 QML 的 ListView
class ChapterModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        DateRole,
        SelectedRole,
        DownloadedRole,
        StatusRole,
        EpisodeIdRole,
        EpisodeUrlRole
    };
    Q_ENUM(Roles)

    explicit ChapterModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    QHash<int, QByteArray> roleNames() const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    // 用解析结果填充章节列表
    void setWork(const NovelWork &work);
    void clear();
    int chapterCount() const { return m_chapters.size(); }

    // 返回当前勾选的章节
    QVector<Chapter> selectedChapters() const;
    // 按行号取章节（不受勾选状态影响）
    Chapter chapterAt(int row) const;
    // 标题搜索过滤（仅影响显示，不影响勾选）
    void setFilter(const QString &filter);
    QString filter() const { return m_filter; }
    void setAllSelected(bool selected);
    void invertSelection();

    // 按 episodeId 更新章节状态（"未下载"/"下载中"/"已下载"/"下载失败"）
    void setChapterStatus(const QString &episodeId, const QString &status);
    // 清空所有下载状态
    void resetStatuses();

private:
    void rebuildFilter();
    QVector<Chapter> m_chapters;
    QVector<bool> m_selected;
    QHash<QString, QString> m_status; // episodeId -> 状态
    QString m_filter;                 // 当前搜索词
    QVector<int> m_filteredIndex;     // 显示行 -> 真实行
};

#endif // CHAPTERMODEL_H


