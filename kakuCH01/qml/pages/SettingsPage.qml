import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Qt.labs.settings
import KakuCH

Page {
    id: settingsPage
    objectName: "settings"
    onVisibleChanged: if (!visible) userGuideDialog.close()
    background: Rectangle { color: Theme.pageBackground }
    UserGuideDialog { id: userGuideDialog }
    Connections {
        target: Navigation
        function onDismissPanelsRequested() { userGuideDialog.close() }
    }
    Component.onCompleted: {
        if (backgroundSettings.backgroundImage.toString() !== "") Theme.backgroundImage = backgroundSettings.backgroundImage
        Theme.componentAlpha = Math.max(0, Math.min(1, backgroundSettings.componentAlpha))
        Theme.componentTint = Math.max(0, Math.min(Theme.componentTints.length - 1, backgroundSettings.componentTint))
    }
    function urlToPath(value) {
        var path = String(value || "")
        return decodeURIComponent(path.indexOf("file:///") === 0 ? path.substring(8) : path)
    }
    FolderDialog {
        id: saveRootDialog
        title: "选择保存根目录"
        onAccepted: { controller.setSaveRoot(urlToPath(selectedFolder)); saveRootField.text = controller.saveRoot() }
    }
    FolderDialog {
        id: modelDirDialog
        title: "选择模型目录"
        onAccepted: { controller.setModelDir(urlToPath(selectedFolder)); modelDirField.text = controller.modelDir() }
    }
    FileDialog {
        id: backgroundDialog
        title: "选择背景图片"
        nameFilters: ["图片 (*.png *.jpg *.jpeg *.bmp *.gif *.webp)"]
        onAccepted: { Theme.backgroundImage = selectedFile; backgroundSettings.backgroundImage = selectedFile }
    }
    Settings {
        id: backgroundSettings
        property url backgroundImage
        property real componentAlpha: 0.2
        property int componentTint: 0
    }
    component SettingLabel: Text {
        color: Theme.textPrimary
        font.family: Theme.uiFont
        font.pixelSize: Theme.fontBody
    }
    component PathField: TextField {
        Layout.fillWidth: true
        selectByMouse: true
        font.family: Theme.uiFont
        font.pixelSize: Theme.fontBody
        color: Theme.textPrimary
        background: Rectangle { radius: Theme.radiusSmall; color: Theme.background; border.width: 1; border.color: Theme.border }
    }
    component SettingsCard: Rectangle {
        default property alias entries: contents.data
        Layout.fillWidth: true
        implicitHeight: contents.implicitHeight + 32
        radius: Theme.radius
        color: Theme.card
        border.width: 1
        border.color: Theme.border
        LiquidGlass { }
        ColumnLayout {
            id: contents
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 16
            spacing: 12
        }
    }
    Flickable {
        id: scroll
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: contentColumn.implicitHeight + 40
        ScrollBar.vertical: ScrollBar { }
        ColumnLayout {
            id: contentColumn
            x: 24; y: 20
            width: scroll.width - 48
            spacing: Theme.spacingLarge
            SettingsCard {
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        SettingLabel { text: "使用教程"; font.bold: true }
                    }
                    SecondaryButton {
                        objectName: "openUserGuideButton"
                        text: "使用说明"
                        onClicked: userGuideDialog.open()
                    }
                }
            }
            SettingsCard {
                SettingLabel { text: "外观"; font.bold: true }
                RowLayout {
                    Layout.fillWidth: true
                    SettingLabel { text: "深色模式" }
                    Item { Layout.fillWidth: true }
                    Switch { checked: Theme.dark; onToggled: Theme.dark = checked }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    SettingLabel { text: "组件不透明度" }
                    Slider {
                        Layout.fillWidth: true
                        from: 0; to: 1; stepSize: 0.1; snapMode: Slider.SnapAlways
                        value: Theme.componentAlpha
                        onMoved: { var v = Math.round(value * 10) / 10; Theme.componentAlpha = v; backgroundSettings.componentAlpha = v }
                    }
                    SettingLabel { Layout.preferredWidth: 48; text: Math.round(Theme.componentAlpha * 100) + "%"; horizontalAlignment: Text.AlignRight }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    SettingLabel { text: "组件颜色" }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: Theme.componentTints.length
                            Rectangle {
                                width: 24; height: 24; radius: 12
                                color: Theme.tintPreview(index)
                                border.width: Theme.componentTint === index ? 2 : 1
                                border.color: Theme.componentTint === index ? Theme.accent : Theme.border
                                scale: swatchMouse.pressed ? 0.92 : 1
                                Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                                Rectangle { anchors.fill: parent; anchors.margins: 4; radius: 8; visible: index === 7; color: "transparent"; border.width: 1; border.color: "#CCFFFFFF" }
                                ToolTip.visible: swatchMouse.containsMouse
                                ToolTip.delay: 350
                                ToolTip.text: Theme.componentTints[index].name
                                MouseArea {
                                    id: swatchMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: { Theme.componentTint = index; backgroundSettings.componentTint = index }
                                }
                            }
                        }
                    }
                }
                SettingLabel {
                    Layout.fillWidth: true
                    text: Theme.componentTints[Theme.componentTint].name + (Theme.liquidGlass ? " · 背景柔化、边缘折射与镜面高光" : "")
                    color: Theme.textSecondary; font.pixelSize: Theme.fontSmall; wrapMode: Text.Wrap
                }
                SettingLabel { text: "背景图片" }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    PathField { text: String(Theme.backgroundImage); readOnly: true }
                    SecondaryButton { text: "选择图片"; onClicked: backgroundDialog.open() }
                    SecondaryButton { text: "恢复预设"; onClicked: { Theme.backgroundImage = ""; backgroundSettings.backgroundImage = "" } }
                }
            }
            SettingsCard {
                SettingLabel { text: "目录"; font.bold: true }
                SettingLabel { text: "保存根目录" }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    PathField {
                        id: saveRootField
                        text: controller.saveRoot()
                        onEditingFinished: {
                            if (text !== controller.saveRoot()) controller.setSaveRoot(text)
                            text = controller.saveRoot()
                        }
                    }
                    SecondaryButton { text: "浏览"; onClicked: saveRootDialog.open() }
                    SecondaryButton { text: "恢复默认"; onClicked: { controller.resetSaveRoot(); saveRootField.text = controller.saveRoot() } }
                }
                SettingLabel { text: "建议使用空文件夹"; color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
                SettingLabel { text: "模型目录" }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    PathField { id: modelDirField; text: controller.modelDir(); readOnly: true }
                    SecondaryButton { text: "浏览"; onClicked: modelDirDialog.open() }
                }
            }
            SettingsCard {
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    SecondaryButton { text: "清除所有概要翻译"; enabled: !controller.siteOverviewTranslating; onClicked: controller.clearAllOverviewTranslations() }
                    SecondaryButton { text: "清除所有评论翻译"; enabled: !controller.siteOverviewTranslating; onClicked: controller.clearAllCommentsTranslations() }
                    Item { Layout.fillWidth: true }
                }
            }
        }
    }
}
