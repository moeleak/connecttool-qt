pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Qcm.Material as MD
import ConnectTool

MD.ItemDelegate {
    id: root

    required property string displayName
    required property string steamId
    required property string avatar
    required property string ip
    required property int ping
    required property string relay
    required property bool isFriend
    required property bool isSelf

    readonly property color pingColor: root.ping < 0 ? Theme.foregroundMuted
                                       : (root.ping <= 100 ? Theme.success
                                          : (root.ping <= 200 ? Theme.warning : Theme.danger))
    readonly property string connectionLabel: {
        if (root.relay.length > 0 && root.relay !== "-")
            return root.relay
        return root.ping >= 0 ? qsTr("直连") : qsTr("连接中")
    }

    implicitHeight: 82
    leftPadding: 10
    rightPadding: 7
    topPadding: 8
    bottomPadding: 8
    hoverEnabled: true

    background: Rectangle {
        radius: 11
        color: root.hovered ? Theme.surfaceContainerHigh : Theme.surfaceContainer
        border.width: 1
        border.color: Theme.alpha(root.isSelf ? Theme.primary : Theme.outline,
                                  root.hovered || root.isSelf ? 0.72 : 0.38)

        Behavior on color { ColorAnimation { duration: 120 } }
    }

    contentItem: RowLayout {
        spacing: 10

        Avatar {
            Layout.preferredWidth: 44
            Layout.preferredHeight: 44
            Layout.alignment: Qt.AlignVCenter
            source: root.avatar
            name: root.displayName
            online: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            MD.Label {
                Layout.fillWidth: true
                text: root.displayName
                color: Theme.foreground
                typescale: MD.Token.typescale.title_small
                prominent: true
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
            }

            MD.Label {
                Layout.fillWidth: true
                text: qsTr("SteamID: %1").arg(root.steamId)
                color: Theme.foregroundMuted
                typescale: MD.Token.typescale.label_small
                elide: Text.ElideMiddle
            }

            MD.Label {
                Layout.fillWidth: true
                text: root.ip.length > 0
                      ? qsTr("IP: %1").arg(root.ip) : qsTr("IP: 等待分配")
                color: root.ip.length > 0 ? Theme.secondary : Theme.foregroundMuted
                typescale: MD.Token.typescale.label_small
                elide: Text.ElideMiddle
            }
        }

        Item {
            id: identitySlot

            Layout.minimumWidth: 64
            Layout.preferredWidth: 64
            Layout.maximumWidth: 64
            Layout.preferredHeight: identityBadge.implicitHeight
            Layout.alignment: Qt.AlignVCenter

            StatusPill {
                id: identityBadge

                anchors.centerIn: identitySlot
                visible: root.isSelf || root.isFriend
                text: root.isSelf ? qsTr("自己") : qsTr("好友")
                accent: root.isSelf ? Theme.secondary : Theme.primary
            }
        }

        ColumnLayout {
            Layout.minimumWidth: 70
            Layout.preferredWidth: 70
            Layout.maximumWidth: 70
            Layout.alignment: Qt.AlignVCenter
            spacing: 1

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 4

                MD.Icon {
                    name: MD.Token.icon.network_ping
                    size: 15
                    color: root.pingColor
                }

                MD.Label {
                    text: root.ping >= 0 ? qsTr("%1 ms").arg(root.ping) : "—"
                    color: root.pingColor
                    typescale: MD.Token.typescale.label_medium
                    prominent: true
                }
            }

            MD.Label {
                Layout.alignment: Qt.AlignRight
                Layout.maximumWidth: 70
                text: root.connectionLabel
                color: Theme.foregroundMuted
                typescale: MD.Token.typescale.label_small
                elide: Text.ElideRight
            }
        }

        ColumnLayout {
            Layout.minimumWidth: 36
            Layout.preferredWidth: 36
            Layout.maximumWidth: 36
            Layout.alignment: Qt.AlignVCenter
            spacing: 0

            MD.IconButton {
                id: addFriendButton

                visible: !root.isFriend && !root.isSelf
                icon.name: MD.Token.icon.person_add
                mdState.type: MD.Enum.IBtStandard
                mdState.size: MD.Enum.XS
                Accessible.name: qsTr("添加好友")
                onClicked: App.social.addFriend(root.steamId)

                MD.ToolTip {
                    visible: addFriendButton.hovered
                    text: qsTr("添加 Steam 好友")
                }
            }

            MD.IconButton {
                id: copyIpButton

                visible: root.ip.length > 0
                icon.name: MD.Token.icon.content_copy
                mdState.type: MD.Enum.IBtStandard
                mdState.size: MD.Enum.XS
                Accessible.name: qsTr("复制 IP")
                onClicked: App.copy(root.ip)

                MD.ToolTip {
                    visible: copyIpButton.hovered
                    text: qsTr("复制 IP")
                }
            }
        }
    }
}
