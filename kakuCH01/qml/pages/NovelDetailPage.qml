import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KakuCH

// 小说详情页：展示小说信息与章节状态，提供继续阅读/下载/翻译入口。
Page {
    id: page

    // 打开 / 返回的材质与几何过渡由主窗口统一控制。
    objectName: "detail"
    background: Rectangle { color: Theme.pageBackground }

    readonly property int catalogTab: catalogTabs.currentIndex
    onCatalogTabChanged: {
        rebuildTranslatedChapters()
        rebuildBilingualChapters()
    }
    property int libraryRevision: 0

    // ---- 检查更新 / 更新章节：状态机 ----
    // idle：可点「检查更新」；checking：检查中…（点击无效）
    // ready：发现缺失章节，按钮变「更新章节」；updating：更新中…（点击无效）
    // done：检查完成且完整 → 「检查更新」变暗；更新完成 → 「更新完成」变暗（重开本页复原）
    property string updateState: "idle"
    property bool updateComplete: false
    property int updateMissingCount: 0
    property bool updatePendingResult: false
    property bool updateMinElapsed: false
    property int updateResultCount: 0

    function updateButtonText() {
        if (updateState === "checking")
            return "检查中..."
        if (updateState === "updating")
            return "更新中..."
        if (updateState === "done")
            return "更新完成"
        if (updateState === "ready")
            return "更新章节"
        return "检查更新"
    }

    function handleUpdateClick() {
        if (updateState === "idle") {
            updateState = "checking"
            updateMissingCount = 0
            controller.checkCurrentNovelUpdate()
            return
        }
        if (updateState === "ready") {
            updateState = "updating"
            updatePendingResult = false
            updateMinElapsed = false
            updateMinTimer.restart()   // 一次性：保证「更新完成」至少 3.5 秒后才出现
            controller.updateCurrentNovel()
        }
    }

    function finishUpdateSuccess() {
        updatePendingResult = false
        updateState = "done"
        Navigation.hint("更新完成")
        updateDoneTimer.restart()   // 3 秒后回到「检查更新」，之后可再次检查
    }

    // 一次性计时器：只在「更新中」期间跑，用最短时长兜住过快的更新
    Timer {
        id: updateMinTimer
        interval: 3500
        repeat: false
        onTriggered: {
            page.updateMinElapsed = true
            if (page.updateState === "updating" && page.updatePendingResult)
                page.finishUpdateSuccess()
        }
    }

    // 「更新完成」显示 3 秒后自动回到「检查更新」
    Timer {
        id: updateDoneTimer
        interval: 3000
        repeat: false
        onTriggered: {
            if (page.updateState === "done")
                page.updateState = "idle"
        }
    }

    property var translatedChapters: []
    property var bilingualChapters: []
    property int glossaryRevision: 0

    Connections {
        target: controller
        function onLibraryRefreshed() {
            libraryRevision++
            rebuildTranslatedChapters()
            rebuildBilingualChapters()
            rebuildGlossary()
        }
        function onGlossaryChanged() {
            rebuildGlossary()
        }
        // 换小说后术语表信息要重算（启用的是人工还是 Qwen 是每本书各自的设置）
        function onCurrentNovelRowChanged() {
            rebuildGlossary()
        }
        function onNovelUpdateChecked(state, missingCount) {
            if (state === "complete") {
                // 不再变暗：回到「检查更新」，可以反复检查
                page.updateState = "idle"
                Navigation.hint("当前章节已完整，无需更新")
            } else if (state === "incomplete") {
                page.updateMissingCount = missingCount
                page.updateState = "ready"
                Navigation.hint("发现 " + missingCount + " 个缺失章节，可点击「更新章节」")
            } else {
                page.updateState = "idle"
                Navigation.hint("检查更新失败，请稍后再试")
            }
        }
        function onNovelUpdateFinished(ok, addedCount) {
            if (!ok) {
                page.updateState = "ready"
                Navigation.hint("更新失败，请稍后再试")
                return
            }
            page.updatePendingResult = true
            page.updateResultCount = addedCount
            if (page.updateMinElapsed)
                page.finishUpdateSuccess()
        }
    }

    function rebuildTranslatedChapters() {
        // 默认日文目录无需逐章检查译文；切换到中文目录时才构建。
        if (catalogTab !== 1) { translatedChapters = []; return }
        var names = controller.currentNovelChapterNames()
        var result = []
        for (var i = 0; i < names.length; i++) {
            if (controller.currentNovelChapterTranslated(i))
                result.push({ sourceIndex: i, name: names[i] })
        }
        translatedChapters = result
    }

    function rebuildBilingualChapters() {
        if (catalogTab !== 2) { bilingualChapters = []; return }
        var names = controller.currentNovelBilingualChapterNames()
        var result = []
        for (var i = 0; i < names.length; i++) {
            result.push({ name: names[i] })
        }
        bilingualChapters = result
    }

    function chapterIndexForName(name) {
        return controller.currentNovelChapterNames().indexOf(name)
    }
    function chapterTitleForName(name) {
        var idx = chapterIndexForName(name)
        return idx >= 0 ? controller.currentNovelChapterTitle(idx) : name
    }

    function rebuildGlossary() {
        glossaryRevision++
    }

    Component.onCompleted: {
        rebuildTranslatedChapters()
        rebuildBilingualChapters()
        rebuildGlossary()
    }

    GlassDialog {
        id: deleteTranslationDialog
        acceptText: "删除"
        destructive: true
        title: "删除翻译"
        property int pendingIndex: -1
        contentItem: Label {
            wrapMode: Text.Wrap
            text: "确定删除这一章的译文原版和编辑版吗？\n日文原文不会删除，删除后需重新翻译才能再次生成译文。"
            color: Theme.textPrimary
            font.family: Theme.uiFont
            font.pixelSize: Theme.fontBody
        }
        onAccepted: {
            if (pendingIndex >= 0)
                controller.deleteChapterTranslation(pendingIndex)
            pendingIndex = -1
        }
    }

    GlassDialog {
        id: deleteBilingualDialog
        acceptText: "删除"
        destructive: true
        title: "删除双语资源"
        property string pendingName: ""
        contentItem: Label {
            wrapMode: Text.Wrap
            text: "确定删除这一章的双语资源原版和编辑版吗？\n中文译文不会被删除，之后可点击“生成双语资源”重新生成。"
            color: Theme.textPrimary
            font.family: Theme.uiFont
            font.pixelSize: Theme.fontBody
        }
        onAccepted: {
            if (pendingName.length > 0)
                controller.deleteBilingualResource(pendingName)
            pendingName = ""
        }
    }

    GlassDialog {
        id: clearBilingualDialog
        acceptText: "删除"
        destructive: true
        title: "清空双语资源"
        contentItem: Label {
            wrapMode: Text.Wrap
            text: "确定删除这本小说的全部双语资源吗？\n中文译文不会被删除，之后可点击“生成双语资源”重新生成。"
            color: Theme.textPrimary
            font.family: Theme.uiFont
            font.pixelSize: Theme.fontBody
        }
        onAccepted: controller.clearBilingualResources()
    }

    GlassDialog {
        id: clearProgressDialog
        acceptText: "删除"
        destructive: true
        property string directoryMode: "original"
        property string directoryLabel: "日文"
        title: "删除阅读记录"
        contentItem: Label {
            text: "确定删除《" + controller.currentNovelTitle() + "》的「" + clearProgressDialog.directoryLabel
                  + "」目录阅读记录与书签吗？其他目录的记录将保留。"
            wrapMode: Text.Wrap
            color: Theme.textPrimary
            font.family: Theme.uiFont
            font.pixelSize: Theme.fontBody
        }
        onAccepted: {
            controller.clearCurrentNovelReadingProgress(directoryMode)
            page.libraryRevision++
        }
    }


    // 删除整本书的确认弹窗
    GlassDialog {
        id: deleteNovelDialog
        acceptText: "删除"
        destructive: true
        title: "删除书籍"
        contentItem: Label {
            text: "确定删除《" + controller.currentNovelTitle() + "》吗？\n"
                  + "日文原文、译文、双语资源、术语表、概要翻译与阅读进度都会被删除，且无法恢复。"
            color: Theme.textPrimary
            font.family: Theme.uiFont
            font.pixelSize: Theme.fontBody
            wrapMode: Text.Wrap
        }
        onAccepted: {
            if (controller.deleteCurrentNovel())
                Navigation.navigate("home")
        }
    }

    Flickable {
        id: scroll
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: contentColumn.height
        ScrollBar.vertical: ScrollBar { }

        ColumnLayout {
            id: contentColumn
            width: scroll.width
            spacing: Theme.spacing

            // 返回 + 标题
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.topMargin: 20
                Layout.rightMargin: 24
                spacing: Theme.spacing

                SecondaryButton {
                    text: "← 返回首页"
                    onClicked: Navigation.closeDetail()
                }

                // 标题固定在当前位置：放不下就缩小字号 / 换行，不改变高度、不挤压下方
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    clip: true

                    Text {
                        id: titleSizing
                        anchors.fill: parent
                        visible: false
                        text: controller.currentNovelTitle()
                        color: Theme.textPrimary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.fontHeading
                        font.bold: true
                        minimumPixelSize: 11
                        fontSizeMode: Text.Fit
                        wrapMode: Text.Wrap
                        verticalAlignment: Text.AlignVCenter
                    }
                    TextEdit {
                        id: novelTitleText
                        anchors.fill: parent
                        text: controller.currentNovelTitle()
                        readOnly: true
                        selectByMouse: true
                        cursorVisible: false
                        color: Theme.textPrimary
                        selectionColor: Theme.accent
                        selectedTextColor: "white"
                        font.family: Theme.uiFont
                        font.pixelSize: titleSizing.fontInfo.pixelSize
                        font.bold: true
                        wrapMode: TextEdit.Wrap
                        verticalAlignment: TextEdit.AlignVCenter
                    }
                }
            }

            // 信息卡片
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.preferredHeight: 150
                radius: Theme.radius
                color: Theme.card
                LiquidGlass { }
                border.width: 1
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    anchors.topMargin: 16
                    anchors.bottomMargin: 16
                    spacing: 16
                    // 左：按钮 → 下面三行信息
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        spacing: 8

                        RowLayout {
                            spacing: 8

                            PrimaryButton {
                                text: "继续阅读"
                                onClicked: controller.openCurrentNovelForReading()
                            }
                            SecondaryButton {
                                id: updateButton
                                text: page.updateButtonText()
                                enabled: page.updateState === "idle" || page.updateState === "ready"
                                onClicked: page.handleUpdateClick()
                            }
                            SecondaryButton {
                                text: "开始翻译"
                                onClicked: {
                                    controller.queueCurrentNovelForTranslation()
                                    Navigation.navigate("translate")
                                }
                            }
                        }

                        // 作者：名字可鼠标选中复制
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: "作者："
                                color: Theme.textPrimary
                                font.family: Theme.uiFont
                                font.weight: Font.DemiBold
                                font.pixelSize: Theme.fontSmall
                            }
                            TextEdit {
                                id: cardAuthor
                                Layout.fillWidth: true
                                Layout.preferredHeight: 20
                                readOnly: true
                                selectByMouse: true
                                cursorVisible: false
                                clip: true
                                wrapMode: TextEdit.NoWrap
                                text: controller.currentNovelAuthor().length > 0
                                      ? controller.currentNovelAuthor() : "未知"
                                color: Theme.textPrimary
                                selectionColor: Theme.accent
                                font.family: Theme.uiFont
                                font.weight: Font.DemiBold
                                font.pixelSize: Theme.fontSmall
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "已译 " + controller.currentNovelTranslatedCount() + "/"
                                  + controller.currentNovelChapterCount()
                                  + "     已阅 " + controller.currentNovelReadCount() + "/"
                                  + controller.currentNovelChapterCount()
                            color: Theme.textPrimary
                            font.family: Theme.uiFont
                            font.weight: Font.DemiBold
                            font.pixelSize: Theme.fontSmall
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: "作品网址："
                                color: Theme.textPrimary
                                font.family: Theme.uiFont
                                font.weight: Font.DemiBold
                                font.pixelSize: Theme.fontSmall
                            }
                            Text {
                                id: urlText
                                Layout.fillWidth: true
                                text: controller.currentNovelUrl()
                                color: urlMouse.containsMouse ? Theme.accent : Theme.textPrimary
                                font.family: Theme.uiFont
                                font.weight: Font.DemiBold
                                font.pixelSize: Theme.fontSmall
                                font.underline: urlMouse.containsMouse
                                elide: Text.ElideRight

                                MouseArea {
                                    id: urlMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: Qt.openUrlExternally(controller.currentNovelUrl())
                                }
                            }
                        }
                    }

                    // 右：术语表（按钮 + 小灰字）
                    ColumnLayout {
                        Layout.alignment: Qt.AlignTop
                        spacing: 8

                        PrimaryButton {
                            text: "打开术语表"
                            onClicked: Navigation.openGlossary()
                        }
                        Text {
                            Layout.preferredWidth: 320
                            text: {
                                glossaryRevision
                                return "人工术语表 " + controller.glossaryCountIn("manual")
                                + " 条 · Qwen术语表 " + controller.glossaryCountIn("qwen")
                                + " 条 · 当前翻译启用："
                                + (controller.activeGlossaryKind() === "qwen"
                                   ? "Qwen术语表" : "人工术语表")
                            }
                            color: Theme.textPrimary
                            font.family: Theme.uiFont
                            font.weight: Font.DemiBold
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }
                    }
}
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                implicitHeight: catalogueContent.implicitHeight + 32
                radius: Theme.radius
                color: Theme.card
                border.width: 1
                border.color: Theme.border
                LiquidGlass { }
                ColumnLayout {
                    id: catalogueContent
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 14
                    // 章节目录标题与操作
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing

                        Text {
                            Layout.fillWidth: true
                            text: "章节目录"
                            color: Theme.textPrimary
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.fontTitle
                            font.bold: true
                        }
                    }

                    // 日文 / 中文切换式章节目录
                    TabBar {
                        id: catalogTabs
                        objectName: "catalogTabs"
                        Layout.fillWidth: true
                        background: Rectangle { color: "transparent" }

                        TabButton {
                            id: jpTab
                            text: "日文"
                            width: implicitWidth
                            background: Rectangle {
                                color: jpTab.checked ? Qt.rgba(0.4, 0.35, 1.0, 0.12) : "transparent"
                                radius: Theme.radiusSmall
                            }
                            contentItem: Text {
                                text: jpTab.text
                                color: jpTab.checked ? Theme.accent : Theme.textPrimary
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.fontBody
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                        TabButton {
                            id: zhTab
                            text: "中文"
                            width: implicitWidth
                            background: Rectangle {
                                color: zhTab.checked ? Qt.rgba(0.4, 0.35, 1.0, 0.12) : "transparent"
                                radius: Theme.radiusSmall
                            }
                            contentItem: Text {
                                text: zhTab.text
                                color: zhTab.checked ? Theme.accent : Theme.textPrimary
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.fontBody
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                        TabButton {
                            id: bilingualTab
                            text: "双语"
                            width: implicitWidth
                            background: Rectangle {
                                color: bilingualTab.checked ? Qt.rgba(0.4, 0.35, 1.0, 0.12) : "transparent"
                                radius: Theme.radiusSmall
                            }
                            contentItem: Text {
                                text: bilingualTab.text
                                color: bilingualTab.checked ? Theme.accent : Theme.textPrimary
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.fontBody
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: catalogTabs.currentIndex === 2
                        spacing: 8
                        SecondaryButton {
                            text: "生成双语资源"
                            onClicked: controller.generateBilingualResources()
                        }
                        SecondaryButton {
                            text: "清空双语资源"
                            onClicked: clearBilingualDialog.open()
                        }
                        Item { Layout.fillWidth: true }
                    }

                    StackLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 440
                        currentIndex: catalogTabs.currentIndex

                        // 日文章节目录：显示全部原文章节，标记未翻译/已翻译与阅读状态。
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: Theme.radius
                            color: "transparent"

                            ListView {
                                reuseItems: true
                                anchors.fill: parent
                                anchors.margins: 8
                                clip: true
                                model: {
                                    libraryRevision
                                    return page.catalogTab === 0 ? controller.currentNovelChapterNames() : []
                                }
                                spacing: 2

                                delegate: Rectangle {
                                    width: ListView.view.width - 4
                                    height: 42
                                    radius: Theme.radiusSmall
                                    color: controller.currentNovelChapterRead(index, "jp")
                                           ? Qt.rgba(0.4, 0.35, 1.0, 0.08) : "transparent"
                                    scale: jpRowMouse.pressed ? 0.97 : 1.0
                                    transformOrigin: Item.Center
                                    Behavior on scale { SpringAnimation { spring: 2.6; damping: 0.78; epsilon: 0.01 } }

                                    MouseArea {
                                        anchors.fill: parent
                                        id: jpRowMouse
                                        onClicked: controller.openNovelChapterFile(modelData, "jp")
                                    }

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 10
                                        spacing: 8

                                        Text {
                                            Layout.fillWidth: true
                                            text: controller.currentNovelChapterTitle(index)
                                            color: Theme.textPrimary
                                            font.family: Theme.uiFont
                                            font.pixelSize: Theme.fontBody
                                            elide: Text.ElideRight
                                        }
                                        StatusBadge {
                                            status: controller.currentNovelChapterRead(index, "jp") ? "COMPLETED" : "WAITING"
                                            text: controller.currentNovelChapterRead(index, "jp") ? "已读" : "未读"
                                        }
                                        StatusBadge {
                                            status: {
                                                libraryRevision
                                                return controller.currentNovelChapterTranslated(index) ? "COMPLETED" : "WAITING"
                                            }
                                            text: {
                                                libraryRevision
                                                return controller.currentNovelChapterTranslated(index) ? "已翻译" : "未翻译"
                                            }
                                        }
                                        SecondaryButton {
                                            text: "翻译"
                                            onClicked: {
                                                controller.queueChapterForTranslation(index)
                                                Navigation.navigate("translate")
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // 中文章节目录：只显示译文目录中实际存在的章节。
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: Theme.radius
                            color: "transparent"

                            ListView {
                                reuseItems: true
                                anchors.fill: parent
                                anchors.margins: 8
                                clip: true
                                model: page.catalogTab === 1 ? translatedChapters : []
                                spacing: 2

                                delegate: Rectangle {
                                    width: ListView.view.width - 4
                                    height: 42
                                    radius: Theme.radiusSmall
                                    color: controller.currentNovelChapterRead(modelData.sourceIndex, "zh")
                                           ? Qt.rgba(0.4, 0.35, 1.0, 0.08) : "transparent"
                                    scale: zhRowMouse.pressed ? 0.97 : 1.0
                                    transformOrigin: Item.Center
                                    Behavior on scale { SpringAnimation { spring: 2.6; damping: 0.78; epsilon: 0.01 } }

                                    MouseArea {
                                        anchors.fill: parent
                                        id: zhRowMouse
                                        onClicked: controller.openNovelChapterFile(modelData.name, "zh")
                                    }

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 10
                                        spacing: 8

                                        Text {
                                            Layout.preferredWidth: 30
                                            text: (modelData.sourceIndex + 1).toString().padStart(2, '0')
                                            color: Theme.textSecondary
                                            font.family: Theme.uiFont
                                            font.pixelSize: Theme.fontBody
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: controller.currentNovelChapterTitle(modelData.sourceIndex)
                                            color: Theme.textPrimary
                                            font.family: Theme.uiFont
                                            font.pixelSize: Theme.fontBody
                                            elide: Text.ElideRight
                                        }
                                        StatusBadge {
                                            status: controller.currentNovelChapterRead(modelData.sourceIndex, "zh") ? "COMPLETED" : "WAITING"
                                            text: controller.currentNovelChapterRead(modelData.sourceIndex, "zh") ? "已读" : "未读"
                                        }
                                        SecondaryButton {
                                            text: "删除翻译"
                                            onClicked: {
                                                deleteTranslationDialog.pendingIndex = modelData.sourceIndex
                                                deleteTranslationDialog.open()
                                            }
                                        }
                                    }
                                }
                            }

                            Text {
                                visible: translatedChapters.length === 0
                                anchors.centerIn: parent
                                text: "暂无已翻译章节"
                                color: Theme.textSecondary
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.fontBody
                            }
                        }

                        // 双文章节目录：只显示已生成中日双语阅读资源的章节。
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: Theme.radius
                            color: "transparent"

                            ListView {
                                reuseItems: true
                                anchors.fill: parent
                                anchors.margins: 8
                                clip: true
                                model: page.catalogTab === 2 ? bilingualChapters : []
                                spacing: 2

                                delegate: Rectangle {
                                    width: ListView.view.width - 4
                                    height: 42
                                    radius: Theme.radiusSmall
                                    color: {
                                        var idx = chapterIndexForName(modelData.name)
                                        return idx >= 0 && controller.currentNovelChapterRead(idx, "bilingual")
                                               ? Qt.rgba(0.4, 0.35, 1.0, 0.08) : "transparent"
                                    }
                                    scale: biRowMouse.pressed ? 0.97 : 1.0
                                    transformOrigin: Item.Center
                                    Behavior on scale { SpringAnimation { spring: 2.6; damping: 0.78; epsilon: 0.01 } }

                                    MouseArea {
                                        id: biRowMouse
                                        anchors.fill: parent
                                        onClicked: controller.openNovelBilingualChapterFile(modelData.name)
                                    }

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 10
                                        spacing: 8

                                        Text {
                                            Layout.fillWidth: true
                                            text: chapterTitleForName(modelData.name)
                                            color: Theme.textPrimary
                                            font.family: Theme.uiFont
                                            font.pixelSize: Theme.fontBody
                                            elide: Text.ElideRight
                                        }
                                        StatusBadge {
                                            status: {
                                                var idx = chapterIndexForName(modelData.name)
                                                return idx >= 0 && controller.currentNovelChapterRead(idx, "bilingual")
                                                       ? "COMPLETED" : "WAITING"
                                            }
                                            text: {
                                                var idx = chapterIndexForName(modelData.name)
                                                return idx >= 0 && controller.currentNovelChapterRead(idx, "bilingual")
                                                       ? "已读" : "未读"
                                            }
                                        }
                                        StatusBadge {
                                            status: "COMPLETED"
                                            text: "双语"
                                        }
                                        SecondaryButton {
                                            text: "删除"
                                            onClicked: {
                                                deleteBilingualDialog.pendingName = modelData.name
                                                deleteBilingualDialog.open()
                                            }
                                        }
                                    }
                                }
                            }

                            Text {
                                visible: bilingualChapters.length === 0
                                anchors.centerIn: parent
                                text: "暂无双语阅读资源"
                                color: Theme.textSecondary
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.fontBody
                            }
                        }
                    }

                }
            }

            // 页面最下方（章节列表之后）右对齐的删除按钮：
            // 需要向下滚动到页面底部才能看到，不覆盖目录组件。
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.topMargin: 12

                Item { Layout.fillWidth: true }

                SecondaryButton {
                    text: "删除阅读记录"
                    onClicked: {
                        clearProgressDialog.directoryMode = ["original", "translation", "bilingual"][catalogTabs.currentIndex]
                        clearProgressDialog.directoryLabel = ["日文", "中文", "双语"][catalogTabs.currentIndex]
                        clearProgressDialog.open()
                    }
                }
                SecondaryButton {
                    text: "删除书籍"
                    palette.buttonText: Colors.danger
                    onClicked: deleteNovelDialog.open()
                }
            }

            Item { Layout.preferredHeight: 20 }
        }
    }
}
