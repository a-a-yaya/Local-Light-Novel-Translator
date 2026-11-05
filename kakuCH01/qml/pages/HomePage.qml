import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KakuCH

// 首页：本地书库（原「书库」页的内容并入首页）。
Page {
    id: page
    objectName: "home"
    background: Rectangle { color: Theme.pageBackground }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        anchors.topMargin: 20
        anchors.bottomMargin: 12
        spacing: Theme.spacing


        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            TextField {
                Layout.fillWidth: true
                placeholderText: "搜索书名…"
                selectByMouse: true
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontBody
                color: Theme.textPrimary
                id: searchField
                onTextChanged: searchDelay.restart()
                Timer {
                    id: searchDelay
                    interval: 150
                    onTriggered: controller.libraryModel.setFilter(searchField.text)
                }
                background: Rectangle {
                    radius: Theme.radius
                    color: Theme.card
                    LiquidGlass { }
                    border.width: 1
                    border.color: Theme.border
                }
            }
        }

        GridView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: controller.libraryModel
            cellWidth: 180
            cellHeight: 240
            ScrollBar.vertical: ScrollBar { }

            delegate: Rectangle {
                id: card
                width: 160
                height: 220
                radius: Theme.radius
                color: Theme.card
                LiquidGlass { }
                border.width: 1
                border.color: Theme.border
                // 正在做「放大 / 缩回」动画时这张卡片本身不显示
                opacity: Navigation.zoomCardItem === card ? 0 : 1
                scale: Navigation.zoomCardItem === card ? 1.0 : (cardMouse.pressed ? 0.975 : 1.0)
                transformOrigin: Item.Center
                Behavior on scale { enabled: Navigation.zoomCardItem !== card; NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    NovelCover {
                        id: cover
                        Layout.fillWidth: true
                        Layout.preferredHeight: 148
                        title: model.title
                    }

                    // 作者（原来「已翻译」所在的位置）
                    Text {
                        Layout.fillWidth: true
                        text: (model.author !== undefined && model.author.length > 0)
                              ? model.author : ""
                        color: Theme.textSecondary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                    }

                    // 已翻译 / 已阅读 放在同一行
                    Text {
                        Layout.fillWidth: true
                        text: "已译 " + model.translatedCount + "/" + model.chapterCount
                              + "     已阅 " + model.readCount + "/" + model.chapterCount
                        color: Theme.textSecondary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.fontSmall
                        minimumPixelSize: 8
                        fontSizeMode: Text.Fit
                        horizontalAlignment: Text.AlignLeft
                    }
                }

                MouseArea {
                    id: cardMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (Navigation.zoomTransitionActive) return
                        controller.selectNovel(index)
                        // 从这张卡片放大铺满进入详情
                        Navigation.openNovelZoom(card)
                    }
                }
            }
        }
    }

    // 右下角刷新：圆形玻璃按钮 + 经典圆环箭头图标
    Rectangle {
        id: refreshBtn
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 30
        anchors.bottomMargin: 30
        width: 46
        height: 46
        radius: width / 2
        color: Theme.dark ? Qt.rgba(0.16, 0.16, 0.18, 0.82) : Qt.rgba(1, 1, 1, 0.88)
        border.width: 1
        border.color: Theme.glassBorder
        scale: refreshMouse.pressed ? 0.93 : 1.0
        Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

        Canvas {
            id: refreshIcon
            anchors.centerIn: parent
            width: 22
            height: 22
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                var cx = width / 2
                var cy = height / 2
                var r = width / 2 - 2.6
                var col = Theme.textPrimary
                ctx.strokeStyle = col
                ctx.fillStyle = col
                ctx.lineWidth = 2
                ctx.lineCap = "round"
                // 圆环（右上留缺口）
                var a0 = -Math.PI * 0.42
                ctx.beginPath()
                ctx.arc(cx, cy, r, a0, Math.PI * 1.30, false)
                ctx.stroke()
                // 箭头
                var ax = cx + r * Math.cos(a0)
                var ay = cy + r * Math.sin(a0)
                ctx.beginPath()
                ctx.moveTo(ax - 3.6, ay - 2.8)
                ctx.lineTo(ax + 3.4, ay - 1.0)
                ctx.lineTo(ax - 0.4, ay + 4.0)
                ctx.closePath()
                ctx.fill()
            }
            Connections {
                target: Theme
                function onDarkChanged() { refreshIcon.requestPaint() }
            }
        }

        MouseArea {
            id: refreshMouse
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: controller.refreshLibrary()
        }
    }
}
