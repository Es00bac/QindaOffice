// SPDX-License-Identifier: GPL-3.0-or-later
#include "show_tools.h"

#include "show_commands.h"
#include "show_document.h"
#include "show_host.h"
#include "slide_canvas_item.h"
#include "slide_factory.h"

#include <QJsonObject>

namespace QindaOffice::Apps::Show {

namespace {

namespace Model = QindaOffice::Show;

// The text boxes of a slide in z order: the addressable [slide.box] parts.
QList<Model::Shape *> textBoxes(Model::Slide &slide)
{
    QList<Model::Shape *> boxes;
    for (Model::Shape &shape : slide.shapes) {
        if (shape.kind == Model::ShapeKind::TextBox) {
            boxes.append(&shape);
        }
    }
    return boxes;
}

QString shapeText(const Model::Shape &shape)
{
    QStringList lines;
    for (const Model::TextParagraph &paragraph : shape.paragraphs) {
        QString line;
        for (const Model::TextRun &run : paragraph.runs) {
            line += run.text;
        }
        lines.append((paragraph.bullet ? QStringLiteral("- ") : QString()) + line);
    }
    return lines.join(QLatin1Char('\n'));
}

// New text into a box, keeping its first run's look; "- " lines become
// bullets.
void setShapeText(Model::Shape *shape, const QString &text)
{
    Model::TextRun look;
    if (!shape->paragraphs.isEmpty() && !shape->paragraphs.first().runs.isEmpty()) {
        look = shape->paragraphs.first().runs.first();
    }
    QList<Model::TextParagraph> paragraphs;
    for (QString line : text.split(QLatin1Char('\n'))) {
        Model::TextParagraph paragraph;
        if (line.startsWith(QLatin1String("- ")) || line.startsWith(QLatin1String("• "))) {
            paragraph.bullet = true;
            line = line.mid(2);
        }
        Model::TextRun run = look;
        run.text = line;
        paragraph.runs = {run};
        paragraphs.append(paragraph);
    }
    shape->paragraphs = paragraphs;
}

int slideIndex(const QList<Model::Slide> &slides, const QString &id)
{
    for (qsizetype i = 0; i < slides.size(); ++i) {
        if (slides.at(i).id == id) {
            return int(i);
        }
    }
    return -1;
}

} // namespace

QString ShowTools::snapshot(int budget)
{
    Model::Presentation *deck = m_host->showDocument()->presentation();
    m_slideIds.clear();
    QString out = QStringLiteral("%1 slides. The current slide is %2.\n")
                      .arg(deck->slides.size())
                      .arg(m_host->currentSlideRow() + 1);
    for (qsizetype i = 0; i < deck->slides.size(); ++i) {
        Model::Slide &slide = deck->slides[i];
        m_slideIds.append(slide.id);
        out += QStringLiteral("\nSlide %1 (layout %2)\n").arg(i + 1).arg(slide.layout);
        const QList<Model::Shape *> boxes = textBoxes(slide);
        for (qsizetype b = 0; b < boxes.size(); ++b) {
            out += QStringLiteral("[%1.%2] %3\n").arg(i + 1).arg(b + 1).arg(shapeText(*boxes.at(b)));
        }
        if (!slide.notes.isEmpty()) {
            out += QStringLiteral("Notes: %1\n").arg(slide.notes);
        }
        if (out.size() > budget) {
            out += QStringLiteral("\n(… later slides omitted)\n");
            break;
        }
    }
    return out;
}

QString ShowTools::operationGuide() const
{
    return QStringLiteral(
        "Slides are numbered as in the snapshot (1-based), text boxes as [slide.box]; on a "
        "titled layout box 1 is the visible title. In text, \"\\n\" separates paragraphs and a "
        "line starting \"- \" is a bullet.\n"
        "- {\"op\":\"set_text\",\"slide\":2,\"box\":2,\"text\":\"- First point\\n- Second\"}\n"
        "- {\"op\":\"add_slide\",\"after\":2,\"layout\":\"title+content\",\"title\":\"…\","
        "\"body\":\"- …\\n- …\",\"notes\":\"…\"} — \"after\": 0 puts it first; layouts: "
        "title, title+content, content, two-content, blank.\n"
        "- {\"op\":\"delete_slide\",\"slide\":4}\n"
        "- {\"op\":\"move_slide\",\"slide\":5,\"to\":2}\n"
        "- {\"op\":\"set_notes\",\"slide\":1,\"text\":\"speaker notes…\"}\n"
        "Numbers always refer to the snapshot, even after earlier edits in the same block.");
}

Ai::EditOutcome ShowTools::apply(const QJsonArray &edits)
{
    Ai::EditOutcome outcome;
    if (SlideCanvasItem *canvas = m_host->canvas()) {
        canvas->commitTextEdit(false); // land an open inline edit first
    }
    Model::Presentation *deck = m_host->showDocument()->presentation();
    const QList<Model::Slide> before = deck->slides;
    const auto resolve = [this, deck](int number) {
        return number >= 1 && number <= m_slideIds.size()
            ? slideIndex(deck->slides, m_slideIds.at(number - 1)) : -1;
    };
    for (const QJsonValue &value : edits) {
        const QJsonObject edit = value.toObject();
        const QString op = edit.value(QStringLiteral("op")).toString();
        const int number = edit.value(QStringLiteral("slide")).toInt();
        if (op == QLatin1String("set_text")) {
            const int at = resolve(number);
            const int box = edit.value(QStringLiteral("box")).toInt();
            const QList<Model::Shape *> boxes = at < 0 ? QList<Model::Shape *>() : textBoxes(deck->slides[at]);
            if (box < 1 || box > boxes.size()) {
                outcome.errors.append(QObject::tr("no text box [%1.%2]").arg(number).arg(box));
                continue;
            }
            setShapeText(boxes.at(box - 1), edit.value(QStringLiteral("text")).toString());
            outcome.changes.append(QObject::tr("rewrote [%1.%2]").arg(number).arg(box));
        } else if (op == QLatin1String("add_slide")) {
            const int after = edit.value(QStringLiteral("after")).toInt(int(m_slideIds.size()));
            const int anchor = after <= 0 ? -1 : resolve(after);
            const QString title = edit.value(QStringLiteral("title")).toString();
            Model::Slide slide = makeSlide(title, edit.value(QStringLiteral("layout"))
                                                      .toString(QStringLiteral("title+content")),
                                           deck->slideSize);
            const QList<Model::Shape *> boxes = textBoxes(slide);
            const QString body = edit.value(QStringLiteral("body")).toString();
            if (!body.isEmpty() && !boxes.isEmpty()) {
                setShapeText(boxes.size() > 1 ? boxes.at(1) : boxes.first(), body);
            }
            slide.notes = edit.value(QStringLiteral("notes")).toString();
            const int insertAt = after <= 0 ? 0 : (anchor < 0 ? int(deck->slides.size()) : anchor + 1);
            deck->slides.insert(insertAt, slide);
            outcome.changes.append(QObject::tr("added the slide “%1”").arg(title));
        } else if (op == QLatin1String("delete_slide") || op == QLatin1String("move_slide")
                   || op == QLatin1String("set_notes")) {
            const int at = resolve(number);
            if (at < 0) {
                outcome.errors.append(QObject::tr("%1: no slide %2").arg(op).arg(number));
                continue;
            }
            if (op == QLatin1String("delete_slide")) {
                if (deck->slides.size() <= 1) {
                    outcome.errors.append(QObject::tr("a presentation keeps at least one slide"));
                    continue;
                }
                deck->slides.removeAt(at);
                outcome.changes.append(QObject::tr("deleted slide %1").arg(number));
            } else if (op == QLatin1String("move_slide")) {
                const int to = qBound(0, edit.value(QStringLiteral("to")).toInt() - 1, int(deck->slides.size()) - 1);
                deck->slides.move(at, to);
                outcome.changes.append(QObject::tr("moved slide %1 to %2").arg(number).arg(to + 1));
            } else {
                deck->slides[at].notes = edit.value(QStringLiteral("text")).toString();
                outcome.changes.append(QObject::tr("wrote the notes of slide %1").arg(number));
            }
        } else {
            outcome.errors.append(QObject::tr("unknown operation “%1”").arg(op));
        }
    }
    outcome.applied = int(outcome.changes.size());
    if (deck->slides != before) {
        // AGENT-GUARD (show_host_slides.cpp): mutate first, then push — the
        // push's redo() is what refreshes the window.
        auto *command = new SlideListCommand(deck, before, deck->slides);
        command->setText(QObject::tr("Assistant Edit"));
        m_host->showDocument()->undoStack()->push(command);
        m_host->showDocument()->setDirty(true);
    }
    m_slideIds.clear(); // a second apply needs a fresh snapshot
    return outcome;
}

void ShowTools::undo()
{
    m_host->showDocument()->undoStack()->undo();
}

} // namespace QindaOffice::Apps::Show
