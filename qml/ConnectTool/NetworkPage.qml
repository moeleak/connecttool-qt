pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ConnectTool

Item {
    id: root

    readonly property bool isWindows: Qt.platform.os === "windows"

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacing

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Label { text: qsTr("网络节点"); color: Theme.text; font.pixelSize: 24; font.weight: Font.DemiBold }
                Label { text: qsTr("Steam 中继可达性与当前连接信息。"); color: Theme.textMuted; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("启动 Steam"); enabled: root.isWindows; onClicked: App.launchSteam(false) }
            Button { text: qsTr("启动蒸汽平台"); enabled: root.isWindows; onClicked: App.launchSteam(true) }
        }

        RowLayout {
            Layout.fillWidth: true
            AppCard {
                Layout.fillWidth: true
                title: qsTr("最优中继往返")
                Label {
                    text: App.network.relayPing >= 0 ? App.network.relayPing + " ms" : qsTr("等待探测")
                    color: Theme.primary
                    font.pixelSize: 30
                    font.weight: Font.DemiBold
                }
            }
            AppCard {
                Layout.fillWidth: true
                title: qsTr("当前模式")
                Label { text: App.session.mode === 1 ? qsTr("TUN 虚拟组网") : qsTr("TCP 端口转发"); color: Theme.secondary; font.pixelSize: 20 }
            }
            AppCard {
                Layout.fillWidth: true
                title: qsTr("本地状态")
                Label { text: App.session.status; color: Theme.text; font.pixelSize: 14; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }

        AppCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: qsTr("可用 Steam POP 节点")
            subtitle: qsTr("按估算往返延迟排序；经由字段表示探测路径。")

            ListView {
                id: relayList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 8
                model: App.network.relayPops
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    id: relayDelegate
                    required property var modelData
                    width: relayList.width
                    implicitHeight: 60
                    radius: 10
                    color: Theme.surfaceRaised
                    border.color: Theme.border

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 11
                        Label { text: relayDelegate.modelData.name || "–"; color: Theme.text; font.pixelSize: 15; Layout.fillWidth: true }
                        Label { text: relayDelegate.modelData.via ? qsTr("经由 %1").arg(relayDelegate.modelData.via) : ""; color: Theme.textMuted }
                        StatusPill {
                            text: relayDelegate.modelData.ping >= 0
                                  ? relayDelegate.modelData.ping + " ms" : qsTr("不可达")
                            accent: relayDelegate.modelData.ping < 0
                                    ? Theme.textMuted
                                    : (relayDelegate.modelData.ping <= 100
                                       ? Theme.success
                                       : (relayDelegate.modelData.ping <= 200
                                          ? Theme.warning : Theme.danger))
                        }
                    }
                }

                Label { anchors.centerIn: parent; visible: relayList.count === 0; text: qsTr("Steam 运行后将自动探测节点"); color: Theme.textMuted }
            }
        }
    }
}
