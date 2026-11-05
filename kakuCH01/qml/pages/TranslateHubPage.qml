import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KakuCH

// 翻译总览页：上方胶囊切换栏在 Sakura / Murasaki / Qwen 三套翻译流程间切换。
Page {
    id: hub
    objectName: "translate"
    background: Rectangle { color: Theme.pageBackground }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 上方栏位：与其它 UI 统一的横向胶囊式切换。
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 64

            CapsuleTabBar {
                id: translateTabs
                objectName: "translateTabs"
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 24
                items: ["Qwen翻译", "Murasaki翻译", "Sakura翻译"]
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: translateTabs.currentIndex

            Loader {
                id: qwenLoader
                objectName: "qwenLoader"
                asynchronous: true
                active: translateTabs.currentIndex === 0 || item !== null
                sourceComponent: Component { QwenTranslatePage {} }
            }
            Loader {
                id: murasakiLoader
                objectName: "murasakiLoader"
                asynchronous: true
                active: translateTabs.currentIndex === 1 || item !== null
                sourceComponent: Component { MurasakiTranslatePage {} }
            }
            Loader {
                id: sakuraLoader
                objectName: "sakuraLoader"
                asynchronous: true
                active: translateTabs.currentIndex === 2 || item !== null
                sourceComponent: Component { TranslatePage {} }
            }
        }
    }
    BusyIndicator {
        anchors.centerIn: parent
        running: [qwenLoader, murasakiLoader, sakuraLoader][translateTabs.currentIndex].status === Loader.Loading
        visible: running
    }
}
