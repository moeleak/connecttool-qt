import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Control {
    id: root

    default property alias contentData: contentColumn.data
    property string title: ""
    property string subtitle: ""

    padding: 18

    background: Rectangle {
        radius: Theme.radius
        color: Theme.surface
        border.color: Theme.border
        border.width: 1
    }

    contentItem: ColumnLayout {
        id: contentColumn
        spacing: Theme.spacing

        ColumnLayout {
            visible: root.title.length > 0 || root.subtitle.length > 0
            spacing: 3
            Layout.fillWidth: true

            Label {
                visible: root.title.length > 0
                text: root.title
                color: Theme.text
                font.pixelSize: 17
                font.weight: Font.DemiBold
            }

            Label {
                visible: root.subtitle.length > 0
                text: root.subtitle
                color: Theme.textMuted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }
}
