pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Qcm.Material as MD
import ConnectTool

Item {
    id: root

    readonly property bool isWindows: Qt.platform.os === "windows"

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacing

        AppCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: root.width * 0.58
            title: qsTr("中继节点延迟")
            subtitle: qsTr("展示当前 Steam 环境下的中继 POP 往返延迟估计值。")

            MD.Pane {
                Layout.fillWidth: true
                implicitHeight: 126
                padding: 18
                radius: 14
                elevation: 0
                backgroundColor: Theme.surfaceContainerHigh

                RowLayout {
                    anchors.fill: parent
                    spacing: 20

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        MD.Label {
                            text: App.network.relayPing >= 0
                                  ? qsTr("%1 ms").arg(App.network.relayPing)
                                  : qsTr("等待探测")
                            color: Theme.primary
                            typescale: MD.Token.typescale.display_small
                            prominent: true
                        }

                        MD.Label {
                            Layout.fillWidth: true
                            text: App.network.relayPing >= 0
                                  ? qsTr("当前最优中继的双向往返估算")
                                  : qsTr("Steam 网络就绪后将自动开始测速")
                            color: Theme.foregroundMuted
                            typescale: MD.Token.typescale.body_small
                            elide: Text.ElideNone
                        }
                    }

                    StatusPill {
                        text: App.session.steamReady
                              ? qsTr("每 2 秒自动刷新")
                              : qsTr("Steam 未就绪")
                        accent: App.session.steamReady ? Theme.success : Theme.danger
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                MD.Label {
                    Layout.fillWidth: true
                    text: qsTr("可用中继节点")
                    color: Theme.foreground
                    typescale: MD.Token.typescale.title_small
                    prominent: true
                }

                StatusPill {
                    text: qsTr("%1 个 POP").arg(relayList.count)
                    accent: Theme.secondary
                }
            }

            MD.VerticalListView {
                id: relayList

                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                model: App.network.relayPops

                delegate: MD.ItemDelegate {
                    id: relayDelegate

                    required property var modelData
                    readonly property color pingColor: {
                        const ping = relayDelegate.modelData.ping
                        if (ping < 0)
                            return Theme.foregroundMuted
                        if (ping <= 100)
                            return Theme.success
                        if (ping <= 200)
                            return Theme.warning
                        return Theme.danger
                    }

                    width: relayList.width
                    implicitHeight: 64
                    leftPadding: 14
                    rightPadding: 14
                    hoverEnabled: true
                    highlighted: hovered || down

                    background: Rectangle {
                        radius: 12
                        color: relayDelegate.highlighted
                               ? Theme.surfaceContainerHigh
                               : Theme.surfaceContainer
                        border.width: 1
                        border.color: Theme.alpha(Theme.outline, relayDelegate.highlighted ? 0.55 : 0.30)

                        Behavior on color {
                            ColorAnimation { duration: 120 }
                        }
                    }

                    contentItem: RowLayout {
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            MD.Label {
                                Layout.fillWidth: true
                                text: relayDelegate.modelData.name || "–"
                                color: Theme.foreground
                                typescale: MD.Token.typescale.title_small
                                prominent: true
                                elide: Text.ElideRight
                            }

                            MD.Label {
                                Layout.fillWidth: true
                                text: relayDelegate.modelData.via
                                      ? qsTr("经由 %1").arg(relayDelegate.modelData.via)
                                      : qsTr("直连探测")
                                color: Theme.foregroundMuted
                                typescale: MD.Token.typescale.body_small
                                elide: Text.ElideRight
                            }
                        }

                        ColumnLayout {
                            spacing: 0

                            MD.Label {
                                Layout.alignment: Qt.AlignRight
                                text: relayDelegate.modelData.ping >= 0
                                      ? qsTr("%1 ms").arg(relayDelegate.modelData.ping)
                                      : qsTr("不可达")
                                color: relayDelegate.pingColor
                                typescale: MD.Token.typescale.title_medium
                                prominent: true
                            }

                            MD.Label {
                                Layout.alignment: Qt.AlignRight
                                text: qsTr("往返估计")
                                color: Theme.foregroundMuted
                                typescale: MD.Token.typescale.label_small
                            }
                        }
                    }
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    visible: relayList.count === 0
                    spacing: 8

                    MD.Icon {
                        Layout.alignment: Qt.AlignHCenter
                        name: MD.Token.icon.network_ping
                        size: 36
                        color: Theme.primary
                    }

                    MD.Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("正在等待中继节点数据")
                        color: Theme.foreground
                        typescale: MD.Token.typescale.title_small
                        prominent: true
                    }

                    MD.Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("保持 Steam 运行，节点会自动出现")
                        color: Theme.foregroundMuted
                        typescale: MD.Token.typescale.body_small
                    }
                }
            }
        }

        AppCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: root.width * 0.42
            title: qsTr("Steam 切换")
            subtitle: root.isWindows
                      ? qsTr("为 Steam.exe 添加或移除 -steamchina 启动参数。")
                      : qsTr("仅 Windows 支持自动切换，当前平台按钮已禁用。")

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                MD.Button {
                    Layout.fillWidth: true
                    text: qsTr("国际版启动")
                    icon.name: MD.Token.icon["public"]
                    mdState.type: MD.Enum.BtFilled
                    enabled: root.isWindows
                    onClicked: App.launchSteam(false)
                }

                MD.Button {
                    Layout.fillWidth: true
                    text: qsTr("蒸汽平台")
                    icon.name: MD.Token.icon.store
                    mdState.type: MD.Enum.BtFilledTonal
                    enabled: root.isWindows
                    onClicked: App.launchSteam(true)
                }
            }

            MD.Label {
                visible: !root.isWindows
                Layout.fillWidth: true
                text: qsTr("当前平台不支持自动切换。")
                color: Theme.foregroundMuted
                typescale: MD.Token.typescale.body_small
                elide: Text.ElideNone
            }

            MD.Divider {
                Layout.fillWidth: true
                Layout.topMargin: 4
                color: Theme.alpha(Theme.outline, 0.42)
            }

            MD.Label {
                text: qsTr("当前连接")
                color: Theme.foreground
                typescale: MD.Token.typescale.title_medium
                prominent: true
            }

            RowLayout {
                Layout.fillWidth: true

                MD.Label {
                    text: qsTr("连接模式")
                    color: Theme.foregroundMuted
                    typescale: MD.Token.typescale.body_medium
                }

                Item { Layout.fillWidth: true }

                StatusPill {
                    text: App.session.mode === 1
                          ? qsTr("TUN 虚拟组网")
                          : qsTr("TCP 端口转发")
                    accent: Theme.secondary
                }
            }

            RowLayout {
                Layout.fillWidth: true

                MD.Label {
                    text: qsTr("Steam 状态")
                    color: Theme.foregroundMuted
                    typescale: MD.Token.typescale.body_medium
                }

                Item { Layout.fillWidth: true }

                StatusPill {
                    text: App.session.steamReady ? qsTr("已就绪") : qsTr("未登录")
                    accent: App.session.steamReady ? Theme.success : Theme.danger
                }
            }

            MD.Label {
                Layout.fillWidth: true
                text: App.session.status
                color: Theme.foregroundMuted
                typescale: MD.Token.typescale.body_small
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Rectangle {
                    implicitWidth: 8
                    implicitHeight: 8
                    radius: 4
                    color: App.session.steamReady ? Theme.success : Theme.danger
                }

                MD.Label {
                    Layout.fillWidth: true
                    text: App.session.steamReady
                          ? qsTr("SteamNetworkingSockets 已就绪")
                          : qsTr("等待 Steam 登录")
                    color: Theme.foregroundMuted
                    typescale: MD.Token.typescale.label_small
                }
            }
        }
    }
}
