pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Qcm.Material as MD
import ConnectTool

AppCard {
    id: root

    readonly property bool sessionActive: App.session.host || App.session.connected
    readonly property bool showDetails: App.session.published && !App.session.connected
                                        || App.session.mode === 0

    padding: 14
    backgroundColor: Theme.surfaceContainerLow

    function toggleConnection() {
        if (root.sessionActive)
            App.session.disconnect()
        else
            App.session.join()
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 10

        MD.TextField {
            id: joinField

            Layout.fillWidth: true
            Layout.minimumWidth: 280
            placeholderText: qsTr("房间 ID 或房主 SteamID64；留空则创建房间")
            leadingIcon: MD.Token.icon.meeting_room
            text: App.session.joinTarget
            enabled: !root.sessionActive
            selectByMouse: true
            type: MD.Enum.TextFieldFilled
            mdState.dense: true
            onTextEdited: App.session.joinTarget = text
            onAccepted: {
                if (connectionButton.enabled)
                    root.toggleConnection()
            }
        }

        MD.ComboBox {
            Layout.preferredWidth: 142
            model: [qsTr("TCP 转发"), qsTr("TUN 组网")]
            currentIndex: App.session.mode
            enabled: !root.sessionActive
            onActivated: App.session.mode = currentIndex
        }

        MD.Button {
            id: connectionButton

            Layout.preferredWidth: 124
            text: root.sessionActive
                  ? qsTr("断开连接")
                  : (joinField.text.trim().length > 0
                     ? qsTr("加入房间") : qsTr("创建房间"))
            icon.name: root.sessionActive
                       ? MD.Token.icon.link_off : MD.Token.icon.link
            mdState.type: root.sessionActive
                          ? MD.Enum.BtFilledTonal : MD.Enum.BtFilled
            enabled: root.sessionActive || App.session.steamReady
            onClicked: root.toggleConnection()
        }

        RowLayout {
            spacing: 7

            MD.Switch {
                id: publishedSwitch

                checked: App.session.published
                enabled: !App.session.connected || App.session.host
                icon.name: checked
                           ? MD.Token.icon["public"] : MD.Token.icon.public_off
                onToggled: App.session.published = checked
            }

            ColumnLayout {
                spacing: 0

                MD.Label {
                    text: qsTr("公开大厅")
                    color: Theme.foreground
                    typescale: MD.Token.typescale.label_medium
                    prominent: true
                }
                MD.Label {
                    text: publishedSwitch.checked ? qsTr("可被发现") : qsTr("仅受邀可见")
                    color: Theme.foregroundMuted
                    typescale: MD.Token.typescale.label_small
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: root.showDetails
        spacing: 10

        MD.TextField {
            Layout.fillWidth: true
            visible: App.session.published && !App.session.connected
            placeholderText: qsTr("公开房间名称")
            leadingIcon: MD.Token.icon.edit
            text: App.session.roomName
            enabled: !App.session.connected
            selectByMouse: true
            type: MD.Enum.TextFieldFilled
            mdState.dense: true
            onTextEdited: App.session.roomName = text
        }

        PortField {
            Layout.preferredWidth: 188
            visible: App.session.mode === 0
            placeholderText: qsTr("主机目标端口")
            value: App.session.localPort
            minimum: 0
            enabled: !root.sessionActive
            onValueEdited: value => App.session.localPort = value
        }

        PortField {
            Layout.preferredWidth: 188
            visible: App.session.mode === 0
            placeholderText: qsTr("本地监听端口")
            value: App.session.bindPort
            minimum: 1
            enabled: !root.sessionActive
            onValueEdited: value => App.session.bindPort = value
        }

        Item { Layout.fillWidth: true }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 9

        Repeater {
            model: [
                {
                    label: qsTr("房间名称"),
                    value: App.session.lobbyName,
                    icon: MD.Token.icon.meeting_room,
                    accent: Theme.secondary
                },
                {
                    label: qsTr("房间 ID"),
                    value: App.session.lobbyId,
                    icon: MD.Token.icon.tag,
                    accent: Theme.primary
                },
                App.session.mode === 1
                    ? {
                        label: qsTr("TUN 信息"),
                        value: App.session.tunIp.length > 0
                               ? App.session.tunIp
                               : qsTr("等待分配"),
                        icon: MD.Token.icon.hub,
                        accent: Theme.success
                    }
                    : {
                        label: qsTr("本地入口"),
                        value: qsTr("localhost:%1").arg(App.session.bindPort),
                        icon: MD.Token.icon.input,
                        accent: Theme.success
                    }
            ]

            delegate: MD.Pane {
                id: infoCard

                required property var modelData

                Layout.fillWidth: true
                implicitHeight: 58
                horizontalPadding: 10
                verticalPadding: 7
                radius: 10
                elevation: 0
                backgroundColor: infoHover.hovered
                                 ? Theme.alpha(infoCard.modelData.accent, 0.13)
                                 : Theme.surfaceContainerHigh
                opacity: infoCard.modelData.value.length > 0 ? 1 : 0.60

                contentItem: RowLayout {
                    spacing: 8

                    MD.Icon {
                        name: infoCard.modelData.icon
                        size: 18
                        color: infoCard.modelData.accent
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        MD.Label {
                            text: infoCard.modelData.label
                            color: infoCard.modelData.accent
                            typescale: MD.Token.typescale.label_small
                            prominent: true
                        }

                        MD.Label {
                            Layout.fillWidth: true
                            text: infoCard.modelData.value.length > 0
                                  ? infoCard.modelData.value : qsTr("未加入")
                            color: Theme.foreground
                            typescale: MD.Token.typescale.body_small
                            elide: Text.ElideMiddle
                        }
                    }

                    MD.Icon {
                        visible: infoCard.modelData.value.length > 0
                        name: MD.Token.icon.content_copy
                        size: 16
                        color: Theme.foregroundMuted
                    }
                }

                HoverHandler { id: infoHover }

                TapHandler {
                    enabled: infoCard.modelData.value.length > 0
                    onTapped: App.copy(infoCard.modelData.value)
                }

                MD.ToolTip {
                    visible: infoHover.hovered && infoCard.modelData.value.length > 0
                    text: qsTr("点击复制")
                }
            }
        }
    }
}
