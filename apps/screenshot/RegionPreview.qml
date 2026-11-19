import Holonight.Screenshot
import QtQuick
import QtQuick.Controls

FrozenOutput {
    id: preview

    property bool primaryOutput: false
    property bool actionsAvailable: false
    readonly property Item actionBar: buttons

    Row {
        id: buttons
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        spacing: 8
        visible: preview.primaryOutput && preview.actionsAvailable

        Button {
            text: qsTr("Copy")
            onClicked: preview.copyRequested()
        }

        Button {
            text: qsTr("Save")
            onClicked: preview.saveRequested()
        }
    }
}
