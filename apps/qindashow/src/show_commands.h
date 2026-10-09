// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaShow undo commands (plan section 6.3). Each is built from the
// canvas's old/new edit payloads; redo() is idempotent (the mutation
// already happened when the command is pushed), so QUndoStack::push()
// re-applying redo is harmless. SlideListCommand covers whole-deck
// changes (add/duplicate/delete/reorder slide); DeckStateCommand (M75)
// everything else deck-wide (themes, slide size, masters, sections).
//
// AGENT-GUARD (M75): every command finds its slide by id at undo/redo
// time (SlideRef), never through a Slide* kept from when it was pushed —
// a whole-deck command replaces the deck's slide list, and the canvas
// detaches it again on the next refresh, so a kept pointer would edit a
// list the deck no longer holds.

#include "presentation.h"

#include <QUndoCommand>

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

namespace QindaOffice::Apps::Show {

// A slide located by id in a deck, each time it is needed.
struct SlideRef {
    ShowModel::Presentation *deck = nullptr;
    QString slideId;
    [[nodiscard]] ShowModel::Slide *get() const { return deck != nullptr ? deck->findSlide(slideId) : nullptr; }
    bool operator==(const SlideRef &) const = default;
};

class InsertShapeCommand final : public QUndoCommand {
public:
    InsertShapeCommand(SlideRef slide, QString shapeId, QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;

private:
    SlideRef m_slide;
    QString m_shapeId;
    ShowModel::Shape m_copy;
    int m_index = -1;
};

class RemoveShapeCommand final : public QUndoCommand {
public:
    RemoveShapeCommand(SlideRef slide, int index, ShowModel::Shape removed,
                       QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;

private:
    SlideRef m_slide;
    int m_index;
    ShowModel::Shape m_removed;
};

class GeometryCommand final : public QUndoCommand {
public:
    GeometryCommand(SlideRef slide, QString shapeId, QRectF oldRect, QRectF newRect,
                    QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;
    bool mergeWith(const QUndoCommand *other) override;
    int id() const override { return 1; }

private:
    SlideRef m_slide;
    QString m_shapeId;
    QRectF m_oldRect;
    QRectF m_newRect;
};

class TextEditCommand final : public QUndoCommand {
public:
    TextEditCommand(SlideRef slide, QString shapeId, QList<ShowModel::TextParagraph> oldText,
                    QList<ShowModel::TextParagraph> newText, QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;

private:
    SlideRef m_slide;
    QString m_shapeId;
    QList<ShowModel::TextParagraph> m_oldText;
    QList<ShowModel::TextParagraph> m_newText;
};

class BuildStepCommand final : public QUndoCommand {
public:
    BuildStepCommand(SlideRef slide, QString shapeId, int oldStep, int newStep,
                     QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;

private:
    SlideRef m_slide;
    QString m_shapeId;
    int m_oldStep;
    int m_newStep;
};

class ShapeOrderCommand final : public QUndoCommand {
public:
    ShapeOrderCommand(SlideRef slide, QStringList oldOrder, QStringList newOrder,
                      QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;

private:
    void apply(const QStringList &order);
    SlideRef m_slide;
    QStringList m_oldOrder;
    QStringList m_newOrder;
};

// Whole-shape state replace for styling ops (M13): the canvas snapshots
// the shape before/after a fill/stroke/text-style change; this command
// swaps between the two. Shape copies share the image payload implicitly.
class ShapeStateCommand final : public QUndoCommand {
public:
    ShapeStateCommand(SlideRef slide, QString shapeId, ShowModel::Shape before,
                      ShowModel::Shape after, QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;

private:
    void apply(const ShowModel::Shape &state);

    SlideRef m_slide;
    QString m_shapeId;
    ShowModel::Shape m_before;
    ShowModel::Shape m_after;
};

// Whole-slide state replace (AI rewrite of the current slide's title/text).
// Follows the ShapeStateCommand pattern: the deck pointer plus the slide id
// locate the slide at undo/redo time.
class SlideStateCommand final : public QUndoCommand {
public:
    SlideStateCommand(ShowModel::Presentation *deck, QString slideId, ShowModel::Slide before,
                      ShowModel::Slide after, QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;

private:
    void apply(const ShowModel::Slide &state);

    ShowModel::Presentation *m_deck;
    QString m_slideId;
    ShowModel::Slide m_before;
    ShowModel::Slide m_after;
};

// Whole-shape replace located by slide id and shape id (M67, D-076): the
// table, cell-text and chart-data edits. Unlike ShapeStateCommand it holds
// no Slide*, so a slide-list change (which reallocates the deck's slides)
// cannot leave it pointing at freed memory.
class ShapeEditCommand final : public QUndoCommand {
public:
    ShapeEditCommand(ShowModel::Presentation *deck, QString slideId, QString shapeId, ShowModel::Shape before,
                     ShowModel::Shape after, const QString &text, QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;

private:
    void apply(const ShowModel::Shape &state);

    ShowModel::Presentation *m_deck;
    QString m_slideId;
    QString m_shapeId;
    ShowModel::Shape m_before;
    ShowModel::Shape m_after;
};

// A slide's speaker notes (M67): typing into one slide's notes is ONE undo
// step until another command intervenes (mergeWith).
class NotesEditCommand final : public QUndoCommand {
public:
    NotesEditCommand(ShowModel::Presentation *deck, QString slideId, QString before, QString after,
                     QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;
    bool mergeWith(const QUndoCommand *other) override;
    int id() const override { return 2; }

private:
    void apply(const QString &notes);

    ShowModel::Presentation *m_deck;
    QString m_slideId;
    QString m_before;
    QString m_after;
};

// A whole-deck change (M75, D-084): themes, slide size, master view edits,
// sections, backgrounds applied to all — the deck before and after,
// swapped wholesale (Qt's implicit sharing keeps the copies cheap).
class DeckStateCommand final : public QUndoCommand {
public:
    DeckStateCommand(ShowModel::Presentation *deck, ShowModel::Presentation before, ShowModel::Presentation after,
                     const QString &text, QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;

private:
    ShowModel::Presentation *m_deck;
    ShowModel::Presentation m_before;
    ShowModel::Presentation m_after;
};

class SlideListCommand final : public QUndoCommand {
public:
    SlideListCommand(ShowModel::Presentation *deck, QList<ShowModel::Slide> before,
                     QList<ShowModel::Slide> after, QUndoCommand *parent = nullptr);
    void redo() override;
    void undo() override;

private:
    ShowModel::Presentation *m_deck;
    QList<ShowModel::Slide> m_before;
    QList<ShowModel::Slide> m_after;
};

} // namespace QindaOffice::Apps::Show
