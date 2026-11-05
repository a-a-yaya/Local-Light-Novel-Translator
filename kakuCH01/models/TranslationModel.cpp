#include "TranslationModel.h"

#include <QDir>
#include <QFileInfo>

TranslationModel::TranslationModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int TranslationModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_entries.size();
}

QVariant TranslationModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return QVariant();

    const Entry &e = m_entries[index.row()];
    switch (role) {
    case FileNameRole:
        return QFileInfo(e.path).fileName();
    case PathRole:
        return e.path;
    case StatusRole:
        return e.status;
    case ProgressRole:
        return e.progress;
    case SelectedRole:
        return e.selected;
    case ErrorRole:
        return QString();
    case TranslatedRole:
        return e.translated;
    case GlossarySelectedRole:
        return e.glossarySelected;
    default:
        return QVariant();
    }
}

bool TranslationModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return false;
    if (role == SelectedRole) {
        if (m_entries[index.row()].translated && value.toBool())
            return false;
        m_entries[index.row()].selected = value.toBool();
        emit dataChanged(index, index, {SelectedRole});
        return true;
    }
    if (role == GlossarySelectedRole) {
        m_entries[index.row()].glossarySelected = value.toBool();
        emit dataChanged(index, index, {GlossarySelectedRole});
        return true;
    }
    return false;
}

QHash<int, QByteArray> TranslationModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[FileNameRole] = "fileName";
    roles[PathRole] = "path";
    roles[StatusRole] = "status";
    roles[ProgressRole] = "progress";
    roles[SelectedRole] = "selected";
    roles[ErrorRole] = "error";
    roles[TranslatedRole] = "translated";
    roles[GlossarySelectedRole] = "glossarySelected";
    return roles;
}

int TranslationModel::indexOf(const QString &path) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].path == path)
            return i;
    }
    return -1;
}

void TranslationModel::addFiles(const QStringList &paths)
{
    const int previousCount = m_entries.size();
    for (const QString &path : paths) {
        const int existing = indexOf(path);
        if (existing >= 0) {
            if (!m_entries[existing].translated) {
                m_entries[existing].selected = true;
                if (m_entries[existing].status != QStringLiteral("TRANSLATING")) {
                    m_entries[existing].status = QStringLiteral("WAITING");
                    m_entries[existing].progress = 0;
                }
                emit dataChanged(index(existing), index(existing), {StatusRole, ProgressRole, SelectedRole});
            }
            continue;
        }
        int row = m_entries.size();
        beginInsertRows(QModelIndex(), row, row);
        Entry e;
        e.path = path;
        m_entries.append(e);
        endInsertRows();
    }
    if (previousCount != m_entries.size()) emit fileCountChanged();
}

void TranslationModel::addFolder(const QString &dir)
{
    QDir d(dir);
    QStringList files = d.entryList(QStringList() << QStringLiteral("*.txt"), QDir::Files);
    QStringList paths;
    for (const QString &f : files)
        paths << d.filePath(f);
    addFiles(paths);
}

void TranslationModel::removeSelected(bool glossaryMode)
{
    const int previousCount = m_entries.size();
    for (int i = m_entries.size() - 1; i >= 0; --i) {
        if (glossaryMode ? m_entries[i].glossarySelected : m_entries[i].selected) {
            beginRemoveRows(QModelIndex(), i, i);
            m_entries.removeAt(i);
            endRemoveRows();
        }
    }
    if (previousCount != m_entries.size()) emit fileCountChanged();
}

void TranslationModel::setSelection(bool selected, bool glossaryMode, bool invert)
{
    if (m_entries.isEmpty()) return;
    for (Entry &entry : m_entries) {
        if (!glossaryMode && entry.translated) {
            entry.selected = false;
            continue;
        }
        bool &value = glossaryMode ? entry.glossarySelected : entry.selected;
        value = invert ? !value : selected;
    }
    emit dataChanged(index(0), index(m_entries.size() - 1),
                     {glossaryMode ? GlossarySelectedRole : SelectedRole});
}

void TranslationModel::clear()
{
    beginResetModel();
    m_entries.clear();
    endResetModel();
    emit fileCountChanged();
}

QStringList TranslationModel::selectedPaths() const
{
    QStringList result;
    for (const Entry &e : m_entries) {
        if (e.selected && !e.translated)
            result << e.path;
    }
    return result;
}

void TranslationModel::setGlossarySelected(const QString &path, bool selected)
{
    const int i = indexOf(path);
    if (i < 0)
        return;
    m_entries[i].glossarySelected = selected;
    emit dataChanged(index(i), index(i), {GlossarySelectedRole});
}

// 术语表提取的输入：只看提取勾选，不看 translated（提取只读原文）。
QStringList TranslationModel::glossarySelectedPaths() const
{
    QStringList result;
    for (const Entry &e : m_entries) {
        if (e.glossarySelected)
            result << e.path;
    }
    return result;
}

void TranslationModel::setFileStatus(const QString &path, const QString &status)
{
    int i = indexOf(path);
    if (i < 0)
        return;
    if (status == QStringLiteral("COMPLETED")) {
        m_entries[i].translated = true;
        m_entries[i].selected = false;
    }
    m_entries[i].status = status;
    emit dataChanged(index(i), index(i), {StatusRole, SelectedRole, TranslatedRole});
}

void TranslationModel::setFileTranslated(const QString &path, bool translated)
{
    const int i = indexOf(path);
    if (i < 0)
        return;
    Entry &entry = m_entries[i];
    entry.translated = translated;
    if (translated) {
        entry.status = QStringLiteral("COMPLETED");
        entry.progress = 100;
        entry.selected = false;
    } else if (entry.status == QStringLiteral("COMPLETED")) {
        entry.status = QStringLiteral("WAITING");
        entry.progress = 0;
    }
    emit dataChanged(index(i), index(i), {StatusRole, ProgressRole, SelectedRole, TranslatedRole});
}

QStringList TranslationModel::filePaths() const
{
    QStringList paths;
    paths.reserve(m_entries.size());
    for (const Entry &entry : m_entries)
        paths.append(entry.path);
    return paths;
}

void TranslationModel::setFileProgress(const QString &path, int progress)
{
    int i = indexOf(path);
    if (i < 0)
        return;
    m_entries[i].progress = progress;
    emit dataChanged(index(i), index(i), {ProgressRole});
}

void TranslationModel::resetStatuses()
{
    for (int i = 0; i < m_entries.size(); ++i) {
        m_entries[i].status = m_entries[i].translated
                                  ? QStringLiteral("COMPLETED")
                                  : QStringLiteral("WAITING");
        m_entries[i].progress = m_entries[i].translated ? 100 : 0;
    }
    if (!m_entries.isEmpty())
        emit dataChanged(index(0), index(m_entries.size() - 1),
                         {StatusRole, ProgressRole, SelectedRole, TranslatedRole,
                          GlossarySelectedRole});
}
