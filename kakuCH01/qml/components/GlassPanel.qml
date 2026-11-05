import QtQuick
import QtQuick.Effects
import Qt5Compat.GraphicalEffects
import KakuCH

// 参照 UIDEMO\yzrt-master\纯CSS液态玻璃\css\styles.css。
// 关键差异：使用软渐变代替硬描边，实现 inset box-shadow 一样的柔和内高光。
Item {
    id: root

    property real radius: Theme.radius
    property color fillColor: Qt.rgba(1, 1, 1, 0.04)
    property color topColor: Qt.rgba(1, 1, 1, 0.8)
    property color borderColor: Qt.rgba(0.2, 0.2, 0.2, 0.08)
    property color highlightColor: Qt.rgba(1, 1, 1, 0.8)
    property color shadowColor: Theme.glassShadow
    property bool shadowEnabled: true

    // 玻璃基座：对应 background + border。
    Rectangle {
        id: surface
        anchors.fill: parent
        radius: root.radius
        antialiasing: true
        color: root.fillColor
        border.width: 1
        border.color: root.borderColor
    }

    // 所有内缘效果统一按圆角裁切，避免方角透出。
    Item {
        id: fx
        anchors.fill: parent
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: glassMask
            brightness: 0.9
            contrast: 1.28
        }

        // 上缘 2px 强高光 + 6px 柔和高光：对应 CSS 的两组 inset 白边。
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 8
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.9) }
                GradientStop { position: 0.25; color: Qt.rgba(1, 1, 1, 0.55) }
                GradientStop { position: 1.0; color: "transparent" }
            }
            layer.enabled: true
            layer.effect: FastBlur { radius: 1.2 }
        }
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 8
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.9) }
                GradientStop { position: 0.25; color: Qt.rgba(1, 1, 1, 0.55) }
                GradientStop { position: 1.0; color: "transparent" }
            }
            layer.enabled: true
            layer.effect: FastBlur { radius: 1.2 }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 8
            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.75; color: Qt.rgba(1, 1, 1, 0.55) }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.9) }
            }
            layer.enabled: true
            layer.effect: FastBlur { radius: 1.2 }
        }
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 8
            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.75; color: Qt.rgba(1, 1, 1, 0.55) }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.9) }
            }
            layer.enabled: true
            layer.effect: FastBlur { radius: 1.2 }
        }

        // 内缘暗色光晕：对应 inset 0 0 2px rgba(0,0,0,0.8)。
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: Math.max(root.radius - 1, 0)
            color: "transparent"
            border.width: 2
            border.color: Qt.rgba(0, 0, 0, 0.8)
            layer.enabled: true
            layer.effect: FastBlur { radius: 2 }
        }

        // 深色模糊内圈：对应 CSS ::before。
        Rectangle {
            anchors.fill: parent
            anchors.margins: 8
            radius: Math.max(root.radius - 8, 0)
            color: "transparent"
            border.width: 1
            border.color: Qt.rgba(0, 0, 0, 0.9)
            layer.enabled: true
            layer.effect: FastBlur { radius: 8 }
        }

        // 45° 镜面高光：对应 CSS ::after。
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: Math.max(root.radius - 1, 0)
            gradient: Gradient {
                GradientStop { position: 0.0; color: root.topColor }
                GradientStop { position: 0.25; color: "transparent" }
                GradientStop { position: 0.75; color: "transparent" }
                GradientStop { position: 1.0; color: root.highlightColor }
            }
            layer.enabled: true
            layer.effect: FastBlur { radius: 3 }
        }

        // 细白外圈：对应 CSS .circle-overlay。
        Rectangle {
            anchors.fill: parent
            anchors.margins: 4
            radius: Math.max(root.radius - 4, 0)
            color: "transparent"
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.2)
            layer.enabled: true
            layer.effect: FastBlur { radius: 1 }
        }
    }

    Rectangle {
        id: glassMask
        anchors.fill: parent
        radius: root.radius
        color: "white"
        visible: false
    }

    // 外阴影：对应 CSS 0 4px 8px rgba(0,0,0,0.2)。
    MultiEffect {
        anchors.fill: surface
        source: surface
        visible: root.shadowEnabled
        shadowEnabled: root.shadowEnabled
        shadowBlur: 0.5
        shadowColor: root.shadowColor
        shadowVerticalOffset: 8
        shadowHorizontalOffset: 0
        autoPaddingEnabled: true
    }
}