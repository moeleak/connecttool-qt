pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Qcm.Material as MD
import ConnectTool

MD.ApplicationWindow {
    id: window

    width: 1180
    height: 760
    minimumWidth: 980
    minimumHeight: 660
    visible: false
    title: qsTr("ConnectTool · Steam P2P")
    color: Theme.background

    MD.MProp.textColor: MD.Token.color.on_surface
    MD.MProp.backgroundColor: MD.Token.color.surface
    MD.MProp.size.windowClass: MD.Token.window_class.select_type(width)

    property int pageIndex: 0
    readonly property var navigation: [
        { icon: MD.Token.icon.meeting_room, title: qsTr("房间"), subtitle: qsTr("连接、聊天与成员") },
        { icon: MD.Token.icon.travel_explore, title: qsTr("大厅"), subtitle: qsTr("发现公开房间") },
        { icon: MD.Token.icon.hub, title: qsTr("节点"), subtitle: qsTr("中继与网络状态") },
        { icon: MD.Token.icon.info, title: qsTr("关于"), subtitle: qsTr("版本、更新与社区") }
    ]

    Component.onCompleted: {
        MD.Token.color.useSysColorSM = false
        MD.Token.color.useSysAccentColor = false
        MD.Token.color.paletteType = MD.Enum.PaletteTonalSpot
        MD.Token.color.accentColor = "#20cdb5"
        MD.Token.themeMode = MD.Enum.Dark
        visible = true
    }

    header: MD.Pane {
        width: window.width
        implicitHeight: 68
        horizontalPadding: 14
        verticalPadding: 8
        backgroundColor: Theme.surfaceContainer
        elevation: MD.Token.elevation.level2

        contentItem: RowLayout {
            spacing: 10

            MD.IconButton {
                Accessible.name: qsTr("打开导航")
                mdState.type: MD.Enum.IBtStandard
                mdState.size: MD.Enum.S
                icon.name: MD.Token.icon.menu
                onClicked: navigationDrawer.open()
            }

            MD.Divider {
                Layout.fillHeight: true
                Layout.topMargin: 8
                Layout.bottomMargin: 8
                orientation: Qt.Vertical
            }

            ColumnLayout {
                spacing: 0

                MD.Text {
                    text: window.navigation[window.pageIndex].title
                    color: Theme.foreground
                    typescale: MD.Token.typescale.title_medium
                    prominent: true
                }
                MD.Text {
                    text: window.navigation[window.pageIndex].subtitle
                    color: Theme.foregroundMuted
                    typescale: MD.Token.typescale.body_small
                }
            }

            Item { Layout.fillWidth: true }
        }
    }

    MD.Dialog {
        id: adminDialog
        title: qsTr("需要管理员权限")
        standardButtons: T.DialogButtonBox.Ok
        width: 420

        MD.Text {
            width: parent.width
            text: qsTr("TUN 模式需要创建和配置虚拟网卡。请允许管理员权限后重试。")
            color: Theme.foreground
            wrapMode: Text.WordWrap
        }
    }

    Connections {
        target: App.session
        function onAdminPrivilegesRequired() { adminDialog.open() }
    }

    Connections {
        target: App
        function onCopied() { notifications.show(qsTr("已复制到剪贴板"), 2200, 0) }
    }

    NavigationDrawer {
        id: navigationDrawer
        entries: window.navigation
        currentIndex: window.pageIndex
        windowWidth: window.width
        onSelected: index => window.pageIndex = index
    }

    StackLayout {
        anchors.fill: parent
        anchors.margins: Theme.pagePadding
        currentIndex: window.pageIndex

        RoomPage {}
        LobbyPage {}
        NetworkPage {}
        AboutPage {}
    }

    MD.SnakeView {
        id: notifications
        z: 1000
        width: Math.min(parent.width - 32, 460)
        height: parent.height
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        bottomToTop: true
    }
}
