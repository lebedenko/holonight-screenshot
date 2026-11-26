pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Templates as T

ApplicationWindow {
    id: root
    required property var workflow

    readonly property var modes: workflow.modes
    readonly property var delays: [0, 3000, 5000, 10000]
    readonly property string diagnostic: workflow.page === "launcher" ? workflow.message : ""
    property var warningPalette: null

    Component.onCompleted: {
        // Keep the optional import isolated from the launcher, and retain the live binding.
        try {
            warningPalette = Qt.createQmlObject(
                'import QtQml; import Holonight.Core; QtObject { readonly property var warning: typeof HoloniightPalette === "undefined" ? undefined : HoloniightPalette.warning }',
                root, "WarningPalette");
        } catch (error) {
            // Standalone installations can use the window palette.
        }
    }

    title: root.workflow.page === "picker" ? qsTr("Choose a Window") : qsTr("HoloNight Screenshot")
    visible: root.workflow.page === "launcher" || root.workflow.page === "picker"
    width: 460
    height: root.workflow.page === "picker" ? 380 : Math.max(190, launcher.implicitHeight + 32)
    minimumWidth: 400
    minimumHeight: root.workflow.page === "picker" ? 260 : launcher.implicitHeight + 32
    onClosing: function (close) {
        close.accepted = root.workflow.page !== "picker";
        root.workflow.closeWindow();
    }

    component SegmentedBar: Rectangle {
        Layout.fillWidth: true
        color: root.palette.button
        border.color: root.palette.mid
        radius: 10
    }

    component LauncherButton: T.Button {
        id: control

        property string glyph: ""
        property bool primary: false
        readonly property bool accented: enabled && (checked || primary)
        readonly property color foreground: accented ? palette.highlightedText : palette.buttonText
        property color glyphColor: foreground

        focusPolicy: Qt.StrongFocus
        hoverEnabled: true
        implicitHeight: primary ? 44 : 36
        implicitWidth: contentItem.implicitWidth + 16

        background: Rectangle {
            radius: 8
            color: {
                if (control.accented)
                    return control.down ? Qt.tint(control.palette.highlight, Qt.alpha(control.palette.highlightedText, 0.18))
                                        : control.hovered ? Qt.tint(control.palette.highlight, Qt.alpha(control.palette.highlightedText, 0.09))
                                                          : control.palette.highlight;
                if (control.enabled && (control.down || control.hovered))
                    return Qt.tint(control.palette.button, Qt.alpha(control.palette.buttonText, control.down ? 0.16 : 0.08));
                return control.primary ? control.palette.button : "transparent";
            }
            border.width: control.visualFocus ? 2 : control.primary && !control.enabled ? 1 : 0
            border.color: control.visualFocus ? (control.accented ? control.palette.highlightedText : control.palette.highlight)
                                              : control.palette.mid
        }

        contentItem: Item {
            implicitWidth: caption.implicitWidth
            implicitHeight: caption.implicitHeight

            Row {
                id: caption
                anchors.centerIn: parent
                spacing: 8

                Shape {
                    width: 20
                    height: 20
                    anchors.verticalCenter: parent.verticalCenter
                    visible: control.glyph !== ""
                    preferredRendererType: Shape.CurveRenderer

                    ShapePath {
                        fillColor: "transparent"
                        strokeColor: control.glyphColor
                        strokeWidth: 1.6
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin

                        PathSvg {
                            path: control.glyph
                        }
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: control.text
                    textFormat: Text.PlainText
                    color: control.foreground
                    font: control.font
                }
            }
        }
    }

    Shortcut {
        sequence: "Escape"
        enabled: root.workflow.page === "picker"
        onActivated: root.workflow.cancelPicker()
    }

    Shortcut {
        sequences: ["Return", "Enter"]
        autoRepeat: false
        enabled: root.workflow.page === "picker"
        onActivated: root.workflow.confirmWindow()
    }

    ColumnLayout {
        id: launcher

        anchors.fill: parent
        anchors.margins: 16
        spacing: 12
        visible: root.workflow.page === "launcher"

        ButtonGroup {
            id: targets
        }

        SegmentedBar {
            implicitHeight: targetRow.implicitHeight + 4

            RowLayout {
                id: targetRow
                anchors.fill: parent
                anchors.margins: 2
                spacing: 2

                Repeater {
                    model: root.modes.length

                    delegate: Item {
                        id: targetCell
                        required property int index
                        readonly property var modelData: root.modes[index]

                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 1
                        implicitHeight: mode.implicitHeight

                        HoverHandler {
                            id: targetHover
                            enabled: !targetCell.modelData.available && targetCell.modelData.reason !== ""
                        }

                        ToolTip {
                            objectName: "availability-tooltip"
                            visible: targetHover.hovered
                            delay: 500
                            text: targetCell.modelData.reason
                        }

                        LauncherButton {
                            id: mode
                            anchors.fill: parent
                            objectName: "target-" + targetCell.modelData.name
                            text: targetCell.modelData.name === "area" ? qsTr("Region") : targetCell.modelData.name === "window" ? qsTr("Window") : qsTr("Screen")
                            glyph: targetCell.modelData.name === "area" ? "M2 7 V3 H7 M13 3 H18 V7 M18 13 V17 H13 M7 17 H2 V13"
                                 : targetCell.modelData.name === "window" ? "M3 3 H17 V17 H3 Z M3 7 H17"
                                 : "M2 3 H18 V14 H2 Z M10 14 V18 M6 18 H14"
                            checkable: true
                            checked: root.workflow.target === targetCell.modelData.name
                            enabled: targetCell.modelData.available
                            Accessible.description: targetCell.modelData.reason
                            ButtonGroup.group: targets
                            onClicked: root.workflow.selectMode(targetCell.modelData.name)
                        }
                    }
                }
            }
        }

        SegmentedBar {
            implicitHeight: delayRow.implicitHeight + 4

            RowLayout {
                id: delayRow
                anchors.fill: parent
                anchors.margins: 2
                spacing: 2

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    text: qsTr("Capture delay")
                }

                ButtonGroup {
                    id: delayButtons
                }

                Repeater {
                    model: [qsTr("None"), qsTr("3 s"), qsTr("5 s"), qsTr("10 s")]

                    delegate: LauncherButton {
                        required property int index
                        required property string modelData

                        objectName: "delay-" + index
                        Layout.preferredWidth: 60
                        implicitHeight: 32
                        text: modelData
                        checkable: true
                        checked: root.workflow.delayMs === root.delays[index]
                        ButtonGroup.group: delayButtons
                        onClicked: root.workflow.delayMs = root.delays[index]
                    }
                }

            }
        }

        Item {
            id: cursorRow
            Layout.fillWidth: true
            implicitHeight: cursor.implicitHeight

            CheckBox {
                id: cursor
                objectName: "include-cursor"
                text: qsTr("Include mouse cursor")
                checked: root.workflow.includeCursor
                onToggled: root.workflow.includeCursor = checked
            }

            LauncherButton {
                id: detailsButton
                objectName: "capture-details-button"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: cursorRow.height
                visible: root.diagnostic !== ""
                glyph: "M10 2 L18.5 17 H1.5 Z M10 7 V11 M10 14 V14.2"
                glyphColor: root.warningPalette?.warning ?? root.palette.windowText
                Accessible.name: qsTr("Show capture details")
                ToolTip.text: Accessible.name
                ToolTip.delay: 500
                ToolTip.visible: hovered && !details.opened
                onClicked: details.open()
                onVisibleChanged: if (!visible)
                    details.close()
            }

            Popup {
                id: details
                objectName: "capture-details-popup"
                x: cursorRow.width - width
                y: -height - 4
                width: Math.min(cursorRow.width, 360)
                padding: 12
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                contentItem: ScrollView {
                    objectName: "capture-details-scroll"
                    clip: true
                    contentWidth: availableWidth
                    implicitHeight: Math.min(diagnosticLabel.implicitHeight,
                                             Math.max(0, cursorRow.y - details.topPadding - details.bottomPadding))
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                    Label {
                        id: diagnosticLabel
                        objectName: "capture-diagnostic"
                        width: details.availableWidth - 12
                        text: root.diagnostic
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                    }
                }
            }
        }

        CheckBox {
            objectName: "open-editor"
            text: qsTr("Open annotation editor")
            checked: root.workflow.openEditor
            onToggled: root.workflow.openEditor = checked
        }

        Item {
            Layout.fillHeight: true
        }

        RowLayout {
            Layout.fillWidth: true

            LauncherButton {
                objectName: "capture"
                Layout.fillWidth: true
                text: qsTr("Capture")
                primary: true
                glyph: "M2 6 H6 L8 3 H12 L14 6 H18 V17 H2 Z M13 11 A3 3 0 1 1 7 11 A3 3 0 1 1 13 11 Z"
                enabled: root.workflow.target !== ""
                onEnabledChanged: if (enabled && visible)
                    forceActiveFocus(Qt.TabFocusReason)
                onVisibleChanged: if (visible && enabled)
                    forceActiveFocus(Qt.TabFocusReason)
                Keys.onPressed: function (event) {
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        event.accepted = true;
                        if (!event.isAutoRepeat)
                            click();
                    }
                }
                onClicked: root.workflow.startCapture()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10
        visible: root.workflow.page === "picker"

        Label {
            text: qsTr("Choose a window to capture")
        }

        ListView {
            id: picker
            Layout.fillWidth: true
            Layout.fillHeight: true

            Accessible.name: qsTr("Open windows")
            clip: true
            model: root.workflow.windows
            keyNavigationEnabled: false

            activeFocusOnTab: true
            onVisibleChanged: if (visible)
                Qt.callLater(forceActiveFocus)
            onCountChanged: Qt.callLater(revealSelection)

            Keys.onUpPressed: root.workflow.moveSelection(-1)
            Keys.onDownPressed: root.workflow.moveSelection(1)

            ScrollBar.vertical: ScrollBar {}

            function revealSelection() {
                for (let i = 0; i < count; ++i) {
                    if (model[i].id === root.workflow.selectedWindow) {
                        positionViewAtIndex(i, ListView.Contain);
                        return;
                    }
                }
            }

            Connections {
                target: root.workflow
                function onSelectionChanged() {
                    Qt.callLater(picker.revealSelection);
                }
            }

            delegate: ItemDelegate {
                id: windowDelegate
                required property var modelData

                width: ListView.view.width
                text: (modelData.title || qsTr("Untitled window")) + (modelData.appName ? "\n" + modelData.appName : "")
                highlighted: root.workflow.selectedWindow === modelData.id
                Accessible.selected: highlighted
                contentItem: Label {
                    text: windowDelegate.text
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    maximumLineCount: 2
                }
                onClicked: {
                    root.workflow.selectWindow(modelData.id);
                    picker.forceActiveFocus();
                }
                onDoubleClicked: {
                    root.workflow.selectWindow(modelData.id);
                    root.workflow.confirmWindow();
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
                onClicked: root.workflow.cancelPicker()
            }

            Button {
                text: qsTr("Capture")
                highlighted: true
                enabled: root.workflow.selectedWindow !== ""
                onClicked: root.workflow.confirmWindow()
            }
        }
    }

    Loader {
        active: root.workflow.page === "editor"
        sourceComponent: ApplicationWindow {
            title: qsTr("HoloNight Screenshot")
            transientParent: null
            id: editorWindow
            Component.onCompleted: root.workflow.showEditorWindow(editorWindow)
            minimumWidth: 760
            minimumHeight: 520
            onClosing: function (close) {
                close.accepted = false;
                Qt.callLater(root.workflow.closeWindow);
            }

            Editor {
                anchors.fill: parent
                workflow: root.workflow
            }
        }
    }
}
