pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Qcm.Material as MD
import ConnectTool

AppCard {
    id: root

    title: qsTr("房间聊天")
    subtitle: App.session.lobbyId.length > 0
              ? qsTr("消息通过当前 Steam P2P 房间传输")
              : qsTr("建立连接后即可发送消息")
    padding: 14
    backgroundColor: Theme.surfaceContainerLow

    MD.Pane {
        Layout.fillWidth: true
        visible: App.chat.model.hasPinned
        horizontalPadding: 10
        verticalPadding: 7
        radius: 10
        elevation: 0
        backgroundColor: Theme.alpha(Theme.warning, 0.13)

        contentItem: RowLayout {
            spacing: 9

            MD.Icon {
                name: MD.Token.icon.keep
                size: 18
                color: Theme.warning
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                MD.Label {
                    text: qsTr("%1 置顶").arg(App.chat.model.pinnedMessage.displayName || "")
                    color: Theme.warning
                    typescale: MD.Token.typescale.label_small
                    prominent: true
                }

                MD.Label {
                    Layout.fillWidth: true
                    text: App.chat.model.pinnedMessage.message || ""
                    color: Theme.foreground
                    typescale: MD.Token.typescale.body_small
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                }
            }

            MD.IconButton {
                id: clearPinButton

                visible: App.session.host
                icon.name: MD.Token.icon.close
                mdState.type: MD.Enum.IBtStandard
                mdState.size: MD.Enum.XS
                Accessible.name: qsTr("取消置顶")
                onClicked: App.chat.clearPin()

                MD.ToolTip {
                    visible: clearPinButton.hovered
                    text: qsTr("取消置顶")
                }
            }
        }
    }

    ListView {
        id: chatList

        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: 8
        model: App.chat.model
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: MD.ScrollBar {}
        onCountChanged: Qt.callLater(positionViewAtEnd)

        delegate: ChatMessageDelegate {
            width: chatList.width
        }

        ColumnLayout {
            anchors.centerIn: parent
            visible: chatList.count === 0
            spacing: 5

            MD.Icon {
                Layout.alignment: Qt.AlignHCenter
                name: MD.Token.icon.forum
                size: 34
                color: Theme.primary
            }

            MD.Label {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("还没有消息")
                color: Theme.foreground
                typescale: MD.Token.typescale.title_small
                prominent: true
            }

            MD.Label {
                text: qsTr("加入房间后，从这里开始交流")
                color: Theme.foregroundMuted
                typescale: MD.Token.typescale.body_small
            }
        }
    }

    MD.Divider {
        Layout.fillWidth: true
        color: Theme.alpha(Theme.outline, 0.45)
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        MD.TextField {
            id: chatInput

            Layout.fillWidth: true
            placeholderText: qsTr("输入要发送的内容…")
            leadingIcon: MD.Token.icon.chat
            enabled: App.session.lobbyId.length > 0
            type: MD.Enum.TextFieldOutlined
            mdState.dense: true
            onAccepted: sendMessage()

            function sendMessage() {
                const trimmed = text.trim()
                if (trimmed.length === 0)
                    return
                App.chat.send(text)
                clear()
            }
        }

        MD.Button {
            text: qsTr("发送")
            icon.name: MD.Token.icon.send
            mdState.type: MD.Enum.BtFilled
            enabled: chatInput.enabled && chatInput.text.trim().length > 0
            onClicked: chatInput.sendMessage()
        }

        RowLayout {
            spacing: 5

            MD.Switch {
                id: reminderSwitch

                checked: App.chat.reminderEnabled
                icon.name: checked
                           ? MD.Token.icon.notifications_active
                           : MD.Token.icon.notifications_off
                onToggled: App.chat.reminderEnabled = checked
            }

            MD.Label {
                text: qsTr("提醒")
                color: Theme.foreground
                typescale: MD.Token.typescale.label_medium
                prominent: true
            }
        }
    }
}
