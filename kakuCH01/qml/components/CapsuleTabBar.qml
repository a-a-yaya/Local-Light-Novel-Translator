import QtQuick
import KakuCH

// 横向胶囊切换栏：玻璃胶囊底座，选中项带一块玻璃高亮胶囊。
// 每一项的宽度由真实 Text 的 implicitWidth 决定，避免文字互相挤压。
// 用法：CapsuleTabBar { items: ["A", "B"]; currentIndex: 0; onActivated: ... }
Item {
    id: root

    property var items: []
    property int currentIndex: 0
    property int itemHeight: 40
    readonly property real inset: 4
    readonly property real itemPadding: 22

    signal activated(int index)

    implicitWidth: row.implicitWidth + inset * 2
    implicitHeight: itemHeight + inset * 2

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: Theme.glassFillStrong
        LiquidGlass { }
        border.width: 1
        border.color: Theme.glassBorder
    }

    Row {
        id: row
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: root.inset
        anchors.topMargin: root.inset
        spacing: 0

        Repeater {
            model: root.items

            Item {
                id: delegateItem
                width: label.implicitWidth + root.itemPadding * 2
                height: root.itemHeight

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: height / 2
                    color: Theme.glassButton
                    LiquidGlass { }
                    border.width: 1
                    border.color: Theme.glassBorder
                    opacity: root.currentIndex === index ? 1 : 0

                    Behavior on opacity {
                        NumberAnimation { duration: 170; easing.type: Easing.OutCubic }
                    }
                }

                Text {
                    id: label
                    anchors.centerIn: parent
                    text: modelData
                    color: root.currentIndex === index ? Theme.textPrimary : Theme.textSecondary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.fontBody

                    Behavior on color {
                        ColorAnimation { duration: 170; easing.type: Easing.OutCubic }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        root.currentIndex = index
                        root.activated(index)
                    }
                }
            }
        }
    }
}
