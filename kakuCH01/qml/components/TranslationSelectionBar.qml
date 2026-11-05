import QtQuick
import QtQuick.Layouts
import KakuCH

RowLayout {
    id: selection
    property bool busy: false
    property bool glossaryMode: false
    visible: controller.translationModel.fileCount > 0
    enabled: !busy
    spacing: Theme.spacing
    SecondaryButton { text: "全选"; onClicked: controller.setTranslationSelection(true, selection.glossaryMode) }
    SecondaryButton { text: "取消全选"; onClicked: controller.setTranslationSelection(false, selection.glossaryMode) }
    SecondaryButton { text: "反选"; onClicked: controller.invertTranslationSelection(selection.glossaryMode) }
    SecondaryButton { text: "移除"; onClicked: controller.removeSelectedFiles(selection.glossaryMode) }
    SecondaryButton { text: "清空"; onClicked: controller.clearFiles() }
    Item { Layout.fillWidth: true }
}
