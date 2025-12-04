import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

ApplicationWindow {
    id: win
    width: 1080
    height: 700
    minimumWidth: 1080
    minimumHeight: 700
    visible: true
    title: qsTr("ConnectTool · Steam P2P")

    Material.theme: Material.Dark
    Material.primary: "#23c9a9"
    Material.accent: "#2ad2ff"

    property string friendFilter: ""
    property string copyHint: ""
    property string currentPage: "room"
    property var navItems: [
        { key: "room", title: qsTr("房间"), subtitle: qsTr("主持或加入到房间") },
        { key: "lobby", title: qsTr("大厅"), subtitle: qsTr("浏览房间列表") },
        { key: "about", title: qsTr("关于"), subtitle: qsTr("关于 ConnectTool") }
    ]

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
    }

    Timer {
        id: copyTimer
        interval: 1600
        repeat: false
        onTriggered: win.copyHint = ""
    }

    Drawer {
        id: navDrawer
        edge: Qt.LeftEdge
        width: Math.min(win.width * 0.6, 300)
        height: win.height
        modal: true
        interactive: true

        background: Rectangle {
            anchors.fill: parent
            color: "#0f1725"
            border.color: "#1f2b3c"
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12

            Label {
                text: qsTr("ConnectTool")
                color: "#e6efff"
                font.pixelSize: 18
            }

            Repeater {
                model: win.navItems
                delegate: Rectangle {
                    required property string key
                    required property string title
                    required property string subtitle
                    Layout.fillWidth: true
                    radius: 10
                    implicitHeight: 56
                    color: win.currentPage === key ? "#162033" : "transparent"
                    border.color: win.currentPage === key ? "#23c9a9" : "#1f2b3c"
                    Behavior on color { ColorAnimation { duration: 250; easing.type: Easing.InOutQuad } }
                    Behavior on border.color { ColorAnimation { duration: 250; easing.type: Easing.InOutQuad } }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 10

                        Rectangle {
                            width: 6
                            height: 24
                            radius: 3
                            color: "#23c9a9"
                            opacity: win.currentPage === key ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: 250; easing.type: Easing.InOutQuad } }
                            Layout.alignment: Qt.AlignVCenter
                        }

                        ColumnLayout {
                            spacing: 2
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter

                            Label {
                                text: title
                                color: "#e6efff"
                                font.pixelSize: 15
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignLeft
                            }
                            Label {
                                text: subtitle
                                color: "#7f8cab"
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignLeft
                            }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            win.currentPage = key
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            ToolButton {
                id: menuButton
                implicitWidth: 44
                implicitHeight: 44
                contentItem: Image {
                    anchors.centerIn: parent
                    source: Qt.resolvedUrl("Menu.svg")
                    width: 22
                    height: 22
                    sourceSize.width: 48
                    sourceSize.height: 48
                    asynchronous: false
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                    opacity: menuButton.enabled ? 1.0 : 0.4
                }
                background: Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: menuButton.hovered ? "#1c293c" : "#161f2e"
                    border.color: "#243149"
                }
                Layout.alignment: Qt.AlignVCenter
                Accessible.name: qsTr("打开导航")
                onClicked: navDrawer.open()
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

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: win.currentPage === "room"
            opacity: visible ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 500; easing.type: Easing.InOutQuad } }

            ColumnLayout {
                anchors.fill: parent
                spacing: 16

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

                            ComboBox {
                                id: modeCombo
                                Layout.preferredWidth: 140
                                Layout.alignment: Qt.AlignVCenter
                                model: [
                                    { text: qsTr("TCP 模式"), value: 0 },
                                    { text: qsTr("TUN 模式"), value: 1 }
                                ]
                                textRole: "text"
                                valueRole: "value"
                                currentIndex: Math.max(0, Math.min(model.length - 1, backend.connectionMode))
                                enabled: !(backend.isHost || backend.isConnected)
                                onActivated: backend.connectionMode = model[currentIndex].value
                            }

                            Switch {
                                text: qsTr("启动")
                                checked: backend.isHost || backend.isConnected
                                Layout.alignment: Qt.AlignVCenter
                                onToggled: {
                                    if (checked && !backend.isConnected && !backend.isHost) {
                                        backend.joinHost()
                                    } else if (!checked && (backend.isConnected || backend.isHost)) {
                                        backend.disconnect()
                                    }
                                }
                            }

                            Switch {
                                id: publishSwitch
                                text: qsTr("公开到大厅")
                                checked: backend.publishLobby
                                enabled: (!backend.isConnected) || backend.isHost
                                Layout.alignment: Qt.AlignVCenter
                                onToggled: backend.publishLobby = checked
                            }
                        }

                        TextField {
                            id: roomNameField
                            Layout.fillWidth: true
                            placeholderText: qsTr("房间名（主持时展示在大厅列表中）")
                            text: backend.roomName
                            onTextChanged: backend.roomName = text
                            color: "#dce7ff"
                            selectByMouse: true
                            enabled: !backend.isConnected
                            visible: publishSwitch.checked && !backend.isConnected
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Repeater {
                                model: [
                                    { title: qsTr("房间名"), value: backend.lobbyName, accent: "#7fded1" },
                                    { title: qsTr("房间 ID"), value: backend.lobbyId, accent: "#23c9a9" },
                                    backend.connectionMode === 1
                                    ? {
                                        title: qsTr("TUN 信息"),
                                        value: backend.tunLocalIp.length > 0
                                        ? (backend.tunDeviceName.length > 0
                                        ? qsTr("%1 · %2").arg(backend.tunLocalIp).arg(backend.tunDeviceName)
                                        : backend.tunLocalIp)
                                        : (backend.tunDeviceName.length > 0
                                        ? qsTr("%1 · 待分配 IP").arg(backend.tunDeviceName)
                                        : qsTr("未启动")),
                                        accent: "#2ad2ff"
                                    }
                                    : {
                                        title: qsTr("连接 IP"),
                                        value: backend.localBindPort > 0 ? qsTr("localhost:%1").arg(backend.localBindPort) : "",
                                        accent: "#2ad2ff"
                                    }
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
                            visible: backend.connectionMode === 0
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
                                enabled: backend.connectionMode === 0 && !(backend.isHost || backend.isConnected)
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
                                enabled: backend.connectionMode === 0 && !(backend.isHost || backend.isConnected)
                                onValueChanged: backend.localBindPort = value
                            }

                            Rectangle { Layout.fillWidth: true; color: "transparent" }

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
                                Layout.preferredHeight: 320

                                Flickable {
                                    id: memberFlick
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    clip: true
                                    interactive: contentHeight > height
                                    contentHeight: membersColumn.implicitHeight
                                    ScrollBar.vertical: ScrollBar {}

                                    Column {
                                        id: membersColumn
                                        width: parent.width
                                        spacing: 12
                                        Repeater {
                                            id: memberRepeater
                                            model: backend.membersModel
                                            delegate: Rectangle {
                                                required property string displayName
                                                required property string steamId
                                                required property string avatar
                                                required property string ip
                                                required property var ping
                                                required property string relay
                                                required property bool isFriend

                                                radius: 10
                                                color: "#162033"
                                                border.color: "#1f2f45"
                                                width: parent ? parent.width : 0
                                                // 高度由内部布局决定，并增加一些内边距
                                                implicitHeight: rowLayout.implicitHeight + 24

                                                RowLayout {
                                                    id: rowLayout
                                                    anchors.fill: parent
                                                    anchors.margins: 12
                                                    spacing: 12

                                                    // --- 1. 头像区域 (保持不变) ---
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

                                                    // --- 2. 用户信息列 (保持不变) ---
                                                    ColumnLayout {
                                                        spacing: 4
                                                        Layout.fillWidth: false // 不要占满，只占需要宽度
                                                        Layout.alignment: Qt.AlignVCenter // 垂直居中

                                                        RowLayout {
                                                            spacing: 8
                                                            Label {
                                                                text: displayName
                                                                font.pixelSize: 16
                                                                color: "#e1edff"
                                                                elide: Text.ElideRight
                                                            }
                                                            Rectangle {
                                                                radius: 8
                                                                color: "#142033"
                                                                border.color: isFriend ? "#23c9a9" : "#ef476f"
                                                                implicitHeight: 22
                                                                implicitWidth: relationLabel.implicitWidth + 14
                                                                Layout.alignment: Qt.AlignVCenter
                                                                Label {
                                                                    id: relationLabel
                                                                    anchors.centerIn: parent
                                                                    text: isFriend ? qsTr("好友") : qsTr("陌生人")
                                                                    color: isFriend ? "#23c9a9" : "#ef476f"
                                                                    font.pixelSize: 11
                                                                }
                                                            }
                                                        }
                                                        Label {
                                                            text: qsTr("SteamID: %1").arg(steamId)
                                                            font.pixelSize: 12
                                                            color: "#7f8cab"
                                                            elide: Text.ElideRight
                                                        }
                                                        Label {
                                                            visible: backend.connectionMode === 1
                                                            text: qsTr("IP: %1").arg(ip && ip.length > 0 ? ip : qsTr("-"))
                                                            font.pixelSize: 12
                                                            color: "#7f8cab"
                                                            elide: Text.ElideRight
                                                        }
                                                    }

                                                    // --- 3. 关键占位符 (Spacer) ---
                                                    // 这个 Item 会占据中间所有剩余空间，把后面的 Ping 和 按钮 推到最右边
                                                    Item {
                                                        Layout.fillWidth: true
                                                    }

                                                    Button {
                                                        visible: !isFriend
                                                        text: qsTr("添加好友")
                                                        Layout.alignment: Qt.AlignVCenter
                                                        onClicked: backend.addFriend(steamId)
                                                    }

                                                    Button {
                                                        visible: backend.connectionMode === 1 && ip && ip.length > 0
                                                        text: qsTr("复制 IP")
                                                        flat: true
                                                        Layout.alignment: Qt.AlignVCenter
                                                        onClicked: backend.copyToClipboard(ip)
                                                    }

                                                    // --- 4. Ping 信息列 ---
                                                    ColumnLayout {
                                                        spacing: 2
                                                        Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                                                        Label {
                                                            text: (ping === undefined || ping === null) ? qsTr("-") : qsTr("%1 ms").arg(ping)
                                                            color: "#7fded1"
                                                            font.pixelSize: 14
                                                            horizontalAlignment: Text.AlignRight
                                                            Layout.alignment: Qt.AlignRight
                                                        }
                                                        Label {
                                                            text: relay.length > 0 ? relay : "-"
                                                            color: "#8ea4c8"
                                                            font.pixelSize: 12
                                                            horizontalAlignment: Text.AlignRight
                                                            Layout.alignment: Qt.AlignRight
                                                        }
                                                    }

                                                    // --- 5. 按钮区域 (移动到这里) ---
                                                    // 直接放在 RowLayout 中，它们会自动排列在 Ping 信息右侧
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
                                Item {
                                    implicitWidth: 28
                                    implicitHeight: 28
                                    Layout.alignment: Qt.AlignVCenter

                                    BusyIndicator {
                                        anchors.fill: parent
                                        running: backend.friendsRefreshing
                                        visible: running
                                    }
                                }
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
                                        required property int inviteCooldown
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
                                                text: inviteCooldown === 0
                                                ? qsTr("邀请")
                                                : qsTr("等待 %1s").arg(inviteCooldown)
                                                enabled: (backend.isHost || backend.isConnected) && inviteCooldown === 0
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
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: win.currentPage === "lobby"
            opacity: visible ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 500; easing.type: Easing.InOutQuad } }

            onVisibleChanged: {
                if (visible) {
                    backend.refreshLobbies()
                }
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Label {
                        text: qsTr("大厅")
                        font.pixelSize: 20
                        color: "#e6efff"
                    }
                    Rectangle { Layout.fillWidth: true; color: "transparent" }
                    Item {
                        implicitWidth: 28
                        implicitHeight: 28
                        Layout.alignment: Qt.AlignVCenter

                        BusyIndicator {
                            anchors.fill: parent
                            running: backend.lobbyRefreshing
                            visible: running
                        }
                    }
                    Label {
                        text: qsTr("房间数: %1").arg(backend.lobbiesModel ? backend.lobbiesModel.count : 0)
                        color: "#7f8cab"
                        Layout.alignment: Qt.AlignVCenter
                    }
                    Button {
                        text: qsTr("刷新")
                        enabled: backend.steamReady && !backend.lobbyRefreshing
                        Layout.alignment: Qt.AlignVCenter
                        onClicked: backend.refreshLobbies()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    TextField {
                        id: lobbySearchField
                        Layout.fillWidth: true
                        placeholderText: qsTr("搜索房间名 / 房主 / 房间 ID …")
                        text: backend.lobbyFilter
                        onTextChanged: backend.lobbyFilter = text
                    }

                    ComboBox {
                        id: lobbySortBox
                        Layout.preferredWidth: 160
                        model: [
                            { text: qsTr("按人数"), value: 0 },
                            { text: qsTr("按房间名"), value: 1 }
                        ]
                        textRole: "text"
                        valueRole: "value"
                        currentIndex: Math.min(model.length - 1, Math.max(0, backend.lobbySortMode))
                        onActivated: backend.lobbySortMode = model[currentIndex].value
                    }
                }

                Frame {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    padding: 16
                    Material.elevation: 6
                    background: Rectangle { radius: 12; color: "#111827"; border.color: "#1f2b3c" }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10

                        Label {
                            text: qsTr("浏览当前可见的房间，点击加入或复制房间 ID。")
                            color: "#8ea4c8"
                            font.pixelSize: 13
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            Flickable {
                                id: lobbyFlick
                                anchors.fill: parent
                                anchors.margins: 4
                                clip: true
                                contentHeight: lobbyColumn.implicitHeight
                                interactive: contentHeight > height
                                ScrollBar.vertical: ScrollBar {}

                                Column {
                                    id: lobbyColumn
                                    width: parent.width
                                    spacing: 10

                                    Repeater {
                                        id: lobbyRepeater
                                        model: backend.lobbiesModel
                                        delegate: Rectangle {
                                            required property string lobbyId
                                            required property string name
                                            required property string hostName
                                            required property string hostId
                                            required property int members
                                            required property var ping
                                            property bool isCurrentLobby: backend.lobbyId && backend.lobbyId === lobbyId
                                            property bool canJoin: backend.steamReady && !isCurrentLobby
                                            radius: 10
                                            color: "#162033"
                                            border.color: "#1f2f45"
                                            width: parent ? parent.width : 0
                                            height: implicitHeight
                                            implicitHeight: row.implicitHeight + 16

                                            RowLayout {
                                                id: row
                                                anchors.fill: parent
                                                anchors.margins: 10
                                                spacing: 10

                                                ColumnLayout {
                                                    spacing: 4
                                                    Layout.fillWidth: true

                                                    RowLayout {
                                                        spacing: 8
                                                        Layout.fillWidth: true
                                                        Label {
                                                            text: name.length > 0
                                                                  ? name
                                                                  : (hostName.length > 0
                                                                     ? qsTr("%1 的房间").arg(hostName)
                                                                     : qsTr("未命名房间"))
                                                            font.pixelSize: 16
                                                            color: "#e1edff"
                                                            elide: Text.ElideRight
                                                            Layout.fillWidth: true
                                                        }
                                                        Rectangle {
                                                            visible: isCurrentLobby && backend.isHost
                                                            radius: 8
                                                            color: "#142033"
                                                            border.color: "#23c9a9"
                                                            implicitHeight: 22
                                                            implicitWidth: badgeText.implicitWidth + 14
                                                            Label {
                                                                id: badgeText
                                                                anchors.centerIn: parent
                                                                text: qsTr("你正在主持")
                                                                color: "#23c9a9"
                                                                font.pixelSize: 11
                                                            }
                                                        }
                                                        Rectangle {
                                                            visible: isCurrentLobby && backend.isConnected && !backend.isHost
                                                            radius: 8
                                                            color: "#142033"
                                                            border.color: "#2ad2ff"
                                                            implicitHeight: 22
                                                            implicitWidth: joinedText.implicitWidth + 14
                                                            Label {
                                                                id: joinedText
                                                                anchors.centerIn: parent
                                                                text: qsTr("已加入此房间")
                                                                color: "#2ad2ff"
                                                                font.pixelSize: 11
                                                            }
                                                        }
                                                    }

                                                    RowLayout {
                                                        spacing: 6
                                                        Layout.fillWidth: true
                                                        Label {
                                                            text: qsTr("房主: %1").arg(hostName.length > 0 ? hostName : hostId)
                                                            color: "#8ea4c8"
                                                            font.pixelSize: 12
                                                            elide: Text.ElideRight
                                                            Layout.fillWidth: true
                                                        }
                                                        Label {
                                                            text: qsTr("房间 ID: %1").arg(lobbyId)
                                                            color: "#7f8cab"
                                                            font.pixelSize: 12
                                                        }
                                                    }
                                                }

                                                Item { Layout.fillWidth: true } // push right

                                                RowLayout {
                                                    spacing: 10
                                                    Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                                                    Label {
                                                        text: qsTr("共有 %1 人").arg(members)
                                                        color: "#dce7ff"
                                                        font.pixelSize: 13
                                                        horizontalAlignment: Text.AlignRight
                                                        Layout.alignment: Qt.AlignVCenter
                                                    }
                                                    Button {
                                                        text: isCurrentLobby ? qsTr("已在此房间") : qsTr("加入")
                                                        Layout.alignment: Qt.AlignVCenter
                                                        enabled: canJoin
                                                        onClicked: backend.joinLobby(lobbyId)
                                                    }
                                                    Button {
                                                        text: qsTr("复制 ID")
                                                        flat: true
                                                        Layout.alignment: Qt.AlignVCenter
                                                        onClicked: win.copyBadge(qsTr("房间 ID"), lobbyId)
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Column {
                                anchors.centerIn: parent
                                spacing: 6
                                visible: !backend.lobbyRefreshing && lobbyRepeater.count === 0
                                Label { text: qsTr("暂无大厅数据"); color: "#8090b3" }
                                Label { text: qsTr("点击右上角刷新获取房间列表。"); color: "#62708f"; font.pixelSize: 12 }
                            }
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: win.currentPage === "about"
            opacity: visible ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 500; easing.type: Easing.InOutQuad } }

            RowLayout {
                anchors.fill: parent
                spacing: 12

                Frame {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: parent.width * 0.55
                    padding: 16
                    Material.elevation: 6
                    background: Rectangle { radius: 12; color: "#111827"; border.color: "#1f2b3c" }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12
                        Label {
                            text: qsTr("关于")
                            font.pixelSize: 20
                            color: "#e6efff"
                        }
                        Label {
                            text: qsTr("ConnectTool 使用 Steam P2P 协助房主与好友建立转发。")
                            color: "#dce7ff"
                            font.pixelSize: 14
                            wrapMode: Text.WordWrap
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            height: 48
                            radius: 10
                            color: "#162033"
                            border.color: "#1f2b3c"

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 10

                                Rectangle {
                                    width: 28
                                    height: 28
                                    radius: 14
                                    color: "#0e121a"
                                    border.color: "#23c9a9"
                                    Image {
                                        anchors.centerIn: parent
                                        width: 18
                                        height: 18
                                        source: Qt.resolvedUrl("QQ.svg")
                                        sourceSize.width: 18
                                        sourceSize.height: 18
                                        asynchronous: false
                                        fillMode: Image.PreserveAspectFit
                                        smooth: true
                                        mipmap: true
                                        layer.enabled: true
                                        layer.effect: ColorOverlay { color: "#23c9a9" }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Label {
                                        text: qsTr("ConnectTool 总群")
                                        color: "#e6efff"
                                        font.pixelSize: 14
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignLeft
                                    }
                                    Label {
                                        text: "616325806"
                                        color: "#7f8cab"
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignLeft
                                    }
                                }

                                Label {
                                    text: "\u2197"
                                    color: "#23c9a9"
                                    font.pixelSize: 16
                                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: Qt.openUrlExternally("https://qm.qq.com/q/616325806")
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            height: 48
                            radius: 10
                            color: "#162033"
                            border.color: "#1f2b3c"

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 10

                                Rectangle {
                                    width: 28
                                    height: 28
                                    radius: 14
                                    color: "#0e121a"
                                    border.color: "#23c9a9"
                                    Image {
                                        anchors.centerIn: parent
                                        width: 18
                                        height: 18
                                        source: Qt.resolvedUrl("QQ.svg")
                                        sourceSize.width: 18
                                        sourceSize.height: 18
                                        asynchronous: false
                                        fillMode: Image.PreserveAspectFit
                                        smooth: true
                                        mipmap: true
                                        layer.enabled: true
                                        layer.effect: ColorOverlay { color: "#23c9a9" }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Label {
                                        text: qsTr("connecttool-qt 交流群")
                                        color: "#e6efff"
                                        font.pixelSize: 14
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignLeft
                                    }
                                    Label {
                                        text: "902943118"
                                        color: "#7f8cab"
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignLeft
                                    }
                                }

                                Label {
                                    text: "\u2197"
                                    color: "#23c9a9"
                                    font.pixelSize: 16
                                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: Qt.openUrlExternally("https://qm.qq.com/q/902943118")
                            }
                        }
                        ColumnLayout {
                            spacing: 8
                            Repeater {
                                model: [
                                    { title: qsTr("GitHub 原项目"), url: "https://github.com/Ayndpa/ConnectTool" },
                                    { title: qsTr("GitHub 本项目"), url: "https://github.com/moeleak/connecttool-qt" }
                                ]
                                delegate: Rectangle {
                                    required property string title
                                    required property string url
                                    Layout.fillWidth: true
                                    height: 48
                                    radius: 10
                                    color: "#162033"
                                    border.color: "#1f2b3c"

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        spacing: 10

                                        Rectangle {
                                            width: 28
                                            height: 28
                                            radius: 14
                                            color: "#0e121a"
                                            border.color: "#23c9a9"
                                            Image {
                                                anchors.centerIn: parent
                                                width: 18
                                                height: 18
                                                source: Qt.resolvedUrl("GitHub.svg")
                                                sourceSize.width: 18
                                                sourceSize.height: 18
                                                asynchronous: false
                                                fillMode: Image.PreserveAspectFit
                                                smooth: true
                                                mipmap: true
                                                layer.enabled: true
                                                layer.effect: ColorOverlay { color: "#23c9a9" }
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2
                                            Label {
                                                text: title
                                                color: "#e6efff"
                                                font.pixelSize: 14
                                                Layout.fillWidth: true
                                                horizontalAlignment: Text.AlignLeft
                                            }
                                            Label {
                                                text: url
                                                color: "#7f8cab"
                                                font.pixelSize: 12
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                                horizontalAlignment: Text.AlignLeft
                                            }
                                        }

                                        Label {
                                            text: "\u2197"
                                            color: "#23c9a9"
                                            font.pixelSize: 16
                                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: Qt.openUrlExternally(url)
                                    }
                                }
                            }
                        }
                        Rectangle { Layout.fillHeight: true; color: "transparent" }
                    }
                }

                Frame {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: parent.width * 0.45
                    Layout.alignment: Qt.AlignTop
                    padding: 16
                    Material.elevation: 6
                    background: Rectangle { radius: 12; color: "#111827"; border.color: "#1f2b3c" }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        Label {
                            text: qsTr("贡献者")
                            color: "#e6efff"
                            font.pixelSize: 18
                            font.bold: true
                        }
                        ColumnLayout {
                            spacing: 4
                            Layout.fillWidth: true
                            Label { text: qsTr("开发"); color: "#9ab6e3"; font.pixelSize: 13 }
                            Label { text: qsTr("Ayndpa, MoeLeak"); color: "#dce7ff"; font.pixelSize: 13; wrapMode: Text.WordWrap }
                        }
                        ColumnLayout {
                            spacing: 4
                            Layout.fillWidth: true
                            Label { text: qsTr("测试"); color: "#9ab6e3"; font.pixelSize: 13 }
                            Label { text: qsTr("旺仔大乔, 梦于枫岚, 虈請, MoeLeak"); color: "#dce7ff"; font.pixelSize: 13; wrapMode: Text.WordWrap }
                        }
                        Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: "transparent" }
                    }
                }
            }
        }
    }

}
