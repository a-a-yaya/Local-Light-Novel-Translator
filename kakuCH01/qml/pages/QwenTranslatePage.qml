import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import KakuCH
import "../LocalPaths.js" as LocalPaths

// Qwen 翻译页：通用模型的翻译流程，与 Sakura / Murasaki 互不共享。
// 与另外两页的区别：额外提供「提取术语表」按钮，只生成术语表、不产出译文。
Page {
    id: qwenPage
    objectName: "qwen"
    background: Rectangle { color: Theme.pageBackground }

    property var models: []
    property bool modelListDirty: true
    // 开启后本页进入「提取术语」模式：章节勾选切换为术语专用勾选，翻译按钮收起。
    property bool extractMode: true
    // 翻译 / 提取术语进行中：不允许改动翻译目录（队列）
    readonly property bool taskBusy: controller.translating || controller.murasakiTranslating
                                     || controller.qwenTranslating || controller.qwenExtracting
                                     || controller.siteOverviewTranslating

    function refreshModelList() {
        models = controller.qwenAvailableModels()
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
            if (!controller.qwenTranslating && !controller.qwenExtracting)
                statusLabel.text = message
        }
        function onQwenTranslateProgressChanged() {
            statusLabel.text = controller.qwenTranslateMessage
        }
        function onQwenExtractProgressChanged() {
            statusLabel.text = controller.qwenExtractMessage
        }
        function onQwenModelNameChanged() {
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

            // Qwen 启动模型栏
            ModelBar {
                id: modelBar
                objectName: "translationModelBar"
                onExpandedChanged: if (expanded && modelListDirty) refreshModelList()
                engineKey: "qwen"
                busy: qwenPage.taskBusy
                extraActions: Component {
                    TranslationActions {
                        busy: qwenPage.taskBusy
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
                currentModelName: controller.qwenModelName
                modelOptions: models
                running: controller.qwenRunning
                starting: controller.qwenStarting
                onStartRequested: controller.startQwen()
                onStopRequested: controller.stopQwen()
                onModelSelected: function(path) {
                    controller.setQwenModelPath(path)
                    refreshModelList()
                }
                onRefreshRequested: {
                    controller.refreshQwenModels()
                    refreshModelList()
                }
            }

            TranslationSelectionBar {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                busy: qwenPage.taskBusy
                glossaryMode: qwenPage.extractMode
            }

            TranslationQueue {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.preferredHeight: 300
                busy: qwenPage.taskBusy
                glossaryMode: qwenPage.extractMode
            }

            // 操作按钮
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                spacing: Theme.spacing

                CapsuleTabBar {
                    items: ["提取术语", "翻译章节"]
                    itemHeight: 30
                    enabled: !qwenPage.taskBusy
                    currentIndex: qwenPage.extractMode ? 0 : 1
                    onActivated: function(index) { qwenPage.extractMode = index === 0 }
                }
                Item { Layout.fillWidth: true }

                PrimaryButton {
                    text: controller.qwenExtracting ? "提取中…" : "开始提取术语"
                    visible: qwenPage.extractMode
                    enabled: !qwenPage.taskBusy
                    onClicked: controller.startQwenGlossaryExtraction()
                }
                SecondaryButton {
                    text: "取消提取"
                    visible: controller.qwenExtracting
                    onClicked: controller.cancelQwenGlossaryExtraction()
                }
                PrimaryButton {
                    text: controller.qwenTranslating ? "翻译中…" : "开始翻译"
                    visible: !qwenPage.extractMode
                    enabled: !qwenPage.taskBusy
                    onClicked: controller.startQwenTranslate("")
                }
                SecondaryButton {
                    text: "停止"
                    visible: controller.qwenTranslating
                    onClicked: controller.stopQwenTranslate()
                }
            }



            // 进度
            Column {
                visible: controller.qwenTranslating || controller.qwenExtracting
                         || statusLabel.text.length > 0
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.bottomMargin: 24
                spacing: 6

                ProgressBar {
                    visible: controller.qwenTranslating
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, controller.qwenTranslateTotal)
                    value: controller.qwenTranslateCurrent
                }
                ProgressBar {
                    visible: controller.qwenExtracting
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, controller.qwenExtractTotal)
                    value: controller.qwenExtractCurrent
                }
                Text {
                    id: statusLabel
                    Layout.fillWidth: true
                    text: controller.qwenExtracting ? controller.qwenExtractMessage : controller.qwenTranslateMessage
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
