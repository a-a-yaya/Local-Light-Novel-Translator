import QtQuick
import QtQuick.Controls
import KakuCH

// 空状态占位：引导用户执行第一步操作。
Item {
    id: root
    property string title: "暂无内容"
    property string subtitle: ""

    implicitHeight: 180

    Column {
        anchors.centerIn: parent
        spacing: 8

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.title
            color: Theme.textPrimary
            font.family: Theme.displayFont
            font.pixelSize: Theme.fontTitle
            font.bold: true
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(implicitWidth, parent.width - 40)
            horizontalAlignment: Text.AlignHCenter
            text: root.subtitle
            color: Theme.textSecondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.fontBody
            wrapMode: Text.Wrap
        }
    }
}