// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// In-place text editing of a shape or connector label (D-050): a
// Tk.TextArea over the item's text box in view space, set in the item's own
// face, size and alignment (document content, not chrome). The page view
// hides the item's painted text while the editor covers it
// (DiagramService.editingTextId). Escape or a click elsewhere stores the
// text as one undo step, as Visio does; Ctrl+Enter too. Rotated shapes edit
// unrotated (the SlideCanvas precedent).
Tk.TextArea {
    id: editor
    objectName: "diagramTextEditor"

    required property var canvas
    required property var diagram
    property string itemId: ""
    readonly property var style: itemId.length > 0 ? (diagram.revision, diagram.item(itemId).textStyle || ({})) : ({})
    readonly property rect box: {
        if (itemId.length === 0) return Qt.rect(0, 0, 0, 0)
        // Depend on the pose so the box follows pan and zoom.
        const pose = canvas.zoom + canvas.centerX + canvas.centerY + canvas.width + canvas.height
        const r = diagram.textBox(itemId)
        const a = canvas.mapFromDocument(Qt.point(r.x, r.y))
        const b = canvas.mapFromDocument(Qt.point(r.x + r.width, r.y + r.height))
        return pose === pose ? Qt.rect(Math.min(a.x, b.x), Math.min(a.y, b.y), Math.abs(b.x - a.x), Math.abs(b.y - a.y))
                             : Qt.rect(0, 0, 0, 0)
    }

    visible: itemId.length > 0
    x: box.x
    y: box.y
    width: Math.max(Tk.Theme.size.fieldWidth, box.width)
    rows: 1
    wrapMode: TextEdit.Wrap
    tooltip: qsTr("Text of the selected item")
    font.family: style.family || Tk.Theme.font.family
    font.pixelSize: Math.max(Tk.Theme.font.caption, (style.size || 11) * 96 / 72 * canvas.zoom)
    font.bold: !!style.bold
    font.italic: !!style.italic
    font.underline: !!style.underline
    horizontalAlignment: style.align === "left" ? TextEdit.AlignLeft : style.align === "right" ? TextEdit.AlignRight
                                                                      : TextEdit.AlignHCenter

    function begin(id) {
        if (itemId.length > 0) commit()
        itemId = id
        text = diagram.item(id).text || ""
        diagram.editingTextId = id
        forceActiveFocus()
        selectAll()
    }
    function commit() {
        if (itemId.length === 0) return
        const id = itemId
        const value = text
        itemId = ""
        diagram.editingTextId = ""
        diagram.setText(id, value)
        canvas.forceActiveFocus()
    }

    Keys.onEscapePressed: commit()
    Keys.onPressed: function(event) {
        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
            commit()
            event.accepted = true
        }
    }
    onActiveFocusChanged: if (!activeFocus && itemId.length > 0) commit()
    Connections {
        target: editor.diagram
        function onCurrentPageChanged() { editor.commit() }
    }
}
