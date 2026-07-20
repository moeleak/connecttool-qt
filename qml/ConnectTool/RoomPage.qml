pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import ConnectTool

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacing

        ConnectionPanel {
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacing

            ChatPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 500
                Layout.preferredWidth: 620
            }

            PeoplePanel {
                Layout.fillHeight: true
                Layout.minimumWidth: 390
                Layout.preferredWidth: 455
            }
        }
    }
}
