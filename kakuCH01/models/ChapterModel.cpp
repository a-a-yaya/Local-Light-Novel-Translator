#include "ChapterModel.h"

ChapterModel::ChapterModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void ChapterModel::rebuildFilter()
{
    // 根据 m_filter 重建“显示行 -> 真实行”映射
    m_filteredIndex.clear();
    for (int i = 0; i < m_chapters.size(); ++i) {
        if (m_filter.isEmpty() || m_chapters[i].title.contains(m_filter, Qt::CaseInsensitive))
            m_filteredIndex.append(i);
    }
}

void ChapterModel::setFilter(const QString &filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter;
    beginResetModel();
    rebuildFilter();
    endResetModel();
}

int ChapterModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_filteredIndex.size();
}

QVariant ChapterModel::data(const QModelIndex &index, int role) const
{
    int row = index.row();
    if (!index.isValid() || row < 0 || row >= rowCount())
        return QVariant();

    // 将显示行映射回真实章节行
    const int real = m_filteredIndex[row];
    const Chapter &ch = m_chapters[real];
    switch (role) {
    case TitleRole:
        return ch.title;
    case DateRole:
        return QString();
    case SelectedRole:
        return m_selected[real];
    case StatusRole:
        return m_status.value(ch.episodeId, QStringLiteral("未下载"));
    case DownloadedRole:
        return m_status.value(ch.episodeId) == QStringLiteral("已下载");
    case EpisodeIdRole:
        return ch.episodeId;
    case EpisodeUrlRole:
        return ch.episodeUrl;
    default:
        return QVariant();
    }
}

bool ChapterModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    int row = index.row();
    if (!index.isValid() || row < 0 || row >= rowCount())
        return false;
    if (role == SelectedRole) {
        const int real = m_filteredIndex[row];
        m_selected[real] = value.toBool();
        emit dataChanged(index, index, {SelectedRole});
        return true;
    }
    return false;
}

QHash<int, QByteArray> ChapterModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[TitleRole] = "title";
    roles[DateRole] = "date";
    roles[SelectedRole] = "selected";
    roles[DownloadedRole] = "downloaded";
    roles[StatusRole] = "status";
    roles[EpisodeIdRole] = "episodeId";
    roles[EpisodeUrlRole] = "episodeUrl";
    return roles;
}

Qt::ItemFlags ChapterModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void ChapterModel::setWork(const NovelWork &work)
{
    beginResetModel();
    m_chapters = work.chapters;
    m_selected.fill(true, m_chapters.size());
    m_status.clear();
    m_filter.clear(); // 新作品清空搜索
    rebuildFilter();
    endResetModel();
}

void ChapterModel::clear()
{
    beginResetModel();
    m_chapters.clear();
    m_selected.clear();
    m_status.clear();
    m_filter.clear();
    rebuildFilter();
    endResetModel();
}

QVector<Chapter> ChapterModel::selectedChapters() const
{
    QVector<Chapter> result;
    for (int i = 0; i < m_chapters.size(); ++i) {
        if (m_selected[i])
            result.append(m_chapters[i]);
    }
    return result;
}

void ChapterModel::setAllSelected(bool selected)
{
    m_selected.fill(selected, m_chapters.size());
    if (rowCount() > 0)
        emit dataChanged(index(0), index(rowCount() - 1), {SelectedRole});
}

void ChapterModel::invertSelection()
{
    for (int i = 0; i < m_selected.size(); ++i)
        m_selected[i] = !m_selected[i];
    if (rowCount() > 0)
        emit dataChanged(index(0), index(rowCount() - 1), {SelectedRole});
}

void ChapterModel::setChapterStatus(const QString &episodeId, const QString &status)
{
    m_status.insert(episodeId, status);
    for (int i = 0; i < m_chapters.size(); ++i) {
        if (m_chapters[i].episodeId == episodeId) {
            // 找到真实行后，映射到当前可能存在的显示行
            const int display = m_filteredIndex.indexOf(i);
            if (display >= 0) {
                QModelIndex idx = index(display);
                emit dataChanged(idx, idx, {StatusRole, DownloadedRole});
            }
            break;
        }
    }
}

void ChapterModel::resetStatuses()
{
    m_status.clear();
    if (rowCount() > 0)
        emit dataChanged(index(0), index(rowCount() - 1), {StatusRole, DownloadedRole});
}

Chapter ChapterModel::chapterAt(int row) const
{
    if (row < 0 || row >= m_chapters.size())
        return Chapter();
    return m_chapters[row];
}
