pragma ComponentBehavior: Bound

import QtQuick
import Qcm.Material as MD

MD.TextField {
    id: root

    property int value: 0
    property int minimum: 0
    property int maximum: 65535
    signal valueEdited(int value)

    type: MD.Enum.TextFieldFilled
    mdState.dense: true
    selectByMouse: true
    horizontalAlignment: TextInput.AlignHCenter
    inputMethodHints: Qt.ImhDigitsOnly
    validator: IntValidator {
        bottom: root.minimum
        top: root.maximum
    }

    function syncValue() {
        if (!activeFocus)
            text = String(value)
    }

    Component.onCompleted: syncValue()
    onValueChanged: syncValue()
    onTextEdited: {
        if (acceptableInput)
            valueEdited(Number(text))
    }
    onEditingFinished: {
        if (acceptableInput)
            valueEdited(Number(text))
        text = String(value)
    }
}
