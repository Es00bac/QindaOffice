// SPDX-License-Identifier: GPL-3.0-or-later
// Insert ▸ Link (show_action_settings.h): an address, an e-mail or a Place
// in This Document for the text selected in the editor, else for the
// selected shape's click.
//
// AGENT-NOTE: the text's selection is taken when the command fires — the
// editor gives its selection up when the dialog takes the focus — and the
// link goes into the shape's paragraphs through a document made from them
// (text_bridge.h), as ONE undo step, whether or not the editor is still
// open: the editor's own edit is committed first.

#include "show_action_settings.h"

#include "show_document.h"
#include "show_host.h"
#include "slide_canvas_item.h"
#include "text_bridge.h"

#include <QTextCursor>
#include <QFileInfo>
#include <QTextDocument>

#include <memory>

namespace QindaOffice::Apps::Show {

void ShowActionSettings::captureSelection()
{
    SlideCanvasItem *canvas = m_host->canvas();
    QObject *editor = m_host->editorItem();
    m_linkSelection = {};
    if (!canvas->editingShapeId().isEmpty() && !canvas->editingCell() && editor != nullptr) {
        m_linkSelection.shapeId = canvas->editingShapeId();
        const int a = editor->property("selectionStart").toInt();
        const int b = editor->property("selectionEnd").toInt();
        const int cursor = editor->property("cursorPosition").toInt();
        m_linkSelection.start = a != b ? qMin(a, b) : cursor;
        m_linkSelection.end = a != b ? qMax(a, b) : cursor;
    }
}

QString ShowActionSettings::linkForPlace(const QVariantMap &link) const
{
    using ShowModel::ActionKind;
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    const QString place = link.value(QStringLiteral("place")).toString();
    const QHash<QString, ActionKind> jumps{{QStringLiteral("first"), ActionKind::FirstSlide},
                                           {QStringLiteral("last"), ActionKind::LastSlide},
                                           {QStringLiteral("next"), ActionKind::NextSlide},
                                           {QStringLiteral("previous"), ActionKind::PreviousSlide}};
    if (jumps.contains(place)) {
        return ShowModel::jumpLink(jumps.value(place));
    }
    const int slide = link.value(QStringLiteral("slide"), -1).toInt();
    if (place == u"slide" && slide >= 0 && slide < deck->slides.size()) {
        return ShowModel::slideLink(deck->slides.at(slide).id);
    }
    const int custom = link.value(QStringLiteral("custom"), -1).toInt();
    if (place == u"custom" && custom >= 0 && custom < deck->customShows.size()) {
        return ShowModel::customShowLink(deck->customShows.at(custom).number, link.value(QStringLiteral("returnAfter")).toBool());
    }
    return {};
}

QVariantMap ShowActionSettings::loadLink() const
{
    const LinkSelection &selection = m_linkSelection;
    const ShowModel::Slide *slide = m_host->canvas()->slide();
    const ShowModel::Shape *shape = nullptr;
    QString text;
    QString link;
    if (!selection.shapeId.isEmpty() && slide != nullptr) {
        shape = const_cast<ShowModel::Slide *>(slide)->findShape(selection.shapeId);
        if (shape != nullptr) {
            const std::unique_ptr<QTextDocument> document(documentFromParagraphs(shape->paragraphs));
            QTextCursor cursor(document.get());
            cursor.setPosition(qMin(selection.start, document->characterCount() - 1));
            cursor.setPosition(qMin(selection.end, document->characterCount() - 1), QTextCursor::KeepAnchor);
            text = cursor.selectedText();
            link = hyperlinkAt(*document, selection.start);
        }
    }
    const bool onText = shape != nullptr;
    if (!onText) {
        shape = target();
        if (shape == nullptr) {
            return {};
        }
        link = shape->click.link;
        text = shape->displayText();
    }
    QVariantMap map{{QStringLiteral("target"), onText ? QStringLiteral("text") : QStringLiteral("shape")},
                    {QStringLiteral("text"), text},
                    {QStringLiteral("linked"), !link.isEmpty()},
                    {QStringLiteral("kind"), QStringLiteral("web")},
                    {QStringLiteral("address"), QString()},
                    {QStringLiteral("place"), QStringLiteral("first")},
                    {QStringLiteral("slide"), -1},
                    {QStringLiteral("custom"), -1}};
    const ShowModel::ActionTarget parsed = onText ? ShowModel::parseAction(link) : ShowModel::parseAction(shape->click);
    ShowModel::ShowAction textLink;
    textLink.link = link;
    const QVariantMap action = actionMap(onText ? textLink : shape->click);
    switch (parsed.kind) {
    case ShowModel::ActionKind::FirstSlide:
    case ShowModel::ActionKind::LastSlide:
    case ShowModel::ActionKind::NextSlide:
    case ShowModel::ActionKind::PreviousSlide:
    case ShowModel::ActionKind::Slide:
    case ShowModel::ActionKind::CustomShow:
        map.insert(QStringLiteral("kind"), QStringLiteral("place"));
        map.insert(QStringLiteral("place"), action.value(QStringLiteral("kind")));
        map.insert(QStringLiteral("slide"), action.value(QStringLiteral("slide")));
        map.insert(QStringLiteral("custom"), action.value(QStringLiteral("custom")));
        map.insert(QStringLiteral("returnAfter"), parsed.returnAfter);
        break;
    case ShowModel::ActionKind::Url:
        if (parsed.address.startsWith(QLatin1String("mailto:"))) {
            map.insert(QStringLiteral("kind"), QStringLiteral("mail"));
            map.insert(QStringLiteral("address"), parsed.address.mid(7));
        } else {
            map.insert(QStringLiteral("address"), parsed.address);
        }
        break;
    case ShowModel::ActionKind::File:
        map.insert(QStringLiteral("address"), parsed.address);
        break;
    default:
        break;
    }
    const QString place = map.value(QStringLiteral("place")).toString();
    if (place == u"viewed" || place == u"end") {
        map.insert(QStringLiteral("place"), QStringLiteral("first"));
    }
    return map;
}

bool ShowActionSettings::applyLink(const QVariantMap &map)
{
    const QString kind = map.value(QStringLiteral("kind")).toString();
    QString link;
    const QString address = map.value(QStringLiteral("address")).toString().trimmed();
    if (kind == u"web" && !address.isEmpty()) {
        if (QFileInfo(address).isAbsolute() || address.startsWith(QLatin1String("./"))
            || address.startsWith(QLatin1String("../"))) {
            link = QUrl::fromLocalFile(QFileInfo(address).absoluteFilePath()).toString();
        } else {
            QVariantMap url{{QStringLiteral("kind"), QStringLiteral("url")}, {QStringLiteral("url"), address}};
            link = actionFrom(url, {}).link;
        }
    } else if (kind == u"mail" && !address.isEmpty()) {
        link = address.startsWith(QLatin1String("mailto:")) ? address : QStringLiteral("mailto:") + address;
    } else if (kind == u"place") {
        link = linkForPlace(map);
    }
    if (!kind.isEmpty() && link.isEmpty()) {
        return false; // nothing to link to
    }
    const LinkSelection selection = m_linkSelection;
    SlideCanvasItem *canvas = m_host->canvas();
    canvas->commitTextEdit(false);
    if (!selection.shapeId.isEmpty() && canvas->slide() != nullptr && canvas->slide()->findShape(selection.shapeId) != nullptr) {
        const QString display = map.value(QStringLiteral("text")).toString();
        const QColor color = hyperlinkColor(*m_host->showDocument()->presentation(), m_host->currentSlide());
        return canvas->editShape(selection.shapeId, link.isEmpty() ? tr("Remove link") : tr("Link"), [&](ShowModel::Shape &shape) {
            const std::unique_ptr<QTextDocument> document(documentFromParagraphs(shape.paragraphs));
            const int last = document->characterCount() - 1;
            int start = qBound(0, selection.start, last);
            int end = qBound(start, selection.end, last);
            if (start == end && !link.isEmpty()) {
                // Nothing selected: the text to display goes in, linked.
                const QString text = display.isEmpty() ? (address.isEmpty() ? link : address) : display;
                QTextCursor cursor(document.get());
                cursor.setPosition(start);
                cursor.insertText(text);
                end = start + int(text.size());
            }
            if (end > start && !link.isEmpty() && !display.isEmpty()) {
                QTextCursor cursor(document.get());
                cursor.setPosition(start);
                cursor.setPosition(end, QTextCursor::KeepAnchor);
                if (cursor.selectedText() != display) {
                    cursor.insertText(display);
                    end = start + int(display.size());
                }
            }
            setHyperlink(*document, start, end, link, color);
            shape.paragraphs = paragraphsFromDocument(*document);
            return true;
        });
    }
    const ShowModel::Shape *shape = target();
    if (shape == nullptr) {
        return false;
    }
    return canvas->editShape(shape->id, link.isEmpty() ? tr("Remove link") : tr("Link"), [&link](ShowModel::Shape &edited) {
        edited.click.link = link;
        edited.click.address.clear();
        return true;
    });
}

} // namespace QindaOffice::Apps::Show
