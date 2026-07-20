pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Qcm.Material as MD
import ConnectTool

AppCard {
    id: root

    property int currentSection: 0

    padding: 12
    backgroundColor: Theme.surfaceContainerLow

    MD.SegmentedButtonGroup {
        id: peopleSelector

        Layout.fillWidth: true
        size: MD.Enum.S

        MD.SegmentedButton {
            width: (peopleSelector.width + 1) / 2
            text: qsTr("房间成员")
            icon.name: MD.Token.icon.groups
            checked: root.currentSection === 0
            onClicked: root.currentSection = 0
        }

        MD.SegmentedButton {
            width: (peopleSelector.width + 1) / 2
            text: qsTr("Steam 好友")
            icon.name: MD.Token.icon.person
            checked: root.currentSection === 1
            onClicked: root.currentSection = 1
        }
    }

    StackLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        currentIndex: root.currentSection

        ListView {
            id: memberList

            clip: true
            spacing: 7
            model: App.social.members
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: MD.ScrollBar {}

            delegate: MemberDelegate {
                width: memberList.width
            }

            ColumnLayout {
                anchors.centerIn: parent
                visible: memberList.count === 0
                spacing: 5

                MD.Icon {
                    Layout.alignment: Qt.AlignHCenter
                    name: MD.Token.icon.group_off
                    size: 32
                    color: Theme.primary
                }

                MD.Label {
                    text: qsTr("暂无房间成员")
                    color: Theme.foregroundMuted
                    typescale: MD.Token.typescale.body_small
                }
            }
        }

        ColumnLayout {
            spacing: 8

            MD.TextField {
                Layout.fillWidth: true
                placeholderText: qsTr("搜索好友…")
                leadingIcon: MD.Token.icon.search
                text: App.social.filter
                type: MD.Enum.TextFieldFilled
                mdState.dense: true
                onTextEdited: App.social.filter = text
            }

            ListView {
                id: friendList

                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 7
                model: App.social.friends
                boundsBehavior: Flickable.StopAtBounds
                T.ScrollBar.vertical: MD.ScrollBar {}

                delegate: FriendDelegate {
                    width: friendList.width
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    visible: friendList.count === 0
                    spacing: 5

                    MD.Icon {
                        Layout.alignment: Qt.AlignHCenter
                        name: MD.Token.icon.person_search
                        size: 32
                        color: Theme.primary
                    }

                    MD.Label {
                        text: App.social.filter.length > 0
                              ? qsTr("没有匹配的好友") : qsTr("未获取到好友")
                        color: Theme.foregroundMuted
                        typescale: MD.Token.typescale.body_small
                    }
                }
            }
        }
    }
}
