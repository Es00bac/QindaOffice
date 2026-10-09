// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Review ▸ Comments: the document's comments in text order, each with its
// replies (WriteComments, write_comments.h). A new comment goes on the
// selected text; a card replies, edits, resolves or deletes; clicking a card
// selects its text in the document. Comments read from Word keep their
// author, date and markup; resolved ones are shown dimmed.
Tk.Flex {
    id: panel

    required property var comments
    property string editing: ""
    property string replying: ""

    function compose() { newText.forceActiveFocus() }

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.sm
    padding: Tk.Theme.space.sm

    Tk.TextArea {
        id: newText
        objectName: "commentNewText"
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        rows: 2
        enabled: panel.comments.canAdd
        placeholderText: panel.comments.canAdd ? qsTr("Comment on the selected text…") : qsTr("Select text to comment on")
    }
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
    Tk.EmptyState {
        Tk.Flex.alignSelf: Tk.Flex.Stretch
        visible: panel.comments.count === 0
        iconName: "message-square"
        title: qsTr("No comments")
        text: qsTr("Select text and add a comment. Comments in Word documents appear here.")
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
                    tooltip: qsTr("Comment by %1").arg(modelData.author)
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
                            visible: card.modelData.quote.length > 0
                            wrapMode: Text.Wrap
                            text: "“" + card.modelData.quote + "”"
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
