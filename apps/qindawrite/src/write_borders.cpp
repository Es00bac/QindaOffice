// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_borders.h"

#include "docx_borders.h"
#include "docx_paragraph_format.h"
#include "docx_table_format.h"
#include "menu_tree.h"
#include "page_text_control.h"
#include "write_document.h"
#include "write_selection.h"

#include <QTextTable>

namespace QindaOffice::Apps::Write {

using Docx::BorderTarget;
using Docx::WordBorder;
using Docx::WordBorders;
using Quick::ActionEntry;
using Quick::MenuNode;

namespace {

// Word's Line Style list (the common part) and its Line Weight list.
const struct {
    const char *name;
    const char *label;
} kStyles[] = {{"single", QT_TR_NOOP("Single")},       {"dotted", QT_TR_NOOP("Dotted")},
               {"dashed", QT_TR_NOOP("Dashed")},       {"dotDash", QT_TR_NOOP("Dot dash")},
               {"dotDotDash", QT_TR_NOOP("Dot dot dash")}, {"double", QT_TR_NOOP("Double")},
               {"triple", QT_TR_NOOP("Triple")},       {"thinThickSmallGap", QT_TR_NOOP("Thin–thick")},
               {"thickThinSmallGap", QT_TR_NOOP("Thick–thin")}, {"wave", QT_TR_NOOP("Wave")}};
const struct {
    int size;
    const char *label;
} kSizes[] = {{2, "¼ pt"}, {4, "½ pt"}, {6, "¾ pt"}, {8, "1 pt"}, {12, "1½ pt"},
              {18, "2¼ pt"}, {24, "3 pt"}, {36, "4½ pt"}, {48, "6 pt"}};

const struct {
    const char *id;
    const char *text;
    const char *icon;
} kEdges[] = {
    {"writeBordersAllAction", QT_TR_NOOP("&All Borders"), "grid-3x3"},
    {"writeBordersOutsideAction", QT_TR_NOOP("&Outside Borders"), "square-round-corner"},
    {"writeBordersInsideAction", QT_TR_NOOP("&Inside Borders"), "grid-2x2"},
    {"writeBordersTopAction", QT_TR_NOOP("&Top Border"), "panel-top"},
    {"writeBordersBottomAction", QT_TR_NOOP("&Bottom Border"), "panel-bottom"},
    {"writeBordersLeftAction", QT_TR_NOOP("&Left Border"), "layout-panel-left"},
    {"writeBordersRightAction", QT_TR_NOOP("&Right Border"), "panel-right"},
    {"writeBordersInsideHAction", QT_TR_NOOP("Inside &Horizontal Border"), "square-centerline-dashed-horizontal"},
    {"writeBordersInsideVAction", QT_TR_NOOP("Inside &Vertical Border"), "square-centerline-dashed-vertical"},
    {"writeBordersNoneAction", QT_TR_NOOP("&No Border"), "square-dashed"},
};

QVariantMap edgeToMap(const WordBorder &b)
{
    return {{QStringLiteral("style"), b.isNone() ? QStringLiteral("nil") : b.style},
            {QStringLiteral("size"), b.size},
            {QStringLiteral("color"), b.color}};
}

WordBorder edgeFromMap(const QVariantMap &m)
{
    const QString style = m.value(QStringLiteral("style")).toString();
    if (style.isEmpty() || style == u"nil" || style == u"none") {
        return WordBorder::none();
    }
    return {style, m.value(QStringLiteral("size"), 4).toInt(), m.value(QStringLiteral("color"), QStringLiteral("auto")).toString(),
            m.value(QStringLiteral("space"), 0).toInt()};
}

// Paragraph sides for the dialog's edge classes and the button's targets.
QString paragraphSide(const QString &edge)
{
    return edge == u"insideH" ? QStringLiteral("between") : edge;
}

QStringList paragraphEdges(BorderTarget target)
{
    switch (target) {
    case BorderTarget::All:
        return {QStringLiteral("top"), QStringLiteral("left"), QStringLiteral("bottom"), QStringLiteral("right"),
                QStringLiteral("between")};
    case BorderTarget::Outside:
        return {QStringLiteral("top"), QStringLiteral("left"), QStringLiteral("bottom"), QStringLiteral("right")};
    case BorderTarget::Inside:
    case BorderTarget::InsideHorizontal:
        return {QStringLiteral("between")};
    case BorderTarget::Top:
        return {QStringLiteral("top")};
    case BorderTarget::Bottom:
        return {QStringLiteral("bottom")};
    case BorderTarget::Left:
        return {QStringLiteral("left")};
    case BorderTarget::Right:
        return {QStringLiteral("right")};
    case BorderTarget::InsideVertical:
    case BorderTarget::None:
        break;
    }
    return {};
}

QString tableEdge(BorderTarget target)
{
    switch (target) {
    case BorderTarget::Top:
        return QStringLiteral("top");
    case BorderTarget::Bottom:
        return QStringLiteral("bottom");
    case BorderTarget::Left:
        return QStringLiteral("left");
    case BorderTarget::Right:
        return QStringLiteral("right");
    case BorderTarget::InsideHorizontal:
        return QStringLiteral("insideH");
    case BorderTarget::InsideVertical:
        return QStringLiteral("insideV");
    default:
        return {};
    }
}

} // namespace

BordersController::BordersController(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model,
                                     QObject *parent)
    : QObject(parent)
    , m_control(control)
    , m_document(document)
{
    m_penColor = QColor(); // Word's "Automatic"
    createActions(model);
    connect(m_control, &PageTextControl::cursorPositionChanged, this, [this] { syncFromCursor(); });
    connect(m_control, &PageTextControl::documentChanged, this, [this] { syncFromCursor(); });
    syncFromCursor();
}

void BordersController::createActions(Quick::ActionModel *model)
{
    for (int i = 0; i < int(std::size(kEdges)); ++i) {
        ActionEntry *entry = model->add(QString::fromLatin1(kEdges[i].id), tr(kEdges[i].text), {},
                                        QString::fromLatin1(kEdges[i].icon));
        connect(entry, &ActionEntry::triggered, this, [this, i] { applyEdges(i); });
        m_edges.append(entry);
    }
    m_dialog = model->add(QStringLiteral("writeBordersShadingAction"), tr("Borders and &Shading…"), {},
                          QStringLiteral("sliders-horizontal"));
    connect(m_dialog, &ActionEntry::triggered, this, &BordersController::dialogRequested);
    m_shade = model->add(QStringLiteral("writeShadingAction"), tr("&Shading"), {}, QStringLiteral("paint-bucket"));
    connect(m_shade, &ActionEntry::triggered, this, [this] { applyShading(m_shading); });
    m_noShade = model->add(QStringLiteral("writeNoShadingAction"), tr("&No Shading"), {}, QStringLiteral("eraser"));
    connect(m_noShade, &ActionEntry::triggered, this, [this] {
        const QColor keep = m_shading;
        applyShading(QColor());
        m_shading = keep;
    });
}

QList<MenuNode> BordersController::menuItems() const
{
    QList<MenuNode> borders;
    for (const ActionEntry *entry : m_edges) {
        borders.append(MenuNode::action(entry->id()));
        if (entry == m_edges.at(2) || entry == m_edges.at(8)) {
            borders.append(MenuNode::separator());
        }
    }
    return {MenuNode::submenu(QStringLiteral("writeBordersMenu"), tr("&Borders"), borders),
            MenuNode::submenu(QStringLiteral("writeShadingMenu"), tr("S&hading"),
                              {MenuNode::action(m_shade->id()), MenuNode::action(m_noShade->id())}),
            MenuNode::action(m_dialog->id())};
}

QStringList BordersController::borderIds() const
{
    QStringList ids;
    for (const ActionEntry *entry : m_edges) {
        ids.append(entry->id());
    }
    return ids;
}

void BordersController::setPenStyleIndex(int index)
{
    if (index >= 0 && index < int(std::size(kStyles)) && index != m_penStyle) {
        m_penStyle = index;
        emit penChanged();
    }
}

void BordersController::setPenSizeIndex(int index)
{
    if (index >= 0 && index < int(std::size(kSizes)) && index != m_penSize) {
        m_penSize = index;
        emit penChanged();
    }
}

void BordersController::setPenColor(const QColor &color)
{
    if (color != m_penColor) {
        m_penColor = color;
        emit penChanged();
    }
}

void BordersController::setShadingColor(const QColor &color)
{
    if (color != m_shading) {
        m_shading = color;
        emit penChanged();
    }
}

QStringList BordersController::penStyleLabels() const
{
    QStringList out;
    for (const auto &s : kStyles) {
        out.append(tr(s.label));
    }
    return out;
}

QStringList BordersController::penSizeLabels() const
{
    QStringList out;
    for (const auto &s : kSizes) {
        out.append(QString::fromUtf8(s.label));
    }
    return out;
}

QStringList BordersController::penStyleNames() const
{
    QStringList out;
    for (const auto &s : kStyles) {
        out.append(QString::fromLatin1(s.name));
    }
    return out;
}

QList<int> BordersController::penSizeValues() const
{
    QList<int> out;
    for (const auto &s : kSizes) {
        out.append(s.size);
    }
    return out;
}

void BordersController::applyEdges(int targetIndex)
{
    const auto target = BorderTarget(targetIndex);
    const WordBorder pen{QString::fromLatin1(kStyles[m_penStyle].name), kSizes[m_penSize].size,
                         m_penColor.isValid() ? m_penColor.name().mid(1).toUpper() : QStringLiteral("auto"), 0};
    QTextCursor cursor = m_control->textCursor();
    cursor.beginEditBlock();
    if (QTextTable *table = cursor.currentTable()) {
        const Docx::CellRange range = Docx::selectedCells(cursor);
        // A single edge the selection already draws with this pen is taken
        // away (Word's toggle); one drawn otherwise takes the pen.
        const QString edge = tableEdge(target);
        const bool off = !edge.isEmpty()
            && Docx::parseBordersSpec(Docx::bordersSpec({{edge, Docx::rangeBorders(table, range).value(edge, WordBorder::none())}}))
                   == Docx::parseBordersSpec(Docx::bordersSpec({{edge, pen}}));
        Docx::applyTableBorders(table, range, target, off ? WordBorder::none() : pen);
    } else {
        const QList<QTextBlock> blocks = selectedBlocks(cursor);
        const QStringList sides = paragraphEdges(target);
        bool all = sides.size() == 1;
        for (const QTextBlock &block : blocks) {
            const WordBorder now = Docx::paragraphBorders(block.blockFormat()).value(sides.value(0), WordBorder::none());
            all = all && Docx::parseBordersSpec(Docx::bordersSpec({{QStringLiteral("x"), now}}))
                    == Docx::parseBordersSpec(Docx::bordersSpec({{QStringLiteral("x"), pen}}));
        }
        for (const QTextBlock &block : blocks) {
            QTextBlockFormat format = block.blockFormat();
            WordBorders borders = Docx::paragraphBorders(format);
            if (target == BorderTarget::None) {
                for (WordBorder &b : borders) {
                    b = WordBorder::none();
                }
            }
            for (const QString &side : sides) {
                borders.insert(side, all ? WordBorder::none() : pen);
            }
            Docx::setParagraphBorders(format, borders);
            QTextCursor(block).setBlockFormat(format);
        }
    }
    cursor.endEditBlock();
    m_document->setDirty(true);
}

void BordersController::applyShading(const QColor &color)
{
    if (color.isValid()) {
        setShadingColor(color);
    }
    QTextCursor cursor = m_control->textCursor();
    cursor.beginEditBlock();
    if (QTextTable *table = cursor.currentTable()) {
        Docx::applyCellShading(table, Docx::selectedCells(cursor), color);
    } else {
        for (const QTextBlock &block : selectedBlocks(cursor)) {
            QTextBlockFormat format = block.blockFormat();
            if (color.isValid() && color.alpha() > 0) {
                format.setBackground(QColor(color.rgb()));
            } else {
                format.clearBackground();
            }
            QTextCursor(block).setBlockFormat(format);
        }
    }
    cursor.endEditBlock();
    m_document->setDirty(true);
}

QVariantMap BordersController::dialogInfo() const
{
    const QTextCursor cursor = m_control->textCursor();
    QVariantMap edges;
    QVariantMap out;
    if (const QTextTable *table = cursor.currentTable()) {
        const Docx::CellRange range = Docx::selectedCells(cursor);
        const WordBorders shown = Docx::rangeBorders(table, range);
        for (auto it = shown.cbegin(); it != shown.cend(); ++it) {
            edges.insert(it.key(), edgeToMap(it.value()));
        }
        const QTextTableCellFormat cell = table->cellAt(range.row, range.column).format().toTableCellFormat();
        out.insert(QStringLiteral("context"), QStringLiteral("table"));
        out.insert(QStringLiteral("applyTo"), range.coversTable(table) ? QStringLiteral("table") : QStringLiteral("cell"));
        out.insert(QStringLiteral("hasInsideH"), range.rows > 1);
        out.insert(QStringLiteral("hasInsideV"), range.columns > 1);
        out.insert(QStringLiteral("fill"), cell.hasProperty(QTextFormat::BackgroundBrush) ? cell.background().color().name() : QString());
    } else {
        const QList<QTextBlock> blocks = selectedBlocks(cursor);
        const QTextBlockFormat format = blocks.value(0).blockFormat();
        const WordBorders own = Docx::paragraphBorders(format);
        for (const char *side : {"top", "left", "bottom", "right"}) {
            edges.insert(QString::fromLatin1(side), edgeToMap(own.value(QString::fromLatin1(side), WordBorder::none())));
        }
        edges.insert(QStringLiteral("insideH"), edgeToMap(own.value(QStringLiteral("between"), WordBorder::none())));
        out.insert(QStringLiteral("context"), QStringLiteral("paragraph"));
        out.insert(QStringLiteral("applyTo"), QStringLiteral("paragraph"));
        out.insert(QStringLiteral("hasInsideH"), blocks.size() > 1);
        out.insert(QStringLiteral("hasInsideV"), false);
        out.insert(QStringLiteral("fill"), format.hasProperty(QTextFormat::BackgroundBrush) ? format.background().color().name() : QString());
    }
    out.insert(QStringLiteral("edges"), edges);
    return out;
}

bool BordersController::applyDialog(const QVariantMap &answer)
{
    QTextCursor cursor = m_control->textCursor();
    const QString applyTo = answer.value(QStringLiteral("applyTo")).toString();
    const QVariantMap edges = answer.value(QStringLiteral("edges")).toMap();
    const bool fillChanged = answer.value(QStringLiteral("fillChanged")).toBool();
    const QColor fill(answer.value(QStringLiteral("fill")).toString());
    QTextTable *table = cursor.currentTable();
    if ((applyTo == u"cell" || applyTo == u"table") && table == nullptr) {
        return false;
    }
    cursor.beginEditBlock();
    if (applyTo == u"cell" || applyTo == u"table") {
        const Docx::CellRange range = applyTo == u"table" ? Docx::wholeTable(table) : Docx::selectedCells(cursor);
        WordBorders sides;
        for (auto it = edges.cbegin(); it != edges.cend(); ++it) {
            sides.insert(it.key(), edgeFromMap(it.value().toMap()));
        }
        Docx::setRangeBorders(table, range, sides);
        if (fillChanged) {
            Docx::applyCellShading(table, range, fill);
        }
    } else {
        for (const QTextBlock &block : selectedBlocks(cursor)) {
            QTextBlockFormat format = block.blockFormat();
            WordBorders borders = Docx::paragraphBorders(format);
            for (auto it = edges.cbegin(); it != edges.cend(); ++it) {
                if (it.key() != u"insideV") {
                    borders.insert(paragraphSide(it.key()), edgeFromMap(it.value().toMap()));
                }
            }
            Docx::setParagraphBorders(format, borders);
            if (fillChanged) {
                if (fill.isValid()) {
                    format.setBackground(fill);
                } else {
                    format.clearBackground();
                }
            }
            QTextCursor(block).setBlockFormat(format);
        }
    }
    cursor.endEditBlock();
    m_document->setDirty(true);
    return true;
}

void BordersController::syncFromCursor()
{
    // Inside vertical lines exist between cells only.
    m_edges.at(int(BorderTarget::InsideVertical))->setEnabled(m_control->textCursor().currentTable() != nullptr);
}

} // namespace QindaOffice::Apps::Write
