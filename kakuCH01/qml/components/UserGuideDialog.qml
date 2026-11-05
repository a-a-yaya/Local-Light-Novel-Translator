import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KakuCH

GlassDialog {
    id: guide
    objectName: "userGuideDialog"
    title: "使用说明"
    width: Math.min(840, parent ? parent.width - 48 : 840)
    height: Math.min(700, parent ? parent.height - 48 : 700)
    standardButtons: Dialog.NoButton
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 24
    topPadding: 16
    bottomPadding: 16
    background: Rectangle {
        radius: 20
        color: Theme.dark ? Qt.rgba(0.11, 0.11, 0.12, 0.95) : Qt.rgba(1, 1, 1, 0.95)
        border.width: 1
        border.color: Theme.glassBorder
    }
    onAboutToShow: {
        guideScroll.contentX = 0
        guideScroll.contentY = 0
    }

    contentItem: Flickable {
        id: guideScroll
        objectName: "userGuideScroll"
        clip: true
        contentWidth: Math.max(width, guideText.contentWidth + guideText.leftPadding + guideText.rightPadding)
        contentHeight: guideText.height
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickDirection
        ScrollBar.vertical: ScrollBar { }
        ScrollBar.horizontal: ScrollBar { }
        TextEdit {
            id: guideText
            objectName: "userGuideContent"
            width: guideScroll.width
            height: contentHeight
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            textFormat: TextEdit.RichText
            text: guide.visible ? controller.userGuideHtml(Theme.accent, Theme.glassBorder, font) : ""
            onLinkActivated: function(link) {
                if (/^https?:\/\//i.test(link))
                    Qt.openUrlExternally(link)
            }
            color: Theme.textPrimary
            selectionColor: Theme.accent
            selectedTextColor: "white"
            font.family: Theme.uiFont
            font.pixelSize: 15
            leftPadding: 2
            rightPadding: 18
            topPadding: 0
            bottomPadding: 16
            HoverHandler {
                cursorShape: guideText.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.IBeamCursor
            }
        }
    }
    footer: RowLayout {
        spacing: 12
        Item { Layout.fillWidth: true }
        SecondaryButton {
            objectName: "closeUserGuideButton"
            Layout.rightMargin: 26
            Layout.bottomMargin: 20
            text: "关闭"
            onClicked: guide.close()
        }
    }
}
