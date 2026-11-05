import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KakuCH

GlassDialog {
    id: dialog
    objectName: "modelGpuConfigDialog"
    property string engineKey: ""
    property string modelPath: ""
    property bool busy: false
    property int pendingRequestId: -1
    property string feedback: ""
    readonly property bool calculating: pendingRequestId >= 0
    readonly property bool modelActive: controller.sakuraRunning || controller.sakuraStarting
        || controller.murasakiRunning || controller.murasakiStarting
        || controller.qwenRunning || controller.qwenStarting
    title: "模型配置"
    width: Math.min(540, parent ? parent.width - 40 : 540)
    implicitHeight: 430
    height: Math.min(implicitHeight, parent ? Math.max(240, parent.height - 48) : implicitHeight)
    standardButtons: Dialog.NoButton
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onBusyChanged: if (busy) close()
    onClosed: pendingRequestId = -1
    onAboutToShow: formScroll.contentY = 0

    function configure(path) {
        if (busy) return
        pendingRequestId = -1
        modelPath = String(path)
        gpuLayers.value = controller.modelGpuConfiguration(engineKey, modelPath).gpuLayers
        feedback = ""
        open()
    }
    Connections {
        target: controller
        function onModelGpuRecommendationReady(requestId, result) {
            if (requestId !== dialog.pendingRequestId || !dialog.visible) return
            dialog.pendingRequestId = -1
            if (result.success) gpuLayers.value = result.gpuLayers
            dialog.feedback = result.message
        }
    }
    Connections {
        target: Navigation
        function onDismissPanelsRequested() { dialog.close() }
        function onNavigateRequested(name) { dialog.close() }
    }

    contentItem: Flickable {
        id: formScroll
        clip: true
        contentWidth: width
        contentHeight: form.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        ColumnLayout {
            id: form
            width: formScroll.width
            spacing: 16
            Text {
                Layout.fillWidth: true
                text: dialog.modelPath.split(/[\\/]/).pop()
                color: Theme.textPrimary
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontBody
                font.bold: true
                wrapMode: Text.WrapAnywhere
            }
            Text {
                Layout.fillWidth: true
                text: dialog.modelPath
                color: Theme.textSecondary
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: "GPU 层数"
                    color: Theme.textPrimary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.fontBody
                }
                SpinBox {
                    id: gpuLayers
                    objectName: "modelGpuLayersInput"
                    from: 0; to: 200; value: 200
                    editable: true
                    enabled: !dialog.busy && !dialog.calculating
                    Layout.preferredWidth: 150
                    implicitHeight: 38
                    font.family: Theme.uiFont
                    palette.text: Theme.textPrimary
                    palette.buttonText: Theme.textPrimary
                    palette.highlight: Theme.accent
                    up.indicator: Rectangle {
                        anchors.right: parent.right
                        width: 32; height: parent.height
                        radius: Theme.radiusSmall
                        color: gpuLayers.up.pressed ? Theme.glassFillStrong : Theme.glassFill
                        Text {
                            anchors.centerIn: parent
                            text: "+"
                            color: Theme.textPrimary
                            opacity: gpuLayers.enabled ? 1 : 0.4
                            font.pixelSize: 22
                        }
                    }
                    down.indicator: Rectangle {
                        anchors.left: parent.left
                        width: 32; height: parent.height
                        radius: Theme.radiusSmall
                        color: gpuLayers.down.pressed ? Theme.glassFillStrong : Theme.glassFill
                        Text {
                            anchors.centerIn: parent
                            text: "−"
                            color: Theme.textPrimary
                            opacity: gpuLayers.enabled ? 1 : 0.4
                            font.pixelSize: 22
                        }
                    }
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.card
                        border.width: 1
                        border.color: Theme.border
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                text: "0 为仅使用 CPU；200 为当前默认配置，尽可能使用 GPU。\n每个引擎、每个模型文件单独保存，下次启动此模型时生效。"
                color: Theme.textSecondary
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
            }
            RowLayout {
                spacing: 10
                SecondaryButton {
                    objectName: "autoModelGpuButton"
                    text: dialog.calculating ? "正在估算…" : "自动配置"
                    enabled: !dialog.busy && !dialog.calculating && !dialog.modelActive
                    onClicked: {
                        dialog.feedback = "正在读取模型信息与可用显存…"
                        dialog.pendingRequestId = controller.requestModelGpuRecommendation(dialog.engineKey, dialog.modelPath)
                    }
                }
                SecondaryButton {
                    objectName: "resetModelGpuButton"
                    text: "恢复默认"
                    enabled: !dialog.busy && !dialog.calculating
                    onClicked: { gpuLayers.value = 200; dialog.feedback = "已填入默认 200 层，保存后生效" }
                }
            }
            Text {
                Layout.fillWidth: true
                visible: dialog.feedback.length > 0 || dialog.modelActive
                text: dialog.feedback.length ? dialog.feedback : "请先关闭已启动的模型，再自动配置，以便测量可用显存。"
                color: Theme.textSecondary
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
            }
        }
    }
    footer: RowLayout {
        implicitHeight: 64
        height: implicitHeight
        spacing: 10
        Item { Layout.fillWidth: true }
        SecondaryButton {
            text: "取消"
            Layout.bottomMargin: 20
            onClicked: dialog.close()
        }
        PrimaryButton {
            objectName: "saveModelGpuButton"
            text: "保存"
            enabled: !dialog.busy && !dialog.calculating
            Layout.rightMargin: 26
            Layout.bottomMargin: 20
            onClicked: {
                gpuLayers.focus = false
                if (controller.saveModelGpuConfiguration(dialog.engineKey, dialog.modelPath, gpuLayers.value)) dialog.close()
            }
        }
    }
}
