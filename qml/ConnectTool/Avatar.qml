import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    property url source
    property string name: ""
    property bool online: false

    implicitWidth: 42
    implicitHeight: 42
    radius: width / 2
    color: Theme.surfaceRaised
    border.color: online ? Theme.success : Theme.border
    border.width: 2
    clip: true

    Image {
        id: avatarImage
        anchors.fill: parent
        anchors.margins: 2
        source: root.source
        visible: status === Image.Ready
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        smooth: true
    }

    Label {
        anchors.centerIn: parent
        text: root.name.length > 0 ? root.name.charAt(0).toUpperCase() : "?"
        visible: !avatarImage.visible
        color: Theme.textMuted
        font.pixelSize: 16
        font.weight: Font.DemiBold
    }
}
