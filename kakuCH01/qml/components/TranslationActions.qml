import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KakuCH

RowLayout {
    id: actions
    property bool busy: false
    property string userTranslationDirectory: controller.saveRoot().replace(/[\\/]+$/, "") + "/user"
    signal addFileRequested()
    signal addFolderRequested()
    spacing: 10

    function closePanels() { morePanel.close(); bookPanel.close() }
    onVisibleChanged: if (!visible) closePanels()
    onBusyChanged: if (busy) closePanels()
    Connections {
        target: Navigation
        function onDismissPanelsRequested() { actions.closePanels() }
        function onNavigateRequested(name) { actions.closePanels() }
    }
    Connections {
        target: controller
        function onSaveRootChanged() {
            actions.userTranslationDirectory = controller.saveRoot().replace(/[\\/]+$/, "") + "/user"
        }
    }

    SecondaryButton {
        id: booksButton
        text: "添加书籍"
        implicitWidth: 140
        leftPadding: 10
        rightPadding: 42
        enabled: !actions.busy
        contentItem: Text {
            text: booksButton.text
            font: booksButton.font
            color: Theme.accent
            opacity: booksButton.enabled ? 1 : 0.4
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        Rectangle {
            width: 1; height: 18
            anchors.right: parent.right; anchors.rightMargin: 32
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.glassBorder
        }
        ToolButton {
            id: moreButton
            anchors.right: parent.right
            width: 32; height: parent.height
            Accessible.name: "添加文件或文件夹"
            background: Rectangle { radius: Theme.radius; color: moreButton.hovered ? Theme.glassFillStrong : "transparent" }
            contentItem: Item {
                opacity: moreButton.enabled ? 1 : 0.4
                Canvas {
                    anchors.centerIn: parent
                    width: 14; height: 14
                    property real expansion: morePanel.visible ? 1 : 0
                    property color ink: Theme.textSecondary
                    onExpansionChanged: requestPaint()
                    onInkChanged: requestPaint()
                    Behavior on expansion { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                    onPaint: {
                        var c = getContext("2d")
                        c.reset()
                        c.strokeStyle = ink
                        c.lineWidth = 1.8
                        c.lineCap = "round"
                        c.lineJoin = "round"
                        c.beginPath()
                        c.moveTo(3 - expansion, 3 + expansion)
                        c.lineTo(10 - 3 * expansion, 7 + 2 * expansion)
                        c.lineTo(3 + 9 * expansion, 11 - 7 * expansion)
                        c.stroke()
                    }
                }
            }
            onClicked: {
                bookPanel.close()
                morePanel.visible ? morePanel.close() : morePanel.open()
            }
        }
        onClicked: {
            morePanel.close()
            if (bookPanel.visible) bookPanel.close()
            else { controller.refreshLibrary(); bookPanel.open() }
        }
    }
    GlassPopup {
        id: morePanel
        focus: true
        parent: booksButton
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        width: 200
        padding: 14
        onAboutToShow: {
            var position = booksButton.mapToItem(Overlay.overlay, 0, booksButton.height)
            x = Math.min(0, Overlay.overlay.width - width - 16 - position.x)
            y = booksButton.height + 8
        }
        contentItem: ColumnLayout {
            spacing: 6
            SecondaryButton {
                text: "添加文件"
                Layout.fillWidth: true
                onClicked: { morePanel.close(); actions.addFileRequested() }
            }
            SecondaryButton {
                text: "添加文件夹"
                Layout.fillWidth: true
                onClicked: { morePanel.close(); actions.addFolderRequested() }
            }
            Text {
                id: userOutputHint
                Layout.fillWidth: true
                text: "自己添加文件的翻译会保存至"
                      + actions.userTranslationDirectory.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
                      + "，<a href=\"user-output\">点击跳转至保存目录</a>"
                textFormat: Text.RichText
                color: Theme.textPrimary
                linkColor: Theme.accent
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                onLinkActivated: controller.openUserTranslationDirectory()
                HoverHandler {
                    cursorShape: userOutputHint.hoveredLink.length ? Qt.PointingHandCursor : Qt.ArrowCursor
                }
            }
        }
    }

    GlassPopup {
        id: bookPanel
        focus: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(820, parent ? parent.width - 64 : 820)
        height: Math.min(600, parent ? parent.height - 64 : 600)
        contentItem: ColumnLayout {
            spacing: 14
            RowLayout {
                Layout.fillWidth: true
                Text { text: "添加书籍"; color: Theme.textPrimary; font.family: Theme.uiFont; font.pixelSize: Theme.fontTitle; font.bold: true }
                Item { Layout.fillWidth: true }
                SecondaryButton { text: "关闭"; onClicked: bookPanel.close() }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 24
                spacing: 14
                Text { text: "小说标题"; Layout.fillWidth: true; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
                Text { text: "已翻译"; Layout.preferredWidth: 90; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
                Item { Layout.preferredWidth: 88 }
            }
            ListView {
                id: books
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 6
                model: controller.libraryModel
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    width: ListView.view.width - 12
                    height: 62
                    radius: Theme.radiusSmall
                    color: Theme.glassFill
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 14
                        Text {
                            Layout.fillWidth: true
                            text: model.title
                            color: Theme.textPrimary
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.fontBody
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.preferredWidth: 90
                            text: model.translatedCount + " / " + model.chapterCount
                            color: Theme.textSecondary
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.fontBody
                        }
                        SecondaryButton {
                            text: "添加"
                            enabled: !actions.busy
                            onClicked: {
                                controller.addLibraryBookToQueue(index)
                                bookPanel.close()
                            }
                        }
                    }
                }
                Text { anchors.centerIn: parent; visible: books.count === 0; text: "当前书库没有小说"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontBody }
            }
        }
    }
}
