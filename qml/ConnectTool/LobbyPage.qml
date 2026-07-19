pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Qcm.Material as MD
import ConnectTool

Item {
    id: root

    onVisibleChanged: {
        if (visible)
            App.lobbies.refresh()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.sectionSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.compactSpacing

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1

                MD.Text {
                    text: qsTr("公开大厅")
                    color: Theme.foreground
                    typescale: MD.Token.typescale.headline_small
                    prominent: true
                }

                MD.Text {
                    text: qsTr("发现公开房间，并通过 Steam P2P 直接加入")
                    color: Theme.foregroundMuted
                    typescale: MD.Token.typescale.body_small
                }
            }

            StatusPill {
                text: qsTr("%1 个房间").arg(lobbyList.count)
                accent: Theme.secondary
            }

            MD.BusyIndicator {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                indicatorSize: 24
                showDelay: 120
                minHideDelay: 240
                running: App.lobbies.refreshing
            }

            MD.Button {
                text: App.lobbies.refreshing ? qsTr("刷新中") : qsTr("刷新")
                icon.name: MD.Token.icon.refresh
                mdState.type: MD.Enum.BtFilledTonal
                enabled: !App.lobbies.refreshing && App.session.steamReady
                onClicked: App.lobbies.refresh()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            MD.TextField {
                Layout.fillWidth: true
                Layout.minimumWidth: 360
                type: MD.Enum.TextFieldFilled
                mdState.dense: true
                leadingIcon: MD.Token.icon.search
                placeholderText: qsTr("搜索房间名、房主或房间 ID")
                text: App.lobbies.filter
                selectByMouse: true
                onTextEdited: App.lobbies.filter = text
            }

            MD.ComboBox {
                Layout.preferredWidth: 176
                model: [qsTr("成员最多"), qsTr("名称排序"), qsTr("延迟最低")]
                currentIndex: App.lobbies.sortMode
                type: MD.Enum.TextFieldOutlined
                onActivated: App.lobbies.sortMode = currentIndex
            }
        }

        AppCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 14
            title: qsTr("可见房间")
            subtitle: App.session.steamReady
                      ? qsTr("选择房间加入连接；房间 ID 可随时复制分享。")
                      : qsTr("Steam 尚未就绪，登录后即可刷新和加入大厅。")

            ListView {
                id: lobbyList

                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 7
                model: App.lobbies.model
                boundsBehavior: Flickable.StopAtBounds
                T.ScrollBar.vertical: MD.ScrollBar {}

                delegate: MD.Pane {
                    id: lobbyDelegate

                    required property string lobbyId
                    required property string name
                    required property string hostName
                    required property string hostId
                    required property int members
                    required property int ping

                    readonly property bool current: App.session.lobbyId === lobbyDelegate.lobbyId
                    readonly property color pingColor: {
                        if (lobbyDelegate.ping < 0)
                            return Theme.foregroundMuted
                        if (lobbyDelegate.ping <= 100)
                            return Theme.success
                        if (lobbyDelegate.ping <= 200)
                            return Theme.warning
                        return Theme.danger
                    }

                    width: lobbyList.width
                    implicitHeight: 72
                    horizontalPadding: 12
                    radius: MD.Token.shape.corner.medium
                    elevation: MD.Token.elevation.level0
                    backgroundColor: lobbyDelegate.current
                                     ? Theme.alpha(Theme.primary, 0.16)
                                     : (lobbyHover.hovered
                                        ? Theme.surfaceContainerHigh
                                        : Theme.surfaceContainer)

                    HoverHandler { id: lobbyHover }

                    contentItem: RowLayout {
                        spacing: 11

                        MD.Pane {
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            radius: MD.Token.shape.corner.full
                            backgroundColor: lobbyDelegate.current
                                             ? Theme.primaryContainer
                                             : Theme.alpha(Theme.secondary, 0.13)

                            contentItem: MD.Icon {
                                name: lobbyDelegate.current
                                      ? MD.Token.icon.check_circle
                                      : MD.Token.icon.meeting_room
                                size: 21
                                color: lobbyDelegate.current
                                       ? Theme.primaryContainerInk
                                       : Theme.secondary
                                fill: lobbyDelegate.current
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                MD.Label {
                                    Layout.fillWidth: true
                                    text: lobbyDelegate.name.length > 0
                                          ? lobbyDelegate.name
                                          : qsTr("未命名房间")
                                    color: Theme.foreground
                                    typescale: MD.Token.typescale.title_small
                                    prominent: true
                                    elide: Text.ElideRight
                                }

                                StatusPill {
                                    visible: lobbyDelegate.current
                                    text: qsTr("当前房间")
                                    accent: Theme.primary
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                MD.Label {
                                    Layout.fillWidth: true
                                    text: qsTr("房主 %1").arg(
                                              lobbyDelegate.hostName.length > 0
                                              ? lobbyDelegate.hostName
                                              : lobbyDelegate.hostId)
                                    color: Theme.foregroundMuted
                                    typescale: MD.Token.typescale.body_small
                                    elide: Text.ElideRight
                                }

                                MD.Label {
                                    Layout.maximumWidth: 250
                                    text: qsTr("房间 ID：%1").arg(lobbyDelegate.lobbyId)
                                    color: Theme.foregroundMuted
                                    typescale: MD.Token.typescale.label_small
                                    elide: Text.ElideMiddle
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.preferredWidth: 104
                            spacing: 1

                            RowLayout {
                                Layout.alignment: Qt.AlignRight
                                spacing: 5

                                MD.Icon {
                                    name: MD.Token.icon.group
                                    size: 16
                                    color: Theme.secondary
                                }

                                MD.Label {
                                    text: qsTr("%1 人").arg(lobbyDelegate.members)
                                    color: Theme.foreground
                                    typescale: MD.Token.typescale.label_large
                                    prominent: true
                                }
                            }

                            MD.Label {
                                Layout.alignment: Qt.AlignRight
                                text: lobbyDelegate.ping >= 0
                                      ? qsTr("%1 ms").arg(lobbyDelegate.ping)
                                      : qsTr("延迟未知")
                                color: lobbyDelegate.pingColor
                                typescale: MD.Token.typescale.label_small
                            }
                        }

                        MD.Button {
                            Layout.preferredWidth: 92
                            text: lobbyDelegate.current ? qsTr("已加入") : qsTr("加入")
                            icon.name: lobbyDelegate.current
                                       ? MD.Token.icon.check
                                       : MD.Token.icon.login
                            mdState.type: lobbyDelegate.current
                                          ? MD.Enum.BtFilledTonal
                                          : MD.Enum.BtFilled
                            enabled: !lobbyDelegate.current && App.session.steamReady
                            onClicked: App.lobbies.join(lobbyDelegate.lobbyId)
                        }

                        MD.IconButton {
                            id: copyButton

                            icon.name: MD.Token.icon.content_copy
                            mdState.type: MD.Enum.IBtStandard
                            mdState.size: MD.Enum.S
                            mdState.widthMode: MD.Enum.NarrowWidth
                            Accessible.name: qsTr("复制房间 ID")
                            onClicked: App.copy(lobbyDelegate.lobbyId)

                            MD.ToolTip {
                                visible: copyButton.hovered
                                text: qsTr("复制房间 ID")
                            }
                        }
                    }
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    visible: lobbyList.count === 0 && !App.lobbies.refreshing
                    spacing: 8

                    MD.Pane {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 58
                        Layout.preferredHeight: 58
                        radius: MD.Token.shape.corner.full
                        backgroundColor: Theme.alpha(Theme.primary, 0.13)

                        contentItem: MD.Icon {
                            name: MD.Token.icon.travel_explore
                            size: 30
                            color: Theme.primary
                        }
                    }

                    MD.Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: App.lobbies.filter.length > 0
                              ? qsTr("没有匹配的公开房间")
                              : qsTr("暂时没有可见房间")
                        color: Theme.foreground
                        typescale: MD.Token.typescale.title_medium
                        prominent: true
                    }

                    MD.Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: App.lobbies.filter.length > 0
                              ? qsTr("尝试更换关键词或清空搜索条件")
                              : qsTr("刷新大厅，或在房间页创建一个公开房间")
                        color: Theme.foregroundMuted
                        typescale: MD.Token.typescale.body_small
                    }
                }
            }
        }
    }
}
