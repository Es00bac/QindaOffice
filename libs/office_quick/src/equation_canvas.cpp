// SPDX-License-Identifier: GPL-3.0-or-later
#include "equation_canvas.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QPainter>
#include <QMimeData>
#include "math_linear.h"
#include "math_omml.h"

namespace QindaOffice::Quick {
EquationCanvas::EquationCanvas(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setFlag(ItemAcceptsInputMethod, true);
    setActiveFocusOnTab(true);
    setAntialiasing(true);
    connect(this, &EquationCanvas::selectionColorChanged, this, [this] { update(); });
}
void EquationCanvas::setSession(EquationSession *session)
{
    if (m_session == session) return;
    if (m_session) disconnect(m_session->editor(), nullptr, this, nullptr);
    m_session = session;
    if (session) {
        connect(session->editor(), &Math::EquationEditor::changed, this, &EquationCanvas::relayout);
        connect(session->editor(), &Math::EquationEditor::positionChanged, this, [this] { update(); });
    }
    relayout(); emit sessionChanged();
}
void EquationCanvas::relayout()
{
    if (!m_session) return;
    // Magnify small document text for editing without changing its stored size.
    Math::LayoutOptions options; options.fontSizePt = qMax<qreal>(18, m_session->pointSize());
    m_layout = Math::EquationLayout(m_session->editor()->equation(), options);
    const qreal margin = options.fontSizePt;
    m_origin = QPointF(margin, margin + m_layout.ascent());
    setImplicitWidth(m_layout.width() + margin * 2);
    setImplicitHeight(m_layout.ascent() + m_layout.descent() + margin * 2);
    update();
}
void EquationCanvas::paint(QPainter *painter)
{
    painter->fillRect(boundingRect(), Qt::white); // document paper, D-004
    if (!m_session) return;
    auto *editor = m_session->editor();
    if (editor->hasSelection()) {
        const QRectF a = m_layout.caretRect(editor->anchor());
        const QRectF b = m_layout.caretRect(editor->position());
        const QRectF selected(QPointF(qMin(a.x(), b.x()), qMin(a.y(), b.y())),
                              QPointF(qMax(a.x(), b.x()), qMax(a.bottom(), b.bottom())));
        painter->fillRect(selected.translated(m_origin), m_selectionColor);
    }
    m_layout.paint(*painter, m_origin);
    if (hasActiveFocus()) {
        QRectF caret = m_layout.caretRect(editor->position()).translated(m_origin);
        caret.setWidth(1); painter->fillRect(caret, Qt::black);
        if (!m_preedit.isEmpty()) {
            painter->setPen(Qt::black);
            painter->drawText(caret.topLeft() + QPointF(0, caret.height()), m_preedit);
        }
    }
}
bool EquationCanvas::event(QEvent *event)
{
    // A private draft owns its keyboard history/clipboard. Window shortcuts
    // must not simultaneously edit the underlying document behind the dialog.
    if (event->type() == QEvent::ShortcutOverride && hasActiveFocus()) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->matches(QKeySequence::Undo) || key->matches(QKeySequence::Redo)
            || key->matches(QKeySequence::Copy) || key->matches(QKeySequence::Cut)
            || key->matches(QKeySequence::Paste) || key->matches(QKeySequence::SelectAll)) {
            key->accept(); return true;
        }
    }
    return QQuickPaintedItem::event(event);
}
void EquationCanvas::keyPressEvent(QKeyEvent *event)
{
    if (!m_session || !m_session->active()) return;
    auto *editor = m_session->editor();
    const bool extend = event->modifiers().testFlag(Qt::ShiftModifier);
    if (event->matches(QKeySequence::Undo)) editor->undo();
    else if (event->matches(QKeySequence::Redo)) editor->redo();
    else if (event->matches(QKeySequence::Paste)) {
        const auto *mime = QGuiApplication::clipboard()->mimeData();
        const auto pasted = mime ? Math::readOmmlFragment(QString::fromUtf8(mime->data(QStringLiteral("application/x-qindaoffice-omml")))) : std::nullopt;
        if (pasted && pasted->lines.size() == 1) editor->insertArgument(pasted->lines.first());
        else editor->insertText(QGuiApplication::clipboard()->text());
    }
    else if (event->matches(QKeySequence::SelectAll)) {
        auto pos = editor->position(); pos.offset = 0; editor->setPosition(pos);
        pos.offset = Math::atomCount(*Math::slotArgument(editor->equation(), pos)); editor->setPosition(pos, true);
    } else if (event->matches(QKeySequence::Copy) || event->matches(QKeySequence::Cut)) {
        // A slot's text spelling is copied; structural interchange remains OMML.
        if (editor->hasSelection()) {
            Math::Equation selection = Math::makeEquation(false);
            selection.lines = {editor->selectedArgument()};
            auto *mime = new QMimeData;
            mime->setText(Math::toLinear(selection));
            mime->setData(QStringLiteral("application/x-qindaoffice-omml"), Math::writeOmmlFragment(selection).toUtf8());
            QGuiApplication::clipboard()->setMimeData(mime);
            if (event->matches(QKeySequence::Cut)) editor->eraseBackward();
        }
    } else switch (event->key()) {
    case Qt::Key_Left: editor->moveHorizontal(false, extend); break;
    case Qt::Key_Right: editor->moveHorizontal(true, extend); break;
    case Qt::Key_Up: editor->moveVertical(true, extend); break;
    case Qt::Key_Down: editor->moveVertical(false, extend); break;
    case Qt::Key_Tab:
    case Qt::Key_Backtab: {
        const bool backwards = extend || event->key() == Qt::Key_Backtab;
        if (!editor->nextSlot(backwards)) {
            if (auto *next = nextItemInFocusChain(!backwards); next && next != this)
                next->forceActiveFocus(backwards ? Qt::BacktabFocusReason : Qt::TabFocusReason);
        }
        break;
    }
    case Qt::Key_Home:
    case Qt::Key_End: {
        auto pos = editor->position();
        pos.offset = event->key() == Qt::Key_Home ? 0 : Math::atomCount(*Math::slotArgument(editor->equation(), pos));
        editor->setPosition(pos, extend); break;
    }
    case Qt::Key_Backspace: editor->eraseBackward(); break;
    case Qt::Key_Delete: editor->eraseForward(); break;
    default:
        if (!event->text().isEmpty() && !event->modifiers().testFlag(Qt::ControlModifier)
            && !event->modifiers().testFlag(Qt::AltModifier) && event->text().at(0).isPrint())
            editor->insertText(event->text());
        else { event->ignore(); return; }
    }
    event->accept();
}
void EquationCanvas::inputMethodEvent(QInputMethodEvent *event)
{
    if (!m_session || !m_session->active()) { event->ignore(); return; }
    m_preedit = event->preeditString();
    if (!event->commitString().isEmpty()) m_session->editor()->insertText(event->commitString());
    update(); event->accept();
}
QVariant EquationCanvas::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (query == Qt::ImEnabled) return m_session && m_session->active();
    if (query == Qt::ImCursorRectangle && m_session)
        return m_layout.caretRect(m_session->editor()->position()).translated(m_origin);
    // A structural caret has no stable flat-text offsets. Composed text is
    // committed as one insertion; do not expose guessed surrounding indices.
    if (query == Qt::ImSurroundingText) return QString();
    if (query == Qt::ImCursorPosition || query == Qt::ImAnchorPosition) return 0;
    return QQuickPaintedItem::inputMethodQuery(query);
}
void EquationCanvas::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    if (m_session) m_session->editor()->setPosition(m_layout.positionAt(event->position() - m_origin),
                                                   event->modifiers().testFlag(Qt::ShiftModifier));
    event->accept();
}
void EquationCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_session && event->buttons().testFlag(Qt::LeftButton))
        m_session->editor()->setPosition(m_layout.positionAt(event->position() - m_origin), true);
    event->accept();
}
void EquationCanvas::focusInEvent(QFocusEvent *event) { QQuickPaintedItem::focusInEvent(event); update(); }
void EquationCanvas::focusOutEvent(QFocusEvent *event) { QQuickPaintedItem::focusOutEvent(event); update(); }
}
