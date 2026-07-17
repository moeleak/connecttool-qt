pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ConnectTool

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacing

        RowLayout {
            Layout.fillWidth: true

            ColumnLayout {
                spacing: 2
                Label {
                    text: qsTr("房间")
                    color: Theme.text
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                }
                Label {
                    text: App.session.status
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
            }

            Item { Layout.fillWidth: true }

            StatusPill {
                text: App.session.steamReady ? qsTr("Steam 已连接") : qsTr("等待 Steam")
                accent: App.session.steamReady ? Theme.success : Theme.warning
            }
            StatusPill {
                text: App.session.mode === 1 ? qsTr("TUN 组网") : qsTr("TCP 转发")
                accent: Theme.secondary
            }
        }

        AppCard {
            Layout.fillWidth: true
            title: qsTr("连接控制")
            subtitle: qsTr("留空目标即可主持；也可输入房间 ID 或 SteamID64 加入。")

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                TextField {
                    Layout.fillWidth: true
                    placeholderText: qsTr("房间 ID / SteamID64")
                    text: App.session.joinTarget
                    enabled: !(App.session.host || App.session.connected)
                    selectByMouse: true
                    onTextEdited: App.session.joinTarget = text
                }

                ComboBox {
                    Layout.preferredWidth: 140
                    model: [qsTr("TCP 模式"), qsTr("TUN 模式")]
                    currentIndex: App.session.mode
                    enabled: !(App.session.host || App.session.connected)
                    onActivated: App.session.mode = currentIndex
                }

                Button {
                    highlighted: !(App.session.host || App.session.connected)
                    text: App.session.host || App.session.connected
                          ? qsTr("断开")
                          : (App.session.joinTarget.trim().length > 0
                             ? qsTr("加入") : qsTr("主持"))
                    enabled: App.session.steamReady
                    onClicked: {
                        if (App.session.host || App.session.connected)
                            App.session.disconnect()
                        else
                            App.session.join()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                TextField {
                    Layout.fillWidth: true
                    placeholderText: qsTr("公开房间名称")
                    text: App.session.roomName
                    enabled: !App.session.connected
                    onTextEdited: App.session.roomName = text
                }

                Switch {
                    text: qsTr("公开到大厅")
                    checked: App.session.published
                    enabled: !App.session.connected || App.session.host
                    onToggled: App.session.published = checked
                }

                Label {
                    visible: App.session.mode === 0
                    text: qsTr("转发")
                    color: Theme.textMuted
                }
                SpinBox {
                    visible: App.session.mode === 0
                    from: 0
                    to: 65535
                    value: App.session.localPort
                    editable: true
                    enabled: !(App.session.host || App.session.connected)
                    onValueModified: App.session.localPort = value
                }
                Label {
                    visible: App.session.mode === 0
                    text: qsTr("绑定")
                    color: Theme.textMuted
                }
                SpinBox {
                    visible: App.session.mode === 0
                    from: 1
                    to: 65535
                    value: App.session.bindPort
                    editable: true
                    enabled: !(App.session.host || App.session.connected)
                    onValueModified: App.session.bindPort = value
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Repeater {
                    model: [
                        { label: qsTr("房间 ID"), value: App.session.lobbyId },
                        { label: qsTr("房间名称"), value: App.session.lobbyName },
                        App.session.mode === 1
                            ? { label: qsTr("虚拟网络"), value: App.session.tunIp.length > 0
                                  ? App.session.tunIp + (App.session.tunDevice.length > 0 ? " · " + App.session.tunDevice : "")
                                  : qsTr("等待分配") }
                            : { label: qsTr("本地入口"), value: "localhost:" + App.session.bindPort }
                    ]

                    delegate: Rectangle {
                        id: connectionInfo
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 56
                        radius: 10
                        color: Theme.surfaceRaised
                        border.color: Theme.border

                        Column {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: 10
                            spacing: 3
                            Label { text: connectionInfo.modelData.label; color: Theme.textMuted; font.pixelSize: 11 }
                            Label {
                                width: parent.width
                                text: connectionInfo.modelData.value.length > 0
                                      ? connectionInfo.modelData.value : qsTr("未加入")
                                color: Theme.text
                                elide: Text.ElideMiddle
                            }
                        }

                        TapHandler {
                            enabled: connectionInfo.modelData.value.length > 0
                            onTapped: App.copy(connectionInfo.modelData.value)
                        }
                        ToolTip.visible: hover.hovered && connectionInfo.modelData.value.length > 0
                        ToolTip.text: qsTr("点击复制")
                        HoverHandler { id: hover }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacing

            AppCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 620
                title: qsTr("房间聊天")
                subtitle: qsTr("消息仅在当前房间可见；房主可以置顶重要说明。")

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: pinnedColumn.implicitHeight + 16
                    visible: App.chat.model.hasPinned
                    radius: 10
                    color: "#2a2517"
                    border.color: Theme.warning

                    ColumnLayout {
                        id: pinnedColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 10
                        spacing: 3
                        Label {
                            text: qsTr("已置顶 · %1").arg(App.chat.model.pinnedMessage.displayName || "")
                            color: Theme.warning
                            font.pixelSize: 11
                        }
                        Label {
                            Layout.fillWidth: true
                            text: App.chat.model.pinnedMessage.message || ""
                            color: Theme.text
                            wrapMode: Text.Wrap
                        }
                    }

                    ToolButton {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        visible: App.session.host
                        text: "×"
                        onClicked: App.chat.clearPin()
                    }
                }

                ListView {
                    id: chatList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 8
                    model: App.chat.model
                    ScrollBar.vertical: ScrollBar {}
                    onCountChanged: positionViewAtEnd()

                    delegate: Rectangle {
                        id: messageDelegate
                        required property int index
                        required property string displayName
                        required property string avatar
                        required property string message
                        required property bool isSelf
                        required property bool isPinned
                        required property var timestamp

                        width: chatList.width
                        implicitHeight: messageRow.implicitHeight + 18
                        radius: 11
                        color: isSelf ? "#12332f" : Theme.surfaceRaised
                        border.color: isPinned ? Theme.warning : Theme.border

                        RowLayout {
                            id: messageRow
                            anchors.fill: parent
                            anchors.margins: 9
                            spacing: 9

                            Avatar {
                                source: messageDelegate.avatar
                                name: messageDelegate.displayName
                                Layout.alignment: Qt.AlignTop
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label { text: messageDelegate.displayName; color: Theme.text; font.weight: Font.DemiBold }
                                    Label {
                                        text: messageDelegate.timestamp
                                              ? Qt.formatTime(messageDelegate.timestamp, "HH:mm") : ""
                                        color: Theme.textMuted
                                        font.pixelSize: 10
                                    }
                                    Item { Layout.fillWidth: true }
                                    ToolButton {
                                        visible: App.session.host && !messageDelegate.isPinned
                                        text: qsTr("置顶")
                                        onClicked: App.chat.pin(messageDelegate.index)
                                    }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: messageDelegate.message
                                    color: Theme.text
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: chatList.count === 0
                        text: qsTr("加入房间后即可聊天")
                        color: Theme.textMuted
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: chatInput
                        Layout.fillWidth: true
                        placeholderText: qsTr("输入消息…")
                        enabled: App.session.lobbyId.length > 0
                        onAccepted: sendMessage()
                        function sendMessage() {
                            if (text.trim().length === 0)
                                return
                            App.chat.send(text)
                            clear()
                        }
                    }
                    Button { text: qsTr("发送"); enabled: chatInput.enabled; onClicked: chatInput.sendMessage() }
                    Switch {
                        text: qsTr("提醒")
                        checked: App.chat.reminderEnabled
                        onToggled: App.chat.reminderEnabled = checked
                    }
                }
            }

            AppCard {
                Layout.fillHeight: true
                Layout.preferredWidth: 350
                title: qsTr("成员与好友")

                TabBar {
                    id: peopleTabs
                    Layout.fillWidth: true
                    TabButton { text: qsTr("成员") }
                    TabButton { text: qsTr("好友") }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: peopleTabs.currentIndex

                    ListView {
                        id: memberList
                        clip: true
                        spacing: 7
                        model: App.social.members
                        ScrollBar.vertical: ScrollBar {}

                        delegate: Rectangle {
                            id: memberDelegate
                            required property string displayName
                            required property string steamId
                            required property string avatar
                            required property string ip
                            required property int ping
                            required property string relay
                            required property bool isFriend
                            required property bool isSelf

                            width: memberList.width
                            implicitHeight: 64
                            radius: 10
                            color: Theme.surfaceRaised
                            border.color: Theme.border

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 9
                                Avatar {
                                    source: memberDelegate.avatar
                                    name: memberDelegate.displayName
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    RowLayout {
                                        Label { text: memberDelegate.displayName; color: Theme.text; elide: Text.ElideRight; Layout.fillWidth: true }
                                        StatusPill {
                                            text: memberDelegate.isSelf ? qsTr("自己")
                                                  : (memberDelegate.isFriend ? qsTr("好友") : qsTr("成员"))
                                            accent: memberDelegate.isSelf ? Theme.secondary : Theme.primary
                                        }
                                    }
                                    Label {
                                        text: memberDelegate.ip.length > 0
                                              ? memberDelegate.ip : memberDelegate.steamId
                                        color: Theme.textMuted
                                        font.pixelSize: 11
                                        elide: Text.ElideMiddle
                                        Layout.fillWidth: true
                                    }
                                }
                                ColumnLayout {
                                    Label { text: memberDelegate.ping >= 0 ? memberDelegate.ping + " ms" : "–"; color: Theme.primary }
                                    Label { text: memberDelegate.relay.length > 0 ? memberDelegate.relay : ""; color: Theme.textMuted; font.pixelSize: 10 }
                                }
                                ToolButton {
                                    visible: !memberDelegate.isFriend && !memberDelegate.isSelf
                                    text: "+"
                                    onClicked: App.social.addFriend(memberDelegate.steamId)
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("添加好友")
                                }
                                ToolButton {
                                    visible: memberDelegate.ip.length > 0
                                    text: "⧉"
                                    onClicked: App.copy(memberDelegate.ip)
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("复制 IP")
                                }
                            }
                        }

                        Label { anchors.centerIn: parent; visible: memberList.count === 0; text: qsTr("暂无成员"); color: Theme.textMuted }
                    }

                    ColumnLayout {
                        spacing: 8
                        TextField {
                            Layout.fillWidth: true
                            placeholderText: qsTr("搜索好友…")
                            text: App.social.filter
                            onTextEdited: App.social.filter = text
                        }
                        ListView {
                            id: friendList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 7
                            model: App.social.friends
                            ScrollBar.vertical: ScrollBar {}

                            delegate: Rectangle {
                                id: friendDelegate
                                required property string displayName
                                required property string steamId
                                required property string avatar
                                required property bool online
                                required property string status
                                required property int inviteCooldown

                                width: friendList.width
                                implicitHeight: 62
                                radius: 10
                                color: Theme.surfaceRaised
                                border.color: Theme.border

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 9
                                    Avatar {
                                        source: friendDelegate.avatar
                                        name: friendDelegate.displayName
                                        online: friendDelegate.online
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Label { text: friendDelegate.displayName; color: Theme.text; elide: Text.ElideRight; Layout.fillWidth: true }
                                        Label {
                                            text: friendDelegate.status.length > 0
                                                  ? friendDelegate.status : friendDelegate.steamId
                                            color: friendDelegate.online ? Theme.success : Theme.textMuted
                                            font.pixelSize: 11
                                        }
                                    }
                                    Button {
                                        text: friendDelegate.inviteCooldown > 0
                                              ? friendDelegate.inviteCooldown + "s" : qsTr("邀请")
                                        enabled: App.session.lobbyId.length > 0
                                                 && friendDelegate.inviteCooldown === 0
                                        onClicked: App.social.invite(friendDelegate.steamId)
                                    }
                                }
                            }

                            Label { anchors.centerIn: parent; visible: friendList.count === 0; text: qsTr("未获取到好友"); color: Theme.textMuted }
                        }
                    }
                }
            }
        }
    }
}
