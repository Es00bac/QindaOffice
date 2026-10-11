// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_section_commands.h"

#include "docx_properties.h"
#include "menu_tree.h"
#include "page_story_editor.h"
#include "page_text_control.h"
#include "page_text_item.h"
#include "write_document.h"
#include "write_sections.h"

#include <QKeyEvent>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace QindaOffice::Apps::Write {

using Quick::ActionEntry;
using Quick::MenuNode;

namespace {

struct BreakKind {
    const char *id;
    const char *text;
    const char *icon;
    Docx::SectionStart start;
};

const BreakKind kBreaks[] = {
    {"writeSectionBreakNextPageAction", QT_TRANSLATE_NOOP("SectionsController", "Section Break (&Next Page)"),
     "file-plus-2", Docx::SectionStart::NextPage},
    {"writeSectionBreakContinuousAction", QT_TRANSLATE_NOOP("SectionsController", "Section Break (&Continuous)"),
     "between-vertical-start", Docx::SectionStart::Continuous},
    {"writeSectionBreakEvenPageAction", QT_TRANSLATE_NOOP("SectionsController", "Section Break (&Even Page)"),
     "copy-plus", Docx::SectionStart::EvenPage},
    {"writeSectionBreakOddPageAction", QT_TRANSLATE_NOOP("SectionsController", "Section Break (&Odd Page)"),
     "file-plus", Docx::SectionStart::OddPage},
};

// Word's Page Number Format list.
const std::pair<const char *, const char *> kFormats[] = {
    {"", "1, 2, 3, …"},           {"numberInDash", "- 1 -, - 2 -, - 3 -, …"},
    {"lowerLetter", "a, b, c, …"}, {"upperLetter", "A, B, C, …"},
    {"lowerRoman", "i, ii, iii, …"}, {"upperRoman", "I, II, III, …"},
};

const int kSectionKeys[] = {Docx::kSectionXml, Docx::kSectionSettings, Docx::kSectionSnapshot, Docx::kSectionId};

} // namespace

SectionsController::SectionsController(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model,
                                       QObject *parent)
    : QObject(parent)
    , m_control(control)
    , m_document(document)
{
    for (const BreakKind &kind : kBreaks) {
        ActionEntry *entry = model->add(QString::fromLatin1(kind.id), tr(kind.text), {}, QString::fromLatin1(kind.icon));
        connect(entry, &ActionEntry::triggered, this, [this, start = kind.start] { insertSectionBreak(start); });
        m_breaks.append(entry);
    }
    m_portrait = model->add(QStringLiteral("writeOrientationPortraitAction"), tr("&Portrait"), {},
                            QStringLiteral("file"));
    m_landscape = model->add(QStringLiteral("writeOrientationLandscapeAction"), tr("&Landscape"), {},
                             QStringLiteral("rotate-cw"));
    for (ActionEntry *entry : {m_portrait, m_landscape}) {
        entry->setCheckable(true);
        entry->setRadioGroup(QStringLiteral("writeOrientation"));
    }
    connect(m_portrait, &ActionEntry::triggered, this, [this] { setOrientation(false); });
    connect(m_landscape, &ActionEntry::triggered, this, [this] { setOrientation(true); });
    m_numbers = model->add(QStringLiteral("writePageNumberFormatAction"), tr("&Format Page Numbers…"), {},
                           QStringLiteral("list-ordered"));
    connect(m_numbers, &ActionEntry::triggered, this, &SectionsController::pageNumbersRequested);
    m_firstPage = model->add(QStringLiteral("writeDifferentFirstPageAction"), tr("Different &First Page"), {},
                             QStringLiteral("file-text"));
    m_oddEven = model->add(QStringLiteral("writeDifferentOddEvenAction"), tr("Different &Odd && Even Pages"), {},
                           QStringLiteral("book-open"));
    m_link = model->add(QStringLiteral("writeLinkToPreviousAction"), tr("&Link to Previous"), {},
                        QStringLiteral("link"));
    for (ActionEntry *entry : {m_firstPage, m_oddEven, m_link}) {
        entry->setCheckable(true);
    }
    connect(m_firstPage, &ActionEntry::triggered, this, [this] { toggleStoryOption("first"); });
    connect(m_oddEven, &ActionEntry::triggered, this, [this] { toggleStoryOption("oddEven"); });
    connect(m_link, &ActionEntry::triggered, this, [this] { toggleStoryOption("link"); });
    connect(m_control, &PageTextControl::cursorPositionChanged, this, &SectionsController::sync);
    connect(m_document, &WriteDocument::pageSetupChanged, this, &SectionsController::sync);
    sync();
}

void SectionsController::setPageItem(PageTextItem *item)
{
    m_page = item;
    if (item != nullptr) {
        connect(item, &PageTextItem::storyChanged, this, &SectionsController::sync);
    }
    sync();
}

QList<MenuNode> SectionsController::breakItems() const
{
    QList<MenuNode> out;
    for (const ActionEntry *entry : m_breaks) {
        out.append(MenuNode::action(entry->id()));
    }
    return out;
}

