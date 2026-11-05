import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KakuCH

// 模型控制栏：左侧按钮按状态切换「启动模型 / 关闭模型」，右侧选择模型。
// 选择按钮的三根线条在展开时平滑变形成 X；展开后向下浮出磨砂玻璃的模型列表，
// 高度随模型数量变化（不做成全屏），顶部居中显示当前所选模型名（过长省略）。
Item {
    id: bar

    property string currentModelName: ""
    property string engineKey: ""
    property var modelOptions: []
    property bool running: false
    property bool starting: false
    property bool busy: false
    onBusyChanged: { if (busy) { expanded = false; if (gpuConfig) gpuConfig.close() } }
    onVisibleChanged: if (!visible && gpuConfig) gpuConfig.close()
    property string startText: "启动模型"
    property string stopText: "关闭模型"
    property Component extraActions: null

    signal startRequested()
    signal stopRequested()
    signal modelSelected(string path)
    signal refreshRequested()

    implicitHeight: 52
    property bool expanded: false

    ModelGpuConfigDialog {
        id: gpuConfig
        engineKey: bar.engineKey
        busy: bar.busy || bar.starting
    }

    function fileName(p) {
        return String(p).split("/").pop().split("\\").pop()
    }

    Rectangle {
        id: barBg
        anchors.fill: parent
        radius: Theme.radius
        color: Theme.card
        LiquidGlass { }
        border.width: 1
        border.color: Theme.border

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 12
            spacing: 10

            PrimaryButton {
                text: bar.starting ? "启动中…" : (bar.running ? bar.stopText : bar.startText)
                enabled: !bar.starting && !bar.busy
                onClicked: {
                    if (bar.running)
                        bar.stopRequested()
                    else
                        bar.startRequested()
                }
            }
            Loader {
                sourceComponent: bar.extraActions
                visible: sourceComponent !== null
                Layout.preferredWidth: item ? item.implicitWidth : 0
                Layout.preferredHeight: item ? item.implicitHeight : 0
            }

            Item { Layout.fillWidth: true }

            Text {
                text: bar.currentModelName
                visible: bar.currentModelName.length > 0
                color: Theme.textPrimary
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideMiddle
                Layout.maximumWidth: 200
            }

            // 选择模型按钮：汉堡 ↔ X
            Rectangle {
                id: menuBtn
                enabled: !bar.busy
                opacity: enabled ? 1 : 0.4
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                radius: 12
                color: menuMouse.containsMouse ? Qt.rgba(0, 0, 0, 0.05) : "transparent"

                Item {
                    id: burger
                    anchors.centerIn: parent
                    width: 18
                    height: 14

                    Rectangle {
                        width: parent.width
                        height: 2
                        radius: 1
                        color: Theme.textPrimary
                        x: 0
                        y: bar.expanded ? 6 : 0
                        rotation: bar.expanded ? 45 : 0
                        transformOrigin: Item.Center
                        Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                        Behavior on rotation { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                    }
                    Rectangle {
                        width: parent.width
                        height: 2
                        radius: 1
                        color: Theme.textPrimary
                        x: 0
                        y: 6
                        opacity: bar.expanded ? 0 : 1
                        scale: bar.expanded ? 0.4 : 1
                        Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                        Behavior on scale { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                    }
                    Rectangle {
                        width: parent.width
                        height: 2
                        radius: 1
                        color: Theme.textPrimary
                        x: 0
                        y: bar.expanded ? 6 : 12
                        rotation: bar.expanded ? -45 : 0
                        transformOrigin: Item.Center
                        Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                        Behavior on rotation { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                    }
                }

                MouseArea {
                    id: menuMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: bar.expanded = !bar.expanded
                }
            }
        }
    }

    // 展开的模型列表：磨砂玻璃，高度按模型数量（最多 6 行，其余滚动）
    Rectangle {
        id: panel
        enabled: !bar.busy
        z: 60
        visible: bar.expanded
        anchors.top: barBg.bottom
        anchors.topMargin: 8
        // 与栏位同宽、向下展开的玻璃面板（参考网站页标签面板的形态，但不占满屏）
        anchors.left: barBg.left
        anchors.right: barBg.right
        // 头部（当前模型名）+ 分隔线 + 列表（按数量）+ 刷新按钮，留足余量，
        // 否则列表区会被压到装不下一行
        height: Math.min(bar.modelOptions.length, 6) * 40 + 116
        radius: Theme.radius
        color: Theme.dark ? Qt.rgba(0.11, 0.11, 0.12, 0.80) : Qt.rgba(1, 1, 1, 0.80)
        border.width: 1
        border.color: Theme.glassBorder
        opacity: bar.expanded ? 1 : 0
        scale: bar.expanded ? 1 : 0.98
        transformOrigin: Item.Top
        Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: 220; easing.type: Easing.OutBack; easing.overshoot: 1.4 } }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: bar.currentModelName.length > 0 ? bar.currentModelName : "未选择模型"
                color: Theme.textPrimary
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontSmall
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideMiddle
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.border
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: bar.modelOptions
                spacing: 2
                ScrollBar.vertical: ScrollBar { }

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 36
                    radius: Theme.radiusSmall
                    color: itemMouse.containsMouse ? Qt.rgba(0, 0, 0, 0.05) : "transparent"

                    MouseArea {
                        id: itemMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            bar.modelSelected(modelData)
                            bar.expanded = false
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: bar.fileName(modelData)
                            color: bar.fileName(modelData) === bar.currentModelName
                                   ? Theme.accent : Theme.textPrimary
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.fontSmall
                            elide: Text.ElideMiddle
                        }
                        Text {
                            visible: bar.fileName(modelData) === bar.currentModelName
                            text: "✓"
                            color: Theme.accent
                            font.pixelSize: 12
                            font.bold: true
                        }
                        SecondaryButton {
                            objectName: "openModelGpuConfigButton"
                            text: "模型配置"
                            visible: bar.engineKey.length > 0
                            enabled: !bar.busy && !bar.starting
                            implicitHeight: 28
                            onClicked: {
                                bar.expanded = false
                                gpuConfig.configure(modelData)
                            }
                        }
                    }
                }
            }

            SecondaryButton {
                Layout.fillWidth: true
                text: "刷新模型列表"
                onClicked: bar.refreshRequested()
            }
        }
    }
}
