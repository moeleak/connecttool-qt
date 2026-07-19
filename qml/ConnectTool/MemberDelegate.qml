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

    implicitHeight: 78
    leftPadding: 10
    rightPadding: 8
    topPadding: 7
    bottomPadding: 7
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
        spacing: 9

        Avatar {
            source: root.avatar
            name: root.displayName
            online: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                spacing: 5

                MD.Label {
                    Layout.fillWidth: true
                    text: root.displayName
                    color: Theme.foreground
                    typescale: MD.Token.typescale.title_small
                    prominent: true
                    elide: Text.ElideRight
                }

                StatusPill {
                    text: root.isSelf ? qsTr("自己")
                          : (root.isFriend ? qsTr("好友") : qsTr("成员"))
                    accent: root.isSelf ? Theme.secondary : Theme.primary
                }
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

        ColumnLayout {
            Layout.preferredWidth: 54
            spacing: 0

            MD.Label {
                Layout.alignment: Qt.AlignRight
                text: root.ping >= 0 ? qsTr("%1 ms").arg(root.ping) : "–"
                color: root.ping < 0 ? Theme.foregroundMuted
                      : (root.ping <= 100 ? Theme.success
                         : (root.ping <= 200 ? Theme.warning : Theme.danger))
                typescale: MD.Token.typescale.label_medium
                prominent: true
            }

            MD.Label {
                Layout.alignment: Qt.AlignRight
                text: root.relay.length > 0 ? root.relay : qsTr("直连")
                color: Theme.foregroundMuted
                typescale: MD.Token.typescale.label_small
                elide: Text.ElideRight
            }
        }

        ColumnLayout {
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