QList<MenuNode> SectionsController::orientationItems() const
{
    return {MenuNode::action(m_portrait->id()), MenuNode::action(m_landscape->id())};
}

QList<MenuNode> SectionsController::headerFooterItems() const
{
    return {MenuNode::action(m_numbers->id()), MenuNode::action(m_firstPage->id()),
            MenuNode::action(m_oddEven->id()), MenuNode::action(m_link->id())};
}

int SectionsController::currentSection() const
{
    if (m_page != nullptr && m_page->isEditingStory()) {
        return m_page->storyEditor()->section();
    }
    return m_document->sectionAt(m_control->textCursor().position());
}

bool SectionsController::insertSectionBreak(Docx::SectionStart start)
{
    if (m_page != nullptr && m_page->isEditingStory()) {
        return false;
    }
    QTextCursor cursor = m_control->textCursor();
    if (cursor.currentTable() != nullptr) {
        emit message(tr("A section break cannot go inside a table"));
        return false;
    }
    cursor.setPosition(cursor.selectionStart());
    const SectionLayout &layout = m_document->sections();
    const int index = layout.sectionAt(cursor.position());
    const Docx::SectionRef ref = layout.refs().at(index);
    // The part before the caret becomes a section of its own, starting from
    // the section's markup as it would be written now (D-068: what Write
    // does not model goes with it) and keeping its headers; the part after
    // starts at the break, its numbers continuing (Word copies the restart).
    const QString markup = ref.closing() ? Docx::sectionPropertiesXml(m_document->parts())
                                         : Docx::currentSectionXml(ref.end.blockFormat());
    cursor.beginEditBlock();
    cursor.insertBlock();
    const QTextBlock before = cursor.block().previous();
    QTextBlockFormat format = before.blockFormat();
    const QString key = Docx::newSectionKey();
    Docx::markSectionEnd(format, key, markup);
    format.setProperty(Docx::kSectionSettings, ref.settings.toMap());
    QTextCursor(before).setBlockFormat(format);
    m_document->handOverStories(ref.key, key, false);
    Docx::SectionSettings later = ref.settings;
    later.flow.start = start;
    later.flow.numbering.start = -1;
    m_document->setSectionSettings(index + 1, later);
    cursor.endEditBlock();
    m_control->setTextCursor(cursor);
    sync();
    return true;
}

void SectionsController::applySettings(const QString &scope,
                                       const std::function<void(Docx::SectionSettings &)> &change)
{
    QTextCursor block(m_document->textDocument());
    block.beginEditBlock();
    int from = currentSection();
    int to = from;
    if (scope == u"document") {
        from = 0;
        to = m_document->sections().count() - 1;
    } else if (scope == u"forward") {
        // Word: a next-page break at the caret, unless it starts the section.
        const QTextCursor caret = m_control->textCursor();
        const SectionLayout &layout = m_document->sections();
        const int section = layout.sectionAt(caret.selectionStart());
        const int sectionStart = section == 0 ? 0
                                              : layout.refs().at(section - 1).end.position()
                + layout.refs().at(section - 1).end.length();
        if (caret.selectionStart() > sectionStart) {
            insertSectionBreak(Docx::SectionStart::NextPage);
        }
        from = currentSection();
        to = m_document->sections().count() - 1;
    }
    for (int i = from; i <= to; ++i) {
        Docx::SectionSettings settings = m_document->sections().sections().at(i).ref.settings;
        change(settings);
        m_document->setSectionSettings(i, settings);
    }
    block.endEditBlock();
    sync();
}

void SectionsController::setOrientation(bool landscape)
{
    applySettings(QStringLiteral("section"), [landscape](Docx::SectionSettings &s) {
        if (s.layout.landscape() != landscape) {
            // Word turns the paper and its margins with it.
            s.layout.pageSize.transpose();
            const QMarginsF m = s.layout.margins;
            s.layout.margins = landscape ? QMarginsF(m.top(), m.right(), m.bottom(), m.left())
                                         : QMarginsF(m.bottom(), m.left(), m.top(), m.right());
        }
    });
}

QVariantMap SectionsController::pageNumberInfo() const
{
    const SectionLayout &layout = m_document->sections();
    const int index = qBound(0, currentSection(), layout.count() - 1);
    const Docx::PageNumbering &numbering = layout.sections().at(index).ref.settings.flow.numbering;
    QVariantList formats;
    for (const auto &[format, label] : kFormats) {
        formats.append(QVariantMap{{QStringLiteral("format"), QString::fromLatin1(format)},
                                   {QStringLiteral("label"), QString::fromUtf8(label)}});
    }
    return {{QStringLiteral("format"), numbering.format},
            {QStringLiteral("start"), numbering.start},
            {QStringLiteral("section"), index + 1},
            {QStringLiteral("sections"), layout.count()},
            {QStringLiteral("formats"), formats}};
}

bool SectionsController::applyPageNumbers(const QVariantMap &answer)
{
    const QString format = answer.value(QStringLiteral("format")).toString();
    const int start = answer.value(QStringLiteral("start"), -1).toInt();
    applySettings(QStringLiteral("section"), [&](Docx::SectionSettings &s) {
        s.flow.numbering.format = format == u"decimal" ? QString() : format;
        s.flow.numbering.start = start < 0 ? -1 : start;
    });
    return true;
}

