// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Note

// The page inside a zoomable viewport: the NotePageItem paints paper, ink
// and blocks in document units; a Tk.StylusHandler feeds it pen samples;
// the one live editor (a Tk.TextArea for text, a Tk.TextField for a
// checklist item) sits over the block being edited and scales with the
// page, so typed text zooms like everything else.
Tk.Viewport {
    id: viewport

    required property NoteHost note
    readonly property NotePageItem pageItem: page

    objectName: "noteViewport"
    fitMode: "width"
    focus: true

    NotePageItem {
        id: page
        objectName: "notePage"
        viewZoom: viewport.zoom
        focus: true

        Tk.StylusHandler {
            id: stylus
            objectName: "noteStylus"
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
        Tk.TextArea {
            id: textEditor
            objectName: "noteTextEditor"
            visible: page.editingBlock >= 0 && page.editingKind === "text"
            x: page.editorRect.x
            y: page.editorRect.y
            width: page.editorRect.width
            rows: 1
            font: page.pageFont
            color: "#202028"          // paper ink (D-004)
            wrapMode: TextEdit.Wrap
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
            font: page.pageFont
            color: "#202028"
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
