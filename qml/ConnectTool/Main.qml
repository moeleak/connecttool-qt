pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ConnectTool

ApplicationWindow {
    id: window

    width: 1180
    height: 760
    minimumWidth: 980
    minimumHeight: 660
    visible: true
    title: qsTr("ConnectTool · Steam P2P")

    Material.theme: Material.Dark
    Material.primary: Theme.primary
    Material.accent: Theme.secondary

    property int pageIndex: 0
    readonly property var navigation: [
        { icon: "⌂", title: qsTr("房间"), subtitle: qsTr("连接、聊天与成员") },
        { icon: "◎", title: qsTr("大厅"), subtitle: qsTr("发现公开房间") },
        { icon: "◇", title: qsTr("节点"), subtitle: qsTr("中继与网络状态") },
        { icon: "i", title: qsTr("关于"), subtitle: qsTr("版本、更新与社区") }
    ]

    background: Rectangle {
        color: Theme.background
        gradient: Gradient {
            GradientStop { position: 0; color: "#0c1524" }
            GradientStop { position: 1; color: Theme.background }
        }
    }

    Dialog {
        id: adminDialog
        anchors.centerIn: parent
        title: qsTr("需要管理员权限")
        modal: true
        standardButtons: Dialog.Ok
        width: 400

        Label {
            width: parent.width
            text: qsTr("TUN 模式需要创建和配置虚拟网卡。请允许管理员权限后重试。")
            color: Theme.text
            wrapMode: Text.Wrap
        }
    }

    Connections {
        target: App.session
        function onAdminPrivilegesRequired() {
            adminDialog.open()
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 14

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 238
            radius: 18
            color: Theme.surface
            border.color: Theme.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Rectangle {
                        implicitWidth: 46
                        implicitHeight: 46
                        radius: 14
                        gradient: Gradient {
                            GradientStop { position: 0; color: Theme.primary }
                            GradientStop { position: 1; color: Theme.secondary }
                        }
                        Label {
                            anchors.centerIn: parent
                            text: "CT"
                            color: "#07111c"
                            font.pixelSize: 16
                            font.weight: Font.Bold
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Label {
                            text: "ConnectTool"
                            color: Theme.text
                            font.pixelSize: 17
                            font.weight: Font.DemiBold
                        }
                        Label {
                            text: "Steam P2P"
                            color: Theme.textMuted
                            font.pixelSize: 11
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: Theme.border
                }

                Repeater {
                    model: window.navigation

                    delegate: Button {
                        id: navigationButton
                        required property int index
                        required property var modelData

                        Layout.fillWidth: true
                        implicitHeight: 58
                        flat: true
                        hoverEnabled: true
                        onClicked: window.pageIndex = navigationButton.index

                        background: Rectangle {
                            radius: 12
                            color: window.pageIndex === navigationButton.index
                                   ? "#173530"
                                   : (navigationButton.hovered ? Theme.surfaceHover : "transparent")
                            border.color: window.pageIndex === navigationButton.index
                                          ? Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.5)
                                          : "transparent"
                        }

                        contentItem: RowLayout {
                            spacing: 11
                            Label {
                                text: navigationButton.modelData.icon
                                color: window.pageIndex === navigationButton.index ? Theme.primary : Theme.textMuted
                                font.pixelSize: 18
                                Layout.preferredWidth: 24
                                horizontalAlignment: Text.AlignHCenter
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Label {
                                    text: navigationButton.modelData.title
                                    color: Theme.text
                                    font.pixelSize: 14
                                    font.weight: window.pageIndex === navigationButton.index ? Font.DemiBold : Font.Normal
                                }
                                Label {
                                    text: navigationButton.modelData.subtitle
                                    color: Theme.textMuted
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: statusColumn.implicitHeight + 20
                    radius: 12
                    color: Theme.surfaceRaised
                    border.color: Theme.border

                    ColumnLayout {
                        id: statusColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 10
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            Rectangle {
                                implicitWidth: 9
                                implicitHeight: 9
                                radius: 5
                                color: App.session.steamReady ? Theme.success : Theme.warning
                            }
                            Label {
                                Layout.fillWidth: true
                                text: App.session.steamReady ? qsTr("Steam 在线") : qsTr("等待 Steam")
                                color: Theme.text
                                font.pixelSize: 12
                            }
                            Label {
                                text: "v" + App.version
                                color: Theme.textMuted
                                font.pixelSize: 10
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: App.session.status
                            color: Theme.textMuted
                            font.pixelSize: 10
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: window.pageIndex

            RoomPage {}
            LobbyPage {}
            NetworkPage {}
            AboutPage {}
        }
    }
}
