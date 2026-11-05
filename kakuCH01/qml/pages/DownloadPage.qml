import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KakuCH

// 下载页：输入作品网址，选择章节并下载为 txt。
Page {
    id: downloadPage
    readonly property bool taskBusy: controller.translating || controller.murasakiTranslating
                                     || controller.qwenTranslating || controller.qwenExtracting
                                     || controller.siteOverviewTranslating
    objectName: "download"
    background: Rectangle { color: Theme.pageBackground }

    Connections {
        target: controller
        function onNotify(message) {
            statusLabel.text = message
        }
        function onWorkFailed(error) {
            statusLabel.text = error
        }
        // 「网站」页点「整本下载」时会把网址送过来，这里自动打开
        function onPendingDownloadUrlChanged() { consumePendingUrl() }
    }

    function consumePendingUrl() {
        if (controller.pendingDownloadUrl.length === 0)
            return
        var u = controller.pendingDownloadUrl
        controller.clearPendingDownloadUrl()
        urlField.text = u
        controller.fetchWork(u)
    }

    Component.onCompleted: consumePendingUrl()

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

            // 标题
            Item { Layout.preferredHeight: 20 }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                implicitHeight: siteContent.implicitHeight + 32
                radius: Theme.radius
                color: Theme.card
                border.width: 1
                border.color: Theme.border
                LiquidGlass { }

                ColumnLayout {
                    id: siteContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 10
                    Text {
                        text: "可下载站点"
                        color: Theme.textPrimary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.fontBody
                        font.bold: true
                    }
                    Row {
                        id: siteLinks
                        Layout.fillWidth: true
                        spacing: 10
                        readonly property int columns: Math.max(1, siteRepeater.count)
                        Repeater {
                            id: siteRepeater
                            model: controller.supportedSites()
                            delegate: Button {
                                id: siteLink
                                required property var modelData
                                width: Math.floor((siteLinks.width - (siteLinks.columns - 1) * siteLinks.spacing) / siteLinks.columns)
                                height: 48
                                leftPadding: 12
                                rightPadding: 12
                                hoverEnabled: true
                                Accessible.name: modelData.name + "，用默认浏览器打开"
                                ToolTip.visible: hovered
                                ToolTip.delay: 600
                                ToolTip.text: modelData.url
                                scale: down ? 0.98 : 1
                                Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                                background: Rectangle {
                                    radius: Theme.radiusSmall
                                    color: siteLink.down ? Theme.glassFillStrong : Theme.glassButton
                                    opacity: siteLink.hovered || siteLink.visualFocus ? 0.8 : 0.45
                                    border.width: 1
                                    border.color: siteLink.visualFocus ? Theme.accent : Theme.glassBorder
                                    LiquidGlass { }
                                    Behavior on opacity { NumberAnimation { duration: 150 } }
                                }
                                contentItem: RowLayout {
                                    spacing: 8
                                    Text {
                                        Layout.fillWidth: true
                                        text: siteLink.modelData.name
                                        color: Theme.textPrimary
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.fontBody
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        text: "↗"
                                        color: Theme.accent
                                        font.pixelSize: 18
                                    }
                                }
                                onClicked: Qt.openUrlExternally(modelData.url)
                            }
                        }
                    }
                }
            }

            // URL 输入
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                spacing: Theme.spacing

                PrimaryButton {
                    text: controller.fetching ? "获取中…" : "获取章节"
                    enabled: !controller.fetching && !controller.downloading && urlField.text.length > 0
                    onClicked: {
                        statusLabel.text = "正在获取章节列表…"
                        controller.fetchWork(urlField.text.trim())
                    }
                }
                TextField {
                    id: urlField
                    Layout.fillWidth: true
                    placeholderText: "在网站复制作品链接，粘贴后点击「获取章节」"
                    selectByMouse: true
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.fontBody
                    color: Theme.textPrimary
                    background: Rectangle {
                        radius: Theme.radius
                        color: Theme.card
                        LiquidGlass { }
                        border.width: 1
                        border.color: Theme.border
                    }
                }
            }

            // 作品信息
            Rectangle {
                visible: controller.workTitle.length > 0
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.preferredHeight: 64
                radius: Theme.radius
                color: Theme.card
                LiquidGlass { }
                border.width: 1
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 16
                    SecondaryButton {
                        text: "×"
                        enabled: !controller.fetching && !controller.downloading
                        implicitWidth: 38
                        onClicked: {
                            searchField.text = ""
                            statusLabel.text = ""
                            controller.clearDownloadWork()
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: controller.workTitle
                        color: Theme.textPrimary
                        font.family: Theme.uiFont
                        font.pixelSize: 16
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Text {
                        text: "共 " + controller.chapterCount + " 章"
                        color: Theme.textSecondary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.fontBody
                    }
                }
            }

            // 章节操作
            RowLayout {
                visible: controller.chapterCount > 0
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                spacing: Theme.spacing

                SecondaryButton { text: "全选"; enabled: !downloadPage.taskBusy; onClicked: controller.setAllSelected(true) }
                SecondaryButton { text: "取消全选"; enabled: !downloadPage.taskBusy; onClicked: controller.setAllSelected(false) }
                SecondaryButton { text: "反选"; enabled: !downloadPage.taskBusy; onClicked: controller.invertSelection() }
                Item { Layout.fillWidth: true }
                Text {
                    text: "已选择 " + controller.selectedCount + " 章"
                    color: Theme.textSecondary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.fontBody
                }
            }

            // 搜索章节
            RowLayout {
                visible: controller.chapterCount > 0
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                spacing: Theme.spacing

                TextField {
                    id: searchField
                    Layout.fillWidth: true
                    placeholderText: "搜索章节标题…"
                    selectByMouse: true
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.fontBody
                    color: Theme.textPrimary
                    onTextChanged: controller.setChapterFilter(text)
                    background: Rectangle {
                        radius: Theme.radius
                        color: Theme.card
                        LiquidGlass { }
                        border.width: 1
                        border.color: Theme.border
                    }
                }
            }

            // 章节列表
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.preferredHeight: 300
                radius: Theme.radius
                color: Theme.card
                LiquidGlass { }
                border.width: 1
                border.color: Theme.border

                ListView {
                    anchors.fill: parent
                    anchors.margins: 6
                    clip: true
                    model: controller.chapterModel
                    spacing: 4

                    delegate: Rectangle {
                        width: ListView.view.width - 12
                        height: 46
                        radius: Theme.radiusSmall
                        color: model.selected ? Qt.rgba(0.4, 0.35, 1.0, 0.06) : "transparent"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 10

                            CheckBox {
                                checked: model.selected
                                enabled: !downloadPage.taskBusy
                                opacity: enabled ? 1 : 0.4
                                onToggled: { if (!downloadPage.taskBusy) model.selected = checked }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: model.title
                                color: Theme.textPrimary
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.fontBody
                                elide: Text.ElideRight
                            }
                            StatusBadge {
                                visible: model.status !== "未下载"
                                status: model.status
                                text: model.status
                            }
                        }
                    }
                }
            }

            // 开始下载
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                spacing: Theme.spacing

                Item { Layout.fillWidth: true }
                PrimaryButton {
                    text: controller.downloading ? "下载中…" : "开始下载"
                    enabled: !controller.downloading && controller.selectedCount > 0
                    onClicked: controller.downloadSelected("")
                }
                SecondaryButton {
                    text: "取消"
                    visible: controller.downloading
                    onClicked: controller.cancelDownload()
                }
            }

            // 进度
            Column {
                visible: controller.downloading || statusLabel.text.length > 0
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.bottomMargin: 24
                spacing: 6

                ProgressBar {
                    visible: controller.downloading
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, controller.downloadTotal)
                    value: controller.downloadCurrent
                }
                Text {
                    id: statusLabel
                    Layout.fillWidth: true
                    text: controller.downloadMessage
                    color: Theme.textSecondary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                }
            }

            Item { Layout.preferredHeight: 20 }
        }
    }
}

