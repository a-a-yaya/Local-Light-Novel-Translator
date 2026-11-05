import QtQuick
import QtQuick.Controls
import KakuCH

// Non-modal reader panels use the same surface as application confirmations.
Popup {
    padding: 24
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    font.family: Theme.uiFont
    palette.text: Theme.textPrimary
    palette.windowText: Theme.textPrimary
    palette.buttonText: Theme.textPrimary
    background: Rectangle {
        radius: 20
        color: Theme.dark ? Qt.rgba(0.11, 0.11, 0.12, 0.80) : Qt.rgba(1, 1, 1, 0.80)
        border.width: 1
        border.color: Theme.glassBorder
    }
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: 260; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 150 } }
}
