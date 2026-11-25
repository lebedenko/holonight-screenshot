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
    property size badgePixels
    property rect visibleCrop
    readonly property int cropGap: 8
    readonly property Item actionBar: selectionOnly ? selectionActions : buttons

    function cropAlignedX(itemWidth: real): real {
        return Math.max(0, Math.min(visibleCrop.x + (visibleCrop.width - itemWidth) / 2, width - itemWidth));
    }

    component SelectionAction: Controls.Button {
        id: action

        required property string keycap

        focusPolicy: Qt.NoFocus
        padding: 5
        background: Rectangle {
            radius: 6
            color: action.down ? "#414763" : action.hovered && action.enabled ? "#30364a" : "transparent"
        }
        contentItem: Row {
            spacing: 6
            opacity: action.enabled ? 1 : 0.45

            Rectangle {
                width: keyLabel.implicitWidth + 10
                height: 20
                radius: 4
                color: "#303648"
                border.color: "#42495e"

                Text {
                    id: keyLabel

                    anchors.centerIn: parent
                    text: action.keycap
                    color: "#f2f1ff"
                    font.pixelSize: 12
                    font.bold: true
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: action.text
                font.pixelSize: 13
                color: "#f2f1ff"
            }
        }
    }

    Rectangle {
        id: dimensionsBadge

        readonly property real aboveCrop: preview.visibleCrop.y - height - preview.cropGap
        readonly property real preferredY: Math.max(0, Math.min(aboveCrop >= 0 ? aboveCrop : preview.visibleCrop.y + preview.cropGap,
            preview.height - height))
        readonly property real aboveStrip: selectionActions.y - height - preview.cropGap
        readonly property bool overlapsStrip: selectionActions.visible && !selectionActions.fitsBelow
            && preferredY < selectionActions.y + selectionActions.height && preferredY + height > selectionActions.y

        visible: preview.selectionOnly && preview.badgePixels.width > 0 && preview.badgePixels.height > 0
        x: preview.cropAlignedX(width)
        y: overlapsStrip && aboveStrip >= 0 ? aboveStrip : preferredY
        width: Math.min(preview.width, dimensions.implicitWidth + 16)
        height: Math.min(preview.height, dimensions.implicitHeight + 8)
        radius: 6
        color: "#191e2c"
        border.color: "#42475d"

        Text {
            id: dimensions

            anchors.fill: parent
            anchors.margins: 3
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            fontSizeMode: Text.Fit
            font.pixelSize: 13
            color: "#f2f1ff"
            text: qsTr("%1 × %2").arg(preview.badgePixels.width).arg(preview.badgePixels.height)
        }
    }

    Rectangle {
        id: selectionActions

        readonly property real cropBottom: preview.visibleCrop.y + preview.visibleCrop.height
        readonly property bool fitsBelow: cropBottom + preview.cropGap + height <= preview.height

        x: preview.cropAlignedX(width)
        y: fitsBelow ? cropBottom + preview.cropGap : Math.max(0, cropBottom - height - preview.cropGap)
        width: selectionButtons.implicitWidth + 12
        height: selectionButtons.implicitHeight + 4
        visible: preview.selectionOnly && preview.hasCrop && preview.visibleCrop.width > 0
        radius: 8
        color: "#191e2c"
        border.color: "#42475d"

        Row {
            id: selectionButtons

            anchors.centerIn: parent
            spacing: 4

            SelectionAction {
                keycap: qsTr("Enter")
                text: qsTr("Capture")
                enabled: preview.actionsAvailable
                onClicked: preview.saveRequested()
            }

            SelectionAction {
                keycap: qsTr("Esc")
                text: qsTr("Cancel")
                onClicked: preview.cancelRequested()
            }
        }
    }

    Column {
        id: buttons

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        spacing: 8
        visible: !preview.selectionOnly && preview.primaryOutput && (preview.hasCrop || preview.portalMode)

        Controls.Label {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: preview.portalMode
            textFormat: Text.PlainText
            width: Math.min(implicitWidth, preview.width - 48)
            wrapMode: Text.Wrap
            padding: 8
            color: "white"
            text: preview.pointPicker ? qsTr("Click a pixel to share its color with %1.").arg(preview.appName) : preview.desktop ? qsTr("Share your desktop with %1?").arg(preview.appName) : qsTr("Select an area to share with %1.").arg(preview.appName)

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
            visible: preview.hasCrop && !preview.pointPicker

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
                model: preview.hasCrop ? [{
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
                visible: !preview.portalMode
                text: qsTr("Copy")
                enabled: preview.actionsAvailable
                onClicked: preview.copyRequested()
            }

            Controls.Button {
                visible: !preview.pointPicker
                text: preview.desktop ? qsTr("Share Desktop") : preview.portalMode ? qsTr("Share") : qsTr("Save")
                enabled: preview.actionsAvailable
                onClicked: preview.saveRequested()
            }

            Controls.Button {
                visible: preview.portalMode
                text: qsTr("Cancel")
                onClicked: preview.cancelRequested()
            }

        }

    }

}
