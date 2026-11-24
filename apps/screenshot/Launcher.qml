import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root

    property int delayChoice: 0
    property bool includeCursor: false
    readonly property var delays: [0, 3000, 5000, 10000]

    title: workflow.page === "picker" ? qsTr("Choose a Window") : qsTr("HoloNight Screenshot")
    visible: workflow.page !== ""
    width: workflow.page === "preview" ? 680 : 460
    height: workflow.page === "preview" ? 500 : workflow.page === "picker" ? 380 : Math.max(190, launcher.implicitHeight + 32)
    minimumWidth: 400
    minimumHeight: workflow.page === "launcher" ? launcher.implicitHeight + 32 : 260
    onClosing: function (close) {
        close.accepted = workflow.page !== "picker";
        workflow.closeWindow();
    }

    Shortcut {
        sequence: "Escape"
        enabled: workflow.page === "picker"
        onActivated: workflow.cancelPicker()
    }

    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: workflow.page === "picker"
        onActivated: workflow.confirmWindow()
    }

    ColumnLayout {
        id: launcher

        anchors.fill: parent
        anchors.margins: 16
        spacing: 12
        visible: workflow.page === "launcher"

        ButtonGroup {
            id: targets
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 5

            Repeater {
                model: workflow.modes

                delegate: ColumnLayout {
                    id: mode

                    required property var modelData

                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignTop
                    spacing: 4

                    Button {
                        Layout.fillWidth: true
                        text: mode.modelData.name === "area" ? qsTr("Region") : mode.modelData.name === "window" ? qsTr("Window") : qsTr("Screen")
                        checkable: true
                        checked: workflow.target === mode.modelData.name
                        highlighted: checked
                        enabled: mode.modelData.reason === ""
                        ButtonGroup.group: targets
                        onClicked: workflow.selectMode(mode.modelData.name)
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: mode.modelData.reason
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Label {
                Layout.fillWidth: true
                text: qsTr("Capture delay")
            }

            ButtonGroup {
                id: delayButtons
            }

            Repeater {
                model: [qsTr("None"), qsTr("3 s"), qsTr("5 s"), qsTr("10 s")]

                delegate: Button {
                    required property int index
                    required property string modelData

                    Layout.minimumWidth: 54
                    text: modelData
                    checkable: true
                    checked: root.delayChoice === index
                    highlighted: checked
                    ButtonGroup.group: delayButtons
                    onClicked: root.delayChoice = index
                }
            }
        }

        CheckBox {
            text: qsTr("Include mouse cursor")
            checked: root.includeCursor
            onToggled: root.includeCursor = checked
        }

        Label {
            Layout.fillWidth: true
            visible: text !== ""
            text: launcher.visible ? workflow.message : ""
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
        }

        Item {
            Layout.fillHeight: true
        }

        Button {
            Layout.fillWidth: true
            text: qsTr("Capture")
            highlighted: true
            enabled: workflow.target !== ""
            onClicked: workflow.startCapture(root.delays[root.delayChoice], root.includeCursor)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10
        visible: workflow.page === "picker"

        Label {
            text: qsTr("Choose a window to capture")
        }

        ListView {
            id: picker
            Layout.fillWidth: true
            Layout.fillHeight: true

            Accessible.name: qsTr("Open windows")
            clip: true
            model: workflow.windows
            keyNavigationEnabled: false

            activeFocusOnTab: true
            onVisibleChanged: if (visible)
                Qt.callLater(forceActiveFocus)
            onCountChanged: Qt.callLater(revealSelection)

            Keys.onUpPressed: workflow.moveSelection(-1)
            Keys.onDownPressed: workflow.moveSelection(1)

            ScrollBar.vertical: ScrollBar {}

            function revealSelection() {
                for (let i = 0; i < count; ++i) {
                    if (model[i].id === workflow.selectedWindow) {
                        positionViewAtIndex(i, ListView.Contain);
                        return;
                    }
                }
            }

            Connections {
                target: workflow
                function onSelectionChanged() {
                    Qt.callLater(picker.revealSelection);
                }
            }

            delegate: ItemDelegate {
                required property var modelData

                width: ListView.view.width
                text: (modelData.title || qsTr("Untitled window")) + (modelData.appName ? "\n" + modelData.appName : "")
                highlighted: workflow.selectedWindow === modelData.id
                Accessible.selected: highlighted
                contentItem: Label {
                    text: parent.text
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    maximumLineCount: 2
                }
                onClicked: {
                    workflow.selectWindow(modelData.id);
                    picker.forceActiveFocus();
                }
                onDoubleClicked: {
                    workflow.selectWindow(modelData.id);
                    workflow.confirmWindow();
                }
            }

            Label {
                anchors.centerIn: parent
                visible: picker.count === 0
                text: qsTr("No open windows")
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight

            Button {
                text: qsTr("Cancel")
                onClicked: workflow.cancelPicker()
            }

            Button {
                text: qsTr("Capture")
                highlighted: true
                enabled: workflow.selectedWindow !== ""
                onClicked: workflow.confirmWindow()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12
        visible: workflow.page === "preview"

        Image {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 1
            fillMode: Image.PreserveAspectFit
            source: workflow.page === "preview" ? "image://capture/" + workflow.revision : ""
            sourceSize.width: width
            sourceSize.height: height
            cache: false
            Accessible.name: qsTr("Screenshot preview")
        }

        Label {
            Layout.fillWidth: true
            visible: text !== ""
            text: workflow.page === "preview" ? workflow.message : ""
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true

            Button {
                text: qsTr("New Capture")
                onClicked: workflow.newCapture()
            }

            Item {
                Layout.fillWidth: true
            }

            Button {
                text: qsTr("Copy")
                onClicked: workflow.copy()
            }

            Button {
                text: qsTr("Save…")
                highlighted: true
                onClicked: workflow.save()
            }
        }
    }
}
