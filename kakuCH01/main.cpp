#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDebug>
#include <QIcon>
#include <QQuickWindow>
#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>
#include <qt_windows.h>

// Keep Windows from painting a rectangular non-client frame behind the QML
// surface. A native region would cut off QML's antialiased corner pixels.
class FramelessFrameFilter final : public QAbstractNativeEventFilter
{
public:
    void setWindow(QQuickWindow *window)
    {
        m_hwnd = reinterpret_cast<HWND>(window->winId());
    }

    bool nativeEventFilter(const QByteArray &, void *message, qintptr *result) override
    {
        const auto *msg = static_cast<MSG *>(message);
        if (!m_hwnd || msg->hwnd != m_hwnd)
            return false;
        if (msg->message == WM_NCPAINT) {
            *result = 0;
            return true;
        }
        if (msg->message == WM_NCACTIVATE) {
            // Preserve native activation processing without repainting a frame.
            *result = DefWindowProcW(m_hwnd, msg->message, msg->wParam, -1);
            return true;
        }
        return false;
    }

private:
    HWND m_hwnd = nullptr;
};
#endif
#include "controllers/AppController.h"

int main(int argc, char *argv[])
{
    QGuiApplication a(argc, argv);
    QQuickWindow::setDefaultAlphaBuffer(true);
    QCoreApplication::setOrganizationName(QStringLiteral("KakuReader"));
    QCoreApplication::setApplicationName(QStringLiteral("KakuReader"));
    QCoreApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.ico")));

    // 使用 Basic 样式，便于自定义按钮等控件外观
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // 业务控制器：聚合下载 / 翻译 / Sakura 模型 / 书库 / 阅读
    AppController controller;

    // 使用 QML 引擎加载 UI，并把控制器暴露为根上下文属性
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/"));
    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
#ifdef Q_OS_WIN
    FramelessFrameFilter frameFilter;
    a.installNativeEventFilter(&frameFilter);
    // Attach the filter before the first native frame is shown.
    engine.setInitialProperties({{QStringLiteral("visible"), false}});
#endif

    const QUrl url(QStringLiteral("qrc:/KakuCH/qml/Main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &a, [](const QUrl &failedUrl) {
                         qWarning() << "QML 加载失败:" << failedUrl;
                         QCoreApplication::exit(-1);
                     },
                     Qt::QueuedConnection);
    engine.load(url);
#ifdef Q_OS_WIN
    for (QObject *object : engine.rootObjects()) {
        if (auto *window = qobject_cast<QQuickWindow *>(object)) {
            frameFilter.setWindow(window);
            window->show();
        }
    }
#endif

    // 退出前确保中止请求并关闭 llama-server
    QObject::connect(&a, &QCoreApplication::aboutToQuit, &controller, &AppController::shutdown);

    return a.exec();
}
