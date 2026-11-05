#include "txtexporter.h"
#include "kakuyomuparser.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace {
QString writeChapterDocument(const QString &filePath, const QString &workTitle,
                             const Chapter &chapter, const QString &body,
                             QString *errorMessage)
{
    const QString header = QStringLiteral("%1 - %2\n%3")
                               .arg(workTitle, chapter.title, chapter.episodeUrl);
    const QString content = TxtExporter::mergeChapterDocument(header, body);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法写入文件: %1").arg(filePath);
        return QString();
    }
    file.write("\xEF\xBB\xBF"); // UTF-8 BOM，便于 Windows 记事本识别。
    file.write(content.toUtf8());
    file.close();
    return filePath;
}
} // namespace

void TxtExporter::splitChapterDocument(const QString &text, QString *header, QString *body)
{
    QString cleaned = text;
    if (cleaned.startsWith(QChar(0xFEFF)))
        cleaned.remove(0, 1);
    cleaned.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));

    if (header)
        header->clear();
    if (body)
        *body = cleaned;

    const QStringList lines = cleaned.split(QLatin1Char('\n'));
    if (lines.size() < 2)
        return;
    const QString first = lines.at(0).trimmed();
    const QString second = lines.at(1).trimmed();
    const bool hasUrl = second.startsWith(QStringLiteral("http://"))
                        || second.startsWith(QStringLiteral("https://"));
    if (!first.contains(QStringLiteral(" - ")) || !hasUrl)
        return;

    int bodyStart = 2;
    while (bodyStart < lines.size() && lines.at(bodyStart).trimmed().isEmpty())
        ++bodyStart;
    if (header)
        *header = first + QLatin1Char('\n') + second;
    if (body)
        *body = lines.mid(bodyStart).join(QLatin1Char('\n'));
}

QString TxtExporter::mergeChapterDocument(const QString &header, const QString &body)
{
    if (header.trimmed().isEmpty())
        return body;
    if (body.trimmed().isEmpty())
        return header;
    return header.trimmed() + QStringLiteral("\n\n") + body;
}

QString TxtExporter::chapterTitleFromDocument(const QString &text)
{
    QString header;
    splitChapterDocument(text, &header, nullptr);
    if (header.isEmpty())
        return QString();
    QString title = header.section(QLatin1Char('\n'), 0, 0).trimmed();
    const int divider = title.lastIndexOf(QStringLiteral(" - "));
    return divider >= 0 ? title.mid(divider + 3).trimmed() : QString();
}

QString TxtExporter::exportChapter(const QString &dir, const QString &workTitle,
                                   const Chapter &chapter, const QString &body,
                                   QString *errorMessage)
{
    QDir d(dir);
    // 目录不存在时尝试创建
    if (!d.exists() && !d.mkpath(QStringLiteral("."))) {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法创建目录: %1").arg(dir);
        return QString();
    }

    // 文件名 = 小说标题 - 章节标题.txt
    QString fileName = KakuyomuParser::sanitizeFileName(
        QStringLiteral("%1 - %2").arg(workTitle, chapter.title)) + QStringLiteral(".txt");
    return writeChapterDocument(d.filePath(fileName), workTitle, chapter, body, errorMessage);
}

QString TxtExporter::exportChapterToFile(const QString &filePath, const QString &workTitle,
                                         const Chapter &chapter, const QString &body,
                                         QString *errorMessage)
{
    // 确保父目录存在
    QDir dir = QFileInfo(filePath).absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法创建目录: %1").arg(dir.absolutePath());
        return QString();
    }

    return writeChapterDocument(filePath, workTitle, chapter, body, errorMessage);
}
