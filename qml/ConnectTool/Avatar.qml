import QtQuick
import Qcm.Material as MD

Item {
    id: root

    property url source
    property string name: ""
    property bool online: false

    implicitWidth: 42
    implicitHeight: 42

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Theme.surfaceContainerHigh
        border.color: root.online ? Theme.success : Theme.alpha(Theme.outline, 0.7)
        border.width: 2
    }

    MD.Image {
        id: avatarImage
        anchors.fill: parent
        anchors.margins: 2
        radius: width / 2
        source: root.source
        sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio),
                            Math.ceil(height * Screen.devicePixelRatio))
        visible: status === Image.Ready
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
    }

    MD.Text {
        anchors.centerIn: parent
        text: root.name.length > 0 ? root.name.charAt(0).toUpperCase() : "?"
        visible: !avatarImage.visible
        color: Theme.foregroundMuted
        typescale: MD.Token.typescale.title_medium
        prominent: true
    }

    Rectangle {
        visible: root.online
        width: 10
        height: 10
        radius: 5
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        color: Theme.success
        border.width: 2
        border.color: Theme.surfaceContainerLow
    }
}
