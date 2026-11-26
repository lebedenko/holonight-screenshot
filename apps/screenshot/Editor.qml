pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

FocusScope {
    id: editor

    required property var workflow
    readonly property var document: workflow.document
    readonly property var tools: [qsTr("Select"), qsTr("Pen"), qsTr("Rectangle"), qsTr("Ellipse"),
        qsTr("Arrow"), qsTr("Text"), qsTr("Number"), qsTr("Blur")]
    readonly property var glyphs: ["↖", "✎", "□", "○", "↗", "T", "①", "◈"]
    readonly property color panelColor: Qt.tint(palette.window, Qt.alpha(palette.buttonText, 0.045))
    readonly property var zoomLevels: [0.125, 0.25, 0.5, 0.75, 1, 1.5, 2, 3, 4]
    property int zoomIndex: 0
    readonly property real fitScale: Math.min(viewport.width / Math.max(1, document.view.width),
        viewport.height / Math.max(1, document.view.height))
    readonly property real scaleFactor: zoomIndex === 0 ? Math.max(0.001, fitScale) : zoomLevels[zoomIndex - 1]
    readonly property bool textFocused: draftInput.activeFocus || widthControl.activeFocus
        || radiusControl.activeFocus || sizeControl.activeFocus || strengthControl.activeFocus

    function zoomStep(direction: int) {
        let index = zoomIndex;
        if (index === 0) {
            let next = zoomLevels.findIndex(level => level >= scaleFactor);
            if (next < 0)
                next = zoomLevels.length;
            index = next + (direction < 0 ? 1 : 0);
            if (direction > 0 && zoomLevels[next] === scaleFactor)
                index++;
        }
        zoomIndex = Math.max(1, Math.min(zoomLevels.length, index + direction));
    }

    focus: true
    Component.onCompleted: forceActiveFocus()

    Shortcut {
        sequences: [StandardKey.Copy]
        enabled: !editor.document.busy && !editor.textFocused
        onActivated: editor.workflow.copy()
    }
    Shortcut {
        sequences: [StandardKey.Save]
        enabled: !editor.document.busy
        onActivated: editor.workflow.save()
    }
    Shortcut {
        sequences: [StandardKey.Undo]
        enabled: editor.document.canUndo && !editor.textFocused
        onActivated: editor.document.undo()
    }
    Shortcut {
        sequences: [StandardKey.Redo]
        enabled: editor.document.canRedo && !editor.textFocused
        onActivated: editor.document.redo()
    }
    Shortcut {
        sequence: "Delete"
        enabled: !editor.document.busy && !editor.textFocused
        onActivated: editor.document.deleteSelection()
    }
    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: editor.document.cropping
        onActivated: editor.document.applyCrop()
    }
    Shortcut {
        sequences: ["Ctrl+Return", "Ctrl+Enter"]
        enabled: editor.document.editingText
        onActivated: editor.document.commitText(draftInput.text)
    }
    Shortcut {
        sequence: "Escape"
        enabled: editor.document.busy
        onActivated: {
            if (editor.document.editingText)
                editor.document.cancelText();
            else if (editor.document.cropping)
                editor.document.cancelCrop();
            else
                editor.document.cancelGesture();
        }
    }

    component PropertyNumber: SpinBox {
        required property string key
        editable: true
        from: 1
        to: 256
        value: editor.document.properties[key] ?? from
        onValueModified: editor.document.setPropertyValue(key, value)
    }

    component PropertyColor: Button {
        id: swatch
        required property string key
        readonly property color value: editor.document.properties[key] ?? "transparent"
        text: value.a === 0 ? qsTr("None") : String(value)
        onClicked: {
            colorPicker.key = key;
            colorPicker.selectedColor = value.a === 0 ? editor.document.properties.color : value;
            colorPicker.open();
        }
        contentItem: Row {
            spacing: 6
            Rectangle {
                width: 16
                height: 16
                radius: 3
                color: swatch.value
                border.color: swatch.palette.mid
                anchors.verticalCenter: parent.verticalCenter
            }
            Label { text: swatch.text }
        }
    }

    ColorDialog {
        id: colorPicker
        property string key
        options: ColorDialog.ShowAlphaChannel
        onAccepted: editor.document.setPropertyValue(key, selectedColor)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        RowLayout {
            Layout.fillWidth: true

            ToolButton {
                text: qsTr("Crop")
                enabled: !editor.document.busy
                onClicked: editor.document.startCrop()
            }
            ComboBox {
                objectName: "editor-zoom"
                Accessible.name: qsTr("Zoom")
                model: [qsTr("Fit")].concat(editor.zoomLevels.map(level => qsTr("%1%").arg(level * 100)))
                currentIndex: editor.zoomIndex
                onActivated: index => editor.zoomIndex = index
            }
            ToolButton {
                text: "−"
                Accessible.name: qsTr("Zoom out")
                onClicked: editor.zoomStep(-1)
            }
            ToolButton {
                text: "+"
                Accessible.name: qsTr("Zoom in")
                onClicked: editor.zoomStep(1)
            }

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("Copy")
                enabled: !editor.document.busy
                onClicked: editor.workflow.copy()
            }
            Button {
                text: editor.workflow.fixedSavePath ? qsTr("Save") : qsTr("Save…")
                enabled: !editor.document.busy
                onClicked: editor.workflow.save()
            }
            ToolButton {
                text: "×"
                Accessible.name: qsTr("Close editor")
                onClicked: editor.workflow.closeWindow()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6

            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                spacing: 3

                Repeater {
                    model: editor.tools
                    ToolButton {
                        required property int index
                        required property string modelData
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 34
                        text: editor.glyphs[index]
                        Accessible.name: modelData
                        ToolTip.text: modelData
                        ToolTip.visible: hovered
                        checkable: true
                        checked: editor.document.tool === index
                        enabled: !editor.document.busy
                        onClicked: editor.document.tool = index
                    }
                }
                ToolSeparator { orientation: Qt.Horizontal }
                ToolButton {
                    text: "↶"
                    Accessible.name: qsTr("Undo")
                    enabled: editor.document.canUndo
                    onClicked: editor.document.undo()
                }
                ToolButton {
                    text: "↷"
                    Accessible.name: qsTr("Redo")
                    enabled: editor.document.canRedo
                    onClicked: editor.document.redo()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: editor.palette.base

                Flickable {
                    id: viewport
                    anchors.fill: parent
                    clip: true
                    contentWidth: Math.max(width, raster.width)
                    contentHeight: Math.max(height, raster.height)
                    interactive: false
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.horizontal: ScrollBar {}
                    ScrollBar.vertical: ScrollBar {}

                    Image {
                        id: raster
                        objectName: "editor-canvas"
                        x: Math.max(0, (viewport.width - width) / 2)
                        y: Math.max(0, (viewport.height - height) / 2)
                        width: editor.document.view.width * editor.scaleFactor
                        height: editor.document.view.height * editor.scaleFactor
                        source: "image://capture/" + editor.document.revision
                        cache: false

                        readonly property rect outline: editor.document.cropping ? editor.document.pendingCrop
                            : editor.document.selection

                        Rectangle {
                            id: outline
                            visible: raster.outline.width > 0 && raster.outline.height > 0
                            x: (raster.outline.x - editor.document.view.x) * editor.scaleFactor
                            y: (raster.outline.y - editor.document.view.y) * editor.scaleFactor
                            width: raster.outline.width * editor.scaleFactor
                            height: raster.outline.height * editor.scaleFactor
                            color: "transparent"
                            border.color: editor.palette.highlight
                            border.width: 2

                            Repeater {
                                model: editor.document.cropping ? [0, 0.5, 1] : []
                                Item {
                                    id: handleRow
                                    required property real modelData
                                    Repeater {
                                        model: [0, 0.5, 1]
                                        Rectangle {
                                            required property real modelData
                                            visible: modelData !== 0.5 || handleRow.modelData !== 0.5
                                            x: modelData * outline.width - width / 2
                                            y: handleRow.modelData * outline.height - height / 2
                                            width: 8
                                            height: 8
                                            color: editor.palette.highlightedText
                                            border.color: editor.palette.highlight
                                        }
                                    }
                                }
                            }
                        }

                        Item {
                            id: grid
                            visible: guides.checked
                            readonly property bool useCrop: editor.document.cropping && outline.visible
                            x: useCrop ? outline.x : 0
                            y: useCrop ? outline.y : 0
                            width: useCrop ? outline.width : raster.width
                            height: useCrop ? outline.height : raster.height

                            Repeater {
                                model: [1 / 3, 2 / 3]
                                Item {
                                    id: third
                                    required property real modelData
                                    Rectangle {
                                        x: third.modelData * grid.width
                                        width: 1
                                        height: grid.height
                                        color: editor.palette.highlightedText
                                        opacity: 0.6
                                    }
                                    Rectangle {
                                        y: third.modelData * grid.height
                                        width: grid.width
                                        height: 1
                                        color: editor.palette.highlightedText
                                        opacity: 0.6
                                    }
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: editor.document.tool === 0 && !editor.document.cropping
                                ? Qt.ArrowCursor : Qt.CrossCursor
                            function imagePoint(x: real, y: real): point {
                                return Qt.point(editor.document.view.x + x / editor.scaleFactor,
                                    editor.document.view.y + y / editor.scaleFactor);
                            }
                            onPressed: mouse => {
                                if (editor.document.editingText) {
                                    editor.document.commitText(draftInput.text);
                                    mouse.accepted = false;
                                    return;
                                }
                                editor.forceActiveFocus();
                                editor.document.begin(imagePoint(mouse.x, mouse.y), 6 / editor.scaleFactor);
                            }
                            onPositionChanged: mouse => {
                                if (pressed)
                                    editor.document.move(imagePoint(mouse.x, mouse.y));
                            }
                            onReleased: mouse => editor.document.finish(imagePoint(mouse.x, mouse.y))
                            onCanceled: editor.document.cancelGesture()
                        }

                        Rectangle {
                            id: textBox
                            readonly property rect bounds: editor.document.textBox
                            visible: bounds.width > 0 && bounds.height > 0
                            x: (bounds.x - editor.document.view.x) * editor.scaleFactor
                            y: (bounds.y - editor.document.view.y) * editor.scaleFactor
                            width: bounds.width
                            height: bounds.height
                            scale: editor.scaleFactor
                            transformOrigin: Item.TopLeft
                            color: editor.document.editingText ? editor.palette.base : "transparent"
                            border.width: 1 / editor.scaleFactor
                            border.color: editor.palette.highlight

                            Flickable {
                                id: textViewport
                                anchors.fill: parent
                                visible: editor.document.editingText
                                clip: true
                                contentWidth: width
                                contentHeight: Math.max(height, draftInput.contentHeight)
                                boundsBehavior: Flickable.StopAtBounds

                                onVisibleChanged: {
                                    if (visible) {
                                        draftInput.text = editor.document.draftText;
                                        draftInput.cursorPosition = draftInput.length;
                                        contentY = 0;
                                        draftInput.forceActiveFocus();
                                    } else if (draftInput.activeFocus) {
                                        editor.forceActiveFocus();
                                    }
                                }

                                TextEdit {
                                    id: draftInput
                                    objectName: "annotation-text-input"
                                    width: textViewport.width
                                    height: textViewport.contentHeight
                                    textFormat: TextEdit.PlainText
                                    wrapMode: TextEdit.Wrap
                                    selectByMouse: true
                                    color: editor.document.properties.color
                                    // This font is drawn in source pixels, then scaled with the canvas.
                                    font: Qt.font({pixelSize: editor.document.properties.size ?? 24,
                                        bold: editor.document.properties.bold ?? false,
                                        italic: editor.document.properties.italic ?? false})
                                    Accessible.name: qsTr("Annotation text")
                                    onCursorRectangleChanged: {
                                        const bottom = cursorRectangle.y + cursorRectangle.height;
                                        if (cursorRectangle.y < textViewport.contentY)
                                            textViewport.contentY = cursorRectangle.y;
                                        else if (bottom > textViewport.contentY + textViewport.height)
                                            textViewport.contentY = bottom - textViewport.height;
                                    }
                                }
                            }
                        }
                    }

                    WheelHandler {
                        onWheel: event => {
                            const delta = event.pixelDelta.x || event.pixelDelta.y
                                ? event.pixelDelta : Qt.point(event.angleDelta.x / 3, event.angleDelta.y / 3);
                            const horizontal = (event.modifiers & Qt.ShiftModifier) !== 0;
                            viewport.contentX = Math.max(0, Math.min(viewport.contentWidth - viewport.width,
                                viewport.contentX - (horizontal ? delta.y : delta.x)));
                            viewport.contentY = Math.max(0, Math.min(viewport.contentHeight - viewport.height,
                                viewport.contentY - (horizontal ? 0 : delta.y)));
                        }
                    }
                }
            }

            ScrollView {
                id: sidebar
                Layout.preferredWidth: 208
                padding: 12
                background: Rectangle {
                    color: editor.panelColor
                    radius: 6
                }
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true

                ColumnLayout {
                    width: sidebar.availableWidth
                    enabled: !editor.document.busy
                    readonly property int tool: editor.document.propertyTool
                    id: properties

                    RowLayout {
                        Label {
                            Layout.fillWidth: true
                            text: editor.tools[properties.tool] ?? ""
                            font.bold: true
                        }
                        ToolButton {
                            text: qsTr("Delete")
                            visible: editor.document.selection.width > 0
                            onClicked: editor.document.deleteSelection()
                        }
                    }
                    Label { text: qsTr("Fill"); visible: fillControl.visible }
                    RowLayout {
                        id: fillControl
                        visible: properties.tool === 2 || properties.tool === 3
                        PropertyColor { key: "fill"; Accessible.name: qsTr("Fill color") }
                        ToolButton {
                            text: qsTr("Clear")
                            onClicked: editor.document.setPropertyValue("fill", Qt.rgba(0, 0, 0, 0))
                        }
                    }
                    Label {
                        text: properties.tool <= 4 ? qsTr("Stroke") : qsTr("Color")
                        visible: ink.visible
                    }
                    PropertyColor {
                        id: ink
                        key: "color"
                        Accessible.name: qsTr("Annotation color")
                        visible: properties.tool > 0 && properties.tool < 7
                    }
                    Label { text: qsTr("Stroke width"); visible: widthControl.visible }
                    PropertyNumber {
                        id: widthControl
                        key: "width"
                        Accessible.name: qsTr("Stroke width")
                        visible: properties.tool > 0 && properties.tool <= 4
                    }
                    Label { text: qsTr("Corner radius"); visible: radiusControl.visible }
                    PropertyNumber {
                        id: radiusControl
                        key: "radius"
                        from: 0
                        Accessible.name: qsTr("Corner radius")
                        visible: properties.tool === 2
                    }
                    Button {
                        text: qsTr("Edit text")
                        visible: properties.tool === 5 && editor.document.selection.width > 0
                        onClicked: editor.document.editSelectedText()
                    }
                    CheckBox {
                        text: qsTr("Bold")
                        visible: properties.tool === 5
                        checked: editor.document.properties.bold
                        onToggled: editor.document.setPropertyValue("bold", checked)
                    }
                    CheckBox {
                        text: qsTr("Italic")
                        visible: properties.tool === 5
                        checked: editor.document.properties.italic
                        onToggled: editor.document.setPropertyValue("italic", checked)
                    }
                    ComboBox {
                        visible: properties.tool === 6
                        Accessible.name: qsTr("Number badge style")
                        model: [qsTr("Circle"), qsTr("Rounded square")]
                        currentIndex: editor.document.properties.square ? 1 : 0
                        onActivated: index => editor.document.setPropertyValue("square", index === 1)
                    }
                    Label { text: qsTr("Size"); visible: sizeControl.visible }
                    PropertyNumber {
                        id: sizeControl
                        key: "size"
                        Accessible.name: qsTr("Size")
                        visible: properties.tool === 5 || properties.tool === 6
                    }
                    Label { text: qsTr("Blur strength"); visible: strengthControl.visible }
                    PropertyNumber {
                        id: strengthControl
                        key: "strength"
                        Accessible.name: qsTr("Blur strength")
                        visible: properties.tool === 7
                    }
                }
            }
        }

        RowLayout {
            Label {
                text: qsTr("%1 × %2 px").arg(editor.document.crop.width).arg(editor.document.crop.height)
            }
            Item { Layout.fillWidth: true }
            Label { text: editor.document.cropping ? qsTr("Crop mode") : qsTr("Annotation mode") }
            Button {
                visible: editor.document.cropping
                text: qsTr("Apply")
                highlighted: true
                onClicked: editor.document.applyCrop()
            }
            Button {
                visible: editor.document.cropping
                text: qsTr("Cancel")
                onClicked: editor.document.cancelCrop()
            }
            Button {
                visible: editor.document.editingText
                text: qsTr("Apply")
                highlighted: true
                onClicked: editor.document.commitText(draftInput.text)
            }
            Button {
                visible: editor.document.editingText
                text: qsTr("Cancel")
                onClicked: editor.document.cancelText()
            }
            Item { Layout.fillWidth: true }
            CheckBox {
                id: guides
                text: qsTr("Show guides")
            }
        }
    }
}
