import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KakuCH

Rectangle {
    id: queue
    property bool busy: false
    property bool glossaryMode: false
    implicitHeight: 300
    radius: Theme.radius
    color: Theme.card
    LiquidGlass { }
    border.width: 1
    border.color: Theme.border

    ListView {
        id: queueList
        anchors.fill: parent
        anchors.margins: 6
        clip: true
        model: controller.translationModel
        spacing: 4

        delegate: Rectangle {
            width: ListView.view.width - 12
            height: 44
            radius: Theme.radiusSmall
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10

                // 翻译和术语提取保留各自独立的选择状态。
                CheckBox {
                    visible: !queue.glossaryMode
                    checked: model.selected
                    enabled: !queue.busy && !model.translated
                    opacity: enabled ? 1 : 0.4
                    onToggled: { if (!queue.busy && !model.translated) model.selected = checked }
                }

                // 提取只读原文，已翻译的章节仍可勾选。
                Item {
                    visible: queue.glossaryMode
                    enabled: !queue.busy
                    opacity: enabled ? 1 : 0.4
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22

                    Rectangle {
                        anchors.centerIn: parent
                        width: 18
                        height: 18
                        radius: 9
                        color: model.glossarySelected ? Theme.accent : "transparent"
                        border.width: 1.5
                        border.color: model.glossarySelected ? Theme.accent : Theme.textSecondary

                        Behavior on color {
                            ColorAnimation { duration: 140; easing.type: Easing.OutCubic }
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: model.glossarySelected
                            text: "✓"
                            color: "white"
                            font.pixelSize: 11
                            font.bold: true
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: { if (!queue.busy) model.glossarySelected = !model.glossarySelected }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: model.fileName
                    color: Theme.textPrimary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.fontBody
                    elide: Text.ElideRight
                }
                StatusBadge {
                    status: model.status
                    text: {
                        switch (model.status) {
                        case "WAITING": return "待翻译"
                        case "TRANSLATING": return "翻译中"
                        case "COMPLETED": return "已翻译"
                        case "FAILED": return "失败"
                        default: return model.status
                        }
                    }
                }
            }
        }
    }
}
