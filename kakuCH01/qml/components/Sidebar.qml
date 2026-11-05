import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KakuCH

// 左侧导航：作为浮层覆盖在内容之上，默认收起，收起后仅显示居中的图标。
// 白色液体玻璃侧栏：透明度和描边随明暗主题变化，底部竖向排列窗口控制按钮。
GlassPanel {
    id: root
    signal pageSelected(string name)

    property string currentPage: "home"
    property bool collapsed: true

    property real expansion: collapsed ? 0 : 1
    readonly property real labelOpacity: Math.max(0, (expansion - 0.3) / 0.7)
    width: Theme.railWidth + (208 - Theme.railWidth) * expansion
    clip: true
    radius: Theme.railWidth / 2
    fillColor: Qt.rgba(1, 1, 1, 0.04)
    topColor: Qt.rgba(1, 1, 1, 0.8)
    borderColor: Qt.rgba(0.2, 0.2, 0.2, 0.08)
    highlightColor: Qt.rgba(1, 1, 1, 0.8)
    shadowEnabled: true

    Behavior on expansion {
        NumberAnimation { duration: root.collapsed ? 150 : 190; easing.type: Easing.OutCubic }
    }

    ListModel {
        id: navModel
        // size 用来把各字形的视觉大小对齐到「设置」齿轮那么大
        ListElement { name: "home"; title: "首页"; icon: "⌂"; size: 24 }
        ListElement { name: "site"; title: "网站"; icon: "「 」"; size: 14 }
        ListElement { name: "translate"; title: "翻译"; icon: "✦"; size: 21 }
        ListElement { name: "download"; title: "下载"; icon: "↓"; size: 17 }
        ListElement { name: "settings"; title: "设置"; icon: "⚙"; size: 26 }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            id: headerItem
            Layout.fillWidth: true
            Layout.preferredHeight: 60

            Text {
                x: 18
                height: parent.height
                width: Math.max(0, parent.width - x - 56)
                opacity: root.labelOpacity
                text: "Light Novel Translator"
                color: Theme.textPrimary
                font.family: Theme.displayFont
                font.pixelSize: 17
                font.bold: true
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.WordWrap
            }
            RoundButton {
                width: 32; height: 32
                x: root.width - width - ((Theme.railWidth - width) / 2
                    + (14 - (Theme.railWidth - width) / 2) * root.expansion)
                anchors.verticalCenter: parent.verticalCenter
                text: root.collapsed ? "»" : "«"
                flat: true
                scale: pressed ? 0.9 : 1
                Behavior on scale { NumberAnimation { duration: 100; easing.type: Easing.OutCubic } }
                onClicked: root.collapsed = !root.collapsed
                contentItem: Text {
                    text: parent.text
                    color: Theme.textSecondary
                    font.family: Theme.uiFont
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        Column {
            id: navCol
            Layout.fillWidth: true
            Layout.topMargin: 8
            spacing: 2

            Repeater {
                model: navModel

                Rectangle {
                    id: navItem
                    width: parent.width
                    height: 44
                    radius: Theme.radiusSmall
                    color: currentPage === model.name
                           ? Qt.rgba(0.039, 0.518, 1.0, 0.16)
                           : "transparent"
                    scale: navMouse.pressed ? 0.96 : 1.0
                    transformOrigin: Item.Center
                    Behavior on scale { SpringAnimation { spring: 2.6; damping: 0.78; epsilon: 0.01 } }

                    MouseArea {
                        id: navMouse
                        anchors.fill: parent
                        onClicked: root.pageSelected(model.name)
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 0
                        spacing: 12 * root.expansion

                        Item {
                            id: iconHost
                            Layout.preferredWidth: Theme.railWidth
                            Layout.fillHeight: true

                            readonly property color iconColor: currentPage === model.name
                                                                ? Theme.accent : Theme.textSecondary

                            Text {
                                anchors.centerIn: parent
                                visible: model.name !== "site" && model.name !== "settings"
                                text: model.icon
                                color: parent.iconColor
                                font.pixelSize: model.size
                            }

                            // 网站图标：照 kakuyomu 官方 logo 的「 」绘制。
                            // 官方标记里左右两条竖线是错位的：左括号靠上、右括号靠下
                            // （等高、竖向错开约 1/4 标记高度）。
                            // 坐标全部取偶数：在 150% 缩放下每条边都落在整像素上，
                            // 左右两块才不会因为抗锯齿出现粗细不一。
                            Item {
                                visible: model.name === "site"
                                anchors.centerIn: parent
                                width: 14
                                height: 14

                                // 「 —— 竖线靠上，横臂在顶端
                                Rectangle { x: 0; y: 0; width: 2; height: 10; color: iconHost.iconColor }
                                Rectangle { x: 2; y: 0; width: 2; height: 2; color: iconHost.iconColor }

                                // 」—— 竖线靠下，横臂在底端
                                Rectangle { x: 10; y: 12; width: 2; height: 2; color: iconHost.iconColor }
                                Rectangle { x: 12; y: 4; width: 2; height: 10; color: iconHost.iconColor }
                            }

                            // 设置图标：与其它四个同为线条风格——细圆环 + 8 个齿
                            // （原来的 ⚙ 字形是实心带细节的，风格不统一）
                            Item {
                                visible: model.name === "settings"
                                anchors.centerIn: parent
                                width: 14
                                height: 14

                                Rectangle {
                                    x: 2; y: 2; width: 10; height: 10
                                    radius: 5
                                    color: "transparent"
                                    border.width: 2
                                    border.color: iconHost.iconColor
                                }

                                Repeater {
                                    model: 8
                                    Rectangle {
                                        x: 6; y: 0
                                        width: 2; height: 4
                                        color: iconHost.iconColor
                                        transform: Rotation {
                                            origin.x: 1
                                            origin.y: 7
                                            angle: index * 45
                                        }
                                    }
                                }
                            }
                        }
                        Text {
                            opacity: root.labelOpacity
                            Layout.minimumWidth: 0
                            Layout.fillWidth: true
                            text: model.title
                            color: currentPage === model.name ? Theme.accent : Theme.textPrimary
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.fontBody
                        }
                    }

                    Rectangle {
                        visible: currentPage === model.name
                        width: 3
                        height: 24
                        radius: 2
                        color: Theme.accent
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }

        // 窗口控制：最小化 / 最大化 / 关闭，收起时也保留在侧栏底部。
        Column {
            Layout.preferredWidth: 34
            Layout.preferredHeight: 112
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 14
            spacing: 4

            RoundButton {
                id: minButton
                width: 34
                height: 34
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

            RoundButton {
                id: maxButton
                width: 34
                height: 34
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

            RoundButton {
                id: closeButton
                width: 34
                height: 34
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
