import QtQuick
import QtQuick.Layouts
import Qcm.Material as MD

MD.Pane {
    id: root

    default property alias cardContent: contentColumn.data
    property string title: ""
    property string subtitle: ""

    padding: 18
    radius: MD.Token.shape.corner.large
    elevation: MD.Token.elevation.level1
    backgroundColor: Theme.surfaceContainerLow

    contentItem: ColumnLayout {
        id: contentColumn
        spacing: Theme.sectionSpacing

        ColumnLayout {
            visible: root.title.length > 0 || root.subtitle.length > 0
            Layout.fillWidth: true
            spacing: 3

            MD.Text {
                visible: root.title.length > 0
                text: root.title
                color: Theme.foreground
                typescale: MD.Token.typescale.title_large
                prominent: true
            }

            MD.Text {
                visible: root.subtitle.length > 0
                Layout.fillWidth: true
                text: root.subtitle
                color: Theme.foregroundMuted
                typescale: MD.Token.typescale.body_small
                wrapMode: Text.WordWrap
            }
        }
    }
}
