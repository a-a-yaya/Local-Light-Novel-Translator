import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KakuCH

// Shared confirmation/form surface, matching Main's overview translation panel.
Dialog {
    id: control
    property string acceptText: "确定"
    property string rejectText: "取消"
    property bool destructive: false
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(404, parent ? parent.width - 40 : 404)
    padding: 26
    topPadding: 12
    bottomPadding: 20
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    standardButtons: Dialog.Ok | Dialog.Cancel
    font.family: Theme.uiFont
    palette.windowText: Theme.textPrimary
    background: Rectangle {
        radius: 20
        color: Theme.dark ? Qt.rgba(0.11, 0.11, 0.12, 0.80) : Qt.rgba(1, 1, 1, 0.80)
        border.width: 1
        border.color: Theme.glassBorder
    }
    header: Label {
        text: control.title
        color: Theme.textPrimary
        font.family: Theme.uiFont
        font.pixelSize: Theme.fontTitle
        font.bold: true
        wrapMode: Text.Wrap
        leftPadding: 26; rightPadding: 26; topPadding: 22; bottomPadding: 0
    }
    footer: RowLayout {
        visible: control.standardButtons !== Dialog.NoButton
        spacing: 10
        height: visible ? implicitHeight : 0
        Item { Layout.fillWidth: true }
        SecondaryButton {
            text: control.rejectText
            Layout.bottomMargin: 20
            onClicked: control.reject()
        }
        PrimaryButton {
            id: acceptButton
            text: control.acceptText
            Layout.rightMargin: 26
            Layout.bottomMargin: 20
            onClicked: control.accept()
            background: Rectangle {
                radius: Theme.radius
                color: control.destructive ? (acceptButton.down ? "#B53636" : "#CE4545")
                                           : (acceptButton.down ? Theme.accentPressed : Theme.accent)
            }
        }
    }
    Overlay.modal: Rectangle { color: Theme.dark ? Qt.rgba(0, 0, 0, 0.30) : Qt.rgba(0, 0, 0, 0.10) }
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.90; to: 1; duration: 360; easing.type: Easing.OutBack; easing.overshoot: 1.6 }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; to: 0; duration: 170; easing.type: Easing.InCubic }
            NumberAnimation { property: "scale"; to: 0.92; duration: 190; easing.type: Easing.InCubic }
        }
    }
}
