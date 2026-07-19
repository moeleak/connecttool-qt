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
    required property bool online
    required property string status
    required property int inviteCooldown

    implicitHeight: 68
    leftPadding: 10
    rightPadding: 9
    topPadding: 7
    bottomPadding: 7
    hoverEnabled: true

    background: Rectangle {
        radius: 11
        color: root.hovered ? Theme.surfaceContainerHigh : Theme.surfaceContainer
        border.width: 1
        border.color: Theme.alpha(root.online ? Theme.primary : Theme.outline,
                                  root.hovered ? 0.66 : 0.36)

        Behavior on color { ColorAnimation { duration: 120 } }
    }

    contentItem: RowLayout {
        spacing: 9

        Avatar {
            source: root.avatar
            name: root.displayName
            online: root.online
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
                elide: Text.ElideRight
            }

            MD.Label {
                Layout.fillWidth: true
                text: root.status.length > 0
                      ? root.status : (root.online ? qsTr("在线") : qsTr("离线"))
                color: root.online ? Theme.success : Theme.foregroundMuted
                typescale: MD.Token.typescale.label_small
                elide: Text.ElideRight
            }

            MD.Label {
                Layout.fillWidth: true
                text: root.steamId
                color: Theme.foregroundMuted
                typescale: MD.Token.typescale.label_small
                elide: Text.ElideMiddle
            }
        }

        MD.Button {
            text: root.inviteCooldown > 0
                  ? qsTr("%1s").arg(root.inviteCooldown) : qsTr("邀请")
            icon.name: root.inviteCooldown > 0
                       ? MD.Token.icon.schedule : MD.Token.icon.send
            mdState.type: MD.Enum.BtFilledTonal
            mdState.size: MD.Enum.XS
            enabled: App.session.lobbyId.length > 0
                     && root.inviteCooldown === 0
            onClicked: App.social.invite(root.steamId)
        }
    }
}
