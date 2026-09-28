// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Note

// The page inside a zoomable viewport: the NotePageItem paints paper, ink
// and blocks in document units; a Tk.StylusHandler feeds it pen samples
// (and a finger's, with Draw with Finger on and an ink tool active); the
// one live editor (a Tk.TextArea for text, a Tk.TextField for a checklist
// item) sits over the block being edited and scales with the page, so
// typed text zooms like everything else.
//
// Touch: two fingers pinch-zoom and pan (Tk.Viewport); one finger inks
// when the handler takes it, otherwise it pans the page, and a tap with
// the Text tool places or opens a block.
Tk.Viewport {
    id: viewport

    required property NoteHost note
    readonly property NotePageItem pageItem: page

    objectName: "noteViewport"
    fitMode: "width"
    focus: true
    touchPan: true   // a finger the ink handler does not take scrolls the page

    readonly property bool fingerInks: page.touchDrawing && page.tool !== "text"

    NotePageItem {
        id: page
        objectName: "notePage"
        viewZoom: viewport.zoom
        focus: true

        Tk.StylusHandler {
            id: stylus
            objectName: "noteStylus"
            acceptedDevices: viewport.fingerInks
                ? (PointerDevice.Stylus | PointerDevice.Mouse | PointerDevice.Airbrush
                   | PointerDevice.Puck | PointerDevice.TouchScreen)
                : (PointerDevice.Stylus | PointerDevice.Mouse | PointerDevice.Airbrush
                   | PointerDevice.Puck)
            acceptedPointerTypes: PointerDevice.Pen | PointerDevice.Eraser
                                  | PointerDevice.Generic | PointerDevice.Finger
        }

        // A finger tap with the Text tool (the stylus handler does not take
        // fingers then, so a drag stays free to pan the page).
        TapHandler {
            objectName: "noteTouchTap"
            enabled: page.tool === "text"
            acceptedDevices: PointerDevice.TouchScreen
            onTapped: function(eventPoint) { page.tapAt(eventPoint.position) }
        }

        Component.onCompleted: {
            page.stylus = stylus
            viewport.note.attachPageItem(page)
        }

        // Esc: commit the editor first, then back to the pen.
        Keys.onEscapePressed: function(event) {
            if (page.editingBlock >= 0) {
                page.commitEdit()
            } else {
                page.tool = "pen"
            }
            event.accepted = true
        }

        // The assistant's replies land in the live editor; with none open,
        // a fresh text block takes them (Insert is never a silent no-op).
        Connections {
            target: viewport.note
            function onAssistantInsertRequested(text) {
                if (page.editingBlock < 0) {
                    page.addTextBlock()
                }
                const editor = page.editingKind === "check" ? checkEditor : textEditor
                editor.insert(editor.cursorPosition, text)
            }
            function onAssistantReplaceRequested(text) {
                if (page.editingBlock < 0) {
                    onAssistantInsertRequested(text)
                    return
                }
                const editor = page.editingKind === "check" ? checkEditor : textEditor
                if (editor.selectedText.length > 0) {
                    editor.remove(editor.selectionStart, editor.selectionEnd)
                }
                editor.insert(editor.cursorPosition, text)
            }
        }

        onEditorRequested: {
            if (page.editingKind === "check") {
                checkEditor.text = page.editingText
                checkEditor.forceActiveFocus(Qt.OtherFocusReason)
                checkEditor.cursorPosition = checkEditor.text.length
            } else {
                textEditor.text = page.editingText
                textEditor.forceActiveFocus(Qt.OtherFocusReason)
                textEditor.cursorPosition = textEditor.text.length
            }
        }

        // ---- the live editor ----
        // AGENT-GUARD: the editors sit ON the paper, so they are paper too
        // (D-004: the document surface is the one allowed literal): no
        // chrome inputBg behind the ink — the theme's dark field under
        // "#202028" ink was black-on-black on every dark theme. The painted
        // block is skipped while it is edited, so the page shows through.
        Tk.TextArea {
            id: textEditor
            objectName: "noteTextEditor"
            // A markdown block (live notes, D-057) is edited as its source.
            visible: page.editingBlock >= 0 && (page.editingKind === "text" || page.editingKind === "markdown")
            x: page.editorRect.x
            y: page.editorRect.y
            width: page.editorRect.width
            rows: 1
            padding: page.textPadding
            leftPadding: page.textPadding
            rightPadding: page.textPadding
            font: page.pageFont
            color: "#202028"          // paper ink (D-004)
            selectionColor: "#b4d2ff"
            selectedTextColor: "#202028"
            placeholderText: qsTr("Type a note")
            placeholderTextColor: "#8a8a94"
            renderType: Text.QtRendering
            wrapMode: TextEdit.Wrap
            background: Rectangle {
                color: "transparent"
                radius: 2
                border.width: 1
                border.color: Tk.Theme.color.focus
            }
            onSelectedTextChanged: if (visible) viewport.note.editorSelection = selectedText
            onTextChanged: if (visible) page.updateEdit(text)
            onActiveFocusChanged: if (!activeFocus && visible) page.commitEdit()
            Keys.onEscapePressed: function(event) { page.commitEdit(); event.accepted = true }
        }
        Tk.TextField {
            id: checkEditor
            objectName: "noteCheckEditor"
            visible: page.editingBlock >= 0 && page.editingKind === "check"
            x: page.editorRect.x
            y: page.editorRect.y + (page.editorRect.height - height) / 2
            width: page.editorRect.width
            // editorRect starts where the painted item text starts
            // (note_page_render.cpp); the outline reaches out past it.
            leftPadding: 0
            leftInset: -page.textPadding
            font: page.pageFont
            color: "#202028"
            selectionColor: "#b4d2ff"
            selectedTextColor: "#202028"
            placeholderText: qsTr("To do")
            placeholderTextColor: "#8a8a94"
            renderType: Text.QtRendering
            background: Rectangle {
                color: "transparent"
                radius: 2
                border.width: 1
                border.color: Tk.Theme.color.focus
            }
            onSelectedTextChanged: if (visible) viewport.note.editorSelection = selectedText
            onTextChanged: if (visible) page.updateEdit(text)
            onActiveFocusChanged: if (!activeFocus && visible) page.commitEdit()
            onAccepted: page.insertCheckItemAfter(page.editingBlock)
            Keys.onEscapePressed: function(event) { page.commitEdit(); event.accepted = true }
            Keys.onTabPressed: function(event) { page.indentCheckItem(page.editingBlock, 1); event.accepted = true }
            Keys.onBacktabPressed: function(event) { page.indentCheckItem(page.editingBlock, -1); event.accepted = true }
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Backspace && checkEditor.text.length === 0) {
                    const index = page.editingBlock
                    page.cancelEdit()
                    page.deleteBlock(index)
                    event.accepted = true
                }
            }
        }
    }
}
