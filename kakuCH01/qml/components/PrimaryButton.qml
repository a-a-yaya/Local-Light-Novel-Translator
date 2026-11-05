import QtQuick
import QtQuick.Controls
import KakuCH

// 主要按钮：系统蓝填充，按下时轻微缩放反馈（响应用户动作）。
Button {
    id: control

    implicitHeight: 38
    implicitWidth: Math.max(96, contentItem.implicitWidth + 36)
    leftPadding: 16
    rightPadding: 16

    font.family: Theme.uiFont
    font.pixelSize: Theme.fontBody
    palette.buttonText: "white"

    scale: control.pressed ? 0.97 : 1.0
    Behavior on scale { SpringAnimation { spring: 2.6; damping: 0.78; epsilon: 0.01 } }

    background: Rectangle {
        radius: Theme.radius
        color: control.down ? Theme.accentPressed
             : control.hovered ? Theme.accentHover
             : Theme.accent
        opacity: control.enabled ? 1.0 : 0.45
    }
}