void SectionsController::toggleStoryOption(const char *which)
{
    if (m_syncing) {
        return;
    }
    const QByteArray option(which);
    if (option == "first") {
        const bool on = m_firstPage->isChecked();
        applySettings(QStringLiteral("section"), [on](Docx::SectionSettings &s) { s.titlePage = on; });
    } else if (option == "oddEven") {
        QTextCursor block(m_document->textDocument());
        block.beginEditBlock();
        m_document->setDifferentOddEven(m_oddEven->isChecked());
        block.endEditBlock();
    } else if (option == "link" && m_page != nullptr && m_page->isEditingStory()) {
        StoryEditor *story = m_page->storyEditor();
        const StoryEditor::Kind kind = story->kind();
        const int page = story->page();
        QTextCursor block(m_document->textDocument());
        block.beginEditBlock();
        m_document->setLinkedToPrevious(story->section(), kind == StoryEditor::Kind::Footer, story->slot(),
                                        m_link->isChecked());
        block.endEditBlock();
        // The section shows another story now: edit that one, on the same page.
        story->open(kind, page);
    }
    sync();
}

bool SectionsController::handleKey(QKeyEvent *event)
{
    if ((m_page != nullptr && m_page->isEditingStory())
        || (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        return false;
    }
    const bool back = event->key() == Qt::Key_Backspace;
    if (!back && event->key() != Qt::Key_Delete) {
        return false;
    }
    const QTextCursor cursor = m_control->textCursor();
    if (cursor.hasSelection() || cursor.currentTable() != nullptr) {
        return false;
    }
    QTextBlock first;
    if (back && cursor.positionInBlock() == 0) {
        first = cursor.block().previous();
    } else if (!back && cursor.atBlockEnd()) {
        first = cursor.block();
    }
    if (!first.isValid() || !first.next().isValid() || QTextCursor(first).currentTable() != nullptr
        || QTextCursor(first.next()).currentTable() != nullptr) {
        return false;
    }
    return joinParagraphs(first);
}

bool SectionsController::joinParagraphs(const QTextBlock &first)
{
    const QTextBlock second = first.next();
    const bool firstEnds = Docx::sectionEnds(*m_document->textDocument()).contains(first);
    const bool secondCarries = Docx::carriesSectionBreak(second.blockFormat());
    if (!firstEnds && !secondCarries) {
        return false; // an ordinary join
    }
    const QTextBlockFormat kept = second.blockFormat();
    QTextCursor cursor(first);
    cursor.beginEditBlock();
    if (firstEnds) {
        // Word deletes the section break: the section joins the next one and
        // takes its settings; the headers the next one showed through its
        // links go to it.
        const SectionLayout &layout = m_document->sections();
        const int index = layout.sectionAt(first.position());
        m_document->handOverStories(layout.refs().at(index).key, layout.refs().at(index + 1).key, true);
        QTextBlockFormat format = first.blockFormat();
        Docx::clearSectionEnd(format);
        QTextCursor(first).setBlockFormat(format);
    }
    cursor.movePosition(QTextCursor::EndOfBlock);
    const int at = cursor.position();
    cursor.deleteChar(); // the paragraph mark between them
    if (secondCarries) {
        // The second paragraph's mark survives, as in Word: its break with it.
        QTextBlockFormat merged = cursor.block().blockFormat();
        for (const int key : kSectionKeys) {
            if (kept.hasProperty(key)) {
                merged.setProperty(key, kept.property(key));
            }
        }
        cursor.setBlockFormat(merged);
    }
    m_document->applySections();
    cursor.endEditBlock();
    QTextCursor caret(m_document->textDocument());
    caret.setPosition(at);
    m_control->setTextCursor(caret);
    sync();
    return true;
}

void SectionsController::sync()
{
    if (m_syncing) {
        return;
    }
    m_syncing = true;
    const SectionLayout &layout = m_document->sections();
    const int index = qBound(0, currentSection(), layout.count() - 1);
    const Docx::SectionSettings &settings = layout.sections().at(index).ref.settings;
    const bool story = m_page != nullptr && m_page->isEditingStory();
    const bool inTable = m_control->document() == m_document->textDocument()
        && m_control->textCursor().currentTable() != nullptr;
    for (ActionEntry *entry : std::as_const(m_breaks)) {
        entry->setEnabled(!story && !inTable);
    }
    m_landscape->setChecked(settings.layout.landscape());
    m_portrait->setChecked(!settings.layout.landscape());
    m_firstPage->setChecked(settings.titlePage);
    m_oddEven->setChecked(m_document->differentOddEven());
    bool linked = false;
    if (story) {
        const StoryEditor *editor = m_page->storyEditor();
        linked = m_document->linkedToPrevious(editor->section(), editor->kind() == StoryEditor::Kind::Footer,
                                              editor->slot());
    }
    m_link->setEnabled(story && m_page->storyEditor()->section() > 0);
    m_link->setChecked(linked);
    m_syncing = false;
}

} // namespace QindaOffice::Apps::Write
