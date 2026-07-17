import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform as Platform
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
            const selected = file ? file.toString() : (files.length > 0 ? files[0].toString() : "")
            if (selected.length > 0)
                App.updater.download(root.proxy, selected)
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: Theme.spacing

            ColumnLayout {
                Label { text: qsTr("关于 ConnectTool"); color: Theme.text; font.pixelSize: 24; font.weight: Font.DemiBold }
                Label { text: qsTr("使用 Steam P2P 完成 TCP 转发与跨平台 TUN 组网。"); color: Theme.textMuted; font.pixelSize: 12 }
            }

            AppCard {
                Layout.fillWidth: true
                title: qsTr("版本与更新")
                subtitle: qsTr("当前版本 %1 · Qt 6.11 · C++23").arg(App.version)

                RowLayout {
                    Layout.fillWidth: true
                    ComboBox {
                        model: [qsTr("GitHub"), qsTr("国内中转")]
                        onActivated: root.proxy = currentIndex === 1
                    }
                    Button {
                        text: App.updater.checking ? qsTr("检查中…") : qsTr("检查更新")
                        enabled: !App.updater.checking
                        onClicked: App.updater.check(root.proxy)
                    }
                    Button {
                        text: App.updater.downloading ? qsTr("下载中…") : qsTr("下载更新")
                        enabled: App.updater.updateAvailable && !App.updater.downloading
                        onClicked: downloadDialog.open()
                    }
                    Button {
                        visible: App.updater.releasePage.length > 0
                        text: qsTr("发布页面")
                        onClicked: Qt.openUrlExternally(App.updater.releasePage)
                    }
                    Item { Layout.fillWidth: true }
                    StatusPill {
                        text: App.updater.updateAvailable ? qsTr("有新版本") : qsTr("已就绪")
                        accent: App.updater.updateAvailable ? Theme.warning : Theme.success
                    }
                }
                Label { Layout.fillWidth: true; text: App.updater.statusText; color: Theme.textMuted; wrapMode: Text.Wrap }
                ProgressBar {
                    Layout.fillWidth: true
                    visible: App.updater.downloading || App.updater.progress > 0
                    from: 0
                    to: 1
                    value: App.updater.progress
                }
            }

            AppCard {
                Layout.fillWidth: true
                title: qsTr("项目说明")
                Label {
                    Layout.fillWidth: true
                    text: qsTr("本工具会使用 Steam 网络服务建立点对点连接。请遵守 Steam 订户协议，并自行评估账号与网络风险。")
                    color: Theme.text
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Button { text: qsTr("本项目 GitHub ↗"); onClicked: Qt.openUrlExternally("https://github.com/moeleak/connecttool-qt") }
                    Button { text: qsTr("原项目 GitHub ↗"); onClicked: Qt.openUrlExternally("https://github.com/Ayndpa/ConnectTool") }
                    Button { text: qsTr("Discord 社区 ↗"); onClicked: Qt.openUrlExternally("https://discord.gg/PeRutfW6NA") }
                    Item { Layout.fillWidth: true }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                AppCard {
                    Layout.fillWidth: true
                    title: qsTr("ConnectTool 总群")
                    subtitle: "616325806"
                    Button { text: qsTr("打开群链接"); onClicked: Qt.openUrlExternally("https://qm.qq.com/q/hgAZJYasbS") }
                }
                AppCard {
                    Layout.fillWidth: true
                    title: qsTr("connecttool-qt 交流群")
                    subtitle: "902943118"
                    Button { text: qsTr("打开群链接"); onClicked: Qt.openUrlExternally("https://qm.qq.com/q/aXlMOBDbJm") }
                }
            }

            AppCard {
                Layout.fillWidth: true
                title: qsTr("贡献者")

                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("开发"); color: Theme.textMuted }
                    Label { text: "Ayndpa, MoeLeak"; color: Theme.text }
                    Item { Layout.fillWidth: true }
                    Label { text: qsTr("测试"); color: Theme.textMuted }
                    Label { text: "旺仔大乔, 梦于枫岚, 虈請, MoeLeak"; color: Theme.text; wrapMode: Text.Wrap }
                }
            }
        }
    }
}
