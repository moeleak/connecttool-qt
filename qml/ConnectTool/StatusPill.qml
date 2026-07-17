import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    property string text: ""
    property color accent: Theme.primary

    implicitWidth: label.implicitWidth + 18
    implicitHeight: 26
    radius: height / 2
    color: Qt.rgba(accent.r, accent.g, accent.b, 0.12)
    border.color: Qt.rgba(accent.r, accent.g, accent.b, 0.55)

    Label {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.accent
        font.pixelSize: 11
        font.weight: Font.DemiBold
    }
}
