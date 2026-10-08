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
                pdfDocument.additions.setText(pdfDocument.additions.selected, textInput.text)
                pdfDocument.additions.setTextStyle(pdfDocument.additions.selected,
                    style.family, style.size, style.bold, style.italic, style.underline)
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
    Shortcut { sequence: "Escape"; onActivated: { root.placement = ""; pdfDocument.additions.select(-1) } }
    Connections {
        target: pdfDocument.additions
        function onChanged() { root.selectedObject = pdfDocument.additions.object(pdfDocument.additions.selected) }
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
    Shortcut { sequence: "Ctrl+Shift+S"; enabled: pdfDocument.ready && !pdfDocument.saving; onActivated: saveDialog.open() }
    Shortcut { sequences: [StandardKey.Open]; onActivated: fileDialog.open() }
    Shortcut { sequences: [StandardKey.Close]; onActivated: root.closeDocument() }
    Shortcut { sequence: "Ctrl++"; onActivated: pdfDocument.zoom *= 1.2 }
    Shortcut { sequence: "Ctrl+-"; onActivated: pdfDocument.zoom /= 1.2 }
    Shortcut { sequence: "Ctrl+0"; onActivated: pdfDocument.fitToPage() }

    header: ToolBar {
        implicitHeight: 56
        background: Rectangle { color: "#ffffff"; border.color: "#d5dce5" }
        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10
            Label { text: "PDF FORM EDITOR"; font.pixelSize: 13; font.bold: true; color: "#354a60"; Layout.rightMargin: 12 }
            Button { objectName: "openButton"; text: "Open PDF"; enabled: !pdfDocument.saving; onClicked: fileDialog.open(); Accessible.name: "Open PDF" }
            Button { objectName: "saveButton"; text: pdfDocument.saving ? "Saving…" : "Save As"; enabled: pdfDocument.ready && !pdfDocument.saving; onClicked: saveDialog.open() }
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
                Button { objectName: "editTextButton"; text: "Edit text"; visible: root.selectedObject.type === "text"; onClicked: { textDialog.editing = true; textInput.text = root.selectedObject.text; textDialog.loadStyle(root.selectedObject); textDialog.open() } }
                Button { objectName: "deleteObjectButton"; text: "Delete"; enabled: pdfDocument.additions.selected >= 0; onClicked: pdfDocument.additions.removeSelected() }
                Label { Layout.fillWidth: true; elide: Text.ElideRight; textFormat: Text.PlainText; text: pdfDocument.error || pdfDocument.formError || (root.placement ? "Click a page to place " + root.placement + " · Esc cancels" : pdfDocument.additions.error || (pdfDocument.dirty ? "Unsaved changes · Save As keeps edits" : pdfDocument.savedPath ? "Saved · " + pdfDocument.savedPath : pdfDocument.formType !== "PDF" ? "Click a field · Tab moves focus · Save As keeps edits" : "Add content to a page")); color: "#596878" }
            }
            ListView {
                id: pageView
                objectName: "pageView"
                anchors.fill: parent
                anchors.topMargin: 58
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
                    width: pageView.contentWidth
                    height: modelData.height * pdfDocument.zoom + 32
                    Rectangle {
                        anchors.centerIn: parent
                        width: modelData.width * pdfDocument.zoom + 2
                        height: modelData.height * pdfDocument.zoom + 2
                        color: "#ffffff"
                        border.color: "#c3cbd5"
                        PdfPage { id: fullPage; objectName: "page" + index; anchors.fill: parent; anchors.margins: 1; document: pdfDocument; page: index }
                        FormInput { objectName: "formInput" + index; anchors.fill: fullPage; document: pdfDocument; page: index; enabled: pdfDocument.formType !== "PDF" && !pdfDocument.saving }
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
                            onPressed: function(mouse) {
                                pdfDocument.currentPage = index + 1
                                const x = mouse.x / pdfDocument.zoom, y = mouse.y / pdfDocument.zoom
                                if (root.placement) {
                                    pdfDocument.commitForm()
                                    if (root.placement === "text") {
                                        const id = pdfDocument.additions.addText(index, x, y, root.pendingText)
                                        const style = root.pendingTextStyle
                                        pdfDocument.additions.setTextStyle(id, style.family, style.size,
                                            style.bold, style.italic, style.underline)
                                    }
                                    else pdfDocument.additions.addImage(index, x, y, root.pendingImage, root.placement === "signature")
                                    root.placement = ""
                                    original = ({})
                                    return
                                }
                                const id = pdfDocument.additions.hit(index, x, y)
                                if (id >= 0) pdfDocument.commitForm()
                                pdfDocument.additions.select(id)
                                original = pdfDocument.additions.object(id)
                                startX = x; startY = y
                                resizing = id >= 0 && Math.abs(mouse.x - (original.x + original.width) * pdfDocument.zoom) < 12 && Math.abs(mouse.y - (original.y + original.height) * pdfDocument.zoom) < 12
                                if (id < 0) mouse.accepted = false
                            }
                            onPositionChanged: function(mouse) {
                                if (!pressed || original.id === undefined) return
                                const dx = mouse.x / pdfDocument.zoom - startX, dy = mouse.y / pdfDocument.zoom - startY
                                pdfDocument.additions.geometry(original.id, original.x + (resizing ? 0 : dx), original.y + (resizing ? 0 : dy), original.width + (resizing ? dx : 0), original.height + (resizing ? dy : 0))
                            }
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
