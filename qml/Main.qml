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
    function openFile(url) { pdfDocument.open(url) }

    FileDialog {
        id: fileDialog
        objectName: "openDialog"
        title: "Open a PDF"
        nameFilters: ["PDF documents (*.pdf)", "All files (*)"]
        onAccepted: root.openFile(selectedFile)
    }
    Shortcut { sequences: [StandardKey.Open]; onActivated: fileDialog.open() }
    Shortcut { sequences: [StandardKey.Close]; onActivated: pdfDocument.close() }
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
            Button { objectName: "openButton"; text: "Open PDF"; onClicked: fileDialog.open(); Accessible.name: "Open PDF" }
            Button { objectName: "closeButton"; text: "Close"; enabled: pdfDocument.ready || pdfDocument.loading; onClicked: pdfDocument.close() }
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
                                objectName: "thumbnail" + index
                                anchors.centerIn: parent
                                width: Math.min(110, (parent.height - 10) * modelData.width / modelData.height)
                                height: width * modelData.height / modelData.width
                                document: pdfDocument
                                page: index
                            }
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
            ListView {
                id: pageView
                objectName: "pageView"
                anchors.fill: parent
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
                        PdfPage { objectName: "page" + index; anchors.fill: parent; anchors.margins: 1; document: pdfDocument; page: index }
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
                Label { objectName: "errorLabel"; text: pdfDocument.error || (pdfDocument.loading ? "" : "Open a PDF to browse its pages, zoom in, and navigate with thumbnails."); color: "#63758a"; font.pixelSize: 15; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }
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
            if (pdfDocument.ready) {
                pdfDocument.updateViewport(pageView.width, pageView.height)
                pageView.positionViewAtBeginning()
            }
        }
    }
}
