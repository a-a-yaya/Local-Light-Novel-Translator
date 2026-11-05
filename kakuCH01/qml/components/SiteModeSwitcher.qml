import QtQuick
import QtQuick.Layouts
import KakuCH

Item {
    id: root

    property string leftText: "排行"
    property string rightText: "检索"
    property string rightHint: "作品 · 作者"
    property int currentIndex: 0
    property string searchKind: "works"
    property real motionSpeed: 2.0
    readonly property bool searchExpanded: currentIndex === 1
    readonly property real inset: 3

    signal activated(int index)
    signal searchKindSelected(string kind)

    implicitHeight: 52
    focus: true
    activeFocusOnTab: true
    clip: true

    function selectMode(index) {
        const value = index === 1 ? 1 : 0
        if (currentIndex !== value) {
            currentIndex = value
            activated(value)
        }
        forceActiveFocus()
    }

    function selectSearchKind(kind) {
        const value = kind === "authors" ? "authors" : "works"
        if (searchKind !== value) {
            searchKind = value
            searchKindSelected(value)
        }
        forceActiveFocus()
    }

    Keys.onLeftPressed: {
        if (searchExpanded && searchKind === "authors")
            selectSearchKind("works")
        else
            selectMode(0)
    }
    Keys.onRightPressed: {
        if (!searchExpanded)
            selectMode(1)
        else
            selectSearchKind("authors")
    }
    Keys.onUpPressed: if (searchExpanded) selectSearchKind("works")
    Keys.onDownPressed: if (searchExpanded) selectSearchKind("authors")

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: Theme.dark ? Qt.rgba(1, 1, 1, 0.06)
                          : Qt.rgba(1, 1, 1, 0.62)
        border.width: 1
        border.color: Theme.glassBorder
        clip: true

        Rectangle {
            id: modeIndicator
            x: root.inset + (root.searchExpanded ? width : 0)
            y: root.inset
            width: (parent.width - root.inset * 2) / 2
            height: parent.height - root.inset * 2
            radius: height / 2
            color: Theme.accent
            Behavior on x {
                NumberAnimation {
                    duration: Math.round(420 / root.motionSpeed)
                    easing.type: Easing.BezierSpline
                    easing.bezierCurve: [0.34, 0.96, 0.6, 0.99]
                }
            }
        }

        Item {
            x: root.inset
            y: root.inset
            width: (parent.width - root.inset * 2) / 2
            height: parent.height - root.inset * 2
            Text {
                anchors.centerIn: parent
                text: root.leftText
                color: root.currentIndex === 0 ? "white" : Theme.textSecondary
                font.family: Theme.uiFont
                font.pixelSize: 14
                font.bold: true
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                Accessible.role: Accessible.RadioButton
                Accessible.name: root.leftText
                Accessible.checkable: true
                Accessible.checked: root.currentIndex === 0
                onClicked: root.selectMode(0)
            }
        }

        Item {
            id: rightSegment
            x: root.inset + (parent.width - root.inset * 2) / 2
            y: root.inset
            width: (parent.width - root.inset * 2) / 2
            height: parent.height - root.inset * 2

            Text {
                id: rightLabel
                z: root.searchExpanded ? 1 : 2
                anchors.horizontalCenter: parent.horizontalCenter
                y: Math.max(4, (parent.height - height) / 2 - 6)
                text: root.rightText
                color: root.currentIndex === 1 ? "white" : Theme.textSecondary
                font.family: Theme.uiFont
                font.pixelSize: 14
                font.bold: true
                opacity: root.searchExpanded ? 0 : 1
                scale: root.searchExpanded ? 0.72 : 1
                Behavior on opacity {
                    NumberAnimation { duration: Math.round(300 / root.motionSpeed) }
                }
                Behavior on scale {
                    NumberAnimation {
                        duration: Math.round(300 / root.motionSpeed)
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: [0.34, 0.96, 0.6, 0.99]
                    }
                }
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height - height - 5
                z: root.searchExpanded ? 0 : 1
                text: root.rightHint
                color: Theme.textSecondary
                opacity: root.searchExpanded ? 0 : 0.76
                font.family: Theme.uiFont
                font.pixelSize: 8
                Behavior on opacity {
                    NumberAnimation { duration: Math.round(190 / root.motionSpeed) }
                }
            }

            MouseArea {
                anchors.fill: parent
                enabled: !root.searchExpanded
                cursorShape: Qt.PointingHandCursor
                Accessible.role: Accessible.RadioButton
                Accessible.name: root.rightText
                Accessible.description: root.rightHint
                Accessible.checkable: true
                Accessible.checked: root.searchExpanded
                onClicked: root.selectMode(1)
            }

            Item {
                id: searchMenu
                z: root.searchExpanded ? 2 : 1
                x: (parent.width - width) / 2
                y: root.searchExpanded ? (parent.height - height) / 2
                                       : (parent.height - height) / 2 + 4
                width: parent.width - 6
                height: parent.height - 4
                opacity: root.searchExpanded ? 1 : 0
                scale: root.searchExpanded ? 1 : 0.62
                Behavior on opacity {
                    NumberAnimation { duration: Math.round(340 / root.motionSpeed) }
                }
                Behavior on scale {
                    NumberAnimation {
                        duration: Math.round(460 / root.motionSpeed)
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: [0.34, 0.96, 0.6, 0.99]
                    }
                }
                Behavior on y {
                    NumberAnimation {
                        duration: Math.round(460 / root.motionSpeed)
                        easing.type: Easing.BezierSpline
                        easing.bezierCurve: [0.34, 0.96, 0.6, 0.99]
                    }
                }

                Rectangle {
                    id: searchKindIndicator
                    y: parent.height * 0.1
                    x: root.searchKind === "works" ? parent.width * 0.02
                                                   : parent.width * 0.5
                    width: parent.width * 0.48
                    height: parent.height * 0.8
                    radius: height / 2
                    color: Qt.rgba(1, 1, 1, 0.18)
                    Behavior on x {
                        NumberAnimation {
                            duration: Math.round(460 / root.motionSpeed)
                            easing.type: Easing.BezierSpline
                            easing.bezierCurve: [0.34, 0.96, 0.6, 0.99]
                        }
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    spacing: 0
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Text {
                            anchors.centerIn: parent
                            text: "作品"
                            color: root.searchKind === "works" ? "white"
                                                               : Qt.rgba(1, 1, 1, 0.56)
                            font.family: Theme.uiFont
                            font.pixelSize: 11
                            font.bold: true
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: root.searchExpanded
                            cursorShape: Qt.PointingHandCursor
                            Accessible.role: Accessible.RadioButton
                            Accessible.name: "作品"
                            Accessible.checkable: true
                            Accessible.checked: root.searchKind === "works"
                            onClicked: root.selectSearchKind("works")
                        }
                    }
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Text {
                            anchors.centerIn: parent
                            text: "作者"
                            color: root.searchKind === "authors" ? "white"
                                                                : Qt.rgba(1, 1, 1, 0.56)
                            font.family: Theme.uiFont
                            font.pixelSize: 11
                            font.bold: true
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: root.searchExpanded
                            cursorShape: Qt.PointingHandCursor
                            Accessible.role: Accessible.RadioButton
                            Accessible.name: "作者"
                            Accessible.checkable: true
                            Accessible.checked: root.searchKind === "authors"
                            onClicked: root.selectSearchKind("authors")
                        }
                    }
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: height / 2
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
            opacity: root.activeFocus ? 0.55 : 0
            Behavior on opacity { NumberAnimation { duration: 90 } }
        }
    }
}
