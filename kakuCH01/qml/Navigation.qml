pragma Singleton
import QtQuick

// 页面导航单例：让任意页面都能请求切换到指定页面。
QtObject {
    signal navigateRequested(string name)

    // 从首页卡片「放大铺满」进入小说详情（cardItem 是被点击的卡片）
    // 正在做「放大 / 缩回」动画的那张卡片：动画期间这张卡片本身不显示
    property var zoomCardItem: null
    property bool zoomTransitionActive: false
    signal novelZoomRequested(var cardItem)
    // 详情页返回：缩回原卡片位置
    signal closeDetailRequested()

    function openNovelZoom(cardItem) {
        novelZoomRequested(cardItem)
    }

    function closeDetail() {
        closeDetailRequested()
    }

    // 请求打开术语表窗口（由 Main.qml 统一持有窗口实例）。
    signal glossaryRequested()
    // 概要翻译请求（网站详情页发起，弹窗由 Main.qml 统一处理）
    signal overviewTranslateRequested()
    signal overviewNeedsAiRequested()
    signal commentsTranslateRequested()
    signal commentsNeedsAiRequested()

    function requestOverviewTranslate() {
        overviewTranslateRequested()
    }

    function requestOverviewNeedsAi() {
        overviewNeedsAiRequested()
    }

    function requestCommentsTranslate() {
        commentsTranslateRequested()
    }

    function requestCommentsNeedsAi() {
        commentsNeedsAiRequested()
    }
    // 整本下载 / 去下载页的确认弹窗（同样由 Main.qml 统一处理）
    signal downloadConfirmRequested()
    signal gotoDownloadRequested()

    function requestDownloadConfirm() {
        downloadConfirmRequested()
    }

    function requestGotoDownload() {
        gotoDownloadRequested()
    }
    signal dismissPanelsRequested()

    function dismissPanels() { dismissPanelsRequested() }

    // 请求在灵动岛上显示一条 2 秒的瞬时提示
    signal hintRequested(string text)

    function hint(text) {
        hintRequested(text)
    }

    function navigate(name) {
        navigateRequested(name)
    }

    function openGlossary() {
        glossaryRequested()
    }
}
