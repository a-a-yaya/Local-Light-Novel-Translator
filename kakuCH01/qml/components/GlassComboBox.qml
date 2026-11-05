import QtQuick
import QtQuick.Controls
import KakuCH

ComboBox {
    id: control
    implicitHeight: 40
    implicitWidth: 180
    leftPadding: 14; rightPadding: 34
    font.family: Theme.uiFont
    font.pixelSize: Theme.fontBody
    palette.text: Theme.textPrimary
    palette.buttonText: Theme.textPrimary
    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.glassFillStrong
        border.width: 1
        border.color: control.activeFocus ? Theme.accent : Theme.glassBorder
    }
    indicator: Text {
        x: control.width - width - 14; y: (control.height - height) / 2
        text: "⌄"; color: Theme.textSecondary
    }
    delegate: ItemDelegate {
        width: control.width - 12
        text: control.textRole ? model[control.textRole] : modelData
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: parent.text
            font: control.font
            color: Theme.textPrimary
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 8
            color: parent.highlighted ? Theme.glassButton : "transparent"
        }
    }
    popup: GlassPopup {
        y: control.height + 6
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 12, 280)
        padding: 6
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }
}
