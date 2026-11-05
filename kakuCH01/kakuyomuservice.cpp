#include "kakuyomuservice.h"
#include "novelparser.h"
#include "novelpaths.h"
#include "txtexporter.h"
#include "novelstore.h"
#include <QDir>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <algorithm>

namespace {
QString requestUrl(const QString &url, const QString &site)
{
    if (site != "pixivnet") return url;
    const QString id = NovelPaths::workIdFromUrl(url);
    if (id.startsWith("series_")) return "https://www.pixiv.net/ajax/novel/series/" + id.mid(7);
    if (!id.isEmpty()) return "https://www.pixiv.net/ajax/novel/" + id;
    return url; // Already an API URL (e.g. series content_titles).
}
}

KakuyomuService::KakuyomuService(QObject *parent)
    : QObject(parent)
{
    m_http.setTransferTimeout(30000);
    // 网络层返回结果统一交由本类处理
    connect(&m_http, &HttpClient::requestFinished,
            this, &KakuyomuService::onRequestFinished);
    connect(&m_browser, &BrowserPageFetcher::requestFinished,
            this, &KakuyomuService::onRequestFinished);
}

void KakuyomuService::fetchWork(const QString &workUrl)
{
    abort();
    m_aborted = false;
    m_pendingWork = {};
    m_cataloguePages.clear();
    m_episodeIds.clear();
    QString error;
    const QString workId = NovelPaths::workIdFromUrl(workUrl, &error);
    if (workId.isEmpty()) {
        emit workFailed(error);
        return;
    }
    const QString siteKey = NovelPaths::siteKeyFromUrl(workUrl);
    if (!NovelParser::supportsParsing(siteKey)) {
        emit workFailed(QStringLiteral("%1（%2）尚未实现作品解析，请使用下载页列出的站点")
                            .arg(NovelPaths::siteDisplayName(siteKey), siteKey));
        return;
    }
    // 统一为规范的作品页地址
    const QString canonical = NovelPaths::canonicalWorkUrl(workUrl);
    m_workUrl = canonical;
    m_siteKey = siteKey;
    // alphapolis 只有在请求带任意非空 Cookie 时才返回完整正文
    m_http.setCookieHeader(siteKey == QStringLiteral("alphapoliscopjp")
                               ? QStringLiteral("alphapolis_session=1")
                               : QString());
    requestPage(canonical, QVariant()); // 空 context 表示作品页请求
}

void KakuyomuService::downloadChapters(const NovelWork &work,
                                       const QVector<Chapter> &chapters,
                                       const QString &downloadRoot)
{
    abort();
    // 保存任务状态并开始逐个下载
    m_queue = chapters;
    m_workTitle = work.title;
    if (!work.siteKey.isEmpty())
        m_siteKey = work.siteKey;
    m_index = 0;
    m_success = 0;
    m_failed = 0;
    m_aborted = false;
    m_retries = 0;
    m_http.setCookieHeader(m_siteKey == "alphapoliscopjp" ? QStringLiteral("alphapolis_session=1") : QString());

    // 初始化小说目录（novel.json / chapters/）
    QString root = NovelStore::ensureNovelDirectory(work, downloadRoot);
    if (root.isEmpty()) {
        emit downloadFinished(0, m_queue.size());
        return;
    }
    m_chaptersDir = QDir(root).filePath(QStringLiteral("chapters"));

    emit progressChanged(0, m_queue.size(), QStringLiteral("开始下载…"));
    downloadNext();
}

void KakuyomuService::abort()
{
    m_aborted = true;
    ++m_generation;
    m_http.abortAll();
    m_browser.abort();
    m_queue.clear();
}

void KakuyomuService::requestPage(const QString &url, const QVariant &context)
{
    const QVariantMap ctx{{"generation", m_generation}, {"chapter", context.isValid() ? context.toInt() : -1}};
    const QString target = requestUrl(url, m_siteKey);
    // AlphaPolis currently answers plain HTTP clients with an AWS WAF challenge.
    // A real Chromium process completes that challenge before the DOM is read.
    if (m_siteKey == QStringLiteral("alphapoliscopjp"))
        m_browser.get(target, ctx);
    else
        m_http.get(target, ctx);
}

