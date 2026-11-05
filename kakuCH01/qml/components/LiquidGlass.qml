import QtQuick
import KakuCH

// 仅在液态玻璃选项下装载 GPU 材质，默认配色不增加纹理或效果开销。
Loader {
    id: root
    anchors.fill: parent
    property real radius: parent.radius !== undefined ? parent.radius : Theme.radius
    active: Theme.liquidGlass
    sourceComponent: Component {
        Item {
            id: material
            readonly property bool hasBackdrop: Theme.glassBackdrop !== null
                && material.Window.window === Theme.glassBackdrop.Window.window
            property rect sampleRect: Qt.rect(0, 0, 1, 1)
            function updateSample() {
                if (!hasBackdrop || width <= 0 || height <= 0) return
                const p = material.mapToItem(Theme.glassBackdrop, 0, 0)
                sampleRect = Qt.rect(p.x / Theme.glassBackdrop.width,
                                     p.y / Theme.glassBackdrop.height,
                                     width / Theme.glassBackdrop.width,
                                     height / Theme.glassBackdrop.height)
            }
            Component.onCompleted: updateSample()
            onWidthChanged: updateSample()
            onHeightChanged: updateSample()
            // 祖先的滚动 / 展开也会改变背景采样区域。
            Connections { target: material.Window.window; function onAfterAnimating() { material.updateSample() } }

            Rectangle {
                anchors.fill: parent
                anchors.topMargin: 3
                anchors.bottomMargin: -3
                radius: root.radius
                color: Theme.dark ? "#28000000" : "#0C223344"
            }
            ShaderEffect {
                anchors.fill: parent
                visible: material.hasBackdrop && Theme.glassTexture !== null
                property var backdrop: Theme.glassTexture
                property vector4d sampleRect: Qt.vector4d(material.sampleRect.x, material.sampleRect.y,
                                                         material.sampleRect.width, material.sampleRect.height)
                property vector2d size: Qt.vector2d(width, height)
                property vector2d backdropSize: Qt.vector2d(Theme.glassBackdrop ? Theme.glassBackdrop.width : 1,
                                                           Theme.glassBackdrop ? Theme.glassBackdrop.height : 1)
                property real cornerRadius: Math.min(root.radius, width / 2, height / 2)
                property real obscurity: Theme.componentAlpha
                property real darkMode: Theme.dark ? 1 : 0
                fragmentShader: "qrc:/KakuCH/shaders/liquidglass.frag.qsb"
            }
            Rectangle {
                anchors.fill: parent
                radius: root.radius
                color: material.hasBackdrop ? "transparent" : Theme.glassFillStrong
                border.width: 1
                border.color: Theme.dark ? "#40FFFFFF" : "#65FFFFFF"
                gradient: Gradient {
                    GradientStop { position: 0; color: Theme.dark ? "#18FFFFFF" : "#45FFFFFF" }
                    GradientStop { position: 0.20; color: "transparent" }
                    GradientStop { position: 0.80; color: "transparent" }
                    GradientStop { position: 1; color: Theme.dark ? "#08FFFFFF" : "#12FFFFFF" }
                }
            }
        }
    }
}
