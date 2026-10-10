// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import QindaOffice.Diagram.App

// Review ▸ Comments Pane (M87, D-095; Visio's Comments pane): the current
// page's comment threads with their replies. A new comment goes on the
// selected shape, else on the page; a card replies, edits, resolves
// (reopens) or deletes its thread; clicking a card selects its shape;
// Previous / Next walk every thread of the drawing. Comments read from
// Visio keep their author, date and shape. QindaShow's CommentsPane.qml is
// the model.
Tk.Box {
    id: panel
    objectName: "diagramCommentsPane"

    required property DiagramService diagram
    property string editing: ""
    property string replying: ""

    function compose() { newText.forceActiveFocus() }

    color: Tk.Theme.color.panel

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        padding: Tk.Theme.space.sm

        Tk.Overline { title: qsTr("COMMENTS") }
        Tk.TextArea {
            id: newText
            objectName: "commentNewText"
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            rows: 2
            placeholderText: panel.diagram.selection.length > 0
                             ? qsTr("Comment on %1…").arg(panel.diagram.selected.typeTitle || qsTr("the selection"))
                             : qsTr("Comment on this page…")
        }
        Tk.Flex {
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            gap: Tk.Theme.space.xs
            align: Tk.Flex.Center
            Tk.Button {
                objectName: "commentAdd"
                small: true
                iconName: "message-square-plus"
                text: qsTr("Add comment")
                enabled: newText.text.trim().length > 0
                onClicked: {
                    panel.diagram.addComment(newText.text)
                    newText.text = ""
                }
            }
            Item { Tk.Flex.grow: 1; implicitHeight: 1 }
            Tk.Caption {
                objectName: "commentsCount"
                text: qsTr("%1 here · %2 in all").arg(panel.diagram.comments.length).arg(panel.diagram.commentTotal)
            }
            Tk.IconButton {
                objectName: "commentPrevious"
                small: true
                iconName: "chevron-up"
                tooltip: qsTr("Previous comment")
                enabled: panel.diagram.commentTotal > 0
                onClicked: panel.diagram.stepComment(-1)
            }
            Tk.IconButton {
                objectName: "commentNext"
                small: true
                iconName: "chevron-down"
                tooltip: qsTr("Next comment")
                enabled: panel.diagram.commentTotal > 0
                onClicked: panel.diagram.stepComment(1)
            }
        }
        Tk.EmptyState {
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            visible: panel.diagram.comments.length === 0
            iconName: "message-square"
            title: qsTr("No comments on this page")
            text: qsTr("Select a shape (or nothing, for the page) and add a comment. Comments in Visio drawings appear here.")
        }
        Tk.Scroll {
            id: scroll
            objectName: "commentsScroll"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            overflowX: Tk.Scroll.Hidden
            visible: panel.diagram.comments.length > 0
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.sm
                width: scroll.width - Tk.Theme.space.sm
                Repeater {
                    model: panel.diagram.comments
                    Tk.Card {
                        id: card
                        required property var modelData
                        objectName: "commentCard_" + modelData.id
                        Tk.Flex.alignSelf: Tk.Flex.Stretch
                        interactive: true
                        selected: panel.diagram.activeComment === modelData.id
                        opacity: modelData.done ? 0.6 : 1
                        tooltip: qsTr("Comment by %1 on %2").arg(modelData.author).arg(modelData.anchor)
                        onClicked: panel.diagram.activeComment = modelData.id
                        Tk.Flex {
                            direction: Tk.Flex.Column
                            gap: Tk.Theme.space.xs
                            CommentEntry {
                                Tk.Flex.alignSelf: Tk.Flex.Stretch
                                pane: panel
                                entry: card.modelData
                            }
                            Tk.Caption {
                                Tk.Flex.alignSelf: Tk.Flex.Stretch
                                wrapMode: Text.Wrap
                                text: qsTr("On %1").arg(card.modelData.anchor)
                            }
                            Repeater {
                                model: card.modelData.replies
                                Tk.Box {
                                    id: replyBox
                                    required property var modelData
                                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                                    borderLeft: 2
                                    borderTop: 0
                                    borderRight: 0
                                    borderBottom: 0
                                    borderColor: Tk.Theme.color.border
                                    paddingLeft: Tk.Theme.space.sm
                                    CommentEntry { pane: panel; entry: replyBox.modelData; reply: true }
                                }
                            }
                            Tk.Flex {
                                Tk.Flex.alignSelf: Tk.Flex.Stretch
                                visible: panel.replying === card.modelData.id
                                direction: Tk.Flex.Column
                                gap: Tk.Theme.space.xs
                                Tk.TextArea {
                                    id: answer
                                    objectName: "commentReplyText"
                                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                                    rows: 2
                                    placeholderText: qsTr("Reply…")
                                }
                                Tk.Flex {
                                    gap: Tk.Theme.space.xs
                                    Tk.Button {
                                        objectName: "commentReplySend"
                                        small: true
                                        text: qsTr("Reply")
                                        enabled: answer.text.trim().length > 0
                                        onClicked: {
                                            panel.diagram.addComment(answer.text, card.modelData.id)
                                            answer.text = ""
                                            panel.replying = ""
                                        }
                                    }
                                    Tk.Button { small: true; text: qsTr("Cancel"); onClicked: panel.replying = "" }
                                }
                            }
                            Tk.Flex {
                                gap: Tk.Theme.space.xs
                                Tk.IconButton {
                                    objectName: "commentReply"
                                    small: true
                                    iconName: "reply"
                                    tooltip: qsTr("Reply")
                                    onClicked: { panel.replying = card.modelData.id; answer.forceActiveFocus() }
                                }
                                Tk.IconButton {
                                    objectName: "commentResolve"
                                    small: true
                                    iconName: card.modelData.done ? "rotate-ccw" : "check"
                                    tooltip: card.modelData.done ? qsTr("Reopen") : qsTr("Resolve")
                                    onClicked: panel.diagram.setCommentDone(card.modelData.id, !card.modelData.done)
                                }
                                Tk.IconButton {
                                    objectName: "commentDelete"
                                    small: true
                                    iconName: "trash-2"
                                    tooltip: qsTr("Delete the comment and its replies")
                                    onClicked: panel.diagram.deleteComment(card.modelData.id)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
