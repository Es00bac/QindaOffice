// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Shell

// The local menu bar, one Tk.Menu per top-level MenuTree entry. Rebuilt
// whenever the tree changes (Open Recent, an app inserting its menus);
// item states bind live to the ActionEntry objects, so nothing here is
// ever stale.
Tk.MenuBar {
    id: bar

    property MenuTree tree: null
    property ActionModel actions: null

    Instantiator {
        model: bar.tree !== null ? bar.tree.menus : []
        delegate: MenuFromNode {
            required property var modelData
            node: modelData
            actions: bar.actions
        }
        onObjectAdded: function(index, object) { bar.insertMenu(index, object) }
        onObjectRemoved: function(index, object) { bar.removeMenu(object) }
    }
}
