#ifndef BROWSERPAGEFETCHER_H
#define BROWSERPAGEFETCHER_H

#include <QByteArray>
#include <QObject>
#include <QVariant>

class QProcess;
class QTemporaryDir;

// Fetches a page through a real Chromium browser so JavaScript challenges can
// complete before the DOM is dumped. Used only by sites that reject plain HTTP.
class BrowserPageFetcher : public QObject
{
    Q_OBJECT
public:
    explicit BrowserPageFetcher(QObject *parent = nullptr);
    ~BrowserPageFetcher() override;

    void get(const QString &url, const QVariant &context = QVariant());
    void abort();
    bool isBusy() const;

signals:
    void requestFinished(const QString &url, const QVariant &context,
                         const QByteArray &data, bool ok, const QString &errorString);

private:
    QString browserExecutable() const;
    void finishRequest(bool ok, const QString &errorString,
                       const QByteArray &data = QByteArray());

    QProcess *m_process = nullptr;
    QTemporaryDir *m_profileDir = nullptr;
    QString m_pendingUrl;
    QVariant m_pendingContext;
    bool m_aborted = false;
    bool m_timedOut = false;
};

#endif // BROWSERPAGEFETCHER_H
