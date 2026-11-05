import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KakuCH

// 顶部栏：无系统标题栏后的自定义标题区。
// 提供整窗拖拽、主题开关、Sakura 状态以及最小化/最大化/关闭按钮。
Rectangle {
    id: root
    property string title: "首页"
    property string subtitle: "轻小说下载 · 翻译 · 阅读"

    implicitHeight: 56
    height: 56
    color: Theme.glassFillStrong

    // 拖拽：仅捕获空白区域，按钮与开关仍可交互。
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onPressed: if (mouse.button === Qt.LeftButton) Window.window.startSystemMove()
    }

    // 底部发丝分隔线
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.glassBorder
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 24
        anchors.rightMargin: 10
        spacing: 16

        Column {
            Layout.fillWidth: true
            spacing: 2
            Text {
                text: root.title
                color: Theme.textPrimary
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontTitle
                font.bold: true
            }
            Text {
                text: root.subtitle
                color: Theme.textSecondary
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontSmall
            }
        }

        RowLayout {
            spacing: 8
            Text {
                text: "深色"
                color: Theme.textSecondary
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontSmall
            }
            Switch {
                checked: Theme.dark
                onToggled: Theme.dark = checked
            }
        }

        Rectangle {
            implicitWidth: sakuraRow.implicitWidth + 20
            implicitHeight: 30
            radius: 15
            color: Theme.glassFill
            border.width: 1
            border.color: Theme.glassBorder

            Row {
                id: sakuraRow
                anchors.centerIn: parent
                spacing: 6
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8
                    height: 8
                    radius: 4
                    color: controller.sakuraRunning ? Colors.success
                         : controller.sakuraStarting ? Colors.warning
                         : controller.sakuraStatus === "启动失败" ? Colors.danger
                         : Theme.textSecondary
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Sakura · " + controller.sakuraStatus
                    color: Theme.textPrimary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.fontSmall
                }
            }
        }

        // 窗口控制按钮
        RowLayout {
            spacing: 2

            Button {
                id: minButton
                implicitWidth: 40
                implicitHeight: 30
                flat: true
                hoverEnabled: true
                scale: pressed ? 0.88 : 1.0
                transformOrigin: Item.Center
                Behavior on scale { SpringAnimation { spring: 2.6; damping: 0.78; epsilon: 0.01 } }
                onClicked: Window.window.showMinimized()
                contentItem: Text {
                    text: "─"
                    color: Theme.textSecondary
                    font.family: Theme.uiFont
                    font.pixelSize: 15
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: minButton.hovered ? Qt.rgba(0, 0, 0, 0.06) : "transparent"
                }
            }

            Button {
                id: maxButton
                implicitWidth: 40
                implicitHeight: 30
                flat: true
                hoverEnabled: true
                scale: pressed ? 0.88 : 1.0
                transformOrigin: Item.Center
                Behavior on scale { SpringAnimation { spring: 2.6; damping: 0.78; epsilon: 0.01 } }
                onClicked: Window.window.visibility === Window.Maximized
                           ? Window.window.showNormal()
                           : Window.window.showMaximized()
                contentItem: Text {
                    text: Window.window.visibility === Window.Maximized ? "❐" : "□"
                    color: Theme.textSecondary
                    font.family: Theme.uiFont
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: maxButton.hovered ? Qt.rgba(0, 0, 0, 0.06) : "transparent"
                }
            }

            Button {
                id: closeButton
                implicitWidth: 40
                implicitHeight: 30
                flat: true
                hoverEnabled: true
                scale: pressed ? 0.88 : 1.0
                transformOrigin: Item.Center
                Behavior on scale { SpringAnimation { spring: 2.6; damping: 0.78; epsilon: 0.01 } }
                onClicked: Window.window.close()
                contentItem: Text {
                    text: "✕"
                    color: closeButton.hovered ? "white" : Theme.textSecondary
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: closeButton.hovered ? Colors.danger : "transparent"
                }
            }
        }
    }
}