// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Write

// The ruler above the page: Tk.Ruler (A7) bound to the viewport's zoom
// and scroll, so the tick at 3 cm really is above the 3 cm point of the
// paper, with the cursor paragraph's first-line / left / right indents
// as draggable markers written back through the host. Numbered from the
// text column's left edge — a ruler numbered from the paper edge makes
// every indent read one margin out.
Tk.Ruler {
    id: ruler
    required property WriteHost write
    required property Tk.Viewport page
    readonly property var m: write.rulerMarkers
    // One centimetre at 96 dpi; the page defaults are metric (A4).
    // rulerMarkers carries the left (margin) and right (rightMargin) page
    // margins separately: Word documents rarely have equal ones.
    readonly property real pxPerCm: 96 / 2.54
    function viewX(docX) { return page.fromContent(Qt.point(docX, 0)).x }
    objectName: "writeRuler"
    width: page.width
    origin: viewX(m.margin)
    pixelsPerUnit: pxPerCm * page.zoom
    majorEvery: 2
    minorEvery: 1
    band: [viewX(0), viewX(m.pageWidth)]
    activeBand: [viewX(m.margin), viewX(m.pageWidth - m.rightMargin)]
    markers: [
        { id: "indent-first", position: viewX(m.firstLine), kind: "indent-first", draggable: true },
        { id: "indent-left", position: viewX(m.left), kind: "indent-left", draggable: true },
        { id: "indent-right", position: viewX(m.right), kind: "indent-right", draggable: true }
    ]
    onMarkerMoved: function(id, position) { write.applyRulerMarker(id, page.toContent(Qt.point(position, 0)).x) }
}
