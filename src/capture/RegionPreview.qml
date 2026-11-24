pragma ComponentBehavior: Bound
import Holonight.Screenshot
import QtQuick
import QtQuick.Controls as Controls

FrozenOutput {
    id: preview

    property string appName
    property bool desktop: false
    property bool targetChoice: false
    property bool portalMode: false
    property bool selectionOnly: false
    property bool pointPicker: false
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
        visible: preview.primaryOutput && (preview.hasCrop || preview.portalMode || preview.selectionOnly)

        Controls.Label {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: preview.portalMode || preview.selectionOnly
            textFormat: Text.PlainText
            width: Math.min(implicitWidth, preview.width - 48)
            wrapMode: Text.Wrap
            padding: 8
            color: "white"
            text: preview.selectionOnly ? qsTr("Drag to select a region. Enter confirms, Esc cancels.") : preview.pointPicker ? qsTr("Click a pixel to share its color with %1.").arg(preview.appName) : preview.desktop ? qsTr("Share your desktop with %1?").arg(preview.appName) : qsTr("Select an area to share with %1.").arg(preview.appName)

            background: Rectangle {
                color: "#242424"
                radius: 4
            }

        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 8
            visible: preview.targetChoice

            Controls.Button {
                text: qsTr("Area")
                highlighted: !preview.desktop
                onClicked: preview.desktopRequested(false)
            }

            Controls.Button {
                text: qsTr("Desktop")
                highlighted: preview.desktop
                onClicked: preview.desktopRequested(true)
            }

        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 8
            visible: preview.hasCrop && !preview.pointPicker && !preview.selectionOnly

            Repeater {
                model: [qsTr("Select"), qsTr("Pen"), qsTr("Arrow"), qsTr("Rectangle")]

                Controls.Button {
                    required property int index
                    required property string modelData

                    text: modelData
                    highlighted: preview.activeTool === index
                    enabled: preview.actionsAvailable
                    onClicked: preview.toolRequested(index)
                }

            }

            Controls.Button {
                text: qsTr("Undo")
                enabled: preview.undoAvailable
                onClicked: preview.undoRequested()
            }

        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 8

            Repeater {
                model: preview.hasCrop && !preview.selectionOnly ? [{
                    "label": qsTr("Red"),
                    "color": "red"
                }, {
                    "label": qsTr("Yellow"),
                    "color": "yellow"
                }, {
                    "label": qsTr("White"),
                    "color": "white"
                }, {
                    "label": qsTr("Black"),
                    "color": "black"
                }] : []

                Controls.Button {
                    id: swatch

                    required property var modelData
                    readonly property color swatchColor: modelData.color

                    text: modelData.label
                    highlighted: preview.activeColor === swatchColor
                    enabled: preview.actionsAvailable
                    leftPadding: colorSwatch.width + 12
                    onClicked: preview.colorRequested(swatchColor)

                    Rectangle {
                        id: colorSwatch

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

            Controls.Button {
                visible: !preview.portalMode && !preview.selectionOnly
                text: qsTr("Copy")
                enabled: preview.actionsAvailable
                onClicked: preview.copyRequested()
            }

            Controls.Button {
                visible: !preview.pointPicker
                text: preview.selectionOnly ? qsTr("Confirm") : preview.desktop ? qsTr("Share Desktop") : preview.portalMode ? qsTr("Share") : qsTr("Save")
                enabled: preview.actionsAvailable
                onClicked: preview.saveRequested()
            }

            Controls.Button {
                visible: preview.portalMode || preview.selectionOnly
                text: qsTr("Cancel")
                onClicked: preview.cancelRequested()
            }

        }

    }

}
