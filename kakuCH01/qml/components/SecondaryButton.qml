import QtQuick
import QtQuick.Controls
import KakuCH

// 次要按钮：更“毛”的玻璃底，提升文字可读性；按下时轻微缩放反馈。
Button {
    id: control

    implicitHeight: 38
    implicitWidth: Math.max(88, contentItem.implicitWidth + 36)
    leftPadding: 16
    rightPadding: 16

    font.family: Theme.uiFont
    font.pixelSize: Theme.fontBody
    palette.buttonText: Theme.accent

    scale: control.pressed ? 0.97 : 1.0
    Behavior on scale { SpringAnimation { spring: 2.6; damping: 0.78; epsilon: 0.01 } }

    background: Rectangle {
        radius: Theme.radius
        color: control.down ? Theme.glassFillStrong : Theme.glassButton
        LiquidGlass { }
        border.width: 1
        border.color: Theme.glassBorder
    }
}