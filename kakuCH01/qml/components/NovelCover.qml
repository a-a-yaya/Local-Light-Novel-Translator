import QtQuick
import KakuCH

// Fixed-size title-only cover, with a readable minimum font size.
Rectangle {
    id: root
    property string title: ""
    property string author: ""
    implicitHeight: 148
    radius: Theme.radiusSmall
    color: Theme.glassFillStrong
    border.width: 1
    border.color: Theme.glassBorder
    antialiasing: true
    LiquidGlass { }
    Text {
        id: titleLabel
        anchors.fill: parent
        anchors.margins: 14
        text: root.title
        textFormat: Text.PlainText
        color: Theme.textPrimary
        font.family: Theme.displayFont
        font.pixelSize: 17
        minimumPixelSize: 12
        fontSizeMode: Text.Fit
        font.weight: Font.DemiBold
        wrapMode: Text.Wrap
        lineHeight: 1.2
        maximumLineCount: Math.max(1, Math.floor(height / (minimumPixelSize * lineHeight)))
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        clip: true
    }
}
