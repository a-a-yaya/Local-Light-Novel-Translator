import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import KakuCH
import "../LocalPaths.js" as LocalPaths

// Murasaki 翻译页：独立的推理模型翻译流程（与 Sakura 翻译流程互不共享）。
Page {
    id: murasakiPage
    objectName: "murasaki"
    background: Rectangle { color: Theme.pageBackground }

    property var models: []
    property bool modelListDirty: true
    // 翻译 / 提取术语进行中：不允许改动翻译目录（队列）
    readonly property bool taskBusy: controller.translating || controller.murasakiTranslating
                                     || controller.qwenTranslating || controller.qwenExtracting
                                     || controller.siteOverviewTranslating

    function refreshModelList() {
        models = controller.murasakiAvailableModels()
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
            if (!controller.murasakiTranslating)
                statusLabel.text = message
        }
        function onMurasakiTranslateProgressChanged() {
            statusLabel.text = controller.murasakiTranslateMessage
        }
        function onMurasakiModelNameChanged() {
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

            // Murasaki 启动模型栏
            ModelBar {
                id: modelBar
                objectName: "translationModelBar"
                onExpandedChanged: if (expanded && modelListDirty) refreshModelList()
                engineKey: "murasaki"
                busy: murasakiPage.taskBusy
                extraActions: Component {
                    TranslationActions {
                        busy: murasakiPage.taskBusy
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
                currentModelName: controller.murasakiModelName
                modelOptions: models
                running: controller.murasakiRunning
                starting: controller.murasakiStarting
                onStartRequested: controller.startMurasaki()
                onStopRequested: controller.stopMurasaki()
                onModelSelected: function(path) {
                    controller.setMurasakiModelPath(path)
                    refreshModelList()
                }
                onRefreshRequested: {
                    controller.refreshMurasakiModels()
                    refreshModelList()
                }
            }

            TranslationSelectionBar {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                busy: murasakiPage.taskBusy
                glossaryMode: false
            }

            TranslationQueue {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.preferredHeight: 300
                busy: murasakiPage.taskBusy
            }

            // 开始翻译
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                spacing: Theme.spacing

                Item { Layout.fillWidth: true }
                PrimaryButton {
                    text: controller.murasakiTranslating ? "翻译中…" : "开始翻译"
                    enabled: !murasakiPage.taskBusy
                    onClicked: controller.startMurasakiTranslate("")
                }
                SecondaryButton {
                    text: "停止"
                    visible: controller.murasakiTranslating
                    onClicked: controller.stopMurasakiTranslate()
                }
            }


            // 进度
            Column {
                visible: controller.murasakiTranslating || statusLabel.text.length > 0
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.bottomMargin: 24
                spacing: 6

                ProgressBar {
                    visible: controller.murasakiTranslating
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, controller.murasakiTranslateTotal)
                    value: controller.murasakiTranslateCurrent
                }
                Text {
                    id: statusLabel
                    Layout.fillWidth: true
                    text: controller.murasakiTranslateMessage
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
