import QtQuick
import Qcm.Material as MD

MD.Pane {
    id: root

    property string text: ""
    property color accent: Theme.primary

    horizontalPadding: 9
    verticalPadding: 4
    radius: MD.Token.shape.corner.full
    elevation: MD.Token.elevation.level0
    backgroundColor: Theme.alpha(root.accent, 0.14)

    contentItem: MD.Text {
        text: root.text
        color: root.accent
        typescale: MD.Token.typescale.label_small
        prominent: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
