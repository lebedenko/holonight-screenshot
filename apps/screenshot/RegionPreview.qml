import Holonight.Screenshot
import QtQuick
import QtQuick.Controls

FrozenOutput {
    id: preview

    property bool primaryOutput: false
    property bool hasCrop: false
    property bool actionsAvailable: false
    property bool undoAvailable: false
    property int activeTool: 0
    property color activeColor: "red"
    readonly property Item actionBar: buttons

    Column {
        id: buttons
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        spacing: 8
        visible: preview.primaryOutput && preview.hasCrop

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 8

            Repeater {
                model: [qsTr("Select"), qsTr("Pen"), qsTr("Arrow"), qsTr("Rectangle")]

                Button {
                    required property int index
                    required property string modelData
                    text: modelData
                    highlighted: preview.activeTool === index
                    enabled: preview.actionsAvailable
                    onClicked: preview.toolRequested(index)
                }
            }

            Button {
                text: qsTr("Undo")
                enabled: preview.undoAvailable
                onClicked: preview.undoRequested()
            }
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 8

            Repeater {
                model: [
                    { label: qsTr("Red"), color: "red" },
                    { label: qsTr("Yellow"), color: "yellow" },
                    { label: qsTr("White"), color: "white" },
                    { label: qsTr("Black"), color: "black" }
                ]

                Button {
                    id: swatch
                    required property var modelData
                    readonly property color swatchColor: modelData.color
                    text: modelData.label
                    highlighted: preview.activeColor === swatchColor
                    enabled: preview.actionsAvailable
                    leftPadding: indicator.width + 12
                    onClicked: preview.colorRequested(swatchColor)

                    Rectangle {
                        id: indicator
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: 12
                        height: 12
                        color: swatch.swatchColor
                        border.color: "#808080"
                        radius: 2
                    }
                }
            }

            Button {
                text: qsTr("Copy")
                enabled: preview.actionsAvailable
                onClicked: preview.copyRequested()
            }

            Button {
                text: qsTr("Save")
                enabled: preview.actionsAvailable
                onClicked: preview.saveRequested()
            }
        }
    }
}
