import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KakuCH

// 术语表窗口：点击书库详情页的「术语表」按钮弹出。
// 同时管理两份术语表：人工术语表（用户维护）与 Qwen 术语表（模型生成），
// 翻译时只会启用其中一份（二选一）。
Popup {
    id: root

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(1000, parent ? parent.width - 64 : 1000)
    height: Math.min(700, parent ? parent.height - 64 : 700)
    padding: 0
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: null
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180 }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 220; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 140 } }
    Connections {
        target: Navigation
        function onDismissPanelsRequested() { root.close() }
        function onNavigateRequested(name) { root.close() }
    }

    readonly property string manualKind: "manual"
    readonly property string qwenKind: "qwen"

    property string viewKind: "manual"        // 当前查看的术语表
    property string activeKind: "manual"      // 翻译时启用的术语表
    property string novelTitle: ""            // 当前小说标题（术语窗口是共用实例，
                                              // 必须跟着小说一起刷新，否则看起来像是全局设置）
    property var terms: []
    property int revision: 0
    property string filter: ""
    property string status: ""
    property int gap: 8
    property int radius: 18

    function reload() {
        activeKind = controller.activeGlossaryKind()
        novelTitle = controller.currentNovelTitle()
        terms = controller.glossaryTermsIn(viewKind)
        revision++
        // 蓝字那条是「上一次操作」的即时提示，换书/重开窗口时必须清掉，
        // 否则上本书的提示会一直挂在窗口上，看起来像这本书也是这样。
        status = ""
        // CapsuleTabBar 被点击时会直接给 currentIndex 赋值，声明式绑定随之失效，
        // 之后它就永远停在上次点的那一项（看起来像"所有书共用同一个开关"）。
        // 所以每次刷新都显式同步一次胶囊位置。
        viewTabs.currentIndex = viewKind === qwenKind ? 1 : 0
        activeTabs.currentIndex = activeKind === qwenKind ? 1 : 0
    }

    function filteredTerms() {
        revision
        var q = filter.toLowerCase().trim()
        if (q.length === 0)
            return terms
        var out = []
        for (var i = 0; i < terms.length; i++) {
            var t = terms[i]
            var hay = (t.source + " " + t.target + " " + t.type + " " + t.note).toLowerCase()
            if (hay.indexOf(q) >= 0)
                out.push(t)
        }
        return out
    }

    function kindLabel(kind) {
        return kind === qwenKind ? "Qwen术语表" : "人工术语表"
    }

    function transferTerm(source) {
        var targetKind = viewKind === qwenKind ? manualKind : qwenKind
        var existing = controller.glossaryTermIn(targetKind, source)
        if (!existing.source) {
            controller.copyGlossaryTermTo(viewKind, targetKind, source)
            return
        }
        transferDialog.fromKind = viewKind
        transferDialog.toKind = targetKind
        transferDialog.source = source
        transferDialog.incoming = controller.glossaryTermIn(viewKind, source)
        transferDialog.existing = existing
        transferDialog.warning = ""
        transferDialog.open()
    }

    onVisibleChanged: if (visible) { reload() }
    onAboutToHide: {
        addTermDialog.close()
        editTermDialog.close()
        deleteTermDialog.close()
        clearAllDialog.close()
        transferDialog.close()
    }
    onViewKindChanged: reload()

    Connections {
        target: controller
        function onGlossaryChanged() { root.reload() }
        function onCurrentNovelRowChanged() { transferDialog.close(); root.reload() }
        function onNotify(message) { root.status = message }
    }

    // 窗口外圈：与主窗口一致的白色描边 + 圆角
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: Theme.background
        border.width: 1
        border.color: "white"
        antialiasing: true
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: root.gap
        radius: Math.max(root.radius - root.gap, 0)
        color: Theme.pageBackground
        antialiasing: true
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.gap + 12
        spacing: 12

        // ---- 标题栏（固定位置，不提供拖动） ----
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 34

            RowLayout {
                anchors.fill: parent
                spacing: 10
                Text {
                    text: "术语表"
                    color: Theme.textPrimary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.fontTitle
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: controller.currentNovelTitle()
                    color: Theme.textSecondary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                }
                RoundButton {
                    id: closeButton
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    flat: true
                    text: "✕"
                    onClicked: root.close()
                    contentItem: Text {
                        text: closeButton.text
                        color: Theme.textSecondary
                        font.family: Theme.uiFont
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        // ---- 查看哪一份 / 启用哪一份 ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                text: "查看"
                color: Theme.textSecondary
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontBody
            }
            CapsuleTabBar {
                id: viewTabs
                items: ["人工术语表", "Qwen术语表"]
                currentIndex: root.viewKind === root.qwenKind ? 1 : 0
                onActivated: function(index) {
                    root.viewKind = index === 1 ? root.qwenKind : root.manualKind
                }
            }

            Item { Layout.fillWidth: true }

            Text {
                text: "翻译时启用"
                color: Theme.textSecondary
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontBody
            }
            CapsuleTabBar {
                id: activeTabs
                items: ["人工术语表", "Qwen术语表"]
                currentIndex: root.activeKind === root.qwenKind ? 1 : 0
                onActivated: function(index) {
                    root.activeKind = index === 1 ? root.qwenKind : root.manualKind
                    controller.setActiveGlossaryKind(root.activeKind)
                }
            }
        }

        // ---- 搜索 / 新增 ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            TextField {
                Layout.fillWidth: true
                placeholderText: "搜索术语（原文 / 译名 / 类型 / 备注）"
                selectByMouse: true
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontBody
                color: Theme.textPrimary
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Theme.background
                    border.width: 1
                    border.color: Theme.border
                }
                onTextChanged: root.filter = text
            }
            SecondaryButton {
                text: "+ 添加术语"
                onClicked: addTermDialog.open()
            }
            SecondaryButton {
                text: "删除所有术语"
                enabled: root.terms.length > 0
                palette.buttonText: Colors.danger
                onClicked: clearAllDialog.open()
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.status.length > 0
            text: root.status
            color: Theme.accent
            font.family: Theme.uiFont
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
        }

        Text {
            Layout.fillWidth: true
            visible: root.viewKind === root.qwenKind && !controller.glossaryExistsIn(root.qwenKind)
            text: "还没有 Qwen 术语表。到「翻译 → Qwen翻译」页勾选章节后点「提取术语表」即可生成。"
            color: Theme.textSecondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
        }

        // ---- 术语列表 ----
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radius
            color: Theme.card
            border.width: 1
            border.color: Theme.border

            ListView {
                id: termList
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                model: root.filteredTerms()
                spacing: 4

                delegate: Rectangle {
                    width: ListView.view.width - 12
                    height: 52
                    radius: Theme.radiusSmall
                    color: "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8

                        Text {
                            Layout.preferredWidth: 170
                            text: modelData.source
                            color: modelData.enabled ? Theme.textPrimary : Theme.textSecondary
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.fontBody
                            elide: Text.ElideRight
                        }
                        Text {
                            text: "→"
                            color: Theme.textSecondary
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.fontBody
                        }
                        Text {
                            Layout.preferredWidth: 150
                            text: modelData.target
                            color: modelData.enabled ? Theme.textPrimary : Theme.textSecondary
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.fontBody
                            elide: Text.ElideRight
                        }
                        StatusBadge {
                            text: modelData.type ? modelData.type : "其他"
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.note ? modelData.note : ""
                            color: Theme.textSecondary
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.fontSmall
                            elide: Text.ElideRight
                        }
                        SecondaryButton {
                            text: "编辑"
                            onClicked: {
                                editTermDialog.oldSource = modelData.source
                                editTermDialog.open()
                            }
                        }
                        Button {
                            id: copyTermBtn
                            text: root.viewKind === root.qwenKind ? "→人工" : "→Qwen"
                            implicitHeight: 34
                            implicitWidth: 72
                            palette.buttonText: Theme.accent
                            background: Rectangle {
                                radius: Theme.radiusSmall
                                color: copyTermBtn.hovered ? Qt.rgba(0, 0, 0, 0.05) : "transparent"
                                border.width: 1
                                border.color: Theme.border
                            }
                            onClicked: root.transferTerm(modelData.source)
                        }
                        Button {
                            id: toggleTermBtn
                            text: modelData.enabled ? "禁用" : "启用"
                            implicitHeight: 34
                            implicitWidth: 64
                            palette.buttonText: modelData.enabled ? Theme.textSecondary : "white"
                            background: Rectangle {
                                radius: Theme.radiusSmall
                                color: modelData.enabled
                                       ? (toggleTermBtn.hovered ? Qt.rgba(0, 0, 0, 0.05) : "transparent")
                                       : Theme.accent
                                border.width: 1
                                border.color: modelData.enabled ? Theme.border : "transparent"
                            }
                            onClicked: controller.setGlossaryTermEnabledIn(
                                           root.viewKind, modelData.source, !modelData.enabled)
                        }
                        SecondaryButton {
                            text: "删除"
                            onClicked: {
                                deleteTermDialog.source = modelData.source
                                deleteTermDialog.target = modelData.target
                                deleteTermDialog.open()
                            }
                        }
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 8
                        anchors.verticalCenter: parent.verticalCenter
                        height: 1
                        z: 1
                        visible: !modelData.enabled
                        color: Theme.textSecondary
                        opacity: 0.7
                    }
                }

                Text {
                    visible: root.filteredTerms().length === 0
                    anchors.centerIn: parent
                    text: root.filter.length > 0
                          ? "没有匹配的术语"
                          : (root.viewKind === root.qwenKind
                             ? "Qwen 术语表为空"
                             : "暂无术语，点击右上角「+ 添加术语」")
                    color: Theme.textSecondary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.fontBody
                }
            }
        }

    }

    // ---- 添加 / 编辑 / 删除 ----
    GlassDialog {
        id: addTermDialog
        acceptText: "保存"
        title: "添加术语"
        width: Math.min(480, parent ? parent.width - 40 : 480)

        onAboutToShow: {
            addSourceField.text = ""
            addTargetField.text = ""
            addTypeCombo.currentIndex = 0
            addNoteField.text = ""
            addEnabledSwitch.checked = true
        }
        onAccepted: controller.addGlossaryTermIn(
                        root.viewKind, addSourceField.text, addTargetField.text,
                        addTypeCombo.currentText, addNoteField.text,
                        addEnabledSwitch.checked)

        contentItem: ColumnLayout {
            spacing: 10

            Text { text: "原文"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
            TextField {
                id: addSourceField
                Layout.fillWidth: true
                placeholderText: "请输入日文原文"
                selectByMouse: true
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontBody
                color: Theme.textPrimary
                background: Rectangle { radius: Theme.radiusSmall; color: Theme.background; border.width: 1; border.color: Theme.border }
            }
            Text { text: "术语译名"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
            TextField {
                id: addTargetField
                Layout.fillWidth: true
                placeholderText: "请输入固定中文译名"
                selectByMouse: true
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontBody
                color: Theme.textPrimary
                background: Rectangle { radius: Theme.radiusSmall; color: Theme.background; border.width: 1; border.color: Theme.border }
            }
            Text { text: "类型"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
            GlassComboBox {
                id: addTypeCombo
                Layout.fillWidth: true
                model: ["人物", "地名", "技能名", "专名", "称呼", "物品", "组织", "其他"]
            }
            Text { text: "备注"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
            TextField {
                id: addNoteField
                Layout.fillWidth: true
                placeholderText: "可选"
                selectByMouse: true
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontBody
                color: Theme.textPrimary
                background: Rectangle { radius: Theme.radiusSmall; color: Theme.background; border.width: 1; border.color: Theme.border }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "启用"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontBody }
                Switch { id: addEnabledSwitch; checked: true }
                Item { Layout.fillWidth: true }
            }
        }
    }

    GlassDialog {
        id: editTermDialog
        acceptText: "保存"
        title: "编辑术语"
        width: Math.min(480, parent ? parent.width - 40 : 480)
        property string oldSource: ""

        onAboutToShow: {
            var t = controller.glossaryTermIn(root.viewKind, editTermDialog.oldSource)
            if (!t || !t.source)
                return
            editSourceField.text = t.source
            editTargetField.text = t.target
            editNoteField.text = t.note
            editEnabledSwitch.checked = t.enabled
            var idx = editTypeCombo.find(t.type)
            editTypeCombo.currentIndex = idx >= 0 ? idx : 0
        }
        onAccepted: controller.updateGlossaryTermIn(
                        root.viewKind, editTermDialog.oldSource, editSourceField.text,
                        editTargetField.text, editTypeCombo.currentText,
                        editNoteField.text, editEnabledSwitch.checked)

        contentItem: ColumnLayout {
            spacing: 10

            Text { text: "原文"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
            TextField {
                id: editSourceField
                Layout.fillWidth: true
                selectByMouse: true
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontBody
                color: Theme.textPrimary
                background: Rectangle { radius: Theme.radiusSmall; color: Theme.background; border.width: 1; border.color: Theme.border }
            }
            Text { text: "术语译名"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
            TextField {
                id: editTargetField
                Layout.fillWidth: true
                selectByMouse: true
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontBody
                color: Theme.textPrimary
                background: Rectangle { radius: Theme.radiusSmall; color: Theme.background; border.width: 1; border.color: Theme.border }
            }
            Text { text: "类型"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
            GlassComboBox {
                id: editTypeCombo
                Layout.fillWidth: true
                model: ["人物", "地名", "技能名", "专名", "称呼", "物品", "组织", "其他"]
            }
            Text { text: "备注"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
            TextField {
                id: editNoteField
                Layout.fillWidth: true
                selectByMouse: true
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontBody
                color: Theme.textPrimary
                background: Rectangle { radius: Theme.radiusSmall; color: Theme.background; border.width: 1; border.color: Theme.border }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "启用"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontBody }
                Switch { id: editEnabledSwitch; checked: true }
                Item { Layout.fillWidth: true }
            }
        }
    }

    GlassDialog {
        id: deleteTermDialog
        acceptText: "删除"
        destructive: true
        title: "删除术语"
        property string source: ""
        property string target: ""
        contentItem: Label {
            text: "确定删除术语：\n" + deleteTermDialog.source + " → "
                  + deleteTermDialog.target + "\n\n该术语将不再参与翻译。"
            color: Theme.textPrimary
            font.family: Theme.uiFont
            font.pixelSize: Theme.fontBody
            wrapMode: Text.Wrap
        }
        onAccepted: controller.removeGlossaryTermIn(root.viewKind, deleteTermDialog.source)
    }

    GlassDialog {
        id: transferDialog
        title: "术语已存在，是否覆盖？"
        acceptText: "覆盖"
        rejectText: "保留原有"
        width: Math.min(700, parent ? parent.width - 48 : 700)
        property string fromKind: ""
        property string toKind: ""
        property string source: ""
        property var incoming: ({})
        property var existing: ({})
        property string warning: ""
        contentItem: ColumnLayout {
            spacing: 14
            Text {
                Layout.fillWidth: true
                text: transferDialog.warning || "目标术语表包含相同原文。覆盖将采用左侧的译名、类型、备注和启用状态。"
                color: transferDialog.warning ? Theme.accent : Theme.textSecondary
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontBody
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Repeater {
                    model: [transferDialog.incoming, transferDialog.existing]
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.preferredHeight: comparison.implicitHeight + 28
                        radius: 12
                        color: Theme.glassFill
                        border.width: 1
                        border.color: Theme.glassBorder
                        ColumnLayout {
                            id: comparison
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                            anchors.margins: 14
                            spacing: 12
                            Text { text: index === 0 ? "待转入 · " + root.kindLabel(transferDialog.fromKind) : "原有 · " + root.kindLabel(transferDialog.toKind); color: Theme.accent; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
                            Text { Layout.fillWidth: true; text: modelData.source || ""; textFormat: Text.PlainText; font.bold: true; color: Theme.textPrimary; font.family: Theme.uiFont; font.pixelSize: Theme.fontBody; wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight }
                            Text { Layout.fillWidth: true; text: modelData.target || "（无译名）"; textFormat: Text.PlainText; color: Theme.textPrimary; font.family: Theme.uiFont; font.pixelSize: Theme.fontTitle; wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight }
                            Text { text: (modelData.type || "其他") + " · " + (modelData.enabled ? "已启用" : "已禁用"); color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
                            Text { Layout.fillWidth: true; text: modelData.note || "无备注"; textFormat: Text.PlainText; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall; wrapMode: Text.Wrap; maximumLineCount: 4; elide: Text.ElideRight }
                        }
                    }
                }
            }
        }
        onAccepted: {
            var incomingNow = controller.glossaryTermIn(fromKind, source)
            var existingNow = controller.glossaryTermIn(toKind, source)
            if (!incomingNow.source) { root.status = "源术语已不存在，请刷新后重试"; return }
            if (JSON.stringify(incomingNow) !== JSON.stringify(incoming)
                    || JSON.stringify(existingNow) !== JSON.stringify(existing)) {
                incoming = incomingNow
                existing = existingNow
                warning = "术语内容已更新，请比较最新内容后再选择。"
                Qt.callLater(function() { if (root.visible) transferDialog.open() })
                return
            }
            controller.copyGlossaryTermTo(fromKind, toKind, source, true)
        }
    }

    // ---- 删除整份术语表：只清当前查看的这一份（人工 / Qwen）----
    GlassDialog {
        id: clearAllDialog
        acceptText: "删除"
        destructive: true
        title: "删除所有术语"
        contentItem: Label {
            text: "确定删除《" + controller.currentNovelTitle() + "》的"
                  + root.kindLabel(root.viewKind) + " 全部 " + root.terms.length
                  + " 条术语吗？\n\n只影响这一份术语表，删除后无法恢复。"
            color: Theme.textPrimary
            font.family: Theme.uiFont
            font.pixelSize: Theme.fontBody
            wrapMode: Text.Wrap
        }
        onAccepted: controller.clearGlossaryIn(root.viewKind)
    }
}
