pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Qt.labs.platform as Platform
import Qcm.Material as MD
import ConnectTool

Item {
    id: root

    property bool proxy: false

    Platform.FileDialog {
        id: downloadDialog

        title: qsTr("选择更新保存位置")
        fileMode: Platform.FileDialog.SaveFile
        nameFilters: [qsTr("压缩包 (*.zip)"), qsTr("所有文件 (*)")]
        onAccepted: {
            const selected = file ? file.toString()
                                  : (files.length > 0 ? files[0].toString() : "")
            if (selected.length > 0)
                App.updater.download(root.proxy, selected)
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacing

        AppCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: root.width * 0.62
            title: qsTr("关于 ConnectTool")
            subtitle: qsTr("使用 Steam P2P，为好友建立轻量的端口转发与虚拟组网连接。")

            MD.Pane {
                Layout.fillWidth: true
                padding: 16
                radius: 14
                elevation: 0
                backgroundColor: Theme.surfaceContainerHigh

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        MD.Label {
                            Layout.fillWidth: true
                            text: qsTr("当前版本：%1").arg(App.version)
                            color: Theme.foreground
                            typescale: MD.Token.typescale.title_small
                            prominent: true
                        }

                        StatusPill {
                            text: App.updater.updateAvailable
                                  ? qsTr("有新版本")
                                  : qsTr("已就绪")
                            accent: App.updater.updateAvailable
                                    ? Theme.warning
                                    : Theme.success
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        MD.ComboBox {
                            Layout.preferredWidth: 124
                            model: [qsTr("GitHub"), qsTr("国内中转")]
                            onActivated: root.proxy = currentIndex === 1
                        }

                        MD.Button {
                            Layout.fillWidth: true
                            text: App.updater.checking
                                  ? qsTr("检查中…")
                                  : qsTr("检查更新")
                            icon.name: MD.Token.icon.refresh
                            mdState.type: MD.Enum.BtFilled
                            enabled: !App.updater.checking
                            onClicked: App.updater.check(root.proxy)
                        }

                        MD.Button {
                            Layout.fillWidth: true
                            text: App.updater.downloading
                                  ? qsTr("下载中…")
                                  : qsTr("下载更新")
                            icon.name: MD.Token.icon.download
                            mdState.type: MD.Enum.BtFilledTonal
                            enabled: App.updater.updateAvailable
                                     && !App.updater.downloading
                            onClicked: downloadDialog.open()
                        }

                        MD.Button {
                            visible: App.updater.releasePage.length > 0
                            text: qsTr("发布页")
                            icon.name: MD.Token.icon.open_in_new
                            mdState.type: MD.Enum.BtText
                            onClicked: Qt.openUrlExternally(App.updater.releasePage)
                        }
                    }

                    MD.Label {
                        Layout.fillWidth: true
                        text: App.updater.statusText
                        color: Theme.foregroundMuted
                        typescale: MD.Token.typescale.body_small
                        wrapMode: Text.WordWrap
                        elide: Text.ElideNone
                    }

                    MD.LinearIndicator {
                        Layout.fillWidth: true
                        visible: App.updater.downloading || App.updater.progress > 0
                        from: 0
                        to: 1
                        value: App.updater.progress
                        indeterminate: false
                        color: Theme.primary
                        trackColor: Theme.alpha(Theme.primary, 0.18)
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true

                MD.Label {
                    Layout.fillWidth: true
                    text: qsTr("社区与项目")
                    color: Theme.foreground
                    typescale: MD.Token.typescale.title_small
                    prominent: true
                }

                MD.Icon {
                    name: MD.Token.icon.link
                    size: 20
                    color: Theme.primary
                }
            }

            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: 2
                columnSpacing: 8
                rowSpacing: 4

                LinkCard {
                    Layout.fillWidth: true
                    title: qsTr("ConnectTool 总群")
                    subtitle: "616325806"
                    glyph: "QQ"
                    url: "https://qm.qq.com/q/hgAZJYasbS"
                }

                LinkCard {
                    Layout.fillWidth: true
                    title: qsTr("Qt 交流群")
                    subtitle: "902943118"
                    glyph: "QQ"
                    url: "https://qm.qq.com/q/aXlMOBDbJm"
                }

                LinkCard {
                    Layout.fillWidth: true
                    title: qsTr("本项目源码")
                    subtitle: "moeleak/connecttool-qt"
                    glyph: "GH"
                    url: "https://github.com/moeleak/connecttool-qt"
                    accent: Theme.secondary
                }

                LinkCard {
                    Layout.fillWidth: true
                    title: qsTr("原项目源码")
                    subtitle: "Ayndpa/ConnectTool"
                    glyph: "GH"
                    url: "https://github.com/Ayndpa/ConnectTool"
                    accent: Theme.secondary
                }

                LinkCard {
                    Layout.fillWidth: true
                    Layout.columnSpan: 2
                    title: qsTr("Discord 社区")
                    subtitle: "discord.gg/PeRutfW6NA"
                    glyph: "DS"
                    url: "https://discord.gg/PeRutfW6NA"
                    accent: "#b8c4ff"
                }
            }
        }

        AppCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: root.width * 0.38
            title: qsTr("致谢与说明")
            subtitle: qsTr("感谢参与开发、测试与维护的每一位贡献者。")

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                MD.Icon {
                    name: MD.Token.icon.code
                    size: 24
                    color: Theme.primary
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    MD.Label {
                        text: qsTr("开发")
                        color: Theme.primary
                        typescale: MD.Token.typescale.label_medium
                        prominent: true
                    }

                    MD.Label {
                        Layout.fillWidth: true
                        text: "Ayndpa, MoeLeak"
                        color: Theme.foreground
                        typescale: MD.Token.typescale.body_medium
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                MD.Icon {
                    name: MD.Token.icon.groups
                    size: 24
                    color: Theme.primary
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    MD.Label {
                        text: qsTr("测试")
                        color: Theme.primary
                        typescale: MD.Token.typescale.label_medium
                        prominent: true
                    }

                    MD.Label {
                        Layout.fillWidth: true
                        text: "旺仔大乔, 梦于枫岚, 虈請, MoeLeak"
                        color: Theme.foreground
                        typescale: MD.Token.typescale.body_medium
                        wrapMode: Text.WordWrap
                        elide: Text.ElideNone
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
