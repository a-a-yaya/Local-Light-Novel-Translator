import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import KakuCH
import "../LocalPaths.js" as LocalPaths

// Sakura 翻译页：管理待翻译文件队列并调用本地 Sakura 翻译。
Page {
    id: translatePage
    objectName: "translate"
    background: Rectangle { color: Theme.pageBackground }

    property var models: []
    property bool modelListDirty: true
    // 翻译 / 提取术语进行中：不允许改动翻译目录（队列）
    readonly property bool taskBusy: controller.translating || controller.murasakiTranslating
                                     || controller.qwenTranslating || controller.qwenExtracting
                                     || controller.siteOverviewTranslating

    function refreshModelList() {
        models = controller.availableModels()
        modelListDirty = false
    }


    FileDialog {
        id: fileDialog
        title: "选择要翻译的 txt 文件"
        nameFilters: ["文本文件 (*.txt)"]
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            var paths = []
            for (var i = 0; i < selectedFiles.length; i++)
                paths.push(LocalPaths.filePath(selectedFiles[i]))
            controller.addFiles(paths)
        }
    }

    FolderDialog {
        id: folderDialog
        title: "添加文件夹中的 txt 文件"
        onAccepted: controller.addFolder(LocalPaths.filePath(selectedFolder))
    }

    Connections {
        target: controller
        function onNotify(message) {
            if (!controller.translating)
                statusLabel.text = message
        }
        function onTranslateProgressChanged() {
            statusLabel.text = controller.translateMessage
        }
        function onModelNameChanged() {
            modelListDirty = true
            if (modelBar.expanded) refreshModelList()
        }
    }

    Flickable {
        id: scroll
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: contentColumn.height
        ScrollBar.vertical: ScrollBar { }

        ColumnLayout {
            id: contentColumn
            width: scroll.width
            spacing: Theme.spacing

            Item { Layout.preferredHeight: 20 }

            // Sakura 启动模型栏
            ModelBar {
                id: modelBar
                objectName: "translationModelBar"
                onExpandedChanged: if (expanded && modelListDirty) refreshModelList()
                engineKey: "sakura"
                busy: translatePage.taskBusy
                extraActions: Component {
                    TranslationActions {
                        busy: translatePage.taskBusy
                        onAddFileRequested: {
                            fileDialog.currentFolder = LocalPaths.folderUrl(controller.downloadDir())
                            fileDialog.open()
                        }
                        onAddFolderRequested: {
                            folderDialog.currentFolder = LocalPaths.folderUrl(controller.downloadDir())
                            folderDialog.open()
                        }
                    }
                }
                z: 100
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                currentModelName: controller.modelName
                modelOptions: models
                running: controller.sakuraRunning
                starting: controller.sakuraStarting
                onStartRequested: controller.startSakura()
                onStopRequested: controller.stopSakura()
                onModelSelected: function(path) {
                    controller.setModelPath(path)
                    refreshModelList()
                }
                onRefreshRequested: {
                    controller.refreshModels()
                    refreshModelList()
                }
            }

            TranslationSelectionBar {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                busy: translatePage.taskBusy
                glossaryMode: false
            }

            TranslationQueue {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.preferredHeight: 300
                busy: translatePage.taskBusy
            }

            // 开始翻译
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                spacing: Theme.spacing

                Item { Layout.fillWidth: true }
                PrimaryButton {
                    text: controller.translating ? "翻译中…" : "开始翻译"
                    enabled: !translatePage.taskBusy
                    onClicked: controller.startTranslate("")
                }
                SecondaryButton {
                    text: "停止"
                    visible: controller.translating
                    onClicked: controller.stopTranslate()
                }
            }


            // 进度
            Column {
                visible: controller.translating || statusLabel.text.length > 0
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.bottomMargin: 24
                spacing: 6

                ProgressBar {
                    visible: controller.translating
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, controller.translateTotal)
                    value: controller.translateCurrent
                }
                Text {
                    id: statusLabel
                    Layout.fillWidth: true
                    text: controller.translateMessage
                    color: Theme.textSecondary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                }
            }

            Item { Layout.preferredHeight: 20 }
        }
    }
}