void KakuyomuService::onRequestFinished(const QString &url, const QVariant &context,
                                        const QByteArray &data, bool ok,
                                        const QString &error)
{
    const auto ctx = context.toMap();
    if (m_aborted || ctx.value("generation").toULongLong() != m_generation)
        return;

    const int idx = ctx.value("chapter", -1).toInt();
    if (idx >= 0 && (idx != m_index || idx >= m_queue.size())) return;
    if (ctx.value("pinkAgePost").toBool() && ok) {
        QVariantMap resumed = ctx;
        resumed.remove("pinkAgePost");
        const QString original = resumed.take("pinkReturnUrl").toString();
        m_http.get(original, resumed);
        return;
    }
    if (ok && m_siteKey == "solispiapink" && QString::fromUtf8(data).contains("/r18/proceed")) {
        const QByteArray form = NovelParser::solispiaAgeConfirmationForm(QString::fromUtf8(data), url);
        if (!ctx.value("pinkAgeSubmitted").toBool() && !form.isEmpty()) {
            QVariantMap submitted = ctx;
            submitted.insert("pinkAgeSubmitted", true);
            submitted.insert("pinkAgePost", true);
            submitted.insert("pinkReturnUrl", url);
            m_http.postForm("https://pink.solispia.com/r18/proceed", form, submitted);
            return;
        }
        const QString failure = QStringLiteral("ソリスピア Pink 的站点确认未通过，未保存确认页内容");
        if (idx < 0) emit workFailed(failure);
        else {
            m_failed += m_queue.size() - m_index;
            m_index = m_queue.size();
            emit progressChanged(m_index, m_queue.size(), failure);
            emit downloadFinished(m_success, m_failed);
            m_queue.clear();
        }
        return;
    }
    if (idx < 0) {
        // 作品页：解析章节列表
        if (!ok) {
            if ((m_siteKey == "syosetucom" || m_siteKey == "novelupplus") && !ctx.value("browserFallback").toBool()) {
                QVariantMap fallback = ctx;
                fallback.insert("browserFallback", true);
                m_browser.get(url, fallback);
                return;
            }
            emit workFailed(QStringLiteral("获取作品页失败: %1").arg(error));
            return;
        }
        const QString html = QString::fromUtf8(data);
        const auto response = QJsonDocument::fromJson(data).object();
        if (m_siteKey == "pixivnet" && response.value("error").toBool()) {
            emit workFailed(QStringLiteral("Pixiv 无法公开读取该作品：%1").arg(response.value("message").toString()));
            return;
        }
        NovelWork work = NovelParser::parseWorkPage(m_siteKey, html, m_workUrl);
        if (m_siteKey == "pixivnet" && work.workId.startsWith("series_")) {
            if (!url.endsWith("/content_titles") && !work.title.isEmpty()) {
                m_pendingWork = work;
                requestPage(url + "/content_titles", {});
                return;
            }
            m_pendingWork.chapters = work.chapters;
            work = m_pendingWork;
            m_pendingWork = {};
        }
        if (!work.isValid() || work.chapters.isEmpty()) {
            if (m_siteKey == "novelupplus" && !ctx.value("browserFallback").toBool()) {
                QVariantMap fallback = ctx;
                fallback.insert("browserFallback", true);
                m_browser.get(url, fallback);
                return;
            }
            emit workFailed(m_siteKey == "pixivnet"
                ? QStringLiteral("Pixiv 没有可公开读取的小说正文或系列篇目（登录限定、年龄限制或作品已删除）")
                : QStringLiteral("未能解析出章节列表（可能被网站验证拦截、作品受限或页面结构已变化）"));
            return;
        }
        if (m_pendingWork.workId.isEmpty()) {
            m_pendingWork = work;
            m_pendingWork.chapters.clear();
        }
        for (const auto &chapter : work.chapters) {
            if (m_episodeIds.contains(chapter.episodeId)) continue;
            m_episodeIds.insert(chapter.episodeId);
            m_pendingWork.chapters.append(chapter);
        }
        m_cataloguePages.insert(url);
        const QString next = NovelParser::nextWorkPage(m_siteKey, html, url);
        if (!next.isEmpty()) {
            if (m_cataloguePages.contains(next) || m_cataloguePages.size() >= 1000) {
                emit workFailed(QStringLiteral("章节目录分页异常，未使用不完整目录"));
                return;
            }
            requestPage(next, {});
            return;
        }
        work = m_pendingWork;
        m_pendingWork = {};
        if (m_siteKey == "novelupplus" || m_siteKey == "syosetucom")
            std::stable_sort(work.chapters.begin(), work.chapters.end(), [](const Chapter &a, const Chapter &b) { return a.number < b.number; });
        emit workReady(work);
        return;
    }

    // 章节页：解析正文并保存为 txt
    if (idx != m_index || idx >= m_queue.size()) return;
    if (!ok) {
        if ((m_siteKey == "novelupplus" || m_siteKey == "syosetucom") && !ctx.value("browserFallback").toBool()) {
            QVariantMap fallback = ctx;
            fallback.insert("browserFallback", true);
            m_browser.get(url, fallback);
            return;
        }
        emit progressChanged(m_index, m_queue.size(),
                             QStringLiteral("第 %1 章下载失败: %2").arg(idx + 1).arg(error));
        retryCurrentChapter();
        return;
    }
    const QString html = QString::fromUtf8(data);
    const auto api = QJsonDocument::fromJson(data).object();
    if (m_siteKey == "pixivnet" && api.value("error").toBool()) {
        emit progressChanged(m_index, m_queue.size(), QStringLiteral("该 Pixiv 篇目无法公开读取：%1").arg(api.value("message").toString()));
        ++m_failed;
        ++m_index;
        m_retries = 0;
        QTimer::singleShot(0, this, [this, generation = m_generation] { if (generation == m_generation) downloadNext(); });
        return;
    }
    if (m_siteKey == QStringLiteral("alphapoliscopjp")
        && html.contains(QStringLiteral("g-recaptcha"))
        && html.contains(QStringLiteral("/novel/episode_body"))) {
        const int remaining = m_queue.size() - m_index;
        m_failed += remaining;
        m_index = m_queue.size();
        m_retries = 0;
        emit progressChanged(m_index, m_queue.size(),
                             QStringLiteral("作品目录已解析，但 AlphaPolis 正文要求浏览器人机验证"));
        emit downloadFinished(m_success, m_failed);
        m_queue.clear();
        return;
    }
    QString body = NovelParser::parseEpisodeBody(m_siteKey, html);
    if (body.isEmpty()) {
        if ((m_siteKey == "novelupplus" || m_siteKey == "syosetucom") && !ctx.value("browserFallback").toBool()) {
            QVariantMap fallback = ctx;
            fallback.insert("browserFallback", true);
            m_browser.get(url, fallback);
            return;
        }
        const bool restricted = html.contains("The request could not be satisfied")
            || html.contains("Just a moment...") || html.contains("g-recaptcha") || html.contains("cf-chl-");
        emit progressChanged(m_index, m_queue.size(), restricted
            ? QStringLiteral("第 %1 章被网站访问限制拦截，未保存正文").arg(idx + 1)
            : QStringLiteral("第 %1 章正文解析失败").arg(idx + 1));
        if (ctx.value("browserFallback").toBool()) {
            ++m_failed;
            ++m_index;
            m_retries = 0;
            QTimer::singleShot(0, this, [this, generation = m_generation] { if (generation == m_generation) downloadNext(); });
            return;
        }
        retryCurrentChapter();
        return;
    }
    m_retries = 0; // 成功一次后重置重试计数，供下一章使用
    QString err;
    // 文件名使用稳定章节序号（三位补零，超过 999 自动扩展）
    int number = m_queue[idx].number > 0 ? m_queue[idx].number : (idx + 1);
    QString fileName = QString::number(number).rightJustified(3, QLatin1Char('0'))
                       + QStringLiteral(".txt");
    QString path = TxtExporter::exportChapterToFile(
                       QDir(m_chaptersDir).filePath(fileName),
                       m_workTitle, m_queue[idx], body, &err);
    if (path.isEmpty()) {
        emit progressChanged(m_index, m_queue.size(),
                             QStringLiteral("第 %1 章保存失败: %2").arg(idx + 1).arg(err));
        retryCurrentChapter();
        return;
    } else {
        ++m_success;
        emit progressChanged(m_index, m_queue.size(), QStringLiteral("已保存: %1").arg(path));
    }
    ++m_index;
    downloadNext();
}

