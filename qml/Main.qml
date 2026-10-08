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
    title: pdfDocument.fileName ? pdfDocument.fileName + " — PDF Form Editor" : "PDF Form Editor"
    color: "#e9edf2"
    font.family: "Sans Serif"
    palette.highlight: "#225a91"
    palette.button: "#f5f7fa"
    palette.window: "#ffffff"

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
        title: "Discard changes?"
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Discard | Dialog.Cancel
        Label { text: "Form edits or added content have not been saved. Discard changes to continue?" }
        onDiscarded: { const action = root.discardAction; root.discardAction = null; action() }
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
        onOpened: { textInput.forceActiveFocus(); validateText() }
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
    Shortcut { sequence: "Escape"; enabled: !root.formPopupVisible && !textDialog.visible && !graphicDialog.visible && !imageDialog.visible && !saveDialog.visible && !discardDialog.visible && !fileDialog.visible && !saveFailure.visible && !(root.activeFocusItem instanceof FormInput); onActivated: { root.placement = ""; pdfDocument.additions.cancelEdit(); pdfDocument.additions.select(-1) } }
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
        title: "Save a new PDF"
        fileMode: FileDialog.SaveFile
        nameFilters: ["PDF documents (*.pdf)"]
        defaultSuffix: "pdf"
        onAccepted: pdfDocument.saveAs(selectedFile)
    }
    Dialog {
        id: saveFailure
        title: "Couldn't save the PDF"
        anchors.centerIn: parent
        modal: true
        width: Math.min(480, root.width - 40)
        standardButtons: Dialog.Ok
        Label { width: parent.width; text: pdfDocument.saveError; wrapMode: Text.WordWrap }
    }
    Connections {
        target: pdfDocument
        function onSaveFinished(success) { if (!success && pdfDocument.saveError) saveFailure.open() }
    }
    Shortcut { sequence: "Ctrl+Shift+S"; enabled: pdfDocument.ready && !pdfDocument.saving && !pdfDocument.additions.editing && !root.formPopupVisible; onActivated: saveDialog.open() }
    Shortcut { sequences: [StandardKey.Open]; onActivated: fileDialog.open() }
    Shortcut { sequences: [StandardKey.Close]; onActivated: root.closeDocument() }
    Shortcut { sequence: "Ctrl++"; onActivated: pdfDocument.zoom *= 1.2 }
    Shortcut { sequence: "Ctrl+-"; onActivated: pdfDocument.zoom /= 1.2 }
    Shortcut { sequence: "Ctrl+0"; onActivated: pdfDocument.fitToPage() }
    property bool formPopupVisible: fieldsDialog.visible || validationDialog.visible || resetDialog.visible || choiceDialog.visible || dateDialog.visible || signDialog.visible
    property bool additionHistoryShortcuts: pdfDocument.ready && !pdfDocument.saving
        && !pdfDocument.additions.editing && !graphicDialog.visible && !root.formPopupVisible
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
        implicitHeight: 56
        background: Rectangle { color: "#ffffff"; border.color: "#d5dce5" }
        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10
            Label { text: "PDF FORM EDITOR"; font.pixelSize: 13; font.bold: true; color: "#354a60"; Layout.rightMargin: 12 }
            Button { objectName: "openButton"; text: "Open PDF"; enabled: !pdfDocument.saving; onClicked: fileDialog.open(); Accessible.name: "Open PDF" }
            Button { objectName: "saveButton"; text: pdfDocument.saving ? "Saving…" : "Save As"; enabled: pdfDocument.ready && !pdfDocument.saving && !pdfDocument.additions.editing; onClicked: saveDialog.open() }
            Button { objectName: "closeButton"; text: "Close"; enabled: !pdfDocument.saving && (pdfDocument.ready || pdfDocument.loading || pdfDocument.error || pdfDocument.dirty); onClicked: root.closeDocument() }
            Item { Layout.fillWidth: true }
            Button { objectName: "zoomOutButton"; text: "−"; enabled: pdfDocument.ready && pdfDocument.zoom > 0.25; onClicked: pdfDocument.zoom /= 1.2; Accessible.name: "Zoom out" }
            Label { objectName: "zoomLabel"; text: Math.round(pdfDocument.zoom * 100) + "%"; horizontalAlignment: Text.AlignHCenter; Layout.preferredWidth: 48; visible: pdfDocument.ready }
            Button { objectName: "zoomInButton"; text: "+"; enabled: pdfDocument.ready && pdfDocument.zoom < 3; onClicked: pdfDocument.zoom *= 1.2; Accessible.name: "Zoom in" }
            Button { objectName: "fitButton"; text: "Fit page"; enabled: pdfDocument.ready; highlighted: pdfDocument.fitting; onClicked: pdfDocument.fitToPage() }
        }
    }
    footer: ToolBar {
        implicitHeight: 52
        background: Rectangle { color: "#ffffff"; border.color: "#d5dce5" }
        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            Label { textFormat: Text.PlainText; text: pdfDocument.loading ? "Opening PDF…" : pdfDocument.ready ? pdfDocument.fileName : "Ready"; color: "#596878"; elide: Text.ElideMiddle; Layout.fillWidth: true }
            Label { text: pdfDocument.formType; visible: pdfDocument.ready; color: "#596878" }
            ToolButton { objectName: "undoAdditionsButton"; text: "Undo"; enabled: pdfDocument.ready && !pdfDocument.saving && pdfDocument.additions.canUndo; Accessible.name: "Undo added content"; onClicked: pdfDocument.additions.undo() }
            ToolButton { objectName: "redoAdditionsButton"; text: "Redo"; enabled: pdfDocument.ready && !pdfDocument.saving && pdfDocument.additions.canRedo; Accessible.name: "Redo added content"; onClicked: pdfDocument.additions.redo() }
            Button { objectName: "previousButton"; text: "Previous"; enabled: pdfDocument.ready && pdfDocument.currentPage > 1; onClicked: root.goToPage(pdfDocument.currentPage - 1) }
            SpinBox { objectName: "pageNumber"; from: 1; to: Math.max(1, pdfDocument.pageCount); value: pdfDocument.currentPage; editable: true; enabled: pdfDocument.ready; onValueModified: root.goToPage(value); Accessible.name: "Page number" }
            Label { text: "of " + pdfDocument.pageCount; color: "#596878" }
            Button { objectName: "nextButton"; text: "Next"; enabled: pdfDocument.ready && pdfDocument.currentPage < pdfDocument.pageCount; onClicked: root.goToPage(pdfDocument.currentPage + 1) }
        }
    }
    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            visible: pdfDocument.ready
            Layout.preferredWidth: 170
            Layout.fillHeight: true
            color: "#f5f7fa"
            border.color: "#d5dce5"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                Label { text: "PAGES"; font.bold: true; font.pixelSize: 11; color: "#63758a"; Layout.topMargin: 4; Layout.bottomMargin: 6 }
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
                            color: pdfDocument.currentPage === index + 1 ? "#dceaf8" : "#e8edf3"
                            border.color: pdfDocument.currentPage === index + 1 ? "#225a91" : "#cbd4df"
                            PdfPage {
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
                        Label { anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; text: index + 1; color: "#526579"; font.pixelSize: 12 }
                        MouseArea { anchors.fill: parent; onClicked: root.goToPage(index + 1); cursorShape: Qt.PointingHandCursor }
                    }
                }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            RowLayout {
                id: contentTools
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 8
                height: 42
                visible: pdfDocument.ready
                Button { objectName: "addTextButton"; text: "Text"; onClicked: { textDialog.editing = false; textInput.text = ""; textDialog.loadStyle({}); textDialog.open() } }
                Button { objectName: "addImageButton"; text: "Image"; onClicked: { imageDialog.signature = false; imageDialog.open() } }
                Button { objectName: "addSignatureButton"; text: "Signature"; onClicked: { imageDialog.signature = true; imageDialog.open() } }
                Button { objectName: "editTextButton"; text: "Edit text"; visible: pdfDocument.additions.selectionCount === 1 && (root.selectedObject.type === "text" || root.selectedObject.type === "stamp"); onClicked: { textDialog.editing = true; textInput.text = root.selectedObject.text; textDialog.loadStyle(root.selectedObject); textDialog.open() } }
                Button { objectName: "deleteObjectButton"; text: "Delete"; enabled: pdfDocument.additions.selected >= 0; onClicked: pdfDocument.additions.removeSelected() }
                Label { Layout.fillWidth: true; elide: Text.ElideRight; textFormat: Text.PlainText; text: pdfDocument.error || pdfDocument.formError || (root.placement ? "Click a page to place " + root.placement + " · Esc cancels" : pdfDocument.additions.error || (pdfDocument.dirty ? "Unsaved changes · Save As keeps edits" : pdfDocument.savedPath ? "Saved · " + pdfDocument.savedPath : pdfDocument.formType !== "PDF" ? "Click a field · Tab moves focus · Save As keeps edits" : "Add content to a page")); color: "#596878" }
            }
            Flickable {
                id: extraToolView
                anchors.top: contentTools.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 8
                height: 40
                clip: true
                contentWidth: extraTools.implicitWidth
                contentHeight: height
                visible: pdfDocument.ready
                enabled: !pdfDocument.saving && !pdfDocument.additions.editing
                flickableDirection: Flickable.HorizontalFlick
                ScrollBar.horizontal: ScrollBar {}
                RowLayout {
                    id: extraTools
                    height: parent.height
                    ToolButton {
                        objectName: "drawToolsButton"; text: "Draw ▾"; onClicked: drawMenu.popup()
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
                    ToolButton { objectName: "copyObjectsButton"; text: "Copy"; enabled: pdfDocument.additions.selectionCount > 0; onClicked: pdfDocument.additions.copySelection() }
                    ToolButton { objectName: "cutObjectsButton"; text: "Cut"; enabled: pdfDocument.additions.selectionCount > 0; onClicked: pdfDocument.additions.cutSelection() }
                    ToolButton { objectName: "pasteObjectsButton"; text: "Paste"; onClicked: pdfDocument.additions.paste(pdfDocument.currentPage - 1) }
                    ToolButton { objectName: "selectAllObjectsButton"; text: "Select all"; onClicked: pdfDocument.additions.selectAll(pdfDocument.currentPage - 1) }
                    ToolButton { objectName: "selectAreaButton"; text: "Select area"; onClicked: root.placement = "selection" }
                    ToolButton {
                        objectName: "alignObjectsButton"; text: "Align ▾"; enabled: pdfDocument.additions.selectionCount > 1; onClicked: alignMenu.popup()
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
                    ToolButton { objectName: "appearanceButton"; text: "Style…"; enabled: pdfDocument.additions.selectionCount === 1 && root.selectedObject.type !== "image" && root.selectedObject.type !== "signature"; onClicked: {
                        graphicDialog.editing = true; graphicDialog.kind = root.selectedObject.type
                        inkColor.currentIndex = Math.max(0, root.inkColors.indexOf(root.selectedObject.color.substring(0, 1) + root.selectedObject.color.substring(3)))
                        inkWidth.value = root.selectedObject.lineWidth; stampText.text = root.selectedObject.text; graphicDialog.open()
                    } }
                    Label { text: "Shift-click to select several"; color: "#596878" }
                }
            }
            Flickable {
                id: formTools
                anchors.top: extraToolView.bottom; anchors.left: parent.left; anchors.right: parent.right
                anchors.margins: 8; height: 40; clip: true
                visible: pdfDocument.ready && pdfDocument.formType !== "PDF"
                enabled: !pdfDocument.saving && !pdfDocument.additions.editing
                contentWidth: formToolsRow.implicitWidth; contentHeight: height
                flickableDirection: Flickable.HorizontalFlick; ScrollBar.horizontal: ScrollBar {}
                RowLayout {
                    id: formToolsRow; height: parent.height
                    CheckBox { objectName: "highlightFieldsButton"; text: "Show fields"; checked: root.highlightFields; onToggled: { root.highlightFields = checked; pdfDocument.formEvent(pdfDocument.currentPage-1, "highlight", 0, 0, checked ? 1 : 0) } }
                    ToolButton { objectName: "formFieldsButton"; text: "Fields"; enabled: pdfDocument.formHelpersAvailable; onClicked: fieldsDialog.open() }
                    ToolButton { objectName: "previousFieldButton"; text: "Previous field"; enabled: pdfDocument.formHelpersAvailable; onClicked: pdfDocument.navigateForm(true) }
                    ToolButton { objectName: "nextFieldButton"; text: "Next field"; enabled: pdfDocument.formHelpersAvailable; onClicked: pdfDocument.navigateForm() }
                    ToolButton { objectName: "validateFormButton"; text: "Check form"; enabled: pdfDocument.formHelpersAvailable; onClicked: { root.showValidation = true; pdfDocument.validateForm() } }
                    ToolButton { objectName: "resetFieldButton"; text: "Reset field"; enabled: pdfDocument.formHelpersAvailable && pdfDocument.focusedField.id !== undefined && !pdfDocument.focusedField.readOnly && pdfDocument.focusedField.type >= 2 && pdfDocument.focusedField.type <= 6; onClicked: { resetDialog.fieldId = pdfDocument.focusedField.id; resetDialog.open() } }
                    ToolButton { objectName: "resetWholeFormButton"; text: "Reset form"; enabled: pdfDocument.canResetForm; onClicked: { resetDialog.fieldId = -1; resetDialog.open() } }
                    ToolButton { objectName: "chooseFormValueButton"; text: "Choose value"; visible: (pdfDocument.focusedField.options || []).length > 0; enabled: !!pdfDocument.focusedField.choicesComplete; onClicked: { choiceDialog.field = pdfDocument.focusedField; choiceDialog.open() } }
                    ToolButton { objectName: "enterFormDateButton"; text: "Date…"; visible: (pdfDocument.focusedField.dateFormat || "").length > 0; onClicked: {
                        dateDialog.field = pdfDocument.focusedField
                        const parsed = Date.fromLocaleString(Qt.locale("en_US"), dateDialog.field.value, dateDialog.field.dateFormat.replace("mm", "MM"))
                        const date = isNaN(parsed.getTime()) ? new Date() : parsed
                        dateYear.value = date.getFullYear(); dateMonth.value = date.getMonth()+1; dateDay.value = date.getDate(); dateDialog.open()
                    } }
                    ToolButton { objectName: "signFormButton"; text: "Sign…"; onClicked: signDialog.open() }
                    Label { textFormat: Text.PlainText; text: pdfDocument.formHelpersAvailable ? (pdfDocument.focusedField.label || "Tab moves to the next field") : "Native form controls · Tab moves focus"; color: "#596878" }
                }
            }
            ListView {
                id: pageView
                objectName: "pageView"
                anchors.fill: parent
                anchors.topMargin: formTools.visible ? 154 : 106
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
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(440, parent.width - 64)
                visible: !pdfDocument.ready
                spacing: 20
                BusyIndicator { running: pdfDocument.loading; visible: running; Layout.alignment: Qt.AlignHCenter }
                Label { textFormat: Text.PlainText; text: pdfDocument.loading ? "Opening your document" : pdfDocument.error ? "Couldn't open this document" : "Your documents, in view"; font.pixelSize: 27; color: "#2d4259"; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Label { objectName: "errorLabel"; textFormat: Text.PlainText; text: pdfDocument.error || (pdfDocument.loading ? "" : "Open a PDF to browse its pages, zoom in, and navigate with thumbnails."); color: "#63758a"; font.pixelSize: 15; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }
                Button { text: "Open PDF"; visible: !pdfDocument.loading; Layout.alignment: Qt.AlignHCenter; highlighted: true; onClicked: fileDialog.open() }
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
