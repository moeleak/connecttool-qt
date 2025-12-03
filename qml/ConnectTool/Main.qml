import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

ApplicationWindow {
    id: win
    width: 900
    height: 700
    minimumWidth: 900
    minimumHeight: 640
    visible: true
    title: qsTr("ConnectTool · Steam P2P")

    Material.theme: Material.Dark
    Material.primary: "#23c9a9"
    Material.accent: "#2ad2ff"

    property string friendFilter: ""
    property string lastError: ""
    property string copyHint: ""

    function copyBadge(label, value) {
        if (!value || value.length === 0) {
            return;
        }
        backend.copyToClipboard(value);
        win.copyHint = qsTr("%1 已复制").arg(label);
        copyTimer.restart();
    }

    background: Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0.0; color: "#0f1725" }
            GradientStop { position: 1.0; color: "#0c111b" }
        }
    }

    Connections {
        target: backend
        function onErrorMessage(msg) {
            win.lastError = msg
            errorTimer.restart()
        }
    }

    Timer {
        id: errorTimer
        interval: 4200
        repeat: false
        onTriggered: win.lastError = ""
    }

    Timer {
        id: copyTimer
        interval: 1600
        repeat: false
        onTriggered: win.copyHint = ""
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            Label {
                text: qsTr("ConnectTool")
                font.pixelSize: 28
                font.family: "Fira Sans"
                color: "#e8f5ff"
            }

            Rectangle {
                radius: 12
                Layout.fillWidth: true
                implicitHeight: 56
                color: "#161f2e"
                border.color: "#243149"
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 12
                    Label {
                        text: backend.status
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                        color: "#dce7ff"
                        font.pixelSize: 16
                    }
                    Label {
                        visible: win.copyHint.length > 0
                        text: win.copyHint
                        color: "#7fded1"
                        font.pixelSize: 13
                        Layout.alignment: Qt.AlignVCenter
                    }
                    Rectangle {
                        radius: 8
                        color: backend.steamReady ? "#2dd6c1" : "#ef476f"
                        implicitWidth: 12
                        implicitHeight: 12
                        Layout.alignment: Qt.AlignVCenter
                    }
                    Label {
                        text: backend.steamReady ? qsTr("Steam 已就绪") : qsTr("Steam 未登录")
                        color: "#99a6c7"
                        font.pixelSize: 14
                    }
                }
            }
        }

        Frame {
            Layout.fillWidth: true
            padding: 18
            Material.elevation: 6
            background: Rectangle { radius: 12; color: "#131c2b"; border.color: "#1f2c3f" }

            ColumnLayout {
                anchors.fill: parent
                spacing: 12
                Layout.fillWidth: true

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    TextField {
                        id: joinField
                        Layout.fillWidth: true
                        Layout.minimumWidth: 320
                        placeholderText: qsTr("输入房间 ID 或房主 SteamID64 或留空以主持房间")
                        text: backend.joinTarget
                        enabled: !(backend.isHost || backend.isConnected)
                        onTextChanged: backend.joinTarget = text
                        color: "#dce7ff"
                        selectByMouse: true
                    }

                    CheckBox {
                        text: qsTr("启动")
                        checked: backend.isHost || backend.isConnected
                        Layout.alignment: Qt.AlignVCenter
                        onClicked: {
                            if (checked && !backend.isConnected && !backend.isHost) {
                                backend.joinHost()
                            } else if (!checked && (backend.isConnected || backend.isHost)) {
                                backend.disconnect()
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Repeater {
                        model: [
                            { title: qsTr("房间 ID"), value: backend.lobbyId, accent: "#23c9a9" },
                            { title: qsTr("房主 ID"), value: backend.hostSteamId, accent: "#2ad2ff" }
                        ]
                        delegate: Rectangle {
                            required property string title
                            required property string value
                            required property string accent
                            radius: 10
                            color: "#151e2f"
                            border.color: "#243149"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 58
                            opacity: value.length > 0 ? 1.0 : 0.4

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 4
                                Label {
                                    text: title
                                    color: accent
                                    font.pixelSize: 12
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Label {
                                        text: value.length > 0 ? value : qsTr("未加入")
                                        color: "#dce7ff"
                                        font.pixelSize: 15
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        text: qsTr("点击复制")
                                        visible: value.length > 0
                                        color: "#7f8cab"
                                        font.pixelSize: 12
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                enabled: value.length > 0
                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: win.copyBadge(title, value)
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Label {
                        text: qsTr("本地转发端口")
                        color: "#a7b6d8"
                    }

                    SpinBox {
                        id: portField
                        from: 0
                        to: 65535
                        value: backend.localPort
                        editable: true
                        enabled: !(backend.isHost || backend.isConnected)
                        onValueChanged: backend.localPort = value
                    }

                    Item { width: 24; height: 1 }

                    Label {
                        text: qsTr("本地绑定端口")
                        color: "#a7b6d8"
                    }

                    SpinBox {
                        id: bindPortField
                        from: 1
                        to: 65535
                        value: backend.localBindPort
                        editable: true
                        enabled: !(backend.isHost || backend.isConnected)
                        onValueChanged: backend.localBindPort = value
                    }

                    Rectangle { Layout.fillWidth: true; color: "transparent" }

                    Label {
                        text: qsTr("TCP 客户端: %1").arg(backend.tcpClients)
                        color: "#7fb7ff"
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            Frame {
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: 16
                Material.elevation: 6
                background: Rectangle { radius: 12; color: "#111827"; border.color: "#1f2b3c" }

                ColumnLayout {
                    anchors.fill: parent
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Label {
                            text: qsTr("房间成员")
                            font.pixelSize: 18
                            color: "#e6efff"
                        }
                        Rectangle { Layout.fillWidth: true; color: "transparent" }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredHeight: 280

                        Column {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 12

                            Repeater {
                                id: memberRepeater
                                model: backend.membersModel
                                delegate: Rectangle {
                                    required property string displayName
                                    required property string steamId
                                    required property string avatar
                                    required property var ping
                                    required property string relay
                                    radius: 10
                                    color: "#162033"
                                    border.color: "#1f2f45"
                                    width: parent ? parent.width : 0
                                    height: implicitHeight
                                    implicitHeight: rowLayout.implicitHeight + 24
                                    Component.onCompleted: console.log("[QML] member delegate", displayName, steamId, ping, relay)

                                    RowLayout {
                                        id: rowLayout
                                        anchors.fill: parent
                                        anchors.margins: 12
                                        spacing: 12

                                        Item {
                                            width: 48
                                            height: 48
                                            Layout.alignment: Qt.AlignVCenter
                                            Layout.preferredWidth: 48
                                            Layout.preferredHeight: 48
                                            Rectangle {
                                                id: memberAvatarFrame
                                                anchors.fill: parent
                                                radius: width / 2
                                                color: avatar.length > 0 ? "transparent" : "#1a2436"
                                                border.color: avatar.length > 0 ? "transparent" : "#1f2f45"
                                                layer.enabled: avatar.length > 0
                                                layer.effect: OpacityMask {
                                                    source: memberAvatarFrame
                                                    maskSource: Rectangle {
                                                        width: memberAvatarFrame.width
                                                        height: memberAvatarFrame.height
                                                        radius: memberAvatarFrame.width / 2
                                                        color: "white"
                                                    }
                                                }
                                                Image {
                                                    anchors.fill: parent
                                                    source: avatar
                                                    visible: avatar.length > 0
                                                    fillMode: Image.PreserveAspectCrop
                                                    smooth: true
                                                }
                                                Label {
                                                    anchors.centerIn: parent
                                                    visible: avatar.length === 0
                                                    text: displayName.length > 0 ? displayName[0] : "?"
                                                    color: "#6f7e9c"
                                                    font.pixelSize: 18
                                                }
                                            }
                                        }

                                        ColumnLayout {
                                            spacing: 2
                                            Layout.fillWidth: true
                                            Label {
                                                text: displayName
                                                font.pixelSize: 16
                                                color: "#e1edff"
                                                elide: Text.ElideRight
                                            }
                                            Label {
                                                text: qsTr("SteamID: %1").arg(steamId)
                                                font.pixelSize: 12
                                                color: "#7f8cab"
                                                elide: Text.ElideRight
                                            }
                                        }

                                        ColumnLayout {
                                            spacing: 2
                                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                            Label {
                                                text: (ping === undefined || ping === null)
                                                      ? qsTr("-")
                                                      : qsTr("%1 ms").arg(ping)
                                                color: "#7fded1"
                                                font.pixelSize: 14
                                                horizontalAlignment: Text.AlignRight
                                                Layout.alignment: Qt.AlignRight
                                            }
                                            Label {
                                                text: relay.length > 0 ? relay : "-"
                                                color: "#8ea4c8"
                                                font.pixelSize: 12
                                                elide: Text.ElideRight
                                                horizontalAlignment: Text.AlignRight
                                                Layout.alignment: Qt.AlignRight
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Column {
                            visible: memberRepeater.count === 0
                            anchors.centerIn: parent
                            spacing: 6
                            Label { text: qsTr("暂无成员"); color: "#8090b3" }
                            Label { text: qsTr("创建房间或等待邀请即可出现。"); color: "#62708f"; font.pixelSize: 12 }
                        }
                    }
                }
            }

            Frame {
                Layout.preferredWidth: 380
                Layout.fillHeight: true
                padding: 16
                Material.elevation: 6
                background: Rectangle { radius: 12; color: "#111827"; border.color: "#1f2b3c" }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Label {
                            text: qsTr("邀请好友")
                            font.pixelSize: 18
                            color: "#e6efff"
                        }
                        Rectangle { Layout.fillWidth: true; color: "transparent" }
                        Label {
                            text: qsTr("好友数: %1").arg(backend.friendsModel ? backend.friendsModel.count : 0)
                            color: "#7f8cab"
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Button { text: qsTr("刷新"); onClicked: backend.refreshFriends() }
                    }

                    TextField {
                        id: filterField
                        Layout.fillWidth: true
                        placeholderText: qsTr("搜索好友…")
                        text: win.friendFilter
                        onTextChanged: {
                            win.friendFilter = text
                            backend.friendFilter = text
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredHeight: 320

                        ListView {
                            id: friendList
                            anchors.fill: parent
                            anchors.margins: 8
                            clip: true
                            spacing: 10
                            model: backend.friendsModel
                            ScrollBar.vertical: ScrollBar {}

                            Component.onCompleted: {
                                console.log("[QML] friendList completed, model count", model ? model.count : "<null>")
                            }
                            onModelChanged: console.log("[QML] friendList model changed", model)

                            onCountChanged: console.log("[QML] friendList count", count)

                            delegate: Rectangle {
                                required property string displayName
                                required property string steamId
                                required property string avatar
                                required property bool online
                                required property string status
                                width: friendList.width

                                Component.onCompleted: {
                                    console.log("[QML] delegate", displayName, steamId)
                                }

                                visible: true // ordering handled by proxy, we keep all items
                                radius: 10
                                color: "#162033"
                                border.color: "#1f2f45"
                                implicitHeight: 60
                                Layout.fillWidth: true

                                RowLayout {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.margins: 10
                                    spacing: 10
                                    Layout.alignment: Qt.AlignVCenter
                                    Item {
                                        id: avatarContainer
                                        width: 44
                                        height: 44
                                        Layout.alignment: Qt.AlignVCenter
                                        Layout.preferredWidth: 44
                                        Layout.preferredHeight: 44
                                        Rectangle {
                                            id: avatarFrame
                                            anchors.fill: parent
                                            radius: width / 2
                                            color: avatar.length > 0 ? "transparent" : "#1a2436"
                                            border.color: avatar.length > 0 ? "transparent" : "#1f2f45"
                                            clip: false
                                            layer.enabled: avatar.length > 0
                                            layer.effect: OpacityMask {
                                                source: avatarFrame
                                                maskSource: Rectangle {
                                                    width: avatarFrame.width
                                                    height: avatarFrame.height
                                                    radius: avatarFrame.width / 2
                                                    color: "white"
                                                }
                                            }
                                            Image {
                                                anchors.fill: parent
                                                source: avatar
                                                visible: avatar.length > 0
                                                fillMode: Image.PreserveAspectCrop
                                                smooth: true
                                            }
                                            Label {
                                                anchors.centerIn: parent
                                                visible: avatar.length === 0
                                                text: displayName.length > 0 ? displayName[0] : "?"
                                                color: "#6f7e9c"
                                                font.pixelSize: 16
                                            }
                                        }
                                        Rectangle {
                                            width: 12
                                            height: 12
                                            radius: 6
                                            color: "#2dd6c1"
                                            border.color: "#111827"
                                            border.width: 2
                                            anchors.top: parent.top
                                            anchors.right: parent.right
                                            anchors.margins: -2
                                            z: 2
                                            visible: online
                                        }
                                    }
                                    ColumnLayout {
                                        spacing: 2
                                        Layout.fillWidth: true
                                        Layout.alignment: Qt.AlignVCenter
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 6
                                            Label {
                                                text: displayName
                                                color: "#e1edff"
                                                font.pixelSize: 15
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }
                                            Label {
                                                text: status
                                                color: online ? "#2dd6c1" : "#7f8cab"
                                                font.pixelSize: 12
                                                visible: status.length > 0
                                            }
                                        }
                                        Label { text: steamId; color: "#7f8cab"; font.pixelSize: 12; elide: Text.ElideRight }
                                    }
                                    Item { Layout.fillWidth: true }
                                    Button {
                                        text: backend.inviteCooldown === 0
                                              ? qsTr("邀请")
                                              : qsTr("等待 %1s").arg(backend.inviteCooldown)
                                        enabled: (backend.isHost || backend.isConnected) && backend.inviteCooldown === 0
                                        Layout.alignment: Qt.AlignVCenter
                                        onClicked: backend.inviteFriend(steamId)
                                    }
                                }
                            }
                        }

                        Column {
                            visible: friendList.count === 0
                            anchors.centerIn: parent
                            spacing: 6
                            Label { text: qsTr("未获取到好友列表"); color: "#8090b3" }
                            Label { text: qsTr("确保已登录 Steam 并允许好友可见。"); color: "#62708f"; font.pixelSize: 12 }
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        visible: win.lastError.length > 0
        opacity: visible ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        radius: 10
        color: "#ef476f"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18
        width: Math.min(parent.width - 80, 480)
        height: implicitHeight

        Column {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 4
            Label {
                text: qsTr("提示")
                font.pixelSize: 14
                color: "#ffe3ed"
            }
            Label {
                text: win.lastError
                font.pixelSize: 13
                wrapMode: Text.Wrap
                color: "#fff9fb"
            }
        }
    }
}