void KakuyomuService::retryCurrentChapter()
{
    if (m_aborted)
        return;

    // 重试次数用尽时判为失败，推进到下一章
    if (m_index < 0 || m_index >= m_queue.size() || m_retries >= 3) {
        if (m_index >= 0 && m_index < m_queue.size())
            ++m_failed;
        m_retries = 0;
        ++m_index;
        downloadNext();
        return;
    }

    ++m_retries;
    emit progressChanged(m_index, m_queue.size(),
                         QStringLiteral("第 %1 章下载失败，正在重试 (%2/3)")
                             .arg(m_index + 1).arg(m_retries));
    requestPage(m_queue[m_index].episodeUrl, m_index);
}

void KakuyomuService::downloadNext()
{
    if (m_aborted) {
        m_queue.clear();
        return;
    }

    // 队列下载完毕时汇总结果
    if (m_index >= m_queue.size()) {
        emit progressChanged(m_index, m_queue.size(), QStringLiteral("完成"));
        emit downloadFinished(m_success, m_failed);
        return;
    }
    // 发起下一章请求，context 传当前下标
    const Chapter &ch = m_queue[m_index];
    emit progressChanged(m_index, m_queue.size(), QStringLiteral("正在下载: %1").arg(ch.title));
    requestPage(ch.episodeUrl, m_index);
}
