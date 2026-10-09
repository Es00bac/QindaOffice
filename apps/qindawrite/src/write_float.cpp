// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_float.h"

#include "docx_float.h"
#include "menu_tree.h"
#include "page_picture_tool.h"
#include "page_text_control.h"
#include "write_document.h"

#include <QTextCursor>
#include <QTextDocument>

namespace QindaOffice::Apps::Write {

using Quick::ActionEntry;
using Quick::MenuNode;

namespace {

struct WrapChoice {
    const char *wrap;
    const char *id;
    const char *text;
    const char *icon;
};
constexpr WrapChoice kWraps[] = {
    {"inline", "writeWrapInlineAction", QT_TRANSLATE_NOOP("PictureLayout", "In &Line with Text"), "text-cursor"},
    {"square", "writeWrapSquareAction", QT_TRANSLATE_NOOP("PictureLayout", "&Square"), "square"},
    {"tight", "writeWrapTightAction", QT_TRANSLATE_NOOP("PictureLayout", "&Tight"), "square-dashed"},
    {"through", "writeWrapThroughAction", QT_TRANSLATE_NOOP("PictureLayout", "T&hrough"), "square-dashed-text"},
    {"topAndBottom", "writeWrapTopBottomAction", QT_TRANSLATE_NOOP("PictureLayout", "T&op and Bottom"),
     "between-horizontal-start"},
    {"behind", "writeWrapBehindAction", QT_TRANSLATE_NOOP("PictureLayout", "Behin&d Text"), "send-to-back"},
    {"inFront", "writeWrapFrontAction", QT_TRANSLATE_NOOP("PictureLayout", "In F&ront of Text"), "bring-to-front"}};

} // namespace

PictureLayoutController::PictureLayoutController(PageTextControl *control, WriteDocument *document,
                                                 Quick::ActionModel *model, QObject *parent)
    : QObject(parent)
    , m_control(control)
    , m_document(document)
{
    for (const WrapChoice &choice : kWraps) {
        ActionEntry *entry = model->add(QString::fromLatin1(choice.id), tr(choice.text), {},
                                        QString::fromLatin1(choice.icon));
        entry->setCheckable(true);
        entry->setRadioGroup(QStringLiteral("writeWrap"));
        connect(entry, &ActionEntry::triggered, this, [this, wrap = QString::fromLatin1(choice.wrap)] {
            if (!m_syncing) {
                applyWrap(wrap);
            }
        });
        m_wraps.append(entry);
    }
    m_moveWithText = model->add(QStringLiteral("writeMoveWithTextAction"), tr("&Move with Text"), {},
                                QStringLiteral("move-vertical"));
    m_fixPosition = model->add(QStringLiteral("writeFixPositionAction"), tr("&Fix Position on Page"), {},
                               QStringLiteral("pin"));
    for (ActionEntry *entry : {m_moveWithText, m_fixPosition}) {
        entry->setCheckable(true);
        entry->setRadioGroup(QStringLiteral("writePictureAnchor"));
    }
    connect(m_moveWithText, &ActionEntry::triggered, this, [this] {
        if (!m_syncing) {
            setMoveWithText(true);
        }
    });
    connect(m_fixPosition, &ActionEntry::triggered, this, [this] {
        if (!m_syncing) {
            setMoveWithText(false);
        }
    });
    m_layout = model->add(QStringLiteral("writePictureLayoutAction"), tr("&Position and Wrapping…"), {},
                          QStringLiteral("move"));
    connect(m_layout, &ActionEntry::triggered, this, &PictureLayoutController::layoutDialogRequested);
    connect(m_control, &PageTextControl::selectionChanged, this, &PictureLayoutController::sync);
    connect(m_control, &PageTextControl::cursorPositionChanged, this, &PictureLayoutController::sync);
    sync();
}

QList<MenuNode> PictureLayoutController::menuItems() const
{
    QList<MenuNode> wraps;
    for (const ActionEntry *entry : m_wraps) {
        wraps.append(MenuNode::action(entry->id()));
    }
    wraps.append(MenuNode::separator());
    wraps.append(MenuNode::action(m_moveWithText->id()));
    wraps.append(MenuNode::action(m_fixPosition->id()));
    return {MenuNode::submenu(QStringLiteral("writeWrapTextMenu"), tr("&Wrap Text"), wraps),
            MenuNode::action(m_layout->id())};
}

void PictureLayoutController::sync()
{
    const int position = m_tool != nullptr ? m_tool->selected() : -1;
    m_syncing = true;
    Docx::PictureFloat layout;
    if (position >= 0) {
        layout = Docx::pictureFloat(Docx::characterFormatAt(*m_document->textDocument(), position));
    }
    for (int i = 0; i < m_wraps.size(); ++i) {
        m_wraps.at(i)->setEnabled(position >= 0);
        m_wraps.at(i)->setChecked(position >= 0 && layout.wrap == QLatin1String(kWraps[i].wrap));
    }
    const bool floating = position >= 0 && layout.isFloating();
    m_moveWithText->setEnabled(floating);
    m_fixPosition->setEnabled(floating);
    m_moveWithText->setChecked(floating && layout.movesWithText());
    m_fixPosition->setChecked(floating && !layout.movesWithText());
    m_layout->setEnabled(position >= 0);
    m_syncing = false;
}

bool PictureLayoutController::apply(const std::function<bool(int)> &change)
{
    const int position = m_tool != nullptr ? m_tool->selected() : -1;
    if (position < 0) {
        return false;
    }
    QTextCursor cursor(m_document->textDocument());
    cursor.beginEditBlock();
    const bool changed = change(position);
    cursor.endEditBlock();
    if (changed) {
        m_tool->select(position);
    }
    sync();
    return changed;
}

bool PictureLayoutController::applyWrap(const QString &wrap)
{
    return apply([&](int position) {
        const Docx::PictureFloat layout = Docx::wrappedPictureFloat(*m_document->textDocument(), position, wrap);
        return Docx::setPictureFloat(m_document->textDocument(), position, layout) >= 0;
    });
}

bool PictureLayoutController::setMoveWithText(bool move)
{
    return apply([&](int position) {
        Docx::PictureFloat layout = Docx::pictureFloat(Docx::characterFormatAt(*m_document->textDocument(), position));
        if (!layout.isFloating() || layout.movesWithText() == move) {
            return false;
        }
        // Word keeps the picture where it is: its offset from the page top
        // becomes its offset from the paragraph, or the other way round.
        const QRectF now = Docx::floatingPictureRect(*m_document->textDocument(), position);
        layout.vRelative = move ? QStringLiteral("paragraph") : QStringLiteral("page");
        layout.vAlign.clear();
        layout.vOffset = 0;
        Docx::setPictureFloat(m_document->textDocument(), position, layout);
        const QRectF base = Docx::floatingPictureRect(*m_document->textDocument(), position);
        layout.vOffset = now.top() - base.top();
        return Docx::setPictureFloat(m_document->textDocument(), position, layout) >= 0;
    });
}

QVariantMap PictureLayoutController::layoutInfo() const
{
    const int position = m_tool != nullptr ? m_tool->selected() : -1;
    if (position < 0) {
        return {};
    }
    const Docx::PictureFloat layout = Docx::pictureFloat(Docx::characterFormatAt(*m_document->textDocument(), position));
    QVariantMap info = layout.toMap();
    info.insert(QStringLiteral("moveWithText"), layout.movesWithText());
    return info;
}

bool PictureLayoutController::applyLayout(const QVariantMap &answer)
{
    return apply([&](int position) {
        const Docx::PictureFloat current = Docx::pictureFloat(Docx::characterFormatAt(*m_document->textDocument(), position));
        QVariantMap merged = current.toMap();
        for (auto it = answer.cbegin(); it != answer.cend(); ++it) {
            merged.insert(it.key(), it.value());
        }
        const Docx::PictureFloat layout = Docx::PictureFloat::fromMap(merged);
        if (layout == current) {
            return false;
        }
        return Docx::setPictureFloat(m_document->textDocument(), position, layout) >= 0;
    });
}

} // namespace QindaOffice::Apps::Write
