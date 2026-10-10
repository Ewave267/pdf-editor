// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import PdfEditor 1.0

ApplicationWindow {
    id: root
    objectName: "viewerWindow"
    width: 1160
    height: 820
    minimumWidth: 700
    minimumHeight: 480
    visible: true
    flags: Qt.Window | Qt.WindowTitleHint | Qt.WindowSystemMenuHint
           | Qt.WindowMinimizeButtonHint | Qt.WindowMaximizeButtonHint | Qt.WindowCloseButtonHint
    title: pdfDocument.fileName ? pdfDocument.fileName + " — PDF Editor" : "PDF Editor"
    readonly property color backgroundColor: pdfDocument.darkTheme ? "#20252d" : "#e9edf2"
    readonly property color surfaceColor: pdfDocument.darkTheme ? "#2b323c" : "#ffffff"
    readonly property color panelColor: pdfDocument.darkTheme ? "#343d49" : "#f5f7fa"
    readonly property color textColor: pdfDocument.darkTheme ? "#edf1f7" : "#243342"
    readonly property color mutedColor: pdfDocument.darkTheme ? "#bdc9d8" : "#596878"
    color: backgroundColor
    palette.text: textColor
    palette.windowText: textColor
    palette.buttonText: textColor
    palette.base: surfaceColor
    font.family: "Sans Serif"
    font.pixelSize: 13
    palette.highlight: "#225a91"
    palette.button: panelColor
    palette.window: surfaceColor

    readonly property color borderColor: pdfDocument.darkTheme ? "#45505e" : "#dce2ea"
    readonly property color accentColor: pdfDocument.darkTheme ? "#91bfff" : "#185abc"
    readonly property color accentSurface: pdfDocument.darkTheme ? "#263f61" : "#e8f0fe"
    property bool showThumbnails: true
    readonly property bool canEdit: pdfDocument.ready && !pdfDocument.saving && !pdfDocument.additions.editing
    readonly property bool selectedText: pdfDocument.additions.selectionCount === 1 && root.selectedObject.type === "text"
    function addText() { textDialog.editing = false; textInput.text = ""; textDialog.loadStyle({}); textDialog.open() }
    function editText() { textDialog.editing = true; textInput.text = root.selectedObject.text; textDialog.loadStyle(root.selectedObject); textDialog.open() }
    function applyTextStyle(family, size, bold, italic, underline) {
        if (canEdit && selectedText)
            pdfDocument.additions.setTextStyle(pdfDocument.additions.selected, family, size, bold, italic, underline)
    }
    component EditorButton: ToolButton {
        id: control
        property string hint: text
        property bool primary: false
        implicitHeight: 36
        implicitWidth: Math.max(36, implicitContentWidth + 24)
        font.pixelSize: 13
        hoverEnabled: true
        Accessible.name: hint
        ToolTip.visible: hovered
        ToolTip.delay: 600
        ToolTip.text: Qt.platform.os === "osx" ? hint.replace("Ctrl+", "Command+") : hint
        contentItem: Text {
            text: control.text
            font: control.font
            color: !control.enabled ? root.mutedColor : control.primary ? "white" : control.checked ? root.accentColor : root.textColor
            opacity: control.enabled ? 1 : 0.45
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 6
            color: control.primary ? (control.down ? "#174a95" : "#185abc")
                 : control.checked ? root.accentSurface : control.down || control.hovered ? root.panelColor : "transparent"
            border.color: control.activeFocus ? root.accentColor : "transparent"
            opacity: control.enabled ? 1 : 0.55
        }
    }
    menuBar: MenuBar {
        objectName: "mainMenuBar"
        delegate: MenuBarItem { objectName: "menu" + text.replace("&", ""); font.pixelSize: 13 }
        background: Rectangle { color: root.surfaceColor }
        Menu {
            objectName: "fileMenu"; title: "&File"
            MenuItem { text: "Open PDF…"; enabled: !pdfDocument.saving; onTriggered: fileDialog.open() }
            Menu {
                id: recentMenu
                title: "Recent files"
                Instantiator {
                    model: pdfDocument.recentDocuments
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.name
                        onTriggered: root.openFile(modelData.url)
                    }
                    onObjectAdded: function(index, object) { recentMenu.insertItem(index, object) }
                    onObjectRemoved: function(index, object) { recentMenu.removeItem(object) }
                }
                MenuItem { enabled: false; visible: pdfDocument.recentDocuments.length === 0; text: "No recent documents" }
            }
            MenuSeparator {}
            MenuItem { text: "Save a copy…"; enabled: root.canEdit; onTriggered: saveDialog.open() }
            MenuItem { objectName: "pagesButton"; text: "Organize / export pages…"; enabled: root.canEdit && pdfDocument.formType !== "XFA"; onTriggered: pagesDialog.open() }
            MenuSeparator {}
            MenuItem { text: "Print…"; enabled: root.canEdit && !pdfDocument.formBusy; onTriggered: pdfDocument.print() }
            MenuItem { text: "Print to PDF…"; enabled: root.canEdit && !pdfDocument.formBusy; onTriggered: printSaveDialog.open() }
            MenuSeparator {}
            MenuItem { text: "Recover unsaved work…"; enabled: !pdfDocument.ready && !pdfDocument.loading; onTriggered: recoveryDialog.open() }
            MenuItem { text: "Close document"; enabled: !pdfDocument.saving && (pdfDocument.ready || pdfDocument.loading || pdfDocument.error || pdfDocument.dirty); onTriggered: root.closeDocument() }
        }
        Menu {
            objectName: "editMenu"; title: "&Edit"
            MenuItem { text: "Undo added content"; enabled: root.canEdit && pdfDocument.additions.canUndo; onTriggered: pdfDocument.additions.undo() }
            MenuItem { text: "Redo added content"; enabled: root.canEdit && pdfDocument.additions.canRedo; onTriggered: pdfDocument.additions.redo() }
            MenuSeparator {}
            MenuItem { objectName: "cutObjectsButton"; text: "Cut added content"; enabled: root.canEdit && pdfDocument.additions.selectionCount > 0; onTriggered: pdfDocument.additions.cutSelection() }
            MenuItem { objectName: "copyObjectsButton"; text: "Copy added content"; enabled: root.canEdit && pdfDocument.additions.selectionCount > 0; onTriggered: pdfDocument.additions.copySelection() }
            MenuItem { objectName: "pasteObjectsButton"; text: "Paste"; enabled: root.canEdit; onTriggered: pdfDocument.additions.paste(pdfDocument.currentPage - 1) }
            MenuItem { objectName: "selectAllObjectsButton"; text: "Select all added content on this page"; enabled: root.canEdit; onTriggered: pdfDocument.additions.selectAll(pdfDocument.currentPage - 1) }
            MenuSeparator {}
            MenuItem { text: "Find text…"; enabled: root.canEdit; onTriggered: searchDialog.open() }
        }
        Menu {
            objectName: "viewMenu"; title: "&View"
            MenuItem { text: "Page thumbnails"; checkable: true; checked: root.showThumbnails; onTriggered: root.showThumbnails = checked }
            MenuItem { text: "Fit page"; enabled: pdfDocument.ready; onTriggered: pdfDocument.fitToPage() }
            MenuItem { text: "Fit width"; enabled: pdfDocument.ready; onTriggered: pdfDocument.fitToWidth() }
            MenuSeparator {}
            MenuItem { text: "Dark appearance"; checkable: true; checked: pdfDocument.darkTheme; onTriggered: pdfDocument.darkTheme = checked }
            MenuItem { text: "Full screen"; checkable: true; checked: root.visibility === Window.FullScreen; onTriggered: root.visibility = checked ? Window.FullScreen : Window.Windowed }
        }
        Menu {
            title: "&Insert"; enabled: root.canEdit
            MenuItem { text: "Text box…"; onTriggered: root.addText() }
            MenuItem { text: "Image…"; onTriggered: { imageDialog.signature = false; imageDialog.open() } }
            MenuItem { text: "Signature…"; onTriggered: signDialog.open() }
            MenuSeparator {}
            MenuItem { text: "Checkmark…"; onTriggered: root.chooseGraphic("checkmark") }
            MenuItem { text: "Highlight area…"; onTriggered: root.chooseGraphic("highlight") }
            MenuItem { text: "Shape or drawing…"; onTriggered: drawMenu.popup() }
        }
        Menu {
            title: "F&orm"; enabled: root.canEdit && pdfDocument.formType !== "PDF"
            MenuItem { text: "All fields…"; enabled: pdfDocument.formHelpersAvailable; onTriggered: fieldsDialog.open() }
            MenuItem { text: "Check required fields…"; enabled: pdfDocument.formHelpersAvailable; onTriggered: { root.showValidation = true; pdfDocument.validateForm() } }
            MenuSeparator {}
            MenuItem { objectName: "resetFieldButton"; text: "Reset selected field…"; enabled: pdfDocument.formHelpersAvailable && pdfDocument.focusedField.id !== undefined && !pdfDocument.focusedField.readOnly && pdfDocument.focusedField.type >= 2 && pdfDocument.focusedField.type <= 6; onTriggered: { resetDialog.fieldId = pdfDocument.focusedField.id; resetDialog.open() } }
            MenuItem { text: "Reset form…"; enabled: pdfDocument.canResetForm; onTriggered: { resetDialog.fieldId = -1; resetDialog.open() } }
        }
        Menu {
            title: "&Help"
            MenuItem { text: "How to edit a PDF"; onTriggered: helpDialog.open() }
        }
    }
    Dialog {
        id: helpDialog
        objectName: "helpDialog"
        title: "Editing your PDF"
        anchors.centerIn: parent
        width: Math.min(480, root.width - 40)
        modal: true
        standardButtons: Dialog.Close
        ColumnLayout {
            width: parent.width; spacing: 16
            Label { text: "Fill a form"; font.bold: true }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "Click a highlighted field and type. Tab moves to the next field. Use Check form to review required fields." }
            Label { text: "Add something to a page"; font.bold: true }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "Choose Add text, Image, Sign or Draw, then click the page. Select added content to move, resize or format it. Original PDF text stays as it is." }
            Label { text: "Keep your changes"; font.bold: true }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "Save a copy writes a new PDF and keeps your original safe. You can move and format added objects during this session; after reopening a saved PDF they are part of the page." }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: root.mutedColor; text: "Shortcuts: Ctrl+O open · Ctrl+S save a copy · Ctrl+F find · Ctrl+P print. Use Command instead of Ctrl on Mac. Undo/redo applies to added content; signatures are visual marks." }
        }
    }

    DropArea {
        anchors.fill: parent
        onDropped: function(drop) {
            if (drop.hasUrls && drop.urls.length === 1) {
                root.openFile(drop.urls[0])
                drop.acceptProposedAction()
            }
        }
    }
    Shortcut { sequence: "F11"; onActivated: root.visibility = root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen }
    Shortcut { sequences: [StandardKey.Find]; enabled: root.canEdit && !root.anyDialogVisible; onActivated: searchDialog.open() }
    Dialog {
        id: searchDialog
        objectName: "searchDialog"
        title: "Find text"
        anchors.centerIn: parent
        width: Math.min(420, root.width - 40)
        modal: true
        standardButtons: Dialog.Close
        onOpened: query.forceActiveFocus()
        ColumnLayout {
            width: parent.width
            RowLayout {
                TextField { id: query; Layout.fillWidth: true; maximumLength: 256; Accessible.name: "Search text"; onAccepted: pdfDocument.search(text) }
                Button { text: pdfDocument.searching ? "Searching…" : "Find"; enabled: !pdfDocument.searching; onClicked: pdfDocument.search(query.text) }
            }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: pdfDocument.searchError || (pdfDocument.searching ? "Searching PDF text…" : pdfDocument.searchResults.length + " matching pages. Added content and image-only text are not included.") }
            ListView {
                Layout.fillWidth: true; Layout.preferredHeight: 220
                clip: true; model: pdfDocument.searchResults
                delegate: Button {
                    required property var modelData
                    width: ListView.view.width
                    text: "Page " + modelData.page
                    onClicked: { root.goToPage(modelData.page); searchDialog.close() }
                }
            }
        }
    }
    Shortcut { sequences: [StandardKey.Print]; enabled: root.canEdit && !root.anyDialogVisible; onActivated: pdfDocument.print() }
    Dialog {
        id: recoveryDialog
        title: "Recover previous session"
        anchors.centerIn: parent
        width: Math.min(520, root.width - 40)
        modal: true
        standardButtons: Dialog.Close
        ColumnLayout {
            width: parent.width
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "Unsaved work is checkpointed locally once a minute while idle. Recovery keeps form values and editable additions, but does not restore undo history. Closing or discarding a session removes its checkpoint. Dynamic XFA layout may change when its saved values are reopened." }
            Label { text: pdfDocument.recoveryError || (pdfDocument.recoveryDocuments.length ? "Select a checkpoint:" : "No previous checkpoints found."); Layout.fillWidth: true; wrapMode: Text.WordWrap }
            ListView {
                Layout.fillWidth: true; Layout.preferredHeight: 200; clip: true
                model: pdfDocument.recoveryDocuments
                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width
                    Button { text: "Recover " + modelData.name; Layout.fillWidth: true; onClicked: { pdfDocument.recover(modelData.path); recoveryDialog.close() } }
                    Button { text: "Discard"; onClicked: pdfDocument.discardRecovery(modelData.path) }
                }
            }
        }
    }
    function goToPage(page) {
        pdfDocument.currentPage = page
        pageView.positionViewAtIndex(pdfDocument.currentPage - 1, ListView.Beginning)
    }
    property url initialFolder: typeof startupFolder !== "undefined" ? startupFolder : ""
    property string placement: ""
    property string pendingText: ""
    property var pendingTextStyle: ({})
    property var pendingGraphic: ({})
    property var inkColors: ["#000000", "#c62828", "#225a91", "#237a38", "#ffdf00", "#7638a8"]
    function chooseGraphic(kind) {
        graphicDialog.editing = false
        graphicDialog.kind = kind
        inkColor.currentIndex = kind === "highlight" ? 4 : kind === "stamp" ? 1 : 0
        inkWidth.value = kind === "checkmark" ? 3 : 2
        stampText.text = "APPROVED"
        graphicDialog.open()
    }
    property url pendingImage
    property var selectedObject: ({})
    property bool saveBeforeContinuing: false
    property var discardAction
    function guarded(action) {
        if (pdfDocument.saving) return
        if (pdfDocument.dirty) { discardAction = action; discardDialog.open() }
        else action()
    }
    function closeDocument() { guarded(function() { pdfDocument.close() }) }
    function openFile(url) { guarded(function() { pdfDocument.open(url) }) }
    onClosing: function(close) {
        if (pdfDocument.saving) { close.accepted = false; return }
        if (pdfDocument.dirty) {
            close.accepted = false
            guarded(function() { pdfDocument.close(); root.close() })
        }
    }
    Dialog {
        id: discardDialog
        objectName: "discardDialog"
        title: "Save your changes?"
        anchors.centerIn: parent
        width: Math.min(440, root.width - 40)
        modal: true
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel
        Label { width: parent.width; wrapMode: Text.WordWrap; text: "Your changes haven't been saved. Save a copy to keep them, or discard them to continue." }
        onOpened: standardButton(Dialog.Save).text = "Save a copy"
        onAccepted: { root.saveBeforeContinuing = true; saveDialog.open() }
        onRejected: root.discardAction = null
        onDiscarded: { const action = root.discardAction; root.discardAction = null; if (action) action() }
    }
    Dialog {
        id: graphicDialog
        objectName: "graphicDialog"
        property bool editing: false
        property string kind: "rectangle"
        title: editing ? "Appearance" : "Add " + (kind === "drawing" ? "freehand drawing" : kind)
        anchors.centerIn: parent
        width: Math.min(400, root.width - 40)
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            width: parent.width
            RowLayout {
                Label { text: "Color" }
                ComboBox { id: inkColor; objectName: "inkColor"; model: ["Black", "Red", "Blue", "Green", "Yellow", "Purple"]; Layout.fillWidth: true; Accessible.name: "Ink color" }
            }
            RowLayout {
                visible: graphicDialog.kind !== "highlight" && graphicDialog.kind !== "text"
                Label { text: "Line width (pt)" }
                SpinBox { id: inkWidth; objectName: "inkWidth"; from: 1; to: 12; value: 2; editable: true; Accessible.name: "Line width in points" }
            }
            TextField { id: stampText; objectName: "stampText"; visible: graphicDialog.kind === "stamp"; Layout.fillWidth: true; maximumLength: 200; placeholderText: "Stamp text"; Accessible.name: "Stamp text"; onTextChanged: { if (graphicDialog.visible) graphicDialog.standardButton(Dialog.Ok).enabled = graphicDialog.kind !== "stamp" || text.trim().length > 0 } }
            Label { visible: !graphicDialog.editing; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: graphicDialog.kind === "drawing" ? "Press and drag on a page to draw. Release to finish." : "Click to place, or drag to choose the size." }
        }
        onOpened: standardButton(Dialog.Ok).enabled = kind !== "stamp" || stampText.text.trim().length > 0
        onAccepted: {
            const color = root.inkColors[inkColor.currentIndex]
            if (editing) {
                pdfDocument.additions.beginEdit()
                pdfDocument.additions.setAppearance(pdfDocument.additions.selected, color, inkWidth.value)
                if (kind === "stamp") pdfDocument.additions.setText(pdfDocument.additions.selected, stampText.text)
                pdfDocument.additions.endEdit()
            } else {
                root.pendingGraphic = { color: color, width: inkWidth.value, text: kind === "stamp" ? stampText.text : "" }
                root.placement = kind
            }
        }
    }
    Dialog {
        id: textDialog
        objectName: "textDialog"
        property bool editing: false
        title: editing ? "Edit text" : "Add text"
        anchors.centerIn: parent
        width: Math.min(440, root.width - 40)
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        function validateText() {
            standardButton(Dialog.Ok).enabled = textInput.text.trim().length > 0 && textInput.text.length <= 10000
        }
        function loadStyle(object) {
            const family = object.fontFamily || "Sans Serif"
            fontFamily.currentIndex = Math.max(0, fontFamily.find(family))
            fontSize.value = object.fontSize || 18
            boldText.checked = object.bold || false
            italicText.checked = object.italic || false
            underlineText.checked = object.underline || false
        }
        onOpened: { standardButton(Dialog.Ok).text = editing ? "Apply changes" : "Add to page"; textInput.forceActiveFocus(); validateText() }
        ColumnLayout {
            width: parent.width
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Font" }
                ComboBox {
                    id: fontFamily
                    objectName: "textFontFamily"
                    Layout.fillWidth: true
                    model: pdfDocument.additions.fontFamilies
                    Accessible.name: "Font family"
                }
                Label { text: "Size (pt)" }
                SpinBox {
                    id: fontSize
                    objectName: "textFontSize"
                    from: 6; to: 144; value: 18; editable: true
                    Accessible.name: "Font size in points"
                }
            }
            RowLayout {
                CheckBox { id: boldText; objectName: "textBold"; text: "Bold"; font.bold: true }
                CheckBox { id: italicText; objectName: "textItalic"; text: "Italic"; font.italic: true }
                CheckBox { id: underlineText; objectName: "textUnderline"; text: "Underline"; font.underline: true }
            }
            Label { text: textDialog.editing ? "Update your text below." : "Enter your text, then click the page to place it."; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: root.mutedColor }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 160
                TextArea {
                    id: textInput
                    objectName: "addedTextInput"
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    textFormat: TextEdit.PlainText
                    font.family: fontFamily.currentText
                    font.pixelSize: fontSize.value
                    font.bold: boldText.checked
                    font.italic: italicText.checked
                    font.underline: underlineText.checked
                    placeholderText: "Enter text (up to 10,000 characters)"
                    onTextChanged: { if (textDialog.visible) textDialog.validateText() }
                }
            }
        }
        onAccepted: {
            const style = { family: fontFamily.currentText, size: fontSize.value,
                            bold: boldText.checked, italic: italicText.checked, underline: underlineText.checked }
            if (editing) {
                pdfDocument.additions.beginEdit()
                pdfDocument.additions.setText(pdfDocument.additions.selected, textInput.text)
                pdfDocument.additions.setTextStyle(pdfDocument.additions.selected,
                    style.family, style.size, style.bold, style.italic, style.underline)
                pdfDocument.additions.endEdit()
            }
            else if (textInput.text.trim().length > 0 && textInput.text.length <= 10000) {
                root.pendingText = textInput.text; root.pendingTextStyle = style; root.placement = "text"
            }
        }
    }
    FileDialog {
        id: imageDialog
        objectName: "imageDialog"
        currentFolder: root.initialFolder
        property bool signature: false
        title: signature ? "Choose a signature image" : "Choose an image"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp)"]
        onAccepted: { root.pendingImage = selectedFile; root.placement = signature ? "signature" : "image" }
    }
    Shortcut { sequence: "Escape"; enabled: !helpDialog.visible && !root.formPopupVisible && !root.workflowPopupVisible && !textDialog.visible && !graphicDialog.visible && !imageDialog.visible && !saveDialog.visible && !discardDialog.visible && !fileDialog.visible && !saveFailure.visible && !(root.activeFocusItem instanceof FormInput); onActivated: { root.placement = ""; pdfDocument.additions.cancelEdit(); pdfDocument.additions.select(-1) } }
    Connections {
        target: pdfDocument.additions
        function onChanged() { root.selectedObject = pdfDocument.additions.object(pdfDocument.additions.selected) }
    }


    property bool highlightFields: true
    property bool showValidation: false
    function revealFormField(page, field) {
        pageView.positionViewAtIndex(page, ListView.Beginning)
        Qt.callLater(function() {
            const delegate = pageView.itemAtIndex(page)
            if (!delegate) return
            const surface = delegate.formSurface
            const top = surface ? surface.mapToItem(pageView.contentItem, 0, (field.y || 0) * pdfDocument.zoom).y : delegate.y + (field.y || 0) * pdfDocument.zoom
            const bottom = top + (field.height || 24) * pdfDocument.zoom
            if (bottom > pageView.contentY + pageView.height - 16)
                pageView.contentY = Math.max(delegate.y, bottom - pageView.height + 16)
            if (top < pageView.contentY + 16) pageView.contentY = Math.max(delegate.y, top - 16)
            if (surface) surface.forceActiveFocus(Qt.OtherFocusReason)
        })
    }
    Connections {
        target: pdfDocument
        function onStateChanged() {
            if (!pdfDocument.ready) root.showValidation = false
            if (pdfDocument.ready && pdfDocument.formType !== "PDF" && !root.highlightFields)
                pdfDocument.formEvent(0, "highlight", 0, 0, 0)
        }
        function onFormFocusRequested(page, field) { root.revealFormField(page, field) }
        function onFormEventFinished(id, handled) {
            if (root.showValidation && !pdfDocument.formBusy) {
                root.showValidation = false
                validationDialog.open()
            }
        }
    }
    Dialog {
        id: fieldsDialog; objectName: "fieldsDialog"
        title: "Form fields · * required"
        anchors.centerIn: parent; width: Math.min(520, root.width - 40)
        height: Math.min(520, root.height - 80); modal: true
        standardButtons: Dialog.Close
        ListView {
            anchors.fill: parent; clip: true; spacing: 4
            model: pdfDocument.formFields
            delegate: ItemDelegate {
                required property var modelData
                width: ListView.view.width
                id: fieldEntry
                objectName: "formFieldItem" + modelData.id
                contentItem: Label { text: fieldEntry.text; textFormat: Text.PlainText; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                text: "Page " + (modelData.page + 1) + " · " + (modelData.label || "Unnamed field")
                    + (modelData.required ? " *" : "") + (modelData.readOnly ? " · Read only" : "")
                enabled: !modelData.readOnly
                onClicked: { fieldsDialog.close(); pdfDocument.focusFormField(modelData.id) }
            }
        }
    }
    Dialog {
        id: validationDialog; objectName: "formValidationDialog"
        title: pdfDocument.formValidation.length || pdfDocument.formError ? "Form needs attention" : "Form checks passed"
        anchors.centerIn: parent; width: Math.min(540, root.width - 40)
        height: Math.min(420, root.height - 80); modal: true; standardButtons: Dialog.Close
        ColumnLayout {
            anchors.fill: parent
            Label { Layout.fillWidth: true; visible: !!pdfDocument.formError; wrapMode: Text.WordWrap; textFormat: Text.PlainText; text: pdfDocument.formError; color: "#c62828" }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "Checks required fields and recognized date formats. Document scripts still control accepted values. Review the form before submitting." }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                model: pdfDocument.formValidation
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    id: issueEntry
                    objectName: "formIssueItem" + modelData.id
                    contentItem: Label { text: issueEntry.text; textFormat: Text.PlainText; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                    text: (modelData.label || "Unnamed field") + " · " + modelData.message
                    onClicked: { validationDialog.close(); pdfDocument.focusFormField(modelData.id) }
                }
            }
        }
    }
    Dialog {
        id: resetDialog; objectName: "resetFormDialog"
        property int fieldId: -1
        title: fieldId < 0 ? "Reset form?" : "Reset field?"
        anchors.centerIn: parent; width: Math.min(450, root.width - 40)
        modal: true; standardButtons: Dialog.Ok | Dialog.Cancel
        Label { width: parent.width; wrapMode: Text.WordWrap; text: "Restore values from when this PDF was opened? Text, drawings and other additions are kept. Document calculations and validation still run." }
        onAccepted: { if (fieldId < 0) pdfDocument.resetForm(); else pdfDocument.resetFormField(fieldId) }
    }
    Dialog {
        id: choiceDialog; objectName: "formChoiceDialog"
        property var field: ({})
        title: "Choose a value"
        anchors.centerIn: parent; width: Math.min(440, root.width - 40)
        height: Math.min(420, root.height - 80); modal: true; standardButtons: Dialog.Cancel
        ListView {
            anchors.fill: parent; clip: true
            model: choiceDialog.field.options || []
            delegate: ItemDelegate {
                required property string modelData
                required property int index
                id: choiceEntry
                objectName: "formOptionItem" + index
                contentItem: Label { text: choiceEntry.text; textFormat: Text.PlainText; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                width: ListView.view.width; text: modelData
                highlighted: (choiceDialog.field.selectedOptions || []).indexOf(index) >= 0
                onClicked: { choiceDialog.close(); pdfDocument.chooseFormOption(choiceDialog.field.id, index) }
            }
        }
    }
    Dialog {
        id: dateDialog; objectName: "formDateDialog"
        property var field: ({})
        title: "Enter a date"
        anchors.centerIn: parent; width: Math.min(420, root.width - 40)
        modal: true; standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            width: parent.width
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.PlainText; text: (dateDialog.field.label || "Date") + " · " + (dateDialog.field.dateFormat || "yyyy-mm-dd") }
            RowLayout {
                SpinBox { id: dateYear; objectName: "dateYear"; from: 1900; to: 2100; value: new Date().getFullYear(); editable: true; Accessible.name: "Year" }
                SpinBox { id: dateMonth; objectName: "dateMonth"; from: 1; to: 12; value: new Date().getMonth()+1; editable: true; Accessible.name: "Month" }
                SpinBox { id: dateDay; objectName: "dateDay"; from: 1; to: new Date(dateYear.value, dateMonth.value, 0).getDate(); value: 1; editable: true; Accessible.name: "Day" }
            }
        }
        onAccepted: {
            const format = (field.dateFormat || "yyyy-mm-dd").replace("mm", "MM")
            const value = Qt.formatDate(new Date(dateYear.value, dateMonth.value-1, dateDay.value), format)
            pdfDocument.setFormFieldText(field.id, value)
        }
    }
    Dialog {
        id: signDialog; objectName: "signDialog"
        title: "Add a visual signature"
        anchors.centerIn: parent; width: Math.min(450, root.width - 40)
        modal: true; standardButtons: Dialog.Cancel
        ColumnLayout {
            width: parent.width
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "Draw on the page or import a signature image, then resize and place it. This adds visible content; it does not digitally sign a native signature field. Added signatures cannot be saved on dynamic XFA yet." }
            Button { objectName: "drawSignatureButton"; text: "Draw signature"; onClicked: { signDialog.close(); root.chooseGraphic("drawing") } }
            Button { objectName: "importSignatureButton"; text: "Import signature image"; onClicked: { signDialog.close(); imageDialog.signature = true; imageDialog.open() } }
        }
    }
    FileDialog {
        id: fileDialog
        objectName: "openDialog"
        currentFolder: root.initialFolder
        title: "Open a PDF"
        nameFilters: ["PDF documents (*.pdf)", "All files (*)"]
        onAccepted: root.openFile(selectedFile)
    }
    FileDialog {
        id: saveDialog
        objectName: "saveDialog"
        currentFolder: root.initialFolder
        title: "Save a copy"
        fileMode: FileDialog.SaveFile
        nameFilters: ["PDF documents (*.pdf)"]
        defaultSuffix: "pdf"
        onRejected: { root.saveBeforeContinuing = false; root.discardAction = null }
        onAccepted: pdfDocument.saveAs(selectedFile)
    }
    Dialog {
        id: pagesDialog
        objectName: "pagesDialog"
        property var mergeFiles: []
        title: "Export pages"
        anchors.centerIn: parent
        width: Math.min(520, root.width - 40)
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: { pageOrder.text = "1-" + pdfDocument.pageCount; mergeFiles = [] }
        onAccepted: pagesSaveDialog.open()
        ColumnLayout {
            width: parent.width
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "Export a new PDF. List pages in the order you want, omit pages to remove them, or select a range to extract it. Example: 3,1-2,blank. Blank pages are US Letter size. The open document keeps its edits." }
            TextField { id: pageOrder; objectName: "exportPageOrder"; Layout.fillWidth: true; placeholderText: "1-3,5,blank"; maximumLength: 16000; Accessible.name: "Page order" }
            ComboBox { id: pageRotation; model: ["No rotation", "90° clockwise", "180°", "270° clockwise"]; Accessible.name: "Rotate exported pages" }
            Button { text: "Append PDFs… (" + pagesDialog.mergeFiles.length + ")"; onClicked: mergeDialog.open() }
            CheckBox { id: flattenForms; text: "Flatten annotations and forms"; checked: pdfDocument.formType === "AcroForm" }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "Flattening makes visible form values and annotations permanent. Interactive fields, document scripts, bookmarks and document-level metadata are not carried into the export." }
        }
    }
    FileDialog {
        id: mergeDialog
        currentFolder: root.initialFolder
        title: "Append PDFs in selection order"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["PDF documents (*.pdf)"]
        onAccepted: pagesDialog.mergeFiles = selectedFiles
    }
    FileDialog {
        id: printSaveDialog
        currentFolder: root.initialFolder
        title: "Print to a new PDF"
        fileMode: FileDialog.SaveFile
        nameFilters: ["PDF documents (*.pdf)"]
        defaultSuffix: "pdf"
        onAccepted: pdfDocument.printToPdf(selectedFile)
    }
    FileDialog {
        id: pagesSaveDialog
        currentFolder: root.initialFolder
        title: "Export to a new PDF"
        fileMode: FileDialog.SaveFile
        nameFilters: ["PDF documents (*.pdf)"]
        defaultSuffix: "pdf"
        onAccepted: pdfDocument.exportPages(selectedFile, pageOrder.text, pageRotation.currentIndex * 90, flattenForms.checked, pagesDialog.mergeFiles)
    }
    Dialog {
        id: saveFailure
        objectName: "saveFailureDialog"
        title: "Couldn't save the PDF"
        anchors.centerIn: parent
        modal: true
        width: Math.min(480, root.width - 40)
        standardButtons: Dialog.Ok
        Label { width: parent.width; text: pdfDocument.saveError; wrapMode: Text.WordWrap }
    }
    Connections {
        target: pdfDocument
        function onSaveFinished(success) {
            if (root.saveBeforeContinuing) {
                root.saveBeforeContinuing = false
                const action = root.discardAction
                root.discardAction = null
                if (success && !pdfDocument.dirty && action) action()
            }
            if (!success && pdfDocument.saveError) saveFailure.open()
        }
    }
    Shortcut { sequences: [StandardKey.Save, StandardKey.SaveAs]; enabled: root.canEdit && !root.anyDialogVisible; onActivated: saveDialog.open() }
    Shortcut { sequences: [StandardKey.Open]; enabled: !pdfDocument.saving && !root.anyDialogVisible; onActivated: fileDialog.open() }
    Shortcut { sequences: [StandardKey.Close]; enabled: !pdfDocument.saving && !root.anyDialogVisible; onActivated: root.closeDocument() }
    Shortcut { sequence: "Ctrl++"; onActivated: pdfDocument.zoom *= 1.2 }
    Shortcut { sequence: "Ctrl+-"; onActivated: pdfDocument.zoom /= 1.2 }
    Shortcut { sequence: "Ctrl+0"; onActivated: pdfDocument.fitToPage() }
    property bool workflowPopupVisible: helpDialog.visible || pagesDialog.visible || pagesSaveDialog.visible || mergeDialog.visible || printSaveDialog.visible || searchDialog.visible || recoveryDialog.visible
    property bool formPopupVisible: fieldsDialog.visible || validationDialog.visible || resetDialog.visible || choiceDialog.visible || dateDialog.visible || signDialog.visible
    readonly property bool anyDialogVisible: root.formPopupVisible || root.workflowPopupVisible || textDialog.visible || graphicDialog.visible || imageDialog.visible || fileDialog.visible || saveDialog.visible || discardDialog.visible || saveFailure.visible
    property bool additionHistoryShortcuts: pdfDocument.ready && !pdfDocument.saving
        && !pdfDocument.additions.editing && !graphicDialog.visible && !helpDialog.visible && !root.formPopupVisible && !root.workflowPopupVisible
        && !textDialog.visible && !imageDialog.visible && !saveDialog.visible && !discardDialog.visible && !fileDialog.visible && !saveFailure.visible
        && !(root.activeFocusItem instanceof FormInput)
        && !(root.activeFocusItem && typeof root.activeFocusItem.undo === "function")
    Shortcut { sequences: [StandardKey.Undo]; enabled: root.additionHistoryShortcuts && pdfDocument.additions.canUndo; onActivated: pdfDocument.additions.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: root.additionHistoryShortcuts && pdfDocument.additions.canRedo; onActivated: pdfDocument.additions.redo() }
    Shortcut { sequences: [StandardKey.Copy]; enabled: root.additionHistoryShortcuts && pdfDocument.additions.selectionCount > 0; onActivated: pdfDocument.additions.copySelection() }
    Shortcut { sequences: [StandardKey.Cut]; enabled: root.additionHistoryShortcuts && pdfDocument.additions.selectionCount > 0; onActivated: pdfDocument.additions.cutSelection() }
    Shortcut { sequences: [StandardKey.Paste]; enabled: root.additionHistoryShortcuts; onActivated: pdfDocument.additions.paste(pdfDocument.currentPage - 1) }
    Shortcut { sequences: [StandardKey.SelectAll]; enabled: root.additionHistoryShortcuts; onActivated: pdfDocument.additions.selectAll(pdfDocument.currentPage - 1) }
    Shortcut { sequences: ["Delete", "Backspace"]; enabled: root.additionHistoryShortcuts && pdfDocument.additions.selectionCount > 0; onActivated: pdfDocument.additions.removeSelected() }
    Shortcut { sequence: "Left"; enabled: root.additionHistoryShortcuts && pdfDocument.additions.selectionCount > 0; onActivated: pdfDocument.additions.nudgeSelection(-1, 0) }
    Shortcut { sequence: "Right"; enabled: root.additionHistoryShortcuts && pdfDocument.additions.selectionCount > 0; onActivated: pdfDocument.additions.nudgeSelection(1, 0) }
    Shortcut { sequence: "Up"; enabled: root.additionHistoryShortcuts && pdfDocument.additions.selectionCount > 0; onActivated: pdfDocument.additions.nudgeSelection(0, -1) }
    Shortcut { sequence: "Down"; enabled: root.additionHistoryShortcuts && pdfDocument.additions.selectionCount > 0; onActivated: pdfDocument.additions.nudgeSelection(0, 1) }
    Shortcut { sequence: "Shift+Left"; enabled: root.additionHistoryShortcuts && pdfDocument.additions.selectionCount > 0; onActivated: pdfDocument.additions.nudgeSelection(-10, 0) }
    Shortcut { sequence: "Shift+Right"; enabled: root.additionHistoryShortcuts && pdfDocument.additions.selectionCount > 0; onActivated: pdfDocument.additions.nudgeSelection(10, 0) }
    Shortcut { sequence: "Shift+Up"; enabled: root.additionHistoryShortcuts && pdfDocument.additions.selectionCount > 0; onActivated: pdfDocument.additions.nudgeSelection(0, -10) }
    Shortcut { sequence: "Shift+Down"; enabled: root.additionHistoryShortcuts && pdfDocument.additions.selectionCount > 0; onActivated: pdfDocument.additions.nudgeSelection(0, 10) }

    header: ToolBar {
        implicitHeight: 58
        background: Rectangle { color: root.surfaceColor; border.color: root.borderColor }
        RowLayout {
            anchors.fill: parent; anchors.margins: 10; spacing: 6
            EditorButton { objectName: "openButton"; text: "Open"; hint: "Open a PDF (Ctrl+O)"; enabled: !pdfDocument.saving; onClicked: fileDialog.open() }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: root.borderColor }
            EditorButton { objectName: "undoAdditionsButton"; text: "↶"; font.pixelSize: 22; hint: "Undo added content (Ctrl+Z)"; enabled: root.canEdit && pdfDocument.additions.canUndo; onClicked: pdfDocument.additions.undo() }
            EditorButton { objectName: "redoAdditionsButton"; text: "↷"; font.pixelSize: 22; hint: "Redo added content"; enabled: root.canEdit && pdfDocument.additions.canRedo; onClicked: pdfDocument.additions.redo() }
            Label { textFormat: Text.PlainText; text: pdfDocument.ready ? pdfDocument.fileName : "PDF Editor"; color: root.textColor; font.bold: true; elide: Text.ElideMiddle; Layout.fillWidth: true; Layout.leftMargin: 12 }
            Label { objectName: "documentSaveState"; visible: pdfDocument.ready && root.width >= 900; text: pdfDocument.dirty ? "Unsaved changes" : pdfDocument.savedPath ? "Copy saved" : "Original document"; color: pdfDocument.dirty ? root.accentColor : root.mutedColor; font.pixelSize: 12 }
            EditorButton { objectName: "findButton"; text: "Find"; hint: "Find text in the PDF (Ctrl+F)"; enabled: root.canEdit; onClicked: searchDialog.open() }
            EditorButton { objectName: "saveButton"; text: pdfDocument.saving ? "Saving…" : "Save a copy"; hint: "Save your edits to a new PDF (Ctrl+S)"; primary: true; enabled: root.canEdit; onClicked: saveDialog.open() }
            EditorButton { objectName: "closeButton"; text: "×"; font.pixelSize: 20; hint: "Close document"; enabled: !pdfDocument.saving && (pdfDocument.ready || pdfDocument.loading || pdfDocument.error || pdfDocument.dirty); onClicked: root.closeDocument() }
        }
    }
    footer: ToolBar {
        visible: pdfDocument.ready
        implicitHeight: 44
        background: Rectangle { color: root.surfaceColor; border.color: root.borderColor }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 4
            EditorButton { objectName: "thumbnailsButton"; text: "Pages"; hint: "Show or hide page thumbnails"; checkable: true; checked: root.showThumbnails; enabled: pdfDocument.ready; onClicked: root.showThumbnails = checked }
            Label { objectName: "statusLabel"; textFormat: Text.PlainText; text: pdfDocument.loading ? "Opening…" : pdfDocument.saving ? "Saving…" : pdfDocument.dirty ? "Unsaved changes" : pdfDocument.ready ? "Ready" : "Open a PDF to get started"; color: root.mutedColor; elide: Text.ElideRight; Layout.fillWidth: true }
            EditorButton { objectName: "previousButton"; text: "‹"; font.pixelSize: 22; hint: "Previous page"; enabled: pdfDocument.ready && pdfDocument.currentPage > 1; onClicked: root.goToPage(pdfDocument.currentPage - 1) }
            SpinBox { objectName: "pageNumber"; implicitHeight: 32; implicitWidth: 100; from: 1; to: Math.max(1, pdfDocument.pageCount); value: pdfDocument.currentPage; editable: true; enabled: pdfDocument.ready; onValueModified: root.goToPage(value); Accessible.name: "Page number" }
            Label { text: "of " + pdfDocument.pageCount; color: root.mutedColor }
            EditorButton { objectName: "nextButton"; text: "›"; font.pixelSize: 22; hint: "Next page"; enabled: pdfDocument.ready && pdfDocument.currentPage < pdfDocument.pageCount; onClicked: root.goToPage(pdfDocument.currentPage + 1) }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: root.borderColor; Layout.leftMargin: 8; Layout.rightMargin: 8 }
            EditorButton { objectName: "zoomOutButton"; text: "−"; hint: "Zoom out"; enabled: pdfDocument.ready && pdfDocument.zoom > 0.25; onClicked: pdfDocument.zoom /= 1.2 }
            Label { objectName: "zoomLabel"; text: Math.round(pdfDocument.zoom * 100) + "%"; horizontalAlignment: Text.AlignHCenter; Layout.preferredWidth: 42; color: root.mutedColor }
            EditorButton { objectName: "zoomInButton"; text: "+"; hint: "Zoom in"; enabled: pdfDocument.ready && pdfDocument.zoom < 3; onClicked: pdfDocument.zoom *= 1.2 }
            EditorButton { objectName: "fitButton"; text: "Fit page"; enabled: pdfDocument.ready; checked: pdfDocument.fitting; onClicked: pdfDocument.fitToPage() }
            EditorButton { text: "Fit width"; visible: root.width >= 850; enabled: pdfDocument.ready; onClicked: pdfDocument.fitToWidth() }
        }
    }
    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            visible: pdfDocument.ready && root.showThumbnails
            Layout.preferredWidth: root.width < 900 ? 144 : 170
            Layout.fillHeight: true
            color: root.panelColor
            border.color: root.borderColor
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                Label { text: "Pages"; font.bold: true; font.pixelSize: 13; color: root.mutedColor; Layout.topMargin: 4; Layout.bottomMargin: 6 }
                ListView {
                    id: thumbnails
                    objectName: "thumbnailView"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: pdfDocument.pages
                    clip: true
                    spacing: 12
                    cacheBuffer: 200
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Item {
                        required property int index
                        required property var modelData
                        width: thumbnails.width
                        height: Math.min(180, 110 * modelData.height / modelData.width) + 30
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 120
                            height: parent.height - 22
                            radius: 4
                            color: pdfDocument.currentPage === index + 1 ? (pdfDocument.darkTheme ? "#365373" : "#dceaf8") : root.panelColor
                            border.color: pdfDocument.currentPage === index + 1 ? "#225a91" : "#cbd4df"
                            PdfPage {
                                thumbnail: true
                                id: thumbnailPage
                                objectName: "thumbnail" + index
                                anchors.centerIn: parent
                                width: Math.min(110, (parent.height - 10) * modelData.width / modelData.height)
                                height: width * modelData.height / modelData.width
                                document: pdfDocument
                                page: index
                            }
                            AddedOverlay { anchors.fill: thumbnailPage; content: pdfDocument.additions; page: index; selection: false }
                        }
                        Label { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; text: index + 1; color: root.mutedColor; font.pixelSize: 12 }
                        MouseArea { anchors.fill: parent; onClicked: root.goToPage(index + 1); cursorShape: Qt.PointingHandCursor }
                    }
                }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Rectangle {
                anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                height: contextTools.y + contextTools.height + 6
                color: root.surfaceColor; visible: pdfDocument.ready
            }
            RowLayout {
                id: contentTools
                anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                anchors.margins: 8; height: 40; spacing: 4
                visible: pdfDocument.ready; enabled: root.canEdit
                EditorButton { objectName: "selectToolButton"; text: "Select"; hint: "Select, move or resize added content"; checked: root.placement === ""; onClicked: root.placement = "" }
                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: root.borderColor; Layout.leftMargin: 4; Layout.rightMargin: 4 }
                EditorButton { objectName: "addTextButton"; text: "Add text"; hint: "Add a new text box to the page"; checked: root.placement === "text"; onClicked: root.addText() }
                EditorButton { objectName: "addImageButton"; text: "Image"; hint: "Add an image"; checked: root.placement === "image"; onClicked: { imageDialog.signature = false; imageDialog.open() } }
                EditorButton { objectName: "addSignatureButton"; text: "Sign"; hint: "Draw or import a signature"; checked: root.placement === "signature"; onClicked: signDialog.open() }
                EditorButton {
                    objectName: "drawToolsButton"; text: "Draw ▾"; hint: "Draw, highlight, add shapes or a checkmark"; onClicked: drawMenu.popup()
                        Menu {
                            id: drawMenu
                            MenuItem { objectName: "checkmarkTool"; text: "Checkmark"; onTriggered: root.chooseGraphic("checkmark") }
                            MenuItem { objectName: "drawingTool"; text: "Freehand"; onTriggered: root.chooseGraphic("drawing") }
                            MenuItem { objectName: "highlightTool"; text: "Highlight"; onTriggered: root.chooseGraphic("highlight") }
                            MenuItem { objectName: "rectangleTool"; text: "Rectangle"; onTriggered: root.chooseGraphic("rectangle") }
                            MenuItem { objectName: "ellipseTool"; text: "Ellipse"; onTriggered: root.chooseGraphic("ellipse") }
                            MenuItem { objectName: "lineTool"; text: "Line"; onTriggered: root.chooseGraphic("line") }
                            MenuItem { objectName: "stampTool"; text: "Stamp"; onTriggered: root.chooseGraphic("stamp") }
                        }
                }
                Item { Layout.fillWidth: true }
                EditorButton { objectName: "organizePagesButton"; text: "Organize pages"; visible: root.width >= 1000; enabled: pdfDocument.formType !== "XFA"; onClicked: pagesDialog.open() }
            }
            RowLayout {
                id: contextTools
                anchors.top: contentTools.bottom; anchors.left: parent.left; anchors.right: parent.right
                anchors.leftMargin: 8; anchors.rightMargin: 8; height: 38; spacing: 4
                visible: pdfDocument.ready; enabled: root.canEdit
                EditorButton { objectName: "selectAreaButton"; text: "Select area"; hint: "Drag around added objects to select them together"; visible: !root.placement; checked: root.placement === "selection"; onClicked: root.placement = "selection" }
                Label { objectName: "toolHint"; textFormat: Text.PlainText; Layout.fillWidth: true; elide: Text.ElideRight; color: root.placement ? root.accentColor : root.mutedColor; font.pixelSize: 12; text: pdfDocument.error || pdfDocument.formError || pdfDocument.additions.error || (root.placement ? (root.placement === "selection" ? "Drag around the objects to select" : root.placement === "text" || root.placement === "image" || root.placement === "signature" || root.placement === "stamp" || root.placement === "checkmark" ? "Click the page to place " + root.placement : "Drag on the page to draw " + root.placement) : pdfDocument.additions.selectionCount > 1 ? pdfDocument.additions.selectionCount + " objects selected" : pdfDocument.additions.selectionCount === 1 ? (root.selectedObject.type === "text" ? "Text box selected" : "Object selected") : pdfDocument.formType !== "PDF" ? "Click a highlighted field to fill in your form" : "Choose a tool above, then click the page") }
                EditorButton { objectName: "cancelToolButton"; text: "Cancel"; visible: !!root.placement; hint: "Cancel this tool (Esc)"; onClicked: root.placement = "" }
                ComboBox { objectName: "selectionFontFamily"; visible: root.selectedText && root.width >= 1000 && !root.placement; Layout.preferredWidth: 150; implicitHeight: 32; model: pdfDocument.additions.fontFamilies; currentIndex: Math.max(0, pdfDocument.additions.fontFamilies.indexOf(root.selectedObject.fontFamily || "Sans Serif")); Accessible.name: "Selected text font"; onActivated: root.applyTextStyle(currentText, root.selectedObject.fontSize, root.selectedObject.bold, root.selectedObject.italic, root.selectedObject.underline) }
                SpinBox { objectName: "selectionFontSize"; visible: root.selectedText && root.width >= 1000 && !root.placement; implicitWidth: 100; implicitHeight: 32; from: 6; to: 144; editable: true; value: root.selectedObject.fontSize || 18; Accessible.name: "Selected text size in points"; onValueModified: root.applyTextStyle(root.selectedObject.fontFamily, value, root.selectedObject.bold, root.selectedObject.italic, root.selectedObject.underline) }
                EditorButton { objectName: "selectionBold"; text: "B"; hint: "Bold"; font.bold: true; visible: root.selectedText && !root.placement; checkable: true; checked: root.selectedObject.bold || false; onClicked: root.applyTextStyle(root.selectedObject.fontFamily, root.selectedObject.fontSize, checked, root.selectedObject.italic, root.selectedObject.underline) }
                EditorButton { objectName: "selectionItalic"; text: "I"; hint: "Italic"; font.italic: true; visible: root.selectedText && !root.placement; checkable: true; checked: root.selectedObject.italic || false; onClicked: root.applyTextStyle(root.selectedObject.fontFamily, root.selectedObject.fontSize, root.selectedObject.bold, checked, root.selectedObject.underline) }
                EditorButton { objectName: "selectionUnderline"; text: "U"; hint: "Underline"; font.underline: true; visible: root.selectedText && !root.placement; checkable: true; checked: root.selectedObject.underline || false; onClicked: root.applyTextStyle(root.selectedObject.fontFamily, root.selectedObject.fontSize, root.selectedObject.bold, root.selectedObject.italic, checked) }
                EditorButton { objectName: "editTextButton"; text: "Edit text"; visible: !root.placement && pdfDocument.additions.selectionCount === 1 && (root.selectedObject.type === "text" || root.selectedObject.type === "stamp"); onClicked: root.editText() }
                EditorButton {
                    objectName: "alignObjectsButton"; text: "Align ▾"; visible: !root.placement && pdfDocument.additions.selectionCount > 1; onClicked: alignMenu.popup()
                        Menu {
                            id: alignMenu
                            MenuItem { objectName: "alignLeft"; text: "Left"; onTriggered: pdfDocument.additions.alignSelection("left") }
                            MenuItem { text: "Horizontal center"; onTriggered: pdfDocument.additions.alignSelection("center") }
                            MenuItem { text: "Right"; onTriggered: pdfDocument.additions.alignSelection("right") }
                            MenuItem { text: "Top"; onTriggered: pdfDocument.additions.alignSelection("top") }
                            MenuItem { text: "Vertical center"; onTriggered: pdfDocument.additions.alignSelection("middle") }
                            MenuItem { text: "Bottom"; onTriggered: pdfDocument.additions.alignSelection("bottom") }
                        }
                }
                EditorButton { objectName: "appearanceButton"; text: "Style…"; visible: !root.placement && pdfDocument.additions.selectionCount === 1 && root.selectedObject.type !== "image" && root.selectedObject.type !== "signature"; onClicked: {
                    graphicDialog.editing = true; graphicDialog.kind = root.selectedObject.type
                    inkColor.currentIndex = Math.max(0, root.inkColors.indexOf(root.selectedObject.color.substring(0, 1) + root.selectedObject.color.substring(3)))
                    inkWidth.value = root.selectedObject.lineWidth; stampText.text = root.selectedObject.text; graphicDialog.open()
                } }
                EditorButton { objectName: "deleteObjectButton"; text: "Delete"; visible: !root.placement && pdfDocument.additions.selectionCount > 0; onClicked: pdfDocument.additions.removeSelected() }
            }
            Item {
                id: formTools
                anchors.top: contextTools.bottom; anchors.left: parent.left; anchors.right: parent.right
                anchors.margins: 8; height: formToolsRow.implicitHeight
                visible: pdfDocument.ready && pdfDocument.formType !== "PDF"
                enabled: !pdfDocument.saving && !pdfDocument.additions.editing
                Flow {
                    id: formToolsRow; width: parent.width; spacing: 2
                    CheckBox { objectName: "highlightFieldsButton"; text: "Highlight fields"; checked: root.highlightFields; onToggled: { root.highlightFields = checked; pdfDocument.formEvent(pdfDocument.currentPage-1, "highlight", 0, 0, checked ? 1 : 0) } }
                    EditorButton { objectName: "formFieldsButton"; text: "Fields"; enabled: pdfDocument.formHelpersAvailable; onClicked: fieldsDialog.open() }
                    EditorButton { objectName: "previousFieldButton"; text: "Previous"; enabled: pdfDocument.formHelpersAvailable; onClicked: pdfDocument.navigateForm(true) }
                    EditorButton { objectName: "nextFieldButton"; text: "Next"; enabled: pdfDocument.formHelpersAvailable; onClicked: pdfDocument.navigateForm() }
                    EditorButton { objectName: "validateFormButton"; text: "Check form"; enabled: pdfDocument.formHelpersAvailable; onClicked: { root.showValidation = true; pdfDocument.validateForm() } }
                    EditorButton { objectName: "resetWholeFormButton"; text: "Reset form"; enabled: pdfDocument.canResetForm; onClicked: { resetDialog.fieldId = -1; resetDialog.open() } }
                    EditorButton { objectName: "chooseFormValueButton"; text: "Choose value"; visible: (pdfDocument.focusedField.options || []).length > 0; enabled: !!pdfDocument.focusedField.choicesComplete; onClicked: { choiceDialog.field = pdfDocument.focusedField; choiceDialog.open() } }
                    EditorButton { objectName: "enterFormDateButton"; text: "Date…"; visible: (pdfDocument.focusedField.dateFormat || "").length > 0; onClicked: {
                        dateDialog.field = pdfDocument.focusedField
                        const parsed = Date.fromLocaleString(Qt.locale("en_US"), dateDialog.field.value, dateDialog.field.dateFormat.replace("mm", "MM"))
                        const date = isNaN(parsed.getTime()) ? new Date() : parsed
                        dateYear.value = date.getFullYear(); dateMonth.value = date.getMonth()+1; dateDay.value = date.getDate(); dateDialog.open()
                    } }
                    EditorButton { objectName: "signFormButton"; text: "Sign…"; onClicked: signDialog.open() }

                }
            }
            ListView {
                id: pageView
                objectName: "pageView"
                anchors.fill: parent
                anchors.topMargin: formTools.visible ? formTools.y + formTools.height + 8 : contextTools.y + contextTools.height + 8
                visible: pdfDocument.ready
                model: pdfDocument.pages
                clip: true
                spacing: 0
                cacheBuffer: 600
                contentWidth: Math.max(width, pdfDocument.maxPageWidth * pdfDocument.zoom + 48)
                flickableDirection: Flickable.AutoFlickDirection
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                ScrollBar.horizontal: ScrollBar {}
                onWidthChanged: pdfDocument.updateViewport(width, height)
                onHeightChanged: pdfDocument.updateViewport(width, height)
                onContentYChanged: {
                    const index = indexAt(1, contentY + Math.min(height / 2, 80))
                    if (index >= 0) pdfDocument.currentPage = index + 1
                }
                delegate: Item {
                    required property int index
                    required property var modelData
                    property alias formSurface: nativeFormInput
                    width: pageView.contentWidth
                    height: modelData.height * pdfDocument.zoom + 32
                    Rectangle {
                        anchors.centerIn: parent
                        width: modelData.width * pdfDocument.zoom + 2
                        height: modelData.height * pdfDocument.zoom + 2
                        color: "#ffffff"
                        border.color: "#c3cbd5"
                        PdfPage { id: fullPage; objectName: "page" + index; anchors.fill: parent; anchors.margins: 1; document: pdfDocument; page: index }
                        id: pageDelegate
                        property int pageIndex: index
                        Item {
                            anchors.fill: fullPage
                            visible: root.highlightFields
                            clip: true
                            Repeater {
                                model: pdfDocument.formFields.filter(function(field) { return field.page === pageDelegate.pageIndex })
                                Rectangle {
                                    required property var modelData
                                    x: modelData.x * pdfDocument.zoom; y: modelData.y * pdfDocument.zoom
                                    width: modelData.width * pdfDocument.zoom; height: modelData.height * pdfDocument.zoom
                                    color: "transparent"
                                    border.width: modelData.id === pdfDocument.focusedField.id ? 2 : 1
                                    border.color: pdfDocument.formValidated && pdfDocument.formValidation.some(function(issue) { return issue.id === modelData.id }) ? "#c62828" : modelData.required ? "#a46513" : "#225a91"
                                    opacity: modelData.readOnly ? .35 : 1
                                    Label { anchors.right: parent.right; anchors.top: parent.top; text: modelData.required ? "*" : ""; color: parent.border.color; font.bold: true }
                                }
                            }
                        }
                        FormInput { id: nativeFormInput; objectName: "formInput" + index; anchors.fill: fullPage; document: pdfDocument; page: index; enabled: pdfDocument.formType !== "PDF" && !pdfDocument.saving }
                        AddedOverlay { anchors.fill: fullPage; content: pdfDocument.additions; page: index }
                        MouseArea {
                            objectName: "contentMouse" + index
                            preventStealing: true
                            anchors.fill: fullPage
                            cursorShape: root.placement ? Qt.CrossCursor : Qt.ArrowCursor
                            property var original: ({})
                            property real startX
                            property real startY
                            property bool resizing: false
                            property int resizeX: 0
                            property int resizeY: 0
                            property int drawingId: -1
                            property string drawingKind: ""
                            property bool selectingArea: false
                            property bool additiveArea: false
                            property rect selectionBox: Qt.rect(0, 0, 0, 0)
                            Rectangle { x: parent.selectionBox.x; y: parent.selectionBox.y; width: parent.selectionBox.width; height: parent.selectionBox.height; visible: parent.selectingArea && root.placement === "selection"; color: "#18225a91"; border.color: "#225a91" }
                            onPressed: function(mouse) {
                                pdfDocument.currentPage = index + 1
                                const x = mouse.x / pdfDocument.zoom, y = mouse.y / pdfDocument.zoom
                                if (root.placement === "selection") {
                                    pdfDocument.commitForm(); forceActiveFocus()
                                    startX = x; startY = y; selectingArea = true
                                    additiveArea = (mouse.modifiers & Qt.ShiftModifier) !== 0
                                    selectionBox = Qt.rect(mouse.x, mouse.y, 0, 0)
                                    original = ({})
                                    return
                                }
                                if (root.placement) {
                                    pdfDocument.commitForm()
                                    if (root.placement === "text") {
                                        pdfDocument.additions.beginEdit()
                                        const id = pdfDocument.additions.addText(index, x, y, root.pendingText)
                                        const style = root.pendingTextStyle
                                        pdfDocument.additions.setTextStyle(id, style.family, style.size,
                                            style.bold, style.italic, style.underline)
                                        pdfDocument.additions.endEdit()
                                    }
                                    else if (root.placement === "image" || root.placement === "signature")
                                        pdfDocument.additions.addImage(index, x, y, root.pendingImage, root.placement === "signature")
                                    else {
                                        pdfDocument.additions.beginEdit()
                                        const ink = root.pendingGraphic
                                        drawingKind = root.placement
                                        drawingId = pdfDocument.additions.addGraphic(index, x, y, drawingKind, ink.color, ink.width, ink.text)
                                        startX = x; startY = y
                                        forceActiveFocus()
                                        root.placement = ""
                                        original = ({})
                                        return
                                    }
                                    forceActiveFocus()
                                    root.placement = ""
                                    original = ({})
                                    return
                                }
                                let id = pdfDocument.additions.hit(index, x, y)
                                if (id < 0 && pdfDocument.additions.selectionCount === 1 && root.selectedObject.page === index) {
                                    const box = root.selectedObject
                                    const near = function(a, b) { return Math.abs(a - b) * pdfDocument.zoom < 6 }
                                    const atXEdge = near(x, box.x) || near(x, box.x + box.width)
                                    const atYEdge = near(y, box.y) || near(y, box.y + box.height)
                                    if ((atXEdge && (atYEdge || near(y, box.y + box.height / 2)))
                                        || (atYEdge && near(x, box.x + box.width / 2))) id = box.id
                                }
                                if (id >= 0) pdfDocument.commitForm()
                                let shiftHandle = false
                                if (id >= 0 && pdfDocument.additions.selectionCount === 1 && pdfDocument.additions.selected === id) {
                                    const box = root.selectedObject
                                    const near = function(a, b) { return Math.abs(a - b) * pdfDocument.zoom < 6 }
                                    const horizontal = near(x, box.x) || near(x, box.x + box.width)
                                    const vertical = near(y, box.y) || near(y, box.y + box.height)
                                    shiftHandle = (horizontal && (vertical || near(y, box.y + box.height / 2)))
                                        || (vertical && near(x, box.x + box.width / 2))
                                }
                                if (id >= 0 && (mouse.modifiers & Qt.ShiftModifier) && !shiftHandle) {
                                    pdfDocument.additions.toggleSelection(id)
                                    original = ({})
                                    forceActiveFocus()
                                    return
                                }
                                pdfDocument.additions.focusObject(id)
                                original = pdfDocument.additions.object(id)
                                if (id >= 0) { pdfDocument.additions.beginEdit(); forceActiveFocus() }
                                startX = x; startY = y
                                resizeX = resizeY = 0
                                if (id >= 0 && pdfDocument.additions.selectionCount === 1) {
                                    const near = function(a, b) { return Math.abs(a - b) * pdfDocument.zoom < 6 }
                                    const horizontal = near(x, original.x) ? -1 : near(x, original.x + original.width) ? 1 : 0
                                    const vertical = near(y, original.y) ? -1 : near(y, original.y + original.height) ? 1 : 0
                                    if (horizontal && (vertical || near(y, original.y + original.height / 2))) resizeX = horizontal
                                    if (vertical && (horizontal || near(x, original.x + original.width / 2))) resizeY = vertical
                                }
                                resizing = resizeX !== 0 || resizeY !== 0
                                if (id < 0) mouse.accepted = false
                            }
                            onPositionChanged: function(mouse) {
                                if (!pressed) return
                                if (selectingArea) {
                                    selectionBox = Qt.rect(Math.min(startX * pdfDocument.zoom, mouse.x), Math.min(startY * pdfDocument.zoom, mouse.y), Math.abs(mouse.x - startX * pdfDocument.zoom), Math.abs(mouse.y - startY * pdfDocument.zoom))
                                    return
                                }
                                if (!pdfDocument.additions.editing) return
                                if (drawingId >= 0) {
                                    const x = mouse.x / pdfDocument.zoom, y = mouse.y / pdfDocument.zoom
                                    if (drawingKind === "drawing") pdfDocument.additions.appendStroke(drawingId, x, y)
                                    else if (drawingKind === "line") pdfDocument.additions.setLine(drawingId, startX, startY, x, y)
                                    else pdfDocument.additions.geometry(drawingId, Math.min(startX, x), Math.min(startY, y), Math.max(12, Math.abs(x - startX)), Math.max(12, Math.abs(y - startY)))
                                    return
                                }
                                if (original.id === undefined) return
                                const dx = mouse.x / pdfDocument.zoom - startX, dy = mouse.y / pdfDocument.zoom - startY
                                if (!resizing) { pdfDocument.additions.moveSelection(dx, dy); return }
                                let width = Math.max(12, original.width + resizeX * dx)
                                let height = Math.max(12, original.height + resizeY * dy)
                                if (resizeX && resizeY && (mouse.modifiers & Qt.ShiftModifier)) {
                                    const ratio = original.width / original.height
                                    if (Math.abs(width - original.width) > Math.abs(height - original.height) * ratio) height = width / ratio
                                    else width = height * ratio
                                }
                                pdfDocument.additions.geometry(original.id,
                                    original.x + (resizeX < 0 ? original.width - width : 0),
                                    original.y + (resizeY < 0 ? original.height - height : 0), width, height)
                            }
                            onReleased: {
                                if (selectingArea && root.placement === "selection") {
                                    pdfDocument.additions.selectRect(index, selectionBox.x / pdfDocument.zoom, selectionBox.y / pdfDocument.zoom, selectionBox.width / pdfDocument.zoom, selectionBox.height / pdfDocument.zoom, additiveArea)
                                    root.placement = ""
                                }
                                selectingArea = false
                                pdfDocument.additions.endEdit(); original = ({}); drawingId = -1; drawingKind = ""
                            }
                            onCanceled: { selectingArea = false; root.placement = ""; pdfDocument.additions.cancelEdit(); original = ({}); drawingId = -1; drawingKind = "" }
                        }
                    }
                }
            }
            Rectangle {
                anchors.centerIn: parent
                width: Math.min(560, parent.width - 48)
                height: Math.min(parent.height - 32, welcome.implicitHeight + 64)
                radius: 12; color: root.surfaceColor; border.color: root.borderColor
                visible: !pdfDocument.ready
                ScrollView {
                    id: welcomeScroll
                    anchors.fill: parent; anchors.margins: 32
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        id: welcome
                        width: welcomeScroll.availableWidth; spacing: 16
                        BusyIndicator { running: pdfDocument.loading; visible: running; Layout.alignment: Qt.AlignHCenter }
                        Label { textFormat: Text.PlainText; text: pdfDocument.loading ? "Opening your PDF…" : pdfDocument.error ? "Couldn't open this PDF" : "Make your PDF yours."; font.pixelSize: 28; font.bold: true; color: root.textColor; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Label { objectName: "errorLabel"; textFormat: Text.PlainText; text: pdfDocument.error || (pdfDocument.loading ? "Your document will appear here shortly." : "Fill out a form. Add text, images or a signature.\nSave a copy when you're done."); color: root.mutedColor; font.pixelSize: 15; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        RowLayout {
                            visible: !pdfDocument.loading
                            EditorButton { objectName: "welcomeOpenButton"; text: "Open a PDF"; primary: true; onClicked: fileDialog.open() }
                            Label { text: "or drag a PDF into this window"; color: root.mutedColor; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                        Label { text: "Recent files"; font.bold: true; visible: !pdfDocument.loading && pdfDocument.recentDocuments.length > 0; Layout.topMargin: 12 }
                        Repeater {
                            model: pdfDocument.loading ? [] : pdfDocument.recentDocuments.slice(0, 3)
                            delegate: ItemDelegate {
                                required property var modelData
                                id: recentFileEntry
                                objectName: "welcomeRecentFile"
                                contentItem: Label { text: recentFileEntry.text; textFormat: Text.PlainText; elide: Text.ElideMiddle; verticalAlignment: Text.AlignVCenter }
                                Layout.fillWidth: true
                                text: modelData.name
                                Accessible.name: "Open " + modelData.name
                                onClicked: root.openFile(modelData.url)
                                ToolTip.visible: hovered; ToolTip.delay: 600; ToolTip.text: modelData.url
                            }
                        }
                        EditorButton { text: "Recover unsaved work…"; visible: !pdfDocument.loading && pdfDocument.recoveryDocuments.length > 0; onClicked: recoveryDialog.open() }
                        EditorButton { objectName: "welcomeHelpButton"; text: "How to edit a PDF"; visible: !pdfDocument.loading; onClicked: helpDialog.open() }
                    }
                }
            }
        }
    }
    Connections {
        target: pdfDocument
        function onZoomChanged() {
            const page = pdfDocument.currentPage - 1
            Qt.callLater(function() { if (pdfDocument.ready) pageView.positionViewAtIndex(page, ListView.Beginning) })
        }
        function onStateChanged() {
            root.placement = ""
            if (pdfDocument.ready) {
                pdfDocument.updateViewport(pageView.width, pageView.height)
                pageView.positionViewAtBeginning()
            }
        }
    }
}
