pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ConnectTool

Item {
    id: root

    onVisibleChanged: if (visible) App.lobbies.refresh()

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacing

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Label { text: qsTr("公开大厅"); color: Theme.text; font.pixelSize: 24; font.weight: Font.DemiBold }
                Label { text: qsTr("发现公开房间，按成员、名称或延迟排序。"); color: Theme.textMuted; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            TextField {
                Layout.preferredWidth: 280
                placeholderText: qsTr("搜索房间或房主…")
                text: App.lobbies.filter
                onTextEdited: App.lobbies.filter = text
            }
            ComboBox {
                model: [qsTr("成员最多"), qsTr("名称"), qsTr("延迟最低")]
                currentIndex: App.lobbies.sortMode
                onActivated: App.lobbies.sortMode = currentIndex
            }
            Button {
                text: App.lobbies.refreshing ? qsTr("刷新中…") : qsTr("刷新")
                enabled: !App.lobbies.refreshing && App.session.steamReady
                onClicked: App.lobbies.refresh()
            }
        }

        AppCard {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: lobbyList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 9
                model: App.lobbies.model
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    id: lobbyDelegate
                    required property string lobbyId
                    required property string name
                    required property string hostName
                    required property string hostId
                    required property int members
                    required property int ping

                    readonly property bool current: App.session.lobbyId === lobbyId
                    width: lobbyList.width
                    implicitHeight: 76
                    radius: 12
                    color: current ? "#12332f" : Theme.surfaceRaised
                    border.color: current ? Theme.primary : Theme.border

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 14

                        Rectangle {
                            implicitWidth: 46
                            implicitHeight: 46
                            radius: 12
                            color: "#173148"
                            Label { anchors.centerIn: parent; text: "⌂"; color: Theme.secondary; font.pixelSize: 20 }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            RowLayout {
                                Label { text: lobbyDelegate.name.length > 0 ? lobbyDelegate.name : qsTr("未命名房间"); color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                                StatusPill { visible: lobbyDelegate.current; text: qsTr("当前房间"); accent: Theme.primary }
                            }
                            Label {
                                text: qsTr("房主 %1 · %2")
                                      .arg(lobbyDelegate.hostName.length > 0
                                           ? lobbyDelegate.hostName : lobbyDelegate.hostId)
                                      .arg(lobbyDelegate.lobbyId)
                                color: Theme.textMuted
                                font.pixelSize: 11
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                        }
                        StatusPill { text: qsTr("%1 人").arg(lobbyDelegate.members); accent: Theme.secondary }
                        Label {
                            Layout.preferredWidth: 72
                            horizontalAlignment: Text.AlignRight
                            text: lobbyDelegate.ping >= 0 ? lobbyDelegate.ping + " ms" : "–"
                            color: lobbyDelegate.ping >= 0 && lobbyDelegate.ping <= 100
                                   ? Theme.success
                                   : (lobbyDelegate.ping <= 200 ? Theme.warning : Theme.danger)
                        }
                        ToolButton { text: "⧉"; onClicked: App.copy(lobbyDelegate.lobbyId); ToolTip.visible: hovered; ToolTip.text: qsTr("复制房间 ID") }
                        Button {
                            text: lobbyDelegate.current ? qsTr("已加入") : qsTr("加入")
                            enabled: !lobbyDelegate.current && App.session.steamReady
                            onClicked: App.lobbies.join(lobbyDelegate.lobbyId)
                        }
                    }
                }

                Column {
                    anchors.centerIn: parent
                    visible: lobbyList.count === 0
                    spacing: 6
                    Label { text: qsTr("没有找到公开房间"); color: Theme.text; font.pixelSize: 16 }
                    Label { text: qsTr("尝试刷新，或清空搜索条件。"); color: Theme.textMuted; font.pixelSize: 12 }
                }
            }
        }
    }
}
