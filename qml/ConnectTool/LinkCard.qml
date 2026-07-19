import QtQuick
import QtQuick.Layouts
import Qcm.Material as MD

MD.Card {
    id: root

    property string title: ""
    property string subtitle: ""
    property string glyph: "↗"
    property string url: ""
    property color accent: Theme.primary

    implicitHeight: 64
    enabled: root.url.length > 0
    hoverEnabled: false
    type: MD.Enum.CardOutlined
    Accessible.name: root.title
    onClicked: Qt.openUrlExternally(root.url)

    mdState.backgroundColor: linkHover.hovered
                             ? Theme.alpha(root.accent, 0.08)
                             : Theme.surfaceContainerLow
    mdState.outlineColor: Theme.alpha(root.accent, 0.38)

    contentItem: RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 12

        Rectangle {
            implicitWidth: 36
            implicitHeight: 36
            radius: 18
            color: Theme.alpha(root.accent, 0.14)

            MD.Text {
                anchors.centerIn: parent
                text: root.glyph
                color: root.accent
                typescale: MD.Token.typescale.label_medium
                prominent: true
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1

            MD.Text {
                Layout.fillWidth: true
                text: root.title
                color: Theme.foreground
                typescale: MD.Token.typescale.body_medium
                prominent: true
                elide: Text.ElideRight
            }
            MD.Text {
                Layout.fillWidth: true
                text: root.subtitle
                color: Theme.foregroundMuted
                typescale: MD.Token.typescale.body_small
                elide: Text.ElideRight
            }
        }

        MD.Icon {
            name: MD.Token.icon.open_in_new
            color: root.accent
            size: 20
        }
    }

    HoverHandler { id: linkHover }
}
