// SPDX-License-Identifier: GPL-3.0-or-later
// StylesController (write_styles.h): the commands, the pane's rows and the
// gallery's slots, applying a style, Clear Formatting and Enter's next
// style. New / Modify / Update / Delete Style: write_styles_edit.cpp.

#include "write_styles.h"

#include "docx_borders.h"
#include "docx_format.h"
#include "docx_properties.h"
#include "docx_restyle.h"
#include "docx_style_edit.h"
#include "docx_table_format.h"
#include "ooxml_namespaces.h"
#include "menu_tree.h"
#include "page_text_control.h"
#include "style_catalog.h"
#include "write_document.h"
#include "write_selection.h"
#include "write_style_draft.h"

#include <QKeyEvent>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextList>
#include <QTextTable>

namespace QindaOffice::Apps::Write {

using Quick::ActionEntry;
using Quick::MenuNode;

namespace {

constexpr int kQuickSlots = 16;

MenuNode a(const ActionEntry *entry)
{
    return MenuNode::action(entry->id());
}

} // namespace

StylesController::StylesController(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model,
                                   QObject *parent)
    : QObject(parent)
    , m_control(control)
    , m_document(document)
    , m_draft(new StyleDraft(document, this))
{
    createActions(model);
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(250);
    connect(&m_refresh, &QTimer::timeout, this, &StylesController::refresh);
    connect(m_document, &WriteDocument::stylesChanged, this, [this] {
        m_cache.reset();
        refresh();
    });
    connect(m_document->textDocument(), &QTextDocument::contentsChanged, &m_refresh, qOverload<>(&QTimer::start));
    connect(m_control, &PageTextControl::cursorPositionChanged, this, &StylesController::syncCurrent);
    connect(m_control, &PageTextControl::documentChanged, this, &StylesController::syncCurrent);
    refresh();
}

QObject *StylesController::draftObject() const
{
    return m_draft;
}

const Docx::DocumentStyles &StylesController::documentStyles() const
{
    if (!m_cache) {
        m_cache = std::make_unique<Docx::DocumentStyles>(m_document->parts());
    }
    return *m_cache;
}

void StylesController::createActions(Quick::ActionModel *model)
{
    const auto make = [model](const char *id, const QString &text, const char *icon, const QKeySequence &keys = {}) {
        return model->add(QString::fromLatin1(id), text, keys, QString::fromLatin1(icon));
    };
    m_pane = make("writeStylesPaneAction", tr("&Styles Pane"), "palette", QKeySequence(QStringLiteral("Ctrl+Alt+Shift+S")));
    m_pane->setCheckable(true);
    m_new = make("writeNewStyleAction", tr("&New Style…"), "plus");
    m_modify = make("writeModifyStyleAction", tr("&Modify Style…"), "square-pen");
    m_update = make("writeUpdateStyleAction", tr("&Update Style to Match Selection"), "refresh-cw");
    m_delete = make("writeDeleteStyleAction", tr("&Delete Style"), "trash-2");
    connect(m_pane, &ActionEntry::toggled, this, &StylesController::paneToggleRequested);
    connect(m_new, &ActionEntry::triggered, this, &StylesController::newStyle);
    connect(m_modify, &ActionEntry::triggered, this, [this] { modify(m_current); });
    connect(m_update, &ActionEntry::triggered, this, [this] { updateToMatch(m_current); });
    connect(m_delete, &ActionEntry::triggered, this, [this] { remove(m_current); });
    for (int i = 0; i < kQuickSlots; ++i) {
        ActionEntry *slot = model->add(QStringLiteral("writeQuickStyle%1").arg(i), tr("Quick Style %1").arg(i + 1), {},
                                       QStringLiteral("pilcrow"));
        connect(slot, &ActionEntry::triggered, this, [this, i] {
            if (i < m_quick.size()) {
                apply(m_quick.at(i));
            }
        });
        m_slots.append(slot);
    }
}

QList<MenuNode> StylesController::menuItems() const
{
    QList<MenuNode> gallery;
    for (const ActionEntry *slot : m_slots) {
        gallery.append(a(slot));
    }
    return {a(m_pane), a(m_new), a(m_modify), a(m_update), a(m_delete),
            MenuNode::submenu(QStringLiteral("writeQuickStylesMenu"), tr("&Quick Styles"), gallery)};
}

// ---- the pane, the gallery and the caret's style ------------------------------------

void StylesController::refresh()
{
    const Ooxml::XmlElementPtr root = m_document->stylesRoot();
    const QTextDocument &body = *m_document->textDocument();
    const QList<Docx::StyleEntry> entries = Docx::styleEntries(*root, body);
    // Every built-in the document lacks in one sheet, for their previews.
    const Ooxml::XmlElementPtr all = root->clone();
    for (const Docx::StyleEntry &entry : entries) {
        if (!entry.inDocument) {
            all->appendElement(Docx::builtInStyleFor(*root, entry.id));
        }
    }
    const Docx::StyleSheet sheet = Docx::StyleSheet::parse(all.get());
    const Ooxml::Theme theme = documentStyles().theme();
    m_rows.clear();
    for (const Docx::StyleEntry &entry : entries) {
        const Ooxml::XmlElementPtr runs = entry.type == u"character"
            ? Docx::mergeProperties(sheet.paragraphRunProperties(QString()).get(), sheet.characterRunProperties(entry.id).get())
            : sheet.paragraphRunProperties(entry.id);
        const QTextCharFormat look = Docx::charFormatFromRPr(runs.get(), theme);
        const QStringList families = look.fontFamilies().toStringList();
        m_rows.append(QVariantMap{
            {QStringLiteral("id"), entry.id},
            {QStringLiteral("name"), entry.name},
            {QStringLiteral("type"), entry.type},
            {QStringLiteral("inDocument"), entry.inDocument},
            {QStringLiteral("inUse"), entry.inUse},
            {QStringLiteral("quick"), entry.quick},
            {QStringLiteral("family"), families.isEmpty() ? body.defaultFont().family() : families.first()},
            {QStringLiteral("size"), qBound(8.0, look.fontPointSize(), 16.0)},
            {QStringLiteral("bold"), look.fontWeight() >= QFont::DemiBold},
            {QStringLiteral("italic"), look.fontItalic()},
            {QStringLiteral("color"), look.hasProperty(QTextFormat::ForegroundBrush) ? look.foreground().color().name() : QString()}});
    }
    m_quick.clear();
    for (const Docx::StyleEntry &entry : Docx::quickStyles(*root, body)) {
        if (m_quick.size() < kQuickSlots) {
            m_quick.append(entry.id);
            m_slots.at(m_quick.size() - 1)->setText(entry.name);
        }
    }
    for (int i = 0; i < kQuickSlots; ++i) {
        m_slots.at(i)->setVisible(i < m_quick.size());
    }
    emit stylesChanged();
    syncCurrent();
}

void StylesController::syncCurrent()
{
    const QTextCursor cursor = m_control->textCursor();
    const Docx::DocumentStyles &styles = documentStyles();
    QString current = cursor.charFormat().stringProperty(Docx::kRunStyleId);
    if (current.isEmpty()) {
        current = styles.isEmpty() ? Docx::paragraphStyleIdOf(cursor.block().blockFormat()) : styles.paragraphStyleOf(cursor.block());
    }
    const bool body = onBody();
    m_new->setEnabled(body);
    m_modify->setEnabled(body && styles.sheet().style(current) != nullptr);
    m_update->setEnabled(body && styles.sheet().style(current) != nullptr);
    m_delete->setEnabled(body && canRemove(current));
    if (current != m_current) {
        m_current = current;
        emit currentChanged();
    }
}

bool StylesController::onBody() const
{
    return m_control->document() == m_document->textDocument();
}

QString StylesController::typeOf(const QString &id) const
{
    for (const QVariant &row : m_rows) {
        if (row.toMap().value(QStringLiteral("id")).toString() == id) {
            return row.toMap().value(QStringLiteral("type")).toString();
        }
    }
    return {};
}

// ---- commands ------------------------------------------------------------------------

Ooxml::XmlElementPtr StylesController::withEnsured(const Ooxml::XmlElementPtr &root, const QString &id) const
{
    if (Docx::styleElement(*root, id) != nullptr) {
        return root;
    }
    const Ooxml::XmlElementPtr builtIn = Docx::builtInStyleFor(*root, id);
    return builtIn ? Docx::withStyle(*root, builtIn) : root;
}

void StylesController::commitRoot(const Ooxml::XmlElementPtr &root, const Ooxml::XmlElementPtr &original)
{
    // A new document's styles become the document's own the first time a
    // command needs them (Docx::DocumentStyles reads WordParts::styles).
    const bool stored = m_document->parts().styles || !Docx::DocumentStyles(m_document->parts()).isEmpty();
    if (!stored || root != original) {
        m_document->setStylesRoot(root, m_control->document());
    }
}

bool StylesController::command(const std::function<bool(QTextCursor &)> &edit)
{
    QTextCursor cursor = m_control->textCursor();
    cursor.beginEditBlock();
    const bool changed = edit(cursor);
    cursor.endEditBlock();
    if (changed) {
        m_document->setDirty(true);
        m_control->setTextCursor(m_control->textCursor()); // re-announce the caret's format
    }
    return changed;
}

bool StylesController::apply(const QString &id)
{
    const QString type = typeOf(id);
    if (id.isEmpty() || type.isEmpty()) {
        return false;
    }
    if (type == u"table") {
        return applyTableStyle(id);
    }
    return command([&](QTextCursor &cursor) {
        const Ooxml::XmlElementPtr original = m_document->stylesRoot();
        commitRoot(withEnsured(original, id), original);
        const Docx::DocumentStyles styles(m_document->parts());
        const QList<QTextBlock> blocks = selectedBlocks(cursor);
        const bool partial = cursor.hasSelection() && blocks.size() == 1
            && (cursor.selectionStart() > blocks.first().position()
                || cursor.selectionEnd() < blocks.first().position() + blocks.first().length() - 1);
        if (type == u"character" || (type == u"linked" && partial)) {
            QTextCursor runs = cursor;
            if (!runs.hasSelection()) {
                runs.select(QTextCursor::WordUnderCursor); // Word: the word at the caret
            }
            const QString characterId =
                type == u"character" ? id : Docx::childVal(Docx::styleElement(*m_document->stylesRoot(), id).get(), u"link");
            styles.applyCharacter(runs, characterId);
            return true;
        }
        for (const QTextBlock &block : blocks) {
            styles.apply(block, id);
        }
        return true;
    });
}

bool StylesController::applyTableStyle(const QString &id)
{
    if (m_control->textCursor().currentTable() == nullptr) {
        return false;
    }
    return command([&](QTextCursor &cursor) {
        QTextTable *table = cursor.currentTable();
        const Ooxml::XmlElementPtr original = m_document->stylesRoot();
        commitRoot(withEnsured(original, id), original);
        Docx::ensureWordTable(table);
        // The table names its style (w:tblStyle, written as Word writes it)…
        QTextTableFormat format = table->format();
        Ooxml::XmlElementPtr tblPr = Ooxml::parseFragment(format.stringProperty(Docx::kTableXml));
        if (!tblPr) {
            tblPr = std::make_shared<Ooxml::XmlElement>(QString::fromUtf16(Ooxml::Ns::W), QStringLiteral("tblPr"));
        }
        tblPr->removeChildren(QString::fromUtf16(Ooxml::Ns::W), u"tblStyle");
        auto style = std::make_shared<Ooxml::XmlElement>(QString::fromUtf16(Ooxml::Ns::W), QStringLiteral("tblStyle"));
        style->setAttribute(QString::fromUtf16(Ooxml::Ns::W), QStringLiteral("val"), id);
        tblPr->children.prepend({style, QString()});
        format.setProperty(Docx::kTableXml, Ooxml::serializeFragment(*tblPr));
        table->setFormat(format);
        // …and shows its whole-table borders and shading; the style's
        // conditional rows and columns show when the document is next opened.
        const Docx::DocumentStyles styles(m_document->parts());
        const Docx::TableStyleLayer layer = styles.sheet().tableLayer(id);
        const Docx::WordBorders borders = Docx::readBorders(
            layer.tblPr ? layer.tblPr->child(QString::fromUtf16(Ooxml::Ns::W), u"tblBorders") : nullptr, styles.theme());
        if (!borders.isEmpty()) {
            Docx::setRangeBorders(table, Docx::wholeTable(table), borders);
        }
        const QColor fill = Docx::shadingColor(
            layer.tcPr ? layer.tcPr->child(QString::fromUtf16(Ooxml::Ns::W), u"shd") : nullptr, styles.theme());
        if (fill.isValid()) {
            Docx::applyCellShading(table, Docx::wholeTable(table), fill);
        }
        return true;
    });
}

bool StylesController::clearFormatting()
{
    return command([&](QTextCursor &cursor) {
        const Ooxml::XmlElementPtr original = m_document->stylesRoot();
        Ooxml::XmlElementPtr root = original;
        const Docx::DocumentStyles before(m_document->parts());
        for (const QTextBlock &block : selectedBlocks(cursor)) {
            root = withEnsured(root, before.isEmpty() ? Docx::paragraphStyleIdOf(block.blockFormat()) : before.paragraphStyleOf(block));
        }
        commitRoot(root, original);
        Docx::DocumentStyles(m_document->parts()).clearFormatting(cursor);
        return true;
    });
}

bool StylesController::handleKey(QKeyEvent *event)
{
    const bool enter = (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && (event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier;
    QTextCursor cursor = m_control->textCursor();
    const QTextBlock block = cursor.block();
    if (!enter || cursor.hasSelection() || !cursor.atBlockEnd() || block.textList() != nullptr) {
        return false;
    }
    const Docx::DocumentStyles &styles = documentStyles();
    if (styles.isEmpty()) {
        // Write's own styles (StyleCatalog): after a title or heading, body text.
        const int style = classifyBlockStyle(block);
        if (style < StyleCatalog::Title || style > StyleCatalog::Heading6) {
            return false;
        }
        cursor.beginEditBlock();
        cursor.insertBlock();
        StyleCatalog().apply(cursor, StyleCatalog::Normal);
        cursor.endEditBlock();
        m_control->setTextCursor(cursor);
        return true;
    }
    const QString id = styles.paragraphStyleOf(block);
    const Docx::Style *style = styles.sheet().style(id);
    const QString next = style != nullptr ? style->next : QString();
    if (next.isEmpty() || next == id || styles.sheet().style(next) == nullptr) {
        return false;
    }
    cursor.beginEditBlock();
    cursor.insertBlock();
    styles.apply(cursor.block(), next);
    cursor.endEditBlock();
    m_control->setTextCursor(cursor);
    return true;
}

} // namespace QindaOffice::Apps::Write
