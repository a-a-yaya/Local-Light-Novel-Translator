import QtQuick
import QtQuick.Controls
import KakuCH

// 状态徽标：玻璃胶囊 + 状态色圆点与文本。
Rectangle {
    id: root
    property string status: ""
    property string text: status

    implicitHeight: 24
    implicitWidth: row.implicitWidth + 16

    color: Theme.glassFillStrong

    LiquidGlass { }
    radius: height / 2
    border.width: 1
    border.color: Theme.glassBorder

    property color dotColor: {
        switch (status) {
        case "RUNNING":
        case "COMPLETED":
        case "已下载":
        case "已翻译":
            return Colors.success
        case "STARTING":
        case "TRANSLATING":
        case "WAITING":
        case "待翻译":
        case "翻译中":
            return Colors.warning
        case "FAILED":
        case "ERROR":
            return Colors.danger
        default:
            return Theme.textSecondary
        }
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            radius: 4
            color: root.dotColor
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: Theme.textSecondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
        }
    }
}