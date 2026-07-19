pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Qcm.Material as MD

MD.Drawer {
    id: root

    required property var entries
    property int currentIndex: 0
    property real windowWidth: 1080
    signal selected(int index)

    edge: Qt.LeftEdge
    width: Math.min(root.windowWidth * 0.72, 336)
    modal: true
    interactive: true

    contentItem: ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 6

        MD.Pane {
            Layout.fillWidth: true
            Layout.topMargin: 4
            padding: 14
            radius: MD.Token.shape.corner.large
            backgroundColor: Theme.primaryContainer

            contentItem: RowLayout {
                spacing: 12

                Image {
                    source: Qt.resolvedUrl("AppIcon.png")
                    sourceSize.width: 128
                    sourceSize.height: 128
                    Layout.preferredWidth: 42
                    Layout.preferredHeight: 42
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                }

                MD.Text {
                    Layout.fillWidth: true
                    text: "ConnectTool"
                    color: Theme.primaryContainerInk
                    typescale: MD.Token.typescale.title_medium
                    prominent: true
                }
            }
        }

        MD.DrawerSubheader {
            Layout.fillWidth: true
            text: qsTr("导航")
        }

        Repeater {
            model: root.entries

            delegate: MD.DrawerItem {
                id: navigationItem
                required property int index
                required property var modelData

                Layout.fillWidth: true
                text: navigationItem.modelData.title
                icon.name: navigationItem.modelData.icon
                checked: root.currentIndex === navigationItem.index
                Accessible.description: navigationItem.modelData.subtitle
                onClicked: {
                    root.selected(navigationItem.index)
                    root.close()
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
