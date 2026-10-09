// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_paragraph.h"

#include "docx_format.h"
#include "docx_metrics.h"
#include "docx_paragraph_format.h"
#include "docx_properties.h"
#include "menu_tree.h"
#include "ooxml_units.h"
#include "page_text_control.h"
#include "write_document.h"
#include "write_pagination.h"
#include "write_selection.h"

namespace QindaOffice::Apps::Write {

using Quick::ActionEntry;
using Quick::MenuNode;
namespace Units = Ooxml::Units;

namespace {

QString alignmentName(Qt::Alignment a)
{
    a &= Qt::AlignHorizontal_Mask;
    return a == Qt::AlignHCenter ? QStringLiteral("center")
        : a == Qt::AlignRight    ? QStringLiteral("right")
        : a == Qt::AlignJustify  ? QStringLiteral("justify")
                                 : QStringLiteral("left");
}

Qt::Alignment alignmentOf(const QString &name)
{
    return name == u"center" ? Qt::AlignHCenter
        : name == u"right"   ? Qt::AlignRight
        : name == u"justify" ? Qt::AlignJustify
                             : Qt::AlignLeft;
}

// The dialog's line spacing from the paragraph's own (Word's) height.
std::pair<QString, qreal> lineRuleOf(const QTextBlockFormat &format)
{
    const Docx::LineSpacing own = Docx::ownLineSpacing(format);
    switch (own.type) {
    case QTextBlockFormat::ProportionalHeight:
        if (qAbs(own.value - 100) < 0.5) {
            return {QStringLiteral("single"), 1.0};
        }
        if (qAbs(own.value - 150) < 0.5) {
            return {QStringLiteral("oneHalf"), 1.5};
        }
        if (qAbs(own.value - 200) < 0.5) {
            return {QStringLiteral("double"), 2.0};
        }
        return {QStringLiteral("multiple"), own.value / 100.0};
    case QTextBlockFormat::FixedHeight:
        return {QStringLiteral("exactly"), Units::pxToPt(own.value)};
    case QTextBlockFormat::MinimumHeight:
        return {QStringLiteral("atLeast"), Units::pxToPt(own.value)};
    default:
        return {QStringLiteral("single"), 1.0};
    }
}

std::pair<int, qreal> lineHeightFor(const QString &rule, qreal at)
{
    if (rule == u"oneHalf") {
        return {QTextBlockFormat::ProportionalHeight, 150};
    }
    if (rule == u"double") {
        return {QTextBlockFormat::ProportionalHeight, 200};
    }
    if (rule == u"multiple") {
        return {QTextBlockFormat::ProportionalHeight, qBound(6.0, at * 100.0, 13200.0)};
    }
    if (rule == u"exactly") {
        return {QTextBlockFormat::FixedHeight, Units::ptToPx(qBound(0.7, at, 1584.0))};
    }
    if (rule == u"atLeast") {
        return {QTextBlockFormat::MinimumHeight, Units::ptToPx(qBound(0.0, at, 1584.0))};
    }
    return {QTextBlockFormat::ProportionalHeight, 100};
}

} // namespace

ParagraphController::ParagraphController(PageTextControl *control, WriteDocument *document,
                                         Quick::ActionModel *model, QObject *parent)
    : QObject(parent)
    , m_control(control)
    , m_document(document)
{
    m_paragraph = model->add(QStringLiteral("writeParagraphAction"), tr("&Paragraph…"), {}, QStringLiteral("pilcrow"));
    m_tabs = model->add(QStringLiteral("writeTabsAction"), tr("&Tabs…"), {}, QStringLiteral("ruler"));
    connect(m_paragraph, &ActionEntry::triggered, this, &ParagraphController::paragraphDialogRequested);
    connect(m_tabs, &ActionEntry::triggered, this, &ParagraphController::tabsDialogRequested);
}

QList<MenuNode> ParagraphController::menuItems() const
{
    return {MenuNode::action(m_paragraph->id()), MenuNode::action(m_tabs->id())};
}

QString ParagraphController::paragraphId() const
{
    return m_paragraph->id();
}

QString ParagraphController::tabsId() const
{
    return m_tabs->id();
}

QVariantMap ParagraphController::paragraphInfo() const
{
    return paragraphInfoAt(m_control->textCursor());
}

QVariantMap paragraphInfoAt(const QTextCursor &cursor)
{
    const QTextBlock block = selectedBlocks(cursor).value(0);
    const QTextBlockFormat f = block.blockFormat();
    const Docx::OwnSpacing spacing = Docx::ownSpacing(f);
    const auto [rule, at] = lineRuleOf(f);
    const qreal indent = f.textIndent();
    return {{QStringLiteral("alignment"), alignmentName(f.alignment())},
            {QStringLiteral("outline"), Docx::outlineLevel(f)},
            {QStringLiteral("left"), f.leftMargin()},
            {QStringLiteral("right"), f.rightMargin()},
            {QStringLiteral("special"), indent > 0 ? QStringLiteral("firstLine") : indent < 0 ? QStringLiteral("hanging") : QStringLiteral("none")},
            {QStringLiteral("by"), qAbs(indent)},
            {QStringLiteral("before"), Units::twipsToPt(spacing.before)},
            {QStringLiteral("beforeAuto"), spacing.autoBefore},
            {QStringLiteral("after"), Units::twipsToPt(spacing.after)},
            {QStringLiteral("afterAuto"), spacing.autoAfter},
            {QStringLiteral("lineRule"), rule},
            {QStringLiteral("lineAt"), at},
            {QStringLiteral("contextual"), spacing.contextual},
            {QStringLiteral("widow"), !f.hasProperty(Docx::kWidowControl) || f.boolProperty(Docx::kWidowControl)},
            {QStringLiteral("keepNext"), f.boolProperty(Docx::kKeepWithNext)},
            {QStringLiteral("keepLines"), f.nonBreakableLines()},
            {QStringLiteral("pageBreakBefore"), bool(Docx::ownPageBreaks(f) & QTextFormat::PageBreak_AlwaysBefore)},
            {QStringLiteral("count"), int(selectedBlocks(cursor).size())}};
}

bool ParagraphController::applyParagraph(const QVariantMap &answer)
{
    QTextCursor cursor = m_control->textCursor();
    const QList<QTextBlock> blocks = selectedBlocks(cursor);
    cursor.beginEditBlock();
    const bool applied = applyParagraphAt(cursor, answer);
    cursor.endEditBlock();
    if (!applied) {
        return false;
    }
    m_document->setDirty(true);
    if (!blocks.isEmpty() && m_control->document() == m_document->textDocument()) {
        m_document->keepRules()->schedule(blocks.first().position());
    }
    return true;
}

bool applyParagraphAt(const QTextCursor &cursor, const QVariantMap &answer)
{
    const QStringList changed = answer.value(QStringLiteral("changed")).toStringList();
    if (changed.isEmpty()) {
        return false;
    }
    const auto has = [&](const char *key) { return changed.contains(QString::fromLatin1(key)); };
    const auto value = [&](const char *key) { return answer.value(QString::fromLatin1(key)); };
    const QList<QTextBlock> blocks = selectedBlocks(cursor);
    for (const QTextBlock &block : blocks) {
        QTextBlockFormat f = block.blockFormat();
        if (has("alignment")) {
            f.setAlignment(alignmentOf(value("alignment").toString()));
        }
        if (has("outline")) {
            Docx::setOutlineLevel(f, value("outline").toInt());
        }
        if (has("left")) {
            f.setLeftMargin(value("left").toReal());
        }
        if (has("right")) {
            f.setRightMargin(value("right").toReal());
        }
        if (has("special") || has("by")) {
            const QString special = has("special") ? value("special").toString()
                : f.textIndent() > 0                ? QStringLiteral("firstLine")
                : f.textIndent() < 0                ? QStringLiteral("hanging")
                                                    : QStringLiteral("none");
            const qreal by = has("by") ? qAbs(value("by").toReal()) : qAbs(f.textIndent());
            f.setTextIndent(special == u"firstLine" ? by : special == u"hanging" ? -by : 0.0);
        }
        if (has("lineRule") || has("lineAt")) {
            const auto [rule, at] = lineRuleOf(f);
            const auto [type, height] = lineHeightFor(has("lineRule") ? value("lineRule").toString() : rule,
                                                      has("lineAt") ? value("lineAt").toReal() : at);
            Docx::setWordLineSpacing(f, block, type, height);
        }
        if (has("widow")) {
            if (value("widow").toBool()) {
                f.clearProperty(Docx::kWidowControl);
            } else {
                f.setProperty(Docx::kWidowControl, false);
            }
        }
        if (has("keepNext")) {
            f.setProperty(Docx::kKeepWithNext, value("keepNext").toBool());
        }
        if (has("keepLines")) {
            f.setNonBreakableLines(value("keepLines").toBool());
        }
        if (has("pageBreakBefore")) {
            // The display-only pagination break (kSyntheticBreak) holds the
            // paragraph's own flags while it is in place.
            const QTextFormat::PageBreakFlags own = Docx::ownPageBreaks(f);
            const QTextFormat::PageBreakFlags next = value("pageBreakBefore").toBool()
                ? own | QTextFormat::PageBreak_AlwaysBefore
                : own & ~QTextFormat::PageBreakFlags(QTextFormat::PageBreak_AlwaysBefore);
            if (f.hasProperty(Docx::kSyntheticBreak)) {
                f.setProperty(Docx::kSyntheticBreak, int(next));
            } else {
                f.setPageBreakPolicy(next);
            }
            if (!value("pageBreakBefore").toBool()) {
                f.clearProperty(Docx::kPageBreakRun); // a w:br page break goes with it
            }
        }
        if (has("left") && f.hasProperty(Docx::kTabStops)) {
            Docx::setTabStops(f, Docx::tabStops(f)); // Word's stops stay put; Qt measures from the indent
        }
        QTextCursor(block).setBlockFormat(f);
        if (has("before") || has("beforeAuto") || has("after") || has("afterAuto") || has("contextual")) {
            Docx::OwnSpacing s = Docx::ownSpacing(block.blockFormat());
            if (has("before")) {
                s.before = qRound(Units::ptToTwips(qBound(0.0, value("before").toReal(), 1584.0)));
            }
            if (has("beforeAuto")) {
                s.autoBefore = value("beforeAuto").toBool();
            }
            if (has("after")) {
                s.after = qRound(Units::ptToTwips(qBound(0.0, value("after").toReal(), 1584.0)));
            }
            if (has("afterAuto")) {
                s.autoAfter = value("afterAuto").toBool();
            }
            if (has("contextual")) {
                s.contextual = value("contextual").toBool();
            }
            Docx::setParagraphSpacing(block, s);
        }
    }
    return true;
}

QVariantMap ParagraphController::tabsInfo() const
{
    return {{QStringLiteral("stops"), tabStopsAt(m_control->textCursor())},
            {QStringLiteral("defaultStop"), m_document->parts().defaultTabStop}};
}

QVariantList tabStopsAt(const QTextCursor &cursor)
{
    const QTextBlock block = selectedBlocks(cursor).value(0);
    QVariantList stops;
    for (const Docx::TabStop &s : Docx::tabStops(block.blockFormat())) {
        stops.append(QVariantMap{{QStringLiteral("position"), s.position},
                                 {QStringLiteral("alignment"), s.alignment},
                                 {QStringLiteral("leader"), s.leader}});
    }
    return stops;
}

bool ParagraphController::applyTabs(const QVariantMap &answer)
{
    QTextCursor cursor = m_control->textCursor();
    cursor.beginEditBlock();
    applyTabStopsAt(cursor, answer.value(QStringLiteral("stops")).toList());
    cursor.endEditBlock();
    const qreal defaultStop = answer.value(QStringLiteral("defaultStop"), m_document->parts().defaultTabStop).toReal();
    if (defaultStop > 0 && qAbs(defaultStop - m_document->parts().defaultTabStop) > 0.01) {
        m_document->setDefaultTabStop(defaultStop);
    }
    m_document->setDirty(true);
    return true;
}

void applyTabStopsAt(const QTextCursor &cursor, const QVariantList &answer)
{
    QList<Docx::TabStop> stops;
    for (const QVariant &v : answer) {
        const QVariantMap m = v.toMap();
        const int position = m.value(QStringLiteral("position")).toInt();
        if (position <= 0 || position > 31680) {
            continue; // Word's limit: 22 in
        }
        stops.append({position, m.value(QStringLiteral("alignment"), QStringLiteral("left")).toString(),
                      m.value(QStringLiteral("leader"), QStringLiteral("none")).toString()});
    }
    for (const QTextBlock &block : selectedBlocks(cursor)) {
        QTextBlockFormat f = block.blockFormat();
        Docx::setTabStops(f, stops);
        QTextCursor(block).setBlockFormat(f);
    }
}

} // namespace QindaOffice::Apps::Write
