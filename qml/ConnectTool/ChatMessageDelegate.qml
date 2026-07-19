pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Qcm.Material as MD
import ConnectTool

Item {
    id: root

    required property int index
    required property string displayName
    required property string avatar
    required property string message
    required property bool isSelf
    required property bool isPinned
    required property var timestamp

    implicitHeight: messageRow.implicitHeight

    RowLayout {
        id: messageRow

        width: parent.width
        spacing: 8

        Item {
            visible: root.isSelf
            Layout.fillWidth: true
        }

        Avatar {
            visible: !root.isSelf
            source: root.avatar
            name: root.displayName
            Layout.alignment: Qt.AlignTop
        }

        MD.Pane {
            Layout.fillWidth: true
            Layout.maximumWidth: root.width * 0.76
            horizontalPadding: 12
            verticalPadding: 8
            radius: 12
            elevation: 0
            backgroundColor: root.isPinned
                             ? Theme.alpha(Theme.warning, 0.16)
                             : (root.isSelf
                                ? Theme.alpha(Theme.primary, 0.22)
                                : Theme.surfaceContainerHigh)

            contentItem: ColumnLayout {
                spacing: 3

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 7

                    MD.Label {
                        text: root.isSelf ? qsTr("我") : root.displayName
                        color: root.isSelf ? Theme.primary : Theme.foreground
                        typescale: MD.Token.typescale.label_medium
                        prominent: true
                    }

                    StatusPill {
                        visible: root.isPinned
                        text: qsTr("置顶")
                        accent: Theme.warning
                    }

                    Item { Layout.fillWidth: true }

                    MD.Label {
                        text: root.timestamp
                              ? Qt.formatTime(root.timestamp, "HH:mm") : ""
                        color: Theme.foregroundMuted
                        typescale: MD.Token.typescale.label_small
                    }

                    MD.IconButton {
                        id: pinButton

                        visible: App.session.host && !root.isPinned
                        icon.name: MD.Token.icon.push_pin
                        mdState.type: MD.Enum.IBtStandard
                        mdState.size: MD.Enum.XS
                        Accessible.name: qsTr("置顶消息")
                        onClicked: App.chat.pin(root.index)

                        MD.ToolTip {
                            visible: pinButton.hovered
                            text: qsTr("置顶消息")
                        }
                    }
                }

                MD.Label {
                    Layout.fillWidth: true
                    text: root.message
                    color: Theme.foreground
                    typescale: MD.Token.typescale.body_medium
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                }
            }
        }

        Avatar {
            visible: root.isSelf
            source: root.avatar
            name: root.displayName
            Layout.alignment: Qt.AlignTop
        }

        Item {
            visible: !root.isSelf
            Layout.fillWidth: true
        }
    }
}
