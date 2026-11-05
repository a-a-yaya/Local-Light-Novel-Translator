import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KakuCH

Page {
    id: readerPage
    objectName: "reader"
    focus: true
    clip: true
    property bool applicationFullScreen: false
    // The window supplies the active island's width plus its right margin and a gap.
    property real topOverlayInset: 0
    signal fullScreenRequested()
    property bool controlsShown: true
    readonly property bool topControlsVisible: controlsShown || rm.editing
        || settingsPanel.visible || bookmarkPanel.visible
        || (readerHover.hovered && readerHover.point.position.y < topBar.height + 14)
    readonly property bool bottomControlsVisible: controlsShown || rm.editing
        || (readerHover.hovered && readerHover.point.position.y > height - bottomBar.height - 14)
    readonly property var rm: controller.readerModel
    readonly property bool night: rm.readerTheme === "dark" || rm.readerTheme === "black"
    readonly property color readerBackground: rm.readerTheme === "sepia" ? "#F3EBDD"
        : rm.readerTheme === "black" ? "#080A09" : night ? "#191D1B" : "#F8F8F4"
    readonly property color readerSurface: rm.readerTheme === "sepia" ? "#FBF5E9"
        : night ? "#222823" : "#FFFFFF"
    readonly property color readerText: night ? "#E5E8E1" : "#29362E"
    readonly property color readerSecondary: night ? "#9DA99D" : "#7A857A"
    readonly property color readerAccent: night ? "#B2C9A7" : "#4E6952"
    readonly property color readerBorder: night ? "#39443A" : "#E0E5DB"
    readonly property real textWidth: Math.min(rm.maxWidth, Math.max(240, width - 80))
    readonly property string modeLabel: rm.readingMode === "original" ? "日文原文"
        : rm.readingMode === "bilingual" ? "中日对照" : "中文译文"
    readonly property real chapterProgress: readerScroll.contentHeight <= readerScroll.height ? 1
        : Math.max(0, Math.min(1, readerScroll.contentY / (readerScroll.contentHeight - readerScroll.height)))
    property var pendingAction: null
    property string noticeText: ""
    property bool noticeIsError: false
    property bool restoringPosition: true
    property int restoreAttempts: 0
    property bool findVisible: false
    property int draftRevision: 0
    property int matchCursor: -1
    readonly property var findMatches: {
        if (!findVisible || !rm.editing) return []
        draftRevision
        return rm.findEditorMatches(findField.text, caseCheck.checked)
    }
    background: Rectangle {
        color: readerBackground
        Behavior on color { ColorAnimation { duration: 180 } }
    }

    function hideControls() { if (!rm.editing) controlsShown = false }
    function toggleControls() { if (!rm.editing) controlsShown = !controlsShown }
    function toggleFullScreen() { capturePosition(); fullScreenRequested() }
    function handleReaderKey(event) {
        if (event.key === Qt.Key_F11 && event.modifiers === Qt.NoModifier) {
            if (!event.isAutoRepeat) toggleFullScreen()
            event.accepted = true
        } else event.accepted = false
    }
    onApplicationFullScreenChanged: {
        settingsPanel.close()
        bookmarkPanel.close()
        // Width changes preserve the paragraph and its fractional scroll offset.
        if (rm) Qt.callLater(restorePosition)
    }
    HoverHandler {
        id: readerHover
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
    }

    // Main 的所有离开阅读器入口（含侧栏和窗口关闭）复用此保护。
    function requestLeave(action) {
        if (rm.editing && rm.editDirty) {
            pendingAction = action
            unsavedDialog.open()
            return false
        }
        if (rm.editing) rm.cancelEditing()
        capturePosition()
        rm.saveProgress()
        return true
    }
    function perform(action) { if (requestLeave(action)) action() }
    function changeChapter(step) {
        perform(function() { if (step < 0) rm.previousChapter(); else rm.nextChapter() })
    }
    function startEditing() {
        capturePosition()
        rm.saveProgress()
        settingsPanel.close()
        if (rm.beginEditing() && rm.readingMode === "translation")
            Qt.callLater(function() { chapterEditor.forceActiveFocus() })
    }
    function switchVersion() {
        capturePosition()
        rm.saveProgress()
        rm.setViewingOriginal(!rm.viewingOriginal)
    }
    function capturePosition() {
        if (restoringPosition || rm.editing || readerScroll.count === 0) return
        var index = -1
        for (var y = 1; index < 0 && y < readerScroll.height; y += 8)
            index = readerScroll.indexAt(readerScroll.width / 2, readerScroll.contentY + y)
        if (index < 0) index = readerScroll.atYBeginning ? 0 : readerScroll.count - 1
        var item = readerScroll.itemAtIndex(index)
        var offset = item ? Math.max(0, Math.min(1, (readerScroll.contentY - item.y) / Math.max(1, item.height))) : 0
        rm.setPosition(index, offset)
    }
    function restorePosition() {
        restoringPosition = true
        positionCapture.stop()
        restoreAttempts = 0
        positionRestore.restart()
    }
    function openFind() {
        findVisible = true
        Qt.callLater(function() { findField.forceActiveFocus(); findField.selectAll() })
    }
    function selectMatch(index) {
        if (findMatches.length === 0) { matchCursor = -1; return }
        matchCursor = Math.max(0, Math.min(index, findMatches.length - 1))
        var match = findMatches[matchCursor]
        if (match.block < 0) {
            chapterEditor.forceActiveFocus()
            chapterEditor.select(match.start, match.start + match.length)
        } else {
            bilingualEditor.positionViewAtIndex(match.block, ListView.Contain)
            Qt.callLater(function() {
                bilingualEditor.forceLayout()
                var item = bilingualEditor.itemAtIndex(match.block)
                if (item) {
                    item.editor.forceActiveFocus()
                    item.editor.select(match.start, match.start + match.length)
                    var cursorY = item.editor.mapToItem(bilingualEditor.contentItem, 0, item.editor.cursorRectangle.y).y
                    if (cursorY < bilingualEditor.contentY) bilingualEditor.contentY = cursorY
                    else if (cursorY > bilingualEditor.contentY + bilingualEditor.height - 40)
                        bilingualEditor.contentY = cursorY - bilingualEditor.height + 40
                }
            })
        }
    }
    function nextMatch(step) {
        if (!findMatches.length) return
        var next = matchCursor < 0 ? (step < 0 ? findMatches.length - 1 : 0)
            : (matchCursor + step + findMatches.length) % findMatches.length
        selectMatch(next)
    }
    function replaceMatches(all) {
        if (!findMatches.length) return
        var index = matchCursor < 0 ? 0 : Math.min(matchCursor, findMatches.length - 1)
        var changed = rm.replaceEditorMatches(findField.text, replaceField.text, all ? -1 : index, caseCheck.checked)
        showNotice("已替换 " + changed + " 处，保存后生效", false)
        Qt.callLater(function() { if (findMatches.length) selectMatch(Math.min(index, findMatches.length - 1)); else matchCursor = -1 })
    }

    Timer { id: positionCapture; interval: 100; onTriggered: capturePosition() }
    Timer {
        id: positionRestore
        interval: 50
        onTriggered: {
            if (rm.editing) { restoringPosition = false; return }
            readerScroll.forceLayout()
            var paragraph = Math.min(rm.positionParagraph, Math.max(0, readerScroll.count - 1))
            if (paragraph === 0 && rm.positionOffset === 0) readerScroll.positionViewAtBeginning()
            else readerScroll.positionViewAtIndex(paragraph, ListView.Beginning)
            var item = readerScroll.itemAtIndex(paragraph)
            if (!item && readerScroll.count > 0 && restoreAttempts++ < 4) { restart(); return }
            if (item && (paragraph > 0 || rm.positionOffset > 0))
                readerScroll.contentY = Math.min(item.y + rm.positionOffset * item.height,
                    Math.max(readerScroll.originY, readerScroll.originY + readerScroll.contentHeight - readerScroll.height))
            Qt.callLater(function() { restoringPosition = false })
        }
    }
    function withAlpha(color, alpha) { return Qt.rgba(color.r, color.g, color.b, alpha) }
    function richParagraph(value) {
        var safe = String(value).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/\n/g, "<br/>")
        return '<p style="margin:0;line-height:' + Math.round(rm.lineSpacing * 100) + '%">' + safe + '</p>'
    }
    function showNotice(message, error) {
        noticeText = message; noticeIsError = error
        notice.open(); noticeTimer.restart()
    }
    function finishPending(save) {
        if (save && !rm.saveEdits()) return
        if (!save) rm.cancelEditing()
        var action = pendingAction
        pendingAction = null
        unsavedDialog.close()
        if (action) action()
    }

    component ReaderButton: Button {
        id: button
        property bool emphasized: false
        property bool selected: false
        property bool appStyle: false
        property string helpText: ""
        readonly property color foreground: appStyle ? Theme.textPrimary : readerText
        readonly property color muted: appStyle ? Theme.textSecondary : readerSecondary
        readonly property color accent: appStyle ? Theme.accent : readerAccent
        readonly property color fill: appStyle ? Theme.glassButton : readerBorder
        implicitHeight: 38
        implicitWidth: Math.max(38, label.implicitWidth + 28)
        padding: 0
        hoverEnabled: true
        font.family: Theme.uiFont
        font.pixelSize: 13
        Accessible.name: text
        ToolTip.visible: hovered && helpText.length > 0
        ToolTip.text: helpText
        ToolTip.delay: 500
        contentItem: Text {
            id: label
            text: button.text
            font: button.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: !button.enabled ? button.muted : button.emphasized ? (button.appStyle ? "white" : readerSurface) : button.foreground
            opacity: button.enabled ? 1 : 0.45
        }
        background: Rectangle {
            radius: 10
            color: button.emphasized ? button.accent : button.selected || button.down
                ? button.fill : button.hovered ? withAlpha(button.accent, 0.09) : "transparent"
            Behavior on color { ColorAnimation { duration: 110 } }
        }
    }

    component ReadingText: TextArea {
        id: selectableParagraph
        readonly property bool readingParagraph: true
        property string glossaryTranslation: ""
        persistentSelection: true
        property point pressPoint: Qt.point(0, 0)
        Keys.onPressed: function(event) { readerPage.handleReaderKey(event) }
        onPressed: function(event) { pressPoint = Qt.point(event.x, event.y) }
        onReleased: function(event) {
            if (event.button === Qt.LeftButton && selectedText.length === 0
                    && !readerScroll.dragging && !readerScroll.flicking
                    && Math.hypot(event.x - pressPoint.x, event.y - pressPoint.y) < Qt.styleHints.startDragDistance)
                readerPage.toggleControls()
        }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            preventStealing: true
            cursorShape: Qt.IBeamCursor
            onPressed: function(event) {
                readerPage.openContextMenu(selectableParagraph, selectableParagraph.glossaryTranslation,
                    mapToItem(Overlay.overlay, event.x, event.y))
            }
        }
    }

    Shortcut { sequence: "Ctrl+S"; enabled: rm.editing && !unsavedDialog.visible; onActivated: rm.saveEdits() }
    Shortcut { sequence: "Ctrl+E"; enabled: !rm.editing && rm.canEdit; onActivated: startEditing() }
    Shortcut { sequence: "Ctrl+F"; enabled: rm.editing; onActivated: openFind() }
    Shortcut { sequence: "Ctrl+H"; enabled: rm.editing; onActivated: openFind() }
    Shortcut { sequence: "F3"; enabled: rm.editing && findVisible; onActivated: nextMatch(1) }
    Shortcut { sequence: "Shift+F3"; enabled: rm.editing && findVisible; onActivated: nextMatch(-1) }
    Shortcut { sequence: "Ctrl+Right"; enabled: !rm.editing; onActivated: changeChapter(1) }
    Shortcut { sequence: "Ctrl+Left"; enabled: !rm.editing; onActivated: changeChapter(-1) }
    Shortcut { sequence: "Right"; enabled: !rm.editing && !settingsPanel.visible; onActivated: changeChapter(1) }
    Shortcut { sequence: "Left"; enabled: !rm.editing && !settingsPanel.visible; onActivated: changeChapter(-1) }
    Shortcut { sequence: "+"; enabled: !rm.editing; onActivated: rm.setFontSize(rm.fontSize + 1) }
    Shortcut { sequence: "-"; enabled: !rm.editing; onActivated: rm.setFontSize(rm.fontSize - 1) }
    Shortcut { sequence: "PgDown"; enabled: !rm.editing; onActivated: readerScroll.contentY = Math.min(Math.max(0, readerScroll.contentHeight - readerScroll.height), readerScroll.contentY + readerScroll.height * 0.85) }
    Shortcut { sequence: "PgUp"; enabled: !rm.editing; onActivated: readerScroll.contentY = Math.max(0, readerScroll.contentY - readerScroll.height * 0.85) }
    Shortcut {
        sequence: "Escape"; enabled: !unsavedDialog.visible
        onActivated: {
            if (findVisible && rm.editing) findVisible = false
            else if (bookmarkPanel.visible) bookmarkPanel.close()
            else if (settingsPanel.visible) settingsPanel.close()
            else if (rm.editing) perform(function() {})
            else if (applicationFullScreen) readerPage.toggleFullScreen()
            else Navigation.navigate("detail")
        }
    }

    GlassDialog {
        id: unsavedDialog
        title: "保留这次修改？"
        width: Math.min(520, parent ? parent.width - 40 : 520)
        standardButtons: Dialog.NoButton
        contentItem: ColumnLayout {
            spacing: 20
            Label {
                Layout.fillWidth: true
                text: "本章还有未保存的编辑。保存后，将更新唯一一份编辑稿，原版始终保留。"
                color: Theme.textSecondary; wrapMode: Text.Wrap
                font.family: Theme.uiFont; font.pixelSize: 14
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                SecondaryButton { text: "继续编辑"; onClicked: unsavedDialog.close() }
                SecondaryButton { text: "放弃修改"; onClicked: finishPending(false) }
                PrimaryButton { text: "保存并继续"; onClicked: finishPending(true) }
            }
        }
        onClosed: pendingAction = null
    }

    GlassPopup {
        id: notice
        x: Math.max(16, (readerPage.width - width) / 2)
        y: readerPage.height - height - 90
        width: Math.min(600, readerPage.width - 32)
        padding: 14
        closePolicy: Popup.NoAutoClose
        contentItem: Label { text: noticeText; color: Theme.textPrimary; wrapMode: Text.Wrap; font.family: Theme.uiFont; font.pixelSize: 13 }
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 130 } }
        exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 100 } }
    }
    Timer { id: noticeTimer; interval: noticeIsError ? 7000 : 2600; onTriggered: notice.close() }

    // 右键选中文本后的术语上下文
    property string contextSource: ""
    property string contextTranslation: ""
    property bool contextTermExists: false
    property bool contextTermEnabled: false
    property string contextType: "人物"
    property string contextTarget: ""
    property string contextNote: ""
    property var contextArea: null

    function openContextMenu(area, translation, point) {
        contextArea = area
        var sel = area ? String(area.selectedText) : ""
        contextSource = sel.trim()
        contextTranslation = translation ? String(translation) : ""
        contextTermExists = contextSource.length > 0
                && controller.currentNovelGlossaryContains(contextSource)
        var t = contextTermExists ? controller.currentNovelGlossaryTerm(contextSource) : null
        contextTermEnabled = t ? (t.enabled === true) : true
        contextType = (t && t.type) ? String(t.type) : "人物"
        contextTarget = (t && t.target) ? String(t.target) : ""
        contextNote = (t && t.note) ? String(t.note) : ""
        blockMenu.popup(Math.max(8, Math.min(point.x, Overlay.overlay.width - blockMenu.width - 8)),
                        Math.max(8, Math.min(point.y, Overlay.overlay.height - blockMenu.implicitHeight - 8)))
    }

    Menu {
        id: blockMenu
        parent: Overlay.overlay
        width: 180
        font.family: Theme.uiFont
        palette.text: Theme.textPrimary
        palette.windowText: Theme.textPrimary
        palette.buttonText: Theme.textPrimary
        background: Rectangle {
            radius: 12
            color: Theme.dark ? Qt.rgba(0.11, 0.11, 0.12, 0.80) : Qt.rgba(1, 1, 1, 0.80)
            border.width: 1
            border.color: Theme.glassBorder
        }

        MenuItem {
            text: "复制"
            enabled: contextSource.length > 0
            onTriggered: { if (contextArea) contextArea.copy() }
        }
        MenuSeparator {}
        MenuItem {
            text: "添加为术语"
            enabled: contextSource.length > 0 && !contextTermExists
            onTriggered: addTermDialog.open()
        }
        MenuItem {
            text: "编辑术语"
            visible: contextTermExists
            height: visible ? implicitHeight : 0
            onTriggered: editTermDialog.open()
        }
        MenuItem {
            text: contextTermEnabled ? "禁用术语" : "启用术语"
            visible: contextTermExists
            height: visible ? implicitHeight : 0
            onTriggered: controller.setCurrentNovelGlossaryEnabled(contextSource, !contextTermEnabled)
        }
        MenuItem {
            text: "删除术语"
            visible: contextTermExists
            height: visible ? implicitHeight : 0
            onTriggered: deleteTermDialog.open()
        }
    }

    GlassDialog {
        id: addTermDialog
        acceptText: "保存"
        title: "添加术语"
        width: Math.min(480, parent ? parent.width - 40 : 480)

        onAboutToShow: {
            addSourceField.text = contextSource
            addCurrentTransField.text = contextTranslation
            addTargetField.text = ""
            addNoteField.text = ""
            addTypeCombo.currentIndex = 0
            addEnabledSwitch.checked = true
        }
        onAccepted: {
            controller.addCurrentNovelGlossaryTerm(
                addSourceField.text, addTargetField.text,
                addTypeCombo.currentText, addNoteField.text,
                addEnabledSwitch.checked)
        }

        contentItem: ColumnLayout {
            spacing: 10

            Text { text: "原文"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
            TextField {
                id: addSourceField
                Layout.fillWidth: true
                readOnly: true
                selectByMouse: true
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontBody
                color: Theme.textPrimary
                background: Rectangle { radius: Theme.radiusSmall; color: Theme.background; border.width: 1; border.color: Theme.border }
            }
            Text { text: "当前翻译"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
            TextField {
                id: addCurrentTransField
                Layout.fillWidth: true
                readOnly: true
                selectByMouse: true
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontBody
                color: Theme.textSecondary
                background: Rectangle { radius: Theme.radiusSmall; color: Theme.background; border.width: 1; border.color: Theme.border }
            }
            Text { text: "术语译名"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
            TextField {
                id: addTargetField
                Layout.fillWidth: true
                placeholderText: "请输入固定中文译名"
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontBody
                color: Theme.textPrimary
                background: Rectangle { radius: Theme.radiusSmall; color: Theme.background; border.width: 1; border.color: Theme.border }
            }
            Text { text: "类型"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
            GlassComboBox {
                id: addTypeCombo
                Layout.fillWidth: true
                model: ["人物", "地点", "组织", "技能", "武器", "物品", "种族", "称号", "其他"]
            }
            Text { text: "备注"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
            TextField {
                id: addNoteField
                Layout.fillWidth: true
                placeholderText: "可选"
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

        onAboutToShow: {
            editSourceField.text = contextSource
            editTargetField.text = contextTarget
            editNoteField.text = contextNote
            var types = ["人物", "地点", "组织", "技能", "武器", "物品", "种族", "称号", "其他"]
            var idx = types.indexOf(contextType)
            editTypeCombo.currentIndex = idx >= 0 ? idx : 0
            editEnabledSwitch.checked = contextTermEnabled
        }
        onAccepted: {
            controller.updateCurrentNovelGlossaryTerm(
                contextSource, editSourceField.text, editTargetField.text,
                editTypeCombo.currentText, editNoteField.text,
                editEnabledSwitch.checked)
        }

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
                font.family: Theme.uiFont
                font.pixelSize: Theme.fontBody
                color: Theme.textPrimary
                background: Rectangle { radius: Theme.radiusSmall; color: Theme.background; border.width: 1; border.color: Theme.border }
            }
            Text { text: "类型"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
            GlassComboBox {
                id: editTypeCombo
                Layout.fillWidth: true
                model: ["人物", "地点", "组织", "技能", "武器", "物品", "种族", "称号", "其他"]
            }
            Text { text: "备注"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
            TextField {
                id: editNoteField
                Layout.fillWidth: true
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
        contentItem: Label {
            text: "确定删除术语：\n" + contextSource + " → " + contextTarget + "\n\n该术语将不再参与翻译。"
            color: Theme.textPrimary
            font.family: Theme.uiFont
            font.pixelSize: Theme.fontBody
            wrapMode: Text.Wrap
        }
        onAccepted: controller.removeCurrentNovelGlossaryTerm(contextSource)
    }

    Rectangle {
        id: topBar
        z: 3
        anchors.left: parent.left; anchors.right: parent.right
        y: readerPage.topControlsVisible ? 0 : -height
        opacity: readerPage.topControlsVisible ? 1 : 0
        enabled: readerPage.topControlsVisible
        Behavior on y { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 150 } }
        height: 76
        color: readerBackground
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: readerBorder }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 24
            anchors.rightMargin: Math.max(24, readerPage.topOverlayInset)
            spacing: 12
            ReaderButton { text: "‹  目录"; onClicked: Navigation.navigate("detail") }
            Rectangle { width: 1; height: 20; color: readerBorder }
            ColumnLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 3
                Text {
                    Layout.fillWidth: true
                    text: rm.novelTitle; elide: Text.ElideRight
                    color: readerText; font.family: Theme.uiFont; font.pixelSize: 14; font.weight: Font.Medium
                }
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: rm.editing ? (rm.editDirty ? "正在编辑 · 未保存" : "正在编辑 · 尚无修改")
                        : modeLabel + (rm.viewingOriginal ? " · 编辑前原版" : rm.hasEdits ? " · 已保存的编辑稿" : "")
                    color: readerSecondary; font.family: Theme.uiFont; font.pixelSize: 11
                }
            }
            ReaderButton {
                visible: rm.hasEdits && !rm.editing
                text: rm.viewingOriginal ? "返回编辑稿" : "查看原版"
                selected: rm.viewingOriginal
                helpText: "切换查看编辑前的译文与最新编辑稿"
                onClicked: switchVersion()
            }
            ReaderButton { text: "书签"; visible: !rm.editing; selected: bookmarkPanel.visible; onClicked: { capturePosition(); bookmarkPanel.open() } }
            ReaderButton { text: "编辑译文"; visible: !rm.editing && rm.readingMode !== "original"; enabled: rm.canEdit; helpText: "编辑本章中文 · Ctrl + E"; onClicked: startEditing() }
            ReaderButton { text: "设置"; visible: !rm.editing; selected: settingsPanel.visible; onClicked: settingsPanel.visible ? settingsPanel.close() : settingsPanel.open() }
            ReaderButton { text: "查找替换"; visible: rm.editing; selected: findVisible; onClicked: openFind() }
            ReaderButton { text: "取消"; visible: rm.editing; onClicked: perform(function() {}) }
            ReaderButton { text: "保存"; visible: rm.editing; emphasized: true; onClicked: rm.saveEdits() }
            ReaderButton {
                objectName: "readerFullScreenButton"
                implicitWidth: 38
                text: readerPage.applicationFullScreen ? "退出阅读全屏" : "阅读全屏"
                helpText: text + " · F11"
                selected: readerPage.applicationFullScreen
                onClicked: readerPage.toggleFullScreen()
                contentItem: Item {
                    Canvas {
                        anchors.centerIn: parent
                        width: 20; height: 20
                        property bool expanded: readerPage.applicationFullScreen
                        property color ink: readerText
                        onExpandedChanged: requestPaint()
                        onInkChanged: requestPaint()
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.reset()
                            ctx.strokeStyle = ink
                            ctx.lineWidth = 1.7
                            ctx.lineCap = "round"
                            ctx.lineJoin = "round"
                            var corners = expanded
                                ? [[2,7,7,7,7,2], [13,2,13,7,18,7], [18,13,13,13,13,18], [7,18,7,13,2,13]]
                                : [[2,7,2,2,7,2], [13,2,18,2,18,7], [18,13,18,18,13,18], [7,18,2,18,2,13]]
                            ctx.beginPath()
                            for (var i = 0; i < corners.length; ++i) {
                                var p = corners[i]
                                ctx.moveTo(p[0], p[1]); ctx.lineTo(p[2], p[3]); ctx.lineTo(p[4], p[5])
                            }
                            ctx.stroke()
                        }
                    }
                }
            }
        }
    }

    GlassPopup {
        id: bookmarkPanel
        x: Math.max(16, readerPage.width - width - 24)
        y: topBar.height + 8
        width: Math.min(440, readerPage.width - 32)
        height: Math.min(450, readerPage.height - 120)
        padding: 20
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        contentItem: ColumnLayout {
            spacing: 12
            Text { text: modeLabel + " · 书签"; color: Theme.textPrimary; font.family: Theme.uiFont; font.pixelSize: 17 }
            Text { Layout.fillWidth: true; text: "退出位置自动更新；手动书签保留在添加的位置。"; wrapMode: Text.Wrap; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: 12 }
            ReaderButton { appStyle: true;
                Layout.fillWidth: true
                text: (rm.currentBookmarked ? "移除" : "添加") + "当前位置书签 · 第 " + (rm.positionParagraph + 1) + " 段"
                enabled: readerScroll.count > 0
                selected: rm.currentBookmarked
                onClicked: rm.toggleBookmark()
            }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true; spacing: 6
                model: rm.bookmarks
                ScrollBar.vertical: ScrollBar {}
                delegate: RowLayout {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    Button {
                        Layout.fillWidth: true
                        implicitHeight: 64
                        enabled: modelData.chapterIndex >= 0
                        contentItem: Column {
                            spacing: 5
                            Text { width: parent.width; text: modelData.title + " · 第 " + (modelData.paragraph + 1) + " 段"; elide: Text.ElideRight; color: Theme.textPrimary; font.family: Theme.uiFont; font.pixelSize: 12 }
                            Text { width: parent.width; text: modelData.preview; textFormat: Text.PlainText; elide: Text.ElideRight; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: 11 }
                        }
                        background: Rectangle { radius: 10; color: Theme.background }
                        onClicked: {
                            var selectedIndex = index
                            bookmarkPanel.close()
                            perform(function() { rm.openBookmark(selectedIndex) })
                        }
                    }
                    ReaderButton { appStyle: true; text: "×"; helpText: "删除此书签"; onClicked: rm.removeBookmark(modelData.chapter, modelData.paragraph) }
                }
                Text { anchors.centerIn: parent; visible: rm.bookmarks.length === 0; text: "当前目录暂无书签"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: 13 }
            }
        }
    }

    GlassPopup {
        id: settingsPanel
        x: Math.max(16, readerPage.width - width - 24)
        y: topBar.height + 8
        width: Math.min(500, readerPage.width - 32)
        padding: 24
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        contentItem: ColumnLayout {
            spacing: 18
            Text { text: "阅读偏好"; color: Theme.textPrimary; font.family: Theme.uiFont; font.pixelSize: 17; font.weight: Font.Medium }
            RowLayout {
                Layout.fillWidth: true
                Repeater {
                    model: ["中文译文", "日文原文", "中日对照"]
                    ReaderButton { appStyle: true;
                        required property string modelData
                        required property int index
                        Layout.fillWidth: true
                        text: modelData
                        selected: rm.readingMode === ["translation", "original", "bilingual"][index]
                        onClicked: { capturePosition(); rm.setReadingMode(["translation", "original", "bilingual"][index]); settingsPanel.close() }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Repeater {
                    model: ["素纸", "暖纸", "夜读", "纯黑"]
                    ReaderButton { appStyle: true;
                        required property string modelData
                        required property int index
                        Layout.fillWidth: true; text: modelData
                        selected: rm.readerTheme === ["light", "sepia", "dark", "black"][index]
                        onClicked: rm.setReaderTheme(["light", "sepia", "dark", "black"][index])
                    }
                }
            }
            GridLayout {
                columns: 4; columnSpacing: 12; rowSpacing: 12; Layout.fillWidth: true
                Text { text: "字号"; color: Theme.textSecondary; font.family: Theme.uiFont }
                ReaderButton { appStyle: true; text: "−"; onClicked: rm.setFontSize(rm.fontSize - 1) }
                Text { text: rm.fontSize + " px"; color: Theme.textPrimary; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
                ReaderButton { appStyle: true; text: "+"; onClicked: rm.setFontSize(rm.fontSize + 1) }
                Text { text: "行距"; color: Theme.textSecondary; font.family: Theme.uiFont }
                ReaderButton { appStyle: true; text: "−"; onClicked: rm.setLineSpacing(rm.lineSpacing - 0.1) }
                Text { text: rm.lineSpacing.toFixed(1); color: Theme.textPrimary; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
                ReaderButton { appStyle: true; text: "+"; onClicked: rm.setLineSpacing(rm.lineSpacing + 0.1) }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "版心"; color: Theme.textSecondary; font.family: Theme.uiFont }
                Slider { Layout.fillWidth: true; from: 400; to: 1100; stepSize: 20; value: rm.maxWidth; onMoved: rm.setMaxWidth(value) }
                Text { text: Math.round(rm.maxWidth); color: Theme.textPrimary; font.pixelSize: 12 }
            }
            GlassComboBox {
                Layout.fillWidth: true
                model: ["Noto Serif SC", "Noto Sans SC", "Microsoft YaHei UI"]
                currentIndex: Math.max(0, model.indexOf(rm.fontFamily))
                onActivated: rm.setFontFamily(currentText)
            }
            Text { text: "Ctrl + ← / → 换章  ·  Ctrl + E 编辑  ·  F11 阅读全屏"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: 11 }
        }
    }

    ListView {
        id: readerScroll
        anchors.fill: parent
        visible: !rm.editing
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        spacing: 12 + (rm.lineSpacing - 1) * 14
        topMargin: 0; bottomMargin: bottomBar.height + 48
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        model: rm.readingMode === "bilingual" ? rm.bilingualSources
            : rm.readingMode === "original" ? rm.originalParagraphs : rm.paragraphs
        onContentYChanged: {
            if (!restoringPosition && !rm.editing) {
                positionCapture.restart()
                readerPage.hideControls()
            }
        }
        onMovementStarted: readerPage.hideControls()
        onMovementEnded: capturePosition()
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: function(eventPoint) {
                // TextArea consumes mouse taps itself; avoid toggling twice.
                var item = readerScroll.itemAt(eventPoint.position.x, eventPoint.position.y + readerScroll.contentY)
                if (item) {
                    var p = item.mapFromItem(readerScroll, eventPoint.position)
                    var child = item.childAt(p.x, p.y)
                    if (child && child.readingParagraph === true) return
                }
                readerPage.toggleControls()
            }
        }
        WheelHandler {
            target: null
            blocking: false
            onWheel: function(event) { readerPage.hideControls(); event.accepted = false }
        }
        header: Item {
            width: readerScroll.width; height: topBar.height + chapterHeading.implicitHeight + 54
            Column {
                id: chapterHeading
                y: topBar.height
                width: textWidth; anchors.horizontalCenter: parent.horizontalCenter
                spacing: 14
                Text { text: rm.totalChapters > 0 ? "CHAPTER " + String(rm.currentIndex + 1).padStart(2, "0") + "  /  " + rm.totalChapters : "READING"; color: readerAccent; font.family: Theme.uiFont; font.pixelSize: 11; font.letterSpacing: 2 }
                Text { width: parent.width; text: rm.chapterTitle || "暂无章节"; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: readerText; font.family: Theme.displayFont; font.pixelSize: 30; font.weight: Font.DemiBold }
                Rectangle { width: 40; height: 2; color: readerAccent; opacity: 0.45 }
            }
        }
        delegate: Column {
            id: paragraphBlock
            required property string modelData
            required property int index
            width: textWidth
            x: (readerScroll.width - width) / 2
            spacing: 12
            readonly property bool paired: rm.readingMode === "bilingual"
            ReadingText {
                id: paragraphText
                width: parent.width
                text: richParagraph(paragraphBlock.modelData)
                textFormat: TextEdit.RichText
                readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                color: paragraphBlock.paired ? readerSecondary : readerText
                selectionColor: withAlpha(readerAccent, 0.25); selectedTextColor: readerText
                font.family: rm.fontFamily; font.pixelSize: paragraphBlock.paired ? rm.fontSize - 1 : rm.fontSize
                background: null
                padding: 0
                glossaryTranslation: paragraphBlock.paired ? rm.bilingualTranslations[paragraphBlock.index] || "" : ""
            }
            Rectangle { visible: paragraphBlock.paired; width: 24; height: 1; color: readerBorder }
            ReadingText {
                id: pairedTranslation
                visible: paragraphBlock.paired
                width: parent.width
                text: paragraphBlock.paired ? richParagraph(rm.bilingualTranslations[paragraphBlock.index] || "") : ""
                textFormat: TextEdit.RichText
                readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                color: readerText; selectionColor: withAlpha(readerAccent, 0.25); selectedTextColor: readerText
                font.family: rm.fontFamily; font.pixelSize: rm.fontSize
                background: null; padding: 0
                glossaryTranslation: rm.bilingualTranslations[paragraphBlock.index] || ""
            }
        }
        footer: Item {
            width: readerScroll.width
            height: readerScroll.count === 0 ? 220 : 72
            Column {
                anchors.centerIn: parent; spacing: 16
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: readerScroll.count === 0 ? (rm.readingMode === "bilingual" ? "本章还没有双语资源" : "本章暂时没有可阅读的正文") : "—  本章完  —"
                    color: readerSecondary; font.family: Theme.uiFont; font.pixelSize: 13
                }
                ReaderButton { anchors.horizontalCenter: parent.horizontalCenter; visible: readerScroll.count === 0 && rm.readingMode === "bilingual"; text: "生成双语资源"; onClicked: controller.generateBilingualResources() }
            }
        }
    }

    Item {
        id: editorPane
        anchors.top: topBar.bottom; anchors.bottom: bottomBar.top
        anchors.left: parent.left; anchors.right: parent.right
        visible: rm.editing
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 24; spacing: 16
            RowLayout {
                Layout.fillWidth: true
                Text { text: rm.chapterTitle; color: readerText; font.family: Theme.displayFont; font.pixelSize: 22; Layout.fillWidth: true; elide: Text.ElideRight }
                Text { text: "Ctrl + S 保存"; color: readerSecondary; font.family: Theme.uiFont; font.pixelSize: 12 }
            }
            Text { text: "只保留原版与最新编辑稿 · 日文原文只读"; color: readerSecondary; font.family: Theme.uiFont; font.pixelSize: 12 }
            ColumnLayout {
                Layout.fillWidth: true
                visible: findVisible
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: findField
                        Keys.onPressed: function(event) { readerPage.handleReaderKey(event) }
                        objectName: "chapterFind"
                        Layout.fillWidth: true
                        placeholderText: "查找本章中文"
                        selectByMouse: true
                        color: readerText
                        background: Rectangle { radius: 8; color: readerSurface; border.color: readerBorder }
                        onTextChanged: matchCursor = -1
                        onAccepted: nextMatch(1)
                    }
                    CheckBox { id: caseCheck; text: "区分大小写"; palette.windowText: readerText; onCheckedChanged: matchCursor = -1 }
                    Text { text: findMatches.length ? (matchCursor < 0 ? "共 " : (matchCursor + 1) + " / ") + findMatches.length + " 处" : "无匹配"; color: readerSecondary; font.pixelSize: 12 }
                    ReaderButton { text: "上一处"; enabled: findMatches.length > 0; onClicked: nextMatch(-1) }
                    ReaderButton { text: "下一处"; enabled: findMatches.length > 0; onClicked: nextMatch(1) }
                    ReaderButton { text: "×"; onClicked: findVisible = false }
                }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: replaceField
                        Keys.onPressed: function(event) { readerPage.handleReaderKey(event) }
                        objectName: "chapterReplace"
                        Layout.fillWidth: true
                        placeholderText: "替换为（留空表示删除）"
                        selectByMouse: true
                        color: readerText
                        background: Rectangle { radius: 8; color: readerSurface; border.color: readerBorder }
                    }
                    ReaderButton { text: "替换此处"; enabled: findMatches.length > 0; onClicked: replaceMatches(false) }
                    ReaderButton { text: "全部替换"; enabled: findMatches.length > 0; onClicked: replaceMatches(true) }
                }
            }
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true
                visible: rm.readingMode === "translation"
                clip: true
                TextArea {
                    id: chapterEditor
                    Keys.onPressed: function(event) { readerPage.handleReaderKey(event) }
                    text: rm.editorText
                    textFormat: TextEdit.PlainText
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    color: readerText; selectionColor: withAlpha(readerAccent, 0.25); selectedTextColor: readerText
                    font.family: rm.fontFamily; font.pixelSize: rm.fontSize
                    padding: 24
                    background: Rectangle { color: readerSurface; radius: 14; border.color: readerBorder }
                    onTextChanged: if (activeFocus && rm.editing) rm.editorText = text
                }
            }
            ListView {
                id: bilingualEditor
                Layout.fillWidth: true; Layout.fillHeight: true
                visible: rm.readingMode === "bilingual"
                clip: true; spacing: 24
                model: rm.editorBlocks
                ScrollBar.vertical: ScrollBar {}
                delegate: Column {
                    required property var modelData
                    required property int index
                    property alias editor: bilingualTextEditor
                    width: ListView.view.width - 16
                    spacing: 10
                    Text { text: "对照段落 " + (index + 1); color: readerAccent; font.family: Theme.uiFont; font.pixelSize: 11 }
                    TextArea { width: parent.width; text: modelData.source; textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; color: readerSecondary; font.family: rm.fontFamily; font.pixelSize: rm.fontSize - 1; background: null; padding: 0 }
                    TextArea {
                        id: bilingualTextEditor
                        Keys.onPressed: function(event) { readerPage.handleReaderKey(event) }
                        width: parent.width
                        text: rm.bilingualDraft(index)
                        textFormat: TextEdit.PlainText; selectByMouse: true; wrapMode: TextEdit.Wrap
                        color: readerText; selectionColor: withAlpha(readerAccent, 0.25)
                        font.family: rm.fontFamily; font.pixelSize: rm.fontSize
                        padding: 16
                        background: Rectangle { color: readerSurface; radius: 12; border.color: readerBorder }
                        onTextChanged: if (activeFocus && rm.editing) rm.setBilingualDraft(index, text)
                    }
                }
            }
        }
    }

    Rectangle {
        id: bottomBar
        z: 3
        anchors.left: parent.left; anchors.right: parent.right
        y: readerPage.bottomControlsVisible ? readerPage.height - height : readerPage.height
        opacity: readerPage.bottomControlsVisible ? 1 : 0
        enabled: readerPage.bottomControlsVisible
        Behavior on y { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 150 } }
        height: 70; color: readerBackground
        Rectangle { width: parent.width; height: 1; color: readerBorder }
        Rectangle { visible: !rm.editing; width: parent.width * chapterProgress; height: 2; color: readerAccent; opacity: 0.65; Behavior on width { NumberAnimation { duration: 100 } } }
        RowLayout {
            width: Math.min(parent.width - 48, 920); height: parent.height
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 12
            ReaderButton { text: "‹  上一章"; enabled: rm.currentIndex > 0; onClicked: changeChapter(-1) }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 4
                Text { Layout.alignment: Qt.AlignHCenter; text: (rm.totalChapters > 0 ? rm.currentIndex + 1 : 0) + " / " + rm.totalChapters; color: readerText; font.family: Theme.uiFont; font.pixelSize: 12 }
                Text { Layout.alignment: Qt.AlignHCenter; text: rm.editing ? "编辑中" : "第 " + (readerScroll.count > 0 ? rm.positionParagraph + 1 : 0) + " / " + readerScroll.count + " 段 · 本章 " + Math.round(chapterProgress * 100) + "%"; color: readerSecondary; font.family: Theme.uiFont; font.pixelSize: 10 }
            }
            ReaderButton { text: "下一章  ›"; enabled: rm.currentIndex + 1 < rm.totalChapters; onClicked: changeChapter(1) }
        }
    }

    Connections {
        target: rm
        function onReaderError(message) { showNotice(message, true) }
        function onEditsSaved() {
            showNotice("已保存，原版已保留", false)
            restorePosition()
        }
        function onDraftChanged() { draftRevision++ }
        function onEditStateChanged() { if (!rm.editing) findVisible = false }
        function onRestorePositionRequested() { restorePosition() }
        function onFontSizeChanged() { restorePosition() }
        function onLineSpacingChanged() { restorePosition() }
        function onFontFamilyChanged() { restorePosition() }
        function onMaxWidthChanged() { restorePosition() }
    }
    onWidthChanged: if (readerPage.StackView.status === StackView.Active) restorePosition()
    Component.onCompleted: { forceActiveFocus(); restorePosition() }
}
