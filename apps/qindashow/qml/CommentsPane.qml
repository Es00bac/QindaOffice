// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaOffice.Show

// Review ▸ Comments in QindaShow (M75, D-084): the current slide's comment
// threads with their replies (ShowComments, show_comments.h). A new
// comment goes on the selected shape, else on the slide — the field says
// which; a card replies, edits, resolves (reopens) or deletes its thread;
// clicking a card selects its shape; Previous / Next walk every thread of
// the deck. Comments read from PowerPoint keep their author, date and
// anchor; resolved ones are dimmed. QindaWrite's CommentsPanel.qml is the
// model (D-068).
Tk.Flex {
    id: panel

    required property ShowComments comments
    property string editing: ""
    property string replying: ""

    function compose() { newText.forceActiveFocus() }

    objectName: "commentsPane"
    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm
    padding: Tk.Theme.space.sm

    Tk.TextArea {
        id: newText
        objectName: "commentNewText"
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        rows: 2
        enabled: panel.comments.canAdd
        placeholderText: panel.comments.canAdd ? qsTr("Comment on %1…").arg(panel.comments.anchorName)
                                               : qsTr("Open a slide to comment on it")
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
            enabled: panel.comments.canAdd && newText.text.trim().length > 0
            onClicked: {
                panel.comments.add(newText.text)
                newText.text = ""
            }
        }
        Item { Tk.Flex.grow: 1; implicitHeight: 1 }
        Tk.Caption {
            objectName: "commentsCount"
            text: qsTr("%1 on this slide · %2 in all").arg(panel.comments.count).arg(panel.comments.total)
        }
        Tk.IconButton {
            objectName: "commentPrevious"
            small: true
            iconName: "chevron-up"
            tooltip: qsTr("Previous comment")
            enabled: panel.comments.total > 0
            onClicked: panel.comments.previous()
        }
        Tk.IconButton {
            objectName: "commentNext"
            small: true
            iconName: "chevron-down"
            tooltip: qsTr("Next comment")
            enabled: panel.comments.total > 0
            onClicked: panel.comments.next()
        }
    }
    Tk.EmptyState {
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        visible: panel.comments.count === 0
        iconName: "message-square"
        title: qsTr("No comments on this slide")
        text: qsTr("Select a shape (or nothing, for the slide) and add a comment. Comments in PowerPoint decks appear here.")
    }

    Tk.Scroll {
        id: scroll
        objectName: "commentsScroll"
        Tk.Flex.grow: 1
        Tk.Flex.basis: 0
        Tk.Flex.minHeight: 60
        overflowX: Tk.Scroll.Hidden
        visible: panel.comments.count > 0
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            width: scroll.width - Tk.Theme.space.sm

            Repeater {
                model: panel.comments.comments
                Tk.Card {
                    id: card
                    required property var modelData
                    objectName: "commentCard_" + modelData.id
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                    interactive: true
                    selected: panel.comments.activeId === modelData.id
                    opacity: modelData.done ? 0.6 : 1
                    tooltip: qsTr("Comment by %1 on %2").arg(modelData.author).arg(modelData.anchor)
                    onClicked: panel.comments.select(modelData.id)

                    // The card's one child fills its padding box (Tk.Box).
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
                            // A reply hangs off its thread with a rule on the left.
                            Tk.Box {
                                required property var modelData
                                Tk.Flex.alignSelf: Tk.Flex.Stretch
                                borderLeft: 2
                                borderTop: 0
                                borderRight: 0
                                borderBottom: 0
                                borderColor: Tk.Theme.color.border
                                paddingLeft: Tk.Theme.space.sm
                                CommentEntry {
                                    pane: panel
                                    entry: modelData
                                    reply: true
                                }
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
                                        panel.comments.reply(card.modelData.id, answer.text)
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
                                onClicked: panel.comments.setDone(card.modelData.id, !card.modelData.done)
                            }
                            Tk.IconButton {
                                objectName: "commentDelete"
                                small: true
                                iconName: "trash-2"
                                tooltip: qsTr("Delete the comment and its replies")
                                onClicked: panel.comments.remove(card.modelData.id)
                            }
                        }
                    }
                }
            }
        }
    }
}
