import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.settings
import Qt5Compat.GraphicalEffects
import KakuCH

// 主窗口：无系统标题栏的圆角窗体。
// 外层为玻璃描边，UI 表面内缩一圈，与窗口边缘保持透明空隙。
ApplicationWindow {
    id: root
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowSystemMenuHint
           | Qt.WindowMinimizeButtonHint | Qt.WindowMaximizeButtonHint
    color: "transparent"
    background: null
    width: 1280
    height: 800
    minimumWidth: 900
    minimumHeight: 600
    title: ""
    visible: true

    Component.onCompleted: {
        // Keep Controls popups above the custom window, sidebar and reader layers.
        Overlay.overlay.z = 100
        if (backgroundSettings.backgroundImage.toString() !== "")
            Theme.backgroundImage = backgroundSettings.backgroundImage
        Theme.componentAlpha = Math.max(0, Math.min(1, backgroundSettings.componentAlpha))
        Theme.componentTint = Math.max(0, Math.min(Theme.componentTints.length - 1,
                                                  backgroundSettings.componentTint))
    }

    property string pageTitle: "首页"
    property string currentPage: "home"
    onCurrentPageChanged: Navigation.dismissPanels()
    onActiveChanged: if (!active) Navigation.dismissPanels()
    property bool readerExpanded: false
    readonly property bool readerFullScreen: currentPage === "reader" && readerExpanded
    Shortcut {
        sequence: "F11"
        enabled: root.currentPage === "reader"
        onActivated: if (stack.currentItem) stack.currentItem.toggleFullScreen()
    }
    property var navOrder: ["home", "site", "translate", "download", "settings", "reader", "detail"]
    property int windowRadius: root.visibility === Window.Maximized ? 0 : Theme.railWidth / 2
    property int windowGap: root.visibility === Window.Maximized ? 0 : 15
    property int surfaceRadius: Math.max(root.windowRadius - root.windowGap, 0)

    // 灵动岛事件栈：新事件压栈显示，事件结束后回落到上一个仍在进行的事件。
    property var islandStack: []
    property string islandHintText: ""
    property bool islandHintVisible: false

    function pushIslandEvent(id) {
        var arr = islandStack.slice()
        var i = arr.indexOf(id)
        if (i >= 0)
            arr.splice(i, 1)
        arr.push(id)
        islandStack = arr
    }
    function dropIslandEvent(id) {
        var arr = islandStack.slice()
        var i = arr.indexOf(id)
        if (i >= 0) {
            arr.splice(i, 1)
            islandStack = arr
        }
    }
    function topIslandEvent() {
        if (islandHintVisible && islandHintText.length > 0)
            return "hint"
        if (islandStack.length > 0)
            return islandStack[islandStack.length - 1]
        return "model"
    }
    function showIslandHint(text) {
        islandHintText = text
        islandHintVisible = true
        islandHintTimer.restart()
    }
    Timer {
        id: islandHintTimer
        interval: 2000
        onTriggered: { root.islandHintVisible = false; root.islandHintText = "" }
    }

    // 事件登记：根据控制器状态变化压栈 / 出栈
    Connections {
        target: controller
        function onDownloadingChanged() {
            controller.downloading ? root.pushIslandEvent("download") : root.dropIslandEvent("download")
        }
        function onTranslatingChanged() {
            controller.translating ? root.pushIslandEvent("sakura") : root.dropIslandEvent("sakura")
        }
        function onMurasakiTranslatingChanged() {
            controller.murasakiTranslating ? root.pushIslandEvent("murasaki") : root.dropIslandEvent("murasaki")
        }
        function onQwenTranslatingChanged() {
            controller.qwenTranslating ? root.pushIslandEvent("qwen") : root.dropIslandEvent("qwen")
        }
        function onSiteOverviewTranslatingChanged() {
            controller.siteOverviewTranslating ? root.pushIslandEvent("overview") : root.dropIslandEvent("overview")
        }
        function onQwenExtractingChanged() {
            controller.qwenExtracting ? root.pushIslandEvent("extract") : root.dropIslandEvent("extract")
        }
        function onNovelUpdatingChanged() {
            controller.novelUpdating ? root.pushIslandEvent("novelupdate") : root.dropIslandEvent("novelupdate")
        }
        // 已有翻译在进行时又发起翻译：灵动岛提示 2 秒
        function onNotify(message) {
            if (message.indexOf("其它翻译任务") >= 0)
                root.showIslandHint(message)
        }
    }

    property bool aiActive: islandHintVisible || islandStack.length > 0
                            || controller.sakuraRunning || controller.sakuraStarting
                            || controller.murasakiRunning || controller.murasakiStarting
                            || controller.qwenRunning || controller.qwenStarting

    property string aiTopEvent: topIslandEvent()

    // Keep the active task's label and counters together for title and progress.
    readonly property var islandTask: {
        switch (aiTopEvent) {
        case "download": return { label: "整本下载", current: controller.downloadCurrent, total: controller.downloadTotal }
        case "overview": return { label: controller.siteTranslationKind === "comments" ? "评论翻译" : "概要翻译",
                                  current: controller.siteOverviewCurrent, total: controller.siteOverviewTotal }
        case "extract": return { label: controller.qwenExtractLabel, current: controller.qwenExtractCurrent, total: controller.qwenExtractTotal }
        case "novelupdate": return { label: "更新章节", current: controller.novelUpdateCurrent, total: controller.novelUpdateTotal }
        case "sakura": return { label: "Sakura 翻译", current: controller.translateCurrent, total: controller.translateTotal }
        case "murasaki": return { label: "Murasaki 翻译", current: controller.murasakiTranslateCurrent, total: controller.murasakiTranslateTotal }
        case "qwen": return { label: "Qwen 翻译", current: controller.qwenTranslateCurrent, total: controller.qwenTranslateTotal }
        default: return null
        }
    }

    property string aiTitle: {
        var top = aiTopEvent
        if (top === "hint")
            return islandHintText
        if (islandTask)
            return islandTask.label + " " + islandTask.current + "/" + Math.max(1, islandTask.total)
        if (controller.qwenStarting)
            return "Qwen · 启动中…"
        if (controller.qwenRunning)
            return "Qwen · " + controller.qwenStatus
        if (controller.sakuraStarting)
            return "Sakura · 启动中…"
        if (controller.sakuraRunning)
            return "Sakura · " + controller.sakuraStatus
        if (controller.murasakiStarting)
            return "Murasaki · 启动中…"
        if (controller.murasakiRunning)
            return "Murasaki · " + controller.murasakiStatus
        return ""
    }

    property bool aiShowProgress: islandTask !== null
    property real aiProgress: islandTask && islandTask.total > 0
                              ? islandTask.current / islandTask.total : 0

    // 进行中显示进度色；提示用警示色；仅运行模型时显示就绪色
    property color aiDotColor: aiTopEvent === "hint" ? Colors.warning
                               : (aiShowProgress ? Colors.warning : Colors.success)
    // 灵动岛状态标识：状态切换时弹一下
    property string aiStateKey: {
        var top = aiTopEvent
        if (top === "model")
            return (controller.qwenStarting || controller.sakuraStarting || controller.murasakiStarting)
                   ? "starting" : "running"
        return top
    }

    // ---- 确认弹窗（概要翻译 / 整本下载 / 前往页面）----
    readonly property real askPanelW: 404
    readonly property real askPanelH: 200
    property string askMode: ""      // noai / commentsNoAi / confirm / comments / download / gotodownload
    property bool askVisible: false
    property real askFlyX: 0
    property real askFlyY: 0
    property real askDismissY: 0
    property real askSwitchY: 0
    property string askFlyAction: ""

    function askTitle() {
        if (askMode === "noai" || askMode === "commentsNoAi")
            return "未启动 AI"
        if (askMode === "download")
            return "确认整本下载"
        if (askMode === "gotodownload")
            return "前往下载页"
        return askMode === "comments" ? "开始翻译评论" : "开始翻译概要"
    }

    function askMessage() {
        if (askMode === "noai")
            return "翻译概要需要先启动一个翻译模型（Sakura / Murasaki / Qwen）。"
        if (askMode === "commentsNoAi")
            return "翻译评论需要先启动一个翻译模型（Sakura / Murasaki / Qwen）。"
        if (askMode === "download")
            return "将下载《" + controller.siteTitle + "》的全部共 "
                   + controller.siteChapterCount + " 章到本地。"
        if (askMode === "gotodownload")
            return "需要到「下载」页面选择要下载的章节、查看下载进度。"
        if (askMode === "comments")
            return "将使用当前运行的 " + askModelName()
                   + " 逐条翻译这部作品的评论，完成后显示中日对照。"
        return "将使用当前运行的 " + askModelName()
               + " 翻译这部作品的概要，完成后显示中日上下对照。"
    }

    function askPrimaryText() {
        if (askMode === "noai" || askMode === "commentsNoAi")
            return "前往开启"
        if (askMode === "gotodownload")
            return "前往下载页"
        return "确认"
    }

    function askSecondaryText() {
        return (askMode === "noai" || askMode === "commentsNoAi") ? "确认" : "取消"
    }

    function askPrimaryAction() {
        if (askMode === "noai" || askMode === "commentsNoAi") { root.gotoPage("translate"); return }
        if (askMode === "gotodownload") {
            controller.queueSiteWorkForDownload()
            root.gotoPage("download")
            return
        }
        root.confirmAsk()
    }

    function askModelName() {
        if (controller.qwenRunning) return "Qwen"
        if (controller.murasakiRunning) return "Murasaki"
        if (controller.sakuraRunning) return "Sakura"
        return "当前模型"
    }

    function openAsk(mode) {
        askMode = mode
        askPanel.scale = 1
        askPanel.opacity = 0
        askPanel.x = Math.round((askLayer.width - askPanelW) / 2)
        askPanel.y = Math.round((askLayer.height - askPanelH) / 2)
        askVisible = true
        askPop.restart()
    }

    // 取消：向后缩小 + 下沉 + 淡出
    function closeAsk() {
        askDismissY = askPanel.y + 12
        askDismiss.restart()
    }

    // 确认：飞向右上角灵动岛，途中缩小直至融为一体
    function confirmAsk() {
        askFlyAction = askMode === "download" ? "download"
            : askMode === "comments" ? "comments" : "translate"
        var cx = askLayer.width - 18 - aiPill.width / 2
        var cy = aiPill.height / 2
        askFlyX = Math.round(cx - askPanelW / 2)
        askFlyY = Math.round(cy - askPanelH / 2)
        askFly.restart()
    }

    // 前往某页：弹窗不单独关闭，而是跟着页面切换同步向上 / 向下移出去
    function gotoPage(name) {
        var to = navOrder.indexOf(name)
        var from = navOrder.indexOf(currentPage)
        askSwitchY = (to < from) ? askLayer.height : -askLayer.height
        navigate(name)
        askSwitch.restart()
    }

    Connections {
        target: Navigation
        function onOverviewTranslateRequested() { root.openAsk("confirm") }
        function onOverviewNeedsAiRequested() { root.openAsk("noai") }
        function onCommentsTranslateRequested() { root.openAsk("comments") }
        function onCommentsNeedsAiRequested() { root.openAsk("commentsNoAi") }
        function onDownloadConfirmRequested() { root.openAsk("download") }
        function onGotoDownloadRequested() { root.openAsk("gotodownload") }
    }

    ParallelAnimation {
        id: askPop
        NumberAnimation { target: askPanel; property: "opacity"; from: 0; to: 1; duration: 180; easing.type: Easing.OutCubic }
        NumberAnimation { target: askPanel; property: "scale"; from: 0.90; to: 1; duration: 360; easing.type: Easing.OutBack; easing.overshoot: 1.6 }
    }
    ParallelAnimation {
        id: askDismiss
        NumberAnimation { target: askPanel; property: "opacity"; to: 0; duration: 170; easing.type: Easing.InCubic }
        NumberAnimation { target: askPanel; property: "scale"; to: 0.92; duration: 190; easing.type: Easing.InCubic }
        NumberAnimation { target: askPanel; property: "y"; to: root.askDismissY; duration: 190; easing.type: Easing.InCubic }
        onFinished: { root.askVisible = false; root.askMode = "" }
    }
    ParallelAnimation {
        id: askFly
        NumberAnimation { target: askPanel; property: "x"; to: root.askFlyX; duration: 460; easing.type: Easing.InOutCubic }
        NumberAnimation { target: askPanel; property: "y"; to: root.askFlyY; duration: 460; easing.type: Easing.InOutCubic }
        NumberAnimation { target: askPanel; property: "scale"; to: 0.10; duration: 460; easing.type: Easing.InCubic }
        NumberAnimation { target: askPanel; property: "opacity"; to: 0; duration: 460; easing.type: Easing.InCubic }
        onFinished: {
            var action = root.askFlyAction
            root.askVisible = false
            root.askMode = ""
            root.askFlyAction = ""
            aiBounce.restart()
            if (action === "download")
                controller.siteDownloadWork()
            else if (action === "comments")
                controller.siteTranslateComments()
            else
                controller.siteTranslateOverview()
        }
    }
    // 前往某页：与 StackView 页面切换（240ms / InOutSine）同步，同方向移出
    ParallelAnimation {
        id: askSwitch
        NumberAnimation { target: askPanel; property: "y"; to: root.askSwitchY; duration: 240; easing.type: Easing.InOutSine }
        NumberAnimation { target: askPanel; property: "opacity"; to: 0; duration: 240; easing.type: Easing.InOutSine }
        onFinished: { root.askVisible = false; root.askMode = "" }
    }

    // 页面组件映射，供导航使用
    property var pages: ({
        home: homeComp,
        download: downloadComp,
        site: siteComp,
        translate: translateHubComp,
        reader: readerComp,
        detail: detailComp,
        settings: settingsComp
    })

    property bool zoomBusy: false
    onZoomBusyChanged: Navigation.zoomTransitionActive = zoomBusy
    property bool zoomClosing: false
    property real zoomProgress: 1
    property real zoomCardOpacity: 0
    property int zoomGeneration: 0
    property var zoomDetailPage: null
    property var zoomHomePage: null

    function finishNovelZoom() {
        if (zoomClosing && stack.depth > 1)
            stack.pop(StackView.Immediate)
        // 透明详情页必须只透出壁纸；首页仅在展开 / 收回过程中可见。
        if (zoomHomePage) zoomHomePage.visible = zoomClosing
        zoomHomePage = null
        zoomBusy = false
        zoomDetailPage = null
        stack.zoomActive = false
        if (zoomClosing) {
            Navigation.zoomCardItem = null
            stack.zoomRect = Qt.rect(0, 0, 0, 0)
            currentPage = "home"
            pageTitle = "首页"
        }
        zoomClosing = false
    }

    function openNovelZoom(cardItem) {
        if (!cardItem || zoomBusy || stack.busy || currentPage !== "home") return
        stack.readerMotion = false
        zoomBusy = true
        const generation = ++zoomGeneration
        // 采样按下后的真实四角：解除按压回弹，避免起点跳动。
        const p = stack.mapFromItem(cardItem, 0, 0)
        const q = stack.mapFromItem(cardItem, cardItem.width, cardItem.height)
        stack.zoomRect = Qt.rect(p.x, p.y, q.x - p.x, q.y - p.y)
        const started = cardItem.grabToImage(function (result) {
            if (generation !== zoomGeneration) return
            zoomSnapshot.source = result.url
            zoomHomePage = stack.currentItem
            Navigation.zoomCardItem = cardItem
            stack.zoomActive = true
            zoomClosing = false
            zoomProgress = 0
            zoomCardOpacity = 1
            currentPage = "detail"
            pageTitle = "详情"
            stack.push(pages["detail"], StackView.Immediate)
            zoomDetailPage = stack.currentItem
            zoomHomePage.visible = true
            zoomOpenAnim.restart()
        })
        if (!started) {
            zoomBusy = false
            currentPage = "detail"
            pageTitle = "详情"
            stack.push(pages["detail"], StackView.Immediate)
        }
    }

    function closeDetailPage() {
        if (zoomBusy || stack.busy) return
        stack.readerMotion = false
        // 「放大进入」过的详情页：缩回原卡片位置。
        // 几何以打开时记录的快照矩形 stack.zoomRect 为准 —— 更新章节 / 删除阅读记录 /
        // 生成或清空双语资源等操作会刷新书库并重建首页卡片，Navigation.zoomCardItem
        // 会随旧卡片一起失效；把它当成播放缩回动画的前提，退出就会直接消失、丢掉动效。
        if (stack.depth > 1 && stack.zoomRect.width > 0 && stack.zoomRect.height > 0) {
            const card = Navigation.zoomCardItem
            if (card) {
                // 卡片还在：用它的实时位置让缩回终点更精确
                const p = stack.mapFromItem(card, 0, 0)
                stack.zoomRect = Qt.rect(p.x, p.y, card.width, card.height)
            }
            zoomBusy = true
            zoomClosing = true
            stack.zoomActive = true
            zoomDetailPage = stack.currentItem
            zoomHomePage = stack.get(0)
            zoomHomePage.visible = true
            zoomProgress = 1
            zoomCardOpacity = 0
            zoomCloseAnim.restart()
            return
        }
        Navigation.zoomCardItem = null
        stack.zoomActive = false
        if (stack.depth > 1) stack.pop(StackView.Immediate)
        currentPage = "home"
        pageTitle = "首页"
        if (stack.currentItem.objectName !== "home") root.navigate("home")
    }
    function navigate(name) {
        if ((name === "reader" || currentPage === "reader")
                && (name === currentPage || stack.busy || zoomBusy)) return
        // 阅读器独立压栈，返回时保留目录滚动位置及原有详情页展开状态。
        if (currentPage === "reader" && stack.currentItem
                && !stack.currentItem.requestLeave(function() { root.navigate(name) })) return
        if (name !== "reader") readerExpanded = false
        stack.readerMotion = currentPage === "reader" || name === "reader"
        if (name === "reader") {
            currentPage = "reader"
            pageTitle = "阅读"
            stack.push(pages.reader)
            return
        }
        if (currentPage === "reader" && name === "detail" && stack.depth > 1
                && stack.get(stack.depth - 2).objectName === "detail") {
            currentPage = "detail"
            pageTitle = "详情"
            stack.pop()
            return
        }
        var titles = {
            home: "首页",
            download: "下载",
            site: "网站",
            translate: "翻译",
            reader: "阅读",
            detail: "详情",
            settings: "设置"
        }

        // 「放大进入」的详情页压在首页上时：
        //  · 回首页 → 直接播缩回卡片的动画
        //  · 去其它页 → 直接从这里滑过去（不会先瞬间闪回首页再移动）
        var onZoomedDetail = (stack.depth > 1 && currentPage === "detail")
        if (onZoomedDetail && name === "home" && stack.zoomRect.width > 0) {
            root.closeDetailPage()
            return
        }
        var fromPage = onZoomedDetail ? "home" : currentPage

        stack.zoomActive = false
        ++zoomGeneration
        zoomOpenAnim.stop()
        zoomCloseAnim.stop()
        if (zoomHomePage) zoomHomePage.visible = name === "home"
        zoomHomePage = null
        zoomBusy = false
        zoomClosing = false
        zoomDetailPage = null
        zoomProgress = 1
        zoomCardOpacity = 0
        Navigation.zoomCardItem = null

        var oldIndex = navOrder.indexOf(fromPage)
        var newIndex = navOrder.indexOf(name)
        if (newIndex > oldIndex)
            stack.slideDir = 1
        else if (newIndex < oldIndex)
            stack.slideDir = -1
        else
            stack.slideDir = 0

        currentPage = name

        // 目标页已经在栈里（被详情页压在下面的首页）→ 直接弹回它
        for (var i = 0; i < stack.depth; i++) {
            var item = stack.get(i)
            if (item && item.objectName === name && name !== "detail") {
                if (stack.currentItem !== item)
                    stack.pop(item)
                pageTitle = titles[name] || name
                return
            }
        }

        var comp = pages[name]
        if (comp) {
            stack.replace(comp)
            pageTitle = titles[name] || name
        }
    }

    // 页面导航请求（来自页面内按钮）
    Connections {
        target: Navigation
        function onNavigateRequested(name) { root.navigate(name) }
        function onGlossaryRequested() {
            glossaryWindow.open()
        }
        function onHintRequested(text) { root.showIslandHint(text) }
        function onNovelZoomRequested(cardItem) { root.openNovelZoom(cardItem) }
        function onCloseDetailRequested() { root.closeDetailPage() }
    }
    Connections {
        target: controller
        function onOpenReaderRequested() { root.navigate("reader") }
    }

    Settings {
        id: backgroundSettings
        property url backgroundImage
        property real componentAlpha: 0.2
        property int componentTint: 0
    }

    // 关闭窗口时清理：中止请求并停止模型
    onClosing: function(closeEvent) {
        if (currentPage === "reader" && stack.currentItem
                && !stack.currentItem.requestLeave(function() { root.close() })) {
            closeEvent.accepted = false
            return
        }
        controller.shutdown()
    }

    // 玻璃描边：沿窗口圆角外沿的一圈可见描边（冷调、更高对比）。
    Rectangle {
        id: glassFrame
        z: 0
        visible: root.windowRadius > 0
        anchors.fill: parent
        radius: root.windowRadius
        color: Theme.background
        border.width: 2
        border.color: "white"
        antialiasing: true
    }

    // 自定义背景图片：铺满整个窗口，包括 15px 空隙。
    Item {
        id: backgroundLayer
        z: 0
        anchors.fill: parent
        visible: Theme.customBackground || Theme.liquidGlass
        layer.enabled: visible
        layer.effect: OpacityMask {
            maskSource: windowMask
        }

        Rectangle { anchors.fill: parent; color: Theme.background }
        Image {
            anchors.fill: parent
            visible: Theme.customBackground
            source: Theme.backgroundImage
            fillMode: Image.PreserveAspectCrop
            clip: true
            asynchronous: true
            smooth: true
        }
    }
    ShaderEffectSource {
        id: materialTexture
        sourceItem: Theme.liquidGlass ? backgroundLayer : null
        live: Theme.liquidGlass
        visible: false
        Component.onCompleted: {
            Theme.glassBackdrop = backgroundLayer
            Theme.glassTexture = materialTexture
        }
    }
    // UI 表面：内缩一圈空隙，再被圆角遮罩裁切。
    Item {
        id: windowSurface
        z: 1
        anchors.fill: parent
        anchors.margins: root.readerFullScreen ? 0 : root.windowGap
        layer.enabled: root.surfaceRadius > 0
        layer.effect: OpacityMask {
            maskSource: maskRect
        }

        // 预设纯白背景
        Rectangle {
            id: surfaceBg
            anchors.fill: parent
            radius: root.surfaceRadius
            color: Theme.background
            visible: !Theme.customBackground
            antialiasing: true
        }

        // 内容区：铺满表面，左侧为图标栏让出 railWidth；无顶部标题栏。
        Rectangle {
            id: contentArea
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.leftMargin: root.readerFullScreen ? 0 : Theme.railWidth
            anchors.right: parent.right
            color: Theme.pageBackground
            // The reader needs its own four corners beside the sidebar as well.
            layer.enabled: root.currentPage === "reader"
            layer.effect: OpacityMask { maskSource: readerMask }

            // 单一进度同时控制几何、圆角与内容交接，避免两套动画不同步。
            Rectangle {
                id: zoomOverlay
                z: 6
                visible: root.zoomBusy && root.zoomDetailPage !== null
                readonly property real progress: root.zoomProgress
                x: stack.zoomRect.x * (1 - progress)
                y: stack.zoomRect.y * (1 - progress)
                width: stack.zoomRect.width + (contentArea.width - stack.zoomRect.width) * progress
                height: stack.zoomRect.height + (contentArea.height - stack.zoomRect.height) * progress
                radius: Theme.radius * (1 - progress)
                color: Theme.background
                antialiasing: true
                layer.enabled: visible
                layer.effect: OpacityMask { maskSource: zoomOverlayMask }

                // 过渡里的透明区域只采样壁纸，遮住下方首页的其它卡片。
                ShaderEffectSource {
                    anchors.fill: parent
                    sourceItem: root.zoomBusy && Theme.customBackground ? backgroundLayer : null
                    visible: sourceItem !== null
                    sourceRect: {
                        // 显式依赖几何变化，采样始终与窗口壁纸对齐。
                        zoomOverlay.x; zoomOverlay.y; root.width; root.height
                        const p = zoomOverlay.mapToItem(backgroundLayer, 0, 0)
                        return Qt.rect(p.x, p.y, zoomOverlay.width, zoomOverlay.height)
                    }
                    live: root.zoomBusy
                    smooth: true
                }
                ShaderEffectSource {
                    anchors.fill: parent
                    opacity: 1 - zoomSnapshot.opacity
                    sourceItem: root.zoomDetailPage
                    hideSource: root.zoomBusy
                    live: root.zoomBusy
                    smooth: true
                    // 只在过渡期将详情栅格化，结束后还原清晰的真实控件。
                    textureSize: Qt.size(contentArea.width * root.Screen.devicePixelRatio,
                                         contentArea.height * root.Screen.devicePixelRatio)
                }
                Image {
                    id: zoomSnapshot
                    anchors.fill: parent
                    fillMode: Image.Stretch
                    smooth: true
                    opacity: root.zoomCardOpacity
                }
            }
            Rectangle {
                id: zoomOverlayMask
                width: zoomOverlay.width
                height: zoomOverlay.height
                radius: zoomOverlay.radius
                color: "white"
                antialiasing: true
                visible: false
            }
            ParallelAnimation {
                id: zoomOpenAnim
                NumberAnimation {
                    target: root; property: "zoomProgress"; to: 1; duration: 300
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: [0.18, 0.78, 0.22, 1, 1, 1]
                }
                SequentialAnimation {
                    NumberAnimation {
                        target: root; property: "zoomCardOpacity"; to: 0; duration: 60
                        easing.type: Easing.OutCubic
                    }
                }
                onFinished: root.finishNovelZoom()
            }
            ParallelAnimation {
                id: zoomCloseAnim
                NumberAnimation {
                    target: root; property: "zoomProgress"; to: 0; duration: 290
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: [0.32, 0, 0.2, 1, 1, 1]
                }
                SequentialAnimation {
                    PauseAnimation { duration: 90 }
                    NumberAnimation {
                        target: root; property: "zoomCardOpacity"; to: 1; duration: 60
                        easing.type: Easing.InOutCubic
                    }
                }
                onFinished: root.finishNovelZoom()
            }
            MouseArea {
                anchors.fill: parent
                z: 7
                visible: root.zoomBusy
                // 动画期间不让第二次点击穿透到详情控件。
            }
            StackView {
                id: stack
                anchors.fill: parent
                initialItem: homeComp

                property int slideDir: 0
                property bool readerMotion: false

                replaceEnter: Transition {
                    NumberAnimation {
                        property: "y"
                        from: stack.readerMotion ? 24 : stack.slideDir === 1 ? stack.height : (stack.slideDir === -1 ? -stack.height : 0)
                        to: 0
                        duration: stack.zoomActive ? 0 : 240
                        easing.type: stack.readerMotion ? Easing.OutCubic : Easing.InOutSine
                    }
                    NumberAnimation { property: "opacity"; from: stack.readerMotion ? 0 : 1; to: 1; duration: stack.readerMotion ? 220 : 0 }
                }

                replaceExit: Transition {
                    NumberAnimation {
                        property: "y"
                        from: 0
                        to: stack.readerMotion ? 18 : stack.slideDir === 1 ? -stack.height : (stack.slideDir === -1 ? stack.height : 0)
                        duration: stack.zoomActive ? 0 : 240
                        easing.type: stack.readerMotion ? Easing.OutCubic : Easing.InOutSine
                    }
                    NumberAnimation { property: "opacity"; from: 1; to: stack.readerMotion ? 0 : 1; duration: stack.readerMotion ? 180 : 0 }
                }

                property bool zoomActive: false
                property rect zoomRect: Qt.rect(0, 0, 0, 0)
                pushEnter: Transition {
                    NumberAnimation { property: "y"; from: stack.readerMotion ? 24 : 0; to: 0; duration: stack.readerMotion ? 260 : 0; easing.type: Easing.OutCubic }
                    NumberAnimation { property: "opacity"; from: stack.readerMotion ? 0 : 1; to: 1; duration: stack.readerMotion ? 220 : 0 }
                    NumberAnimation { property: "scale"; from: stack.readerMotion ? 0.985 : 1; to: 1; duration: stack.readerMotion ? 260 : 0; easing.type: Easing.OutCubic }
                }
                pushExit: Transition {
                    NumberAnimation { property: "opacity"; from: 1; to: stack.readerMotion ? 0.4 : 1; duration: stack.readerMotion ? 220 : 0 }
                }
                popExit: Transition {
                    NumberAnimation {
                        property: "y"
                        to: stack.readerMotion ? 24 : stack.zoomActive || stack.slideDir === 0 ? 0
                            : (stack.slideDir === 1 ? -stack.height : stack.height)
                        duration: stack.readerMotion ? 190 : stack.zoomActive || stack.slideDir === 0 ? 0 : 240
                        easing.type: Easing.InOutSine
                    }
                    NumberAnimation { property: "opacity"; from: 1; to: stack.readerMotion ? 0 : 1; duration: stack.readerMotion ? 190 : 0 }
                    NumberAnimation { property: "scale"; from: 1; to: stack.readerMotion ? 0.985 : 1; duration: stack.readerMotion ? 190 : 0 }
                }
                popEnter: Transition {
                    NumberAnimation {
                        property: "y"
                        from: stack.readerMotion ? 0 : stack.zoomActive || stack.slideDir === 0 ? 0
                              : (stack.slideDir === 1 ? stack.height : -stack.height)
                        to: 0
                        duration: stack.readerMotion ? 190 : stack.zoomActive || stack.slideDir === 0 ? 0 : 240
                        easing.type: Easing.InOutSine
                    }
                    NumberAnimation { property: "opacity"; from: stack.readerMotion ? 0.4 : 1; to: 1; duration: stack.readerMotion ? 190 : 0 }
                }            }
        }

        // AI 运行状态：类似灵动岛，未运行时隐藏，运行时从右上角弹入。
        Item {
            id: aiIsland
            visible: !root.readerFullScreen
            z: 8
            anchors.right: parent.right
            anchors.rightMargin: 18
            width: aiPill.width
            height: aiPill.height
            // 展开 / 收起：弹性阻尼贝塞尔（OutBack 带回弹）
            opacity: root.aiActive ? 1 : 0
            y: root.aiActive ? 0 : -14
            scale: root.aiActive ? 1 : 0.72
            transformOrigin: Item.Center
            Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
            Behavior on y {
                NumberAnimation { duration: 420; easing.type: Easing.OutBack; easing.overshoot: 1.5 }
            }
            Behavior on scale {
                NumberAnimation { duration: 420; easing.type: Easing.OutBack; easing.overshoot: 1.5 }
            }

            Rectangle {
                id: aiPill
                width: Math.max(173, aiRow.implicitWidth + 32)
                height: 48
                radius: height / 2
                color: Theme.dark ? Qt.rgba(0.14, 0.14, 0.16, 0.82) : Qt.rgba(1, 1, 1, 0.78)
                border.width: 1.5
                border.color: Theme.dark ? Qt.rgba(1, 1, 1, 0.42) : Qt.rgba(0, 0, 0, 0.14)
                // 事件变短时要立刻收回，这里不做回弹
                Behavior on width {
                    NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
                }

                // 状态变化（启动 / 开始翻译 / 模型切换）时弹一下
                SequentialAnimation {
                    id: aiBounce
                    NumberAnimation { target: aiPill; property: "scale"; to: 1.06; duration: 130; easing.type: Easing.OutCubic }
                    NumberAnimation { target: aiPill; property: "scale"; to: 0.985; duration: 110; easing.type: Easing.InOutQuad }
                    NumberAnimation { target: aiPill; property: "scale"; to: 1.0; duration: 190; easing.type: Easing.OutBack; easing.overshoot: 1.4 }
                }

                Row {
                    id: aiRow
                    anchors.centerIn: parent
                    spacing: 12

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 13
                        height: 13
                        radius: 7
                        color: root.aiDotColor
                    }

                    // 内容交替：旧文案淡出、新文案淡入，宽度平滑过渡
                    Item {
                        id: aiLabelHost
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(aiLabelPrev.implicitWidth, aiLabelCur.implicitWidth)
                        height: Math.max(aiLabelPrev.implicitHeight, aiLabelCur.implicitHeight)

                        Text {
                            id: aiLabelPrev
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: ""
                            opacity: 0
                            color: Theme.textPrimary
                            font.family: Theme.uiFont
                            font.pixelSize: 15
                            font.bold: true
                        }
                        Text {
                            id: aiLabelCur
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.aiTitle
                            opacity: 1
                            color: Theme.textPrimary
                            font.family: Theme.uiFont
                            font.pixelSize: 15
                            font.bold: true
                        }
                    }
                }

                // 翻译时的进度条：只在翻译中显示，结束后整条消失。
                Rectangle {
                    visible: root.aiShowProgress
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    anchors.bottomMargin: 7
                    height: 3
                    radius: 2
                    color: Qt.rgba(0, 0, 0, 0.10)
                    // 岛变窄时不允许填充条越出底槽
                    clip: true

                    Rectangle {
                        id: aiProgressFill
                        // 只对「进度值」做过渡；宽度每帧按底槽实际宽度重算，
                        // 这样底槽随岛一起缩短时填充条同步收缩，不会先溢出一瞬再回弹。
                        // 岛本身的速度（底槽跟随）保持不变。
                        property real shownProgress: root.aiProgress
                        height: parent.height
                        radius: parent.radius
                        width: parent.width * Math.max(0, Math.min(1, shownProgress))
                        color: Theme.accent
                        Behavior on shownProgress {
                            NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
                        }
                    }
                }
            }

            // 文案交替：旧值留在 prev 上淡出，新值由 cur 淡入
            property string shownTitle: ""
            Connections {
                target: root
                function onAiTitleChanged() {
                    if (root.aiTitle === aiIsland.shownTitle)
                        return
                    aiLabelPrev.text = aiIsland.shownTitle
                    aiIsland.shownTitle = root.aiTitle
                    aiLabelPrev.opacity = 1
                    aiLabelCur.opacity = 0
                    aiLabelPrevFade.restart()
                    aiLabelCurFade.restart()
                }
            }
            NumberAnimation {
                id: aiLabelPrevFade
                target: aiLabelPrev
                property: "opacity"
                to: 0
                duration: 150
                easing.type: Easing.OutCubic
                // 淡完就清空，否则旧的长文案一直撑着灵动岛宽度
                onFinished: aiLabelPrev.text = ""
            }
            NumberAnimation {
                id: aiLabelCurFade
                target: aiLabelCur
                property: "opacity"
                from: 0
                to: 1
                duration: 260
                easing.type: Easing.OutCubic
            }
            // 每次状态切换都弹一下
            Connections {
                target: root
                function onAiStateKeyChanged() { aiBounce.restart() }
            }
        }

        // ---- 概要翻译弹窗：80% 不透明度磨砂玻璃 ----
        Item {
            id: askLayer
            z: 7
            anchors.fill: parent
            visible: root.askVisible

            Rectangle {
                id: askScrim
                anchors.fill: parent
                color: Theme.dark ? Qt.rgba(0, 0, 0, 0.30) : Qt.rgba(0, 0, 0, 0.10)
                opacity: root.askVisible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

                // 有确认弹窗时不能操控背景界面
                MouseArea { anchors.fill: parent; hoverEnabled: true }
            }

            Rectangle {
                id: askPanel
                width: root.askPanelW
                height: root.askPanelH
                radius: 20
                transformOrigin: Item.Center
                color: Theme.dark ? Qt.rgba(0.11, 0.11, 0.12, 0.80) : Qt.rgba(1, 1, 1, 0.80)
                border.width: 1
                border.color: Theme.glassBorder
                opacity: 0

                // 面板自身吞掉点击；外部不拦截，
                // 这样「前往开启」切到翻译页后仍可操作背景
                MouseArea { anchors.fill: parent }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 26
                    anchors.rightMargin: 26
                    anchors.topMargin: 22
                    anchors.bottomMargin: 20
                    spacing: 12

                    Text {
                        Layout.fillWidth: true
                        text: root.askTitle()
                        color: Theme.textPrimary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.fontTitle
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        text: root.askMessage()
                        color: Theme.textSecondary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.fontBody
                        wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Item { Layout.fillWidth: true }
                        SecondaryButton {
                            text: root.askSecondaryText()
                            onClicked: root.closeAsk()
                        }
                        PrimaryButton {
                            text: root.askPrimaryText()
                            onClicked: root.askPrimaryAction()
                        }
                    }
                }
            }
        }
    }

    // 圆角遮罩：与 UI 表面同尺寸，仅供 OpacityMask 读取。
    Rectangle {
        id: maskRect
        anchors.fill: windowSurface
        radius: root.readerFullScreen ? root.windowRadius : root.surfaceRadius
        color: "black"
        antialiasing: true
        visible: false
    }

    Rectangle {
        id: readerMask
        width: contentArea.width
        height: contentArea.height
        radius: root.readerFullScreen ? root.windowRadius : root.surfaceRadius
        color: "white"
        antialiasing: true
        visible: false
    }

    // 窗口级圆角遮罩：供全窗口背景图片裁切使用。
    Rectangle {
        id: windowMask
        anchors.fill: parent
        radius: root.windowRadius
        color: "black"
        antialiasing: true
        visible: false
    }

    // 点击遮罩：侧边栏展开时，点击其外任意区域自动收起。
    MouseArea {
        id: clickAway
        z: 9
        anchors.fill: parent
        visible: !root.readerFullScreen && !sidebar.collapsed
        onClicked: sidebar.collapsed = true
    }

    // 侧边栏：内缩一圈的浮层胶囊，覆盖在内容左上侧。
    Sidebar {
        id: sidebar
        visible: !root.readerFullScreen
        z: 10
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.topMargin: root.windowGap
        anchors.bottomMargin: root.windowGap
        anchors.leftMargin: root.windowGap
        currentPage: {
            switch (root.pageTitle) {
            case "下载": return "download"
            case "网站": return "site"
            case "翻译": return "translate"
            case "阅读": return "home"
            case "详情": return "home"
            case "设置": return "settings"
            default: return "home"
            }
        }
        onPageSelected: function(name) { root.navigate(name) }
    }

    // 顶部空隙：按住可拖动窗口。
    MouseArea {
        z: 20
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Math.max(root.windowGap - 6, 0)
        cursorShape: Qt.ArrowCursor
        acceptedButtons: Qt.LeftButton
        onPressed: if (mouse.button === Qt.LeftButton) root.startSystemMove()
    }
    // 边缘缩放：无标题栏后手动触发系统缩放。
    MouseArea { z: 30; anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; height: 6; cursorShape: Qt.SizeVerCursor; acceptedButtons: Qt.LeftButton; onPressed: if (mouse.button === Qt.LeftButton) root.startSystemResize(Qt.TopEdge) }
    MouseArea { z: 30; anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; height: 6; cursorShape: Qt.SizeVerCursor; acceptedButtons: Qt.LeftButton; onPressed: if (mouse.button === Qt.LeftButton) root.startSystemResize(Qt.BottomEdge) }
    MouseArea { z: 30; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.left: parent.left; width: 6; cursorShape: Qt.SizeHorCursor; acceptedButtons: Qt.LeftButton; onPressed: if (mouse.button === Qt.LeftButton) root.startSystemResize(Qt.LeftEdge) }
    MouseArea { z: 30; anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.right: parent.right; width: 6; cursorShape: Qt.SizeHorCursor; acceptedButtons: Qt.LeftButton; onPressed: if (mouse.button === Qt.LeftButton) root.startSystemResize(Qt.RightEdge) }

    MouseArea { z: 31; anchors.top: parent.top; anchors.left: parent.left; width: 14; height: 14; cursorShape: Qt.SizeFDiagCursor; acceptedButtons: Qt.LeftButton; onPressed: if (mouse.button === Qt.LeftButton) root.startSystemResize(Qt.TopEdge | Qt.LeftEdge) }
    MouseArea { z: 31; anchors.top: parent.top; anchors.right: parent.right; width: 14; height: 14; cursorShape: Qt.SizeBDiagCursor; acceptedButtons: Qt.LeftButton; onPressed: if (mouse.button === Qt.LeftButton) root.startSystemResize(Qt.TopEdge | Qt.RightEdge) }
    MouseArea { z: 31; anchors.bottom: parent.bottom; anchors.left: parent.left; width: 14; height: 14; cursorShape: Qt.SizeBDiagCursor; acceptedButtons: Qt.LeftButton; onPressed: if (mouse.button === Qt.LeftButton) root.startSystemResize(Qt.BottomEdge | Qt.LeftEdge) }
    MouseArea { z: 31; anchors.bottom: parent.bottom; anchors.right: parent.right; width: 14; height: 14; cursorShape: Qt.SizeFDiagCursor; acceptedButtons: Qt.LeftButton; onPressed: if (mouse.button === Qt.LeftButton) root.startSystemResize(Qt.BottomEdge | Qt.RightEdge) }

    Component { id: homeComp; HomePage {} }
    Component { id: downloadComp; DownloadPage {} }
    Component { id: siteComp; SitePage {} }
    Component { id: translateHubComp; TranslateHubPage {} }
    Component {
        id: readerComp
        ReaderPage {
            applicationFullScreen: root.readerFullScreen
            topOverlayInset: root.aiActive && !root.readerFullScreen ? aiIsland.width + 30 : 0
            onFullScreenRequested: {
                sidebar.collapsed = true
                root.readerExpanded = !root.readerExpanded
            }
        }
    }
    Component { id: detailComp; NovelDetailPage {} }
    Component { id: settingsComp; SettingsPage {} }

    // 术语表窗口：独立于页面栈存在，切换页面时不会被销毁。
    GlossaryWindow { id: glossaryWindow }
}
