// SPDX-License-Identifier: GPL-3.0-or-later
// Word's list commands on a QTextDocument (docx_list_edit.h).

#include "docx_list_edit.h"

#include "docx_paragraph_format.h"
#include "docx_properties.h"
#include "ooxml_units.h"

#include <QHash>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>

#include <functional>

namespace QindaOffice::Docx {

namespace Units = Ooxml::Units;

namespace {

const QList<int> kNoStarts(9, -1);

QTextDocument *documentOf(const QTextBlock &block)
{
    return const_cast<QTextDocument *>(block.document());
}

// A Word list paragraph's own look: the level's indents, Word's marker
// instead of Qt's (docx_list_labels.h).
void setItemIndents(const QTextBlock &block, const NumberingLevel &level)
{
    QTextBlockFormat f = block.blockFormat();
    f.setLeftMargin(Units::twipsToPx(levelLeft(level)));
    f.setTextIndent(0);
    f.setProperty(kListHanging, Units::twipsToPx(levelHanging(level)));
    f.setProperty(QTextFormat::ListStyle, int(QTextListFormat::ListStyleUndefined));
    if (f.hasProperty(kTabStops)) {
        setTabStops(f, tabStops(f)); // Word's stops stay put; Qt measures from the indent
    }
    QTextCursor(block).setBlockFormat(f);
}

// The QTextLists of instance `numId` by level.
QHash<int, QTextList *> listsOf(const QTextDocument &document, int numId)
{
    QHash<int, QTextList *> out;
    for (QTextBlock b = document.begin(); b.isValid(); b = b.next()) {
        if (QTextList *list = b.textList(); list != nullptr && isWordList(list->format())
            && list->format().intProperty(kListNumId) == numId) {
            const int level = list->format().intProperty(kListLevel);
            if (!out.contains(level)) {
                out.insert(level, list);
            }
        }
    }
    return out;
}

// Puts `block` into `lists`' list for `format`'s level, making that list
// when the instance has none at the level yet.
void join(const QTextBlock &block, QHash<int, QTextList *> &lists, const QTextListFormat &format)
{
    const int level = format.intProperty(kListLevel);
    const auto same = [&format](const QTextListFormat &other) {
        for (const int key : {kListNumId, kListLevel, kListDefinition, kListStarts, kListAbstractKey}) {
            if (other.property(key) != format.property(key)) {
                return false;
            }
        }
        return true;
    };
    if (QTextList *list = lists.value(level); list != nullptr && same(list->format())) {
        list->add(block);
        return;
    }
    QTextCursor cursor(block);
    lists.insert(level, cursor.createList(format));
}

int maxKey(const QTextDocument &document, const std::function<int(const QTextFormat &)> &key)
{
    int out = 0;
    for (const QTextFormat &f : document.allFormats()) {
        if (f.isListFormat()) {
            out = qMax(out, key(f));
        }
    }
    return out;
}

} // namespace

WordListItem wordListItem(const QTextBlock &block)
{
    const QTextList *list = block.isValid() ? block.textList() : nullptr;
    if (list == nullptr || !isWordList(list->format())) {
        return {};
    }
    const QTextListFormat lf = list->format();
    WordListItem item;
    item.numId = lf.intProperty(kListNumId);
    item.level = qBound(0, lf.intProperty(kListLevel), 8);
    item.definition = lf.stringProperty(kListDefinition);
    item.abstractKey = lf.stringProperty(kListAbstractKey);
    item.starts = listStarts(lf);
    return item;
}

int nextNumId(const QTextDocument &document)
{
    return maxKey(document, [](const QTextFormat &f) { return f.intProperty(kListNumId); }) + 1;
}

QString newAbstractKey(const QTextDocument &document)
{
    const int last = maxKey(document, [](const QTextFormat &f) {
        const QString key = f.stringProperty(kListAbstractKey);
        return key.startsWith(u"new:") ? key.mid(4).toInt() : 0;
    });
    return QStringLiteral("new:%1").arg(last + 1);
}

int makeList(const QList<QTextBlock> &blocks, const QString &definition, int level, bool keepLevels)
{
    if (blocks.isEmpty() || blocks.first().document() == nullptr) {
        return 0;
    }
    const QTextDocument &document = *blocks.first().document();
    const int numId = nextNumId(document);
    const QString key = newAbstractKey(document);
    const std::shared_ptr<const ListDefinition> parsed = listDefinition(definition);
    QHash<int, QTextList *> lists;
    for (const QTextBlock &block : blocks) {
        if (!block.isValid() || block.blockFormat().boolProperty(kParagraphSynthetic)) {
            continue;
        }
        int at = qBound(0, level, 8);
        if (keepLevels) {
            if (const WordListItem item = wordListItem(block); item.isValid()) {
                at = item.level;
            } else if (const QTextList *list = block.textList()) {
                at = qBound(0, list->format().indent() - 1, 8);
            }
        }
        join(block, lists, wordListFormat(definition, key, numId, at, kNoStarts));
        setItemIndents(block, parsed->levels[size_t(at)]);
    }
    return numId;
}

int redefineList(QTextDocument *document, int numId, const QString &definition)
{
    QList<QTextList *> lists;
    QList<QTextBlock> items;
    for (QTextBlock b = document->begin(); b.isValid(); b = b.next()) {
        QTextList *list = b.textList();
        if (list != nullptr && isWordList(list->format()) && list->format().intProperty(kListNumId) == numId) {
            items.append(b);
            if (!lists.contains(list)) {
                lists.append(list);
            }
        }
    }
    if (items.isEmpty()) {
        return 0;
    }
    const int fresh = nextNumId(*document);
    const QString key = newAbstractKey(*document);
    for (QTextList *list : std::as_const(lists)) {
        const QTextListFormat old = list->format();
        list->setFormat(wordListFormat(definition, key, fresh, old.intProperty(kListLevel), listStarts(old)));
    }
    const std::shared_ptr<const ListDefinition> parsed = listDefinition(definition);
    for (const QTextBlock &block : std::as_const(items)) {
        setItemIndents(block, parsed->levels[size_t(qBound(0, wordListItem(block).level, 8))]);
    }
    return fresh;
}

void setListLevel(const QTextBlock &block, int level)
{
    const WordListItem item = wordListItem(block);
    level = qBound(0, level, 8);
    if (!item.isValid() || item.level == level) {
        return;
    }
    QHash<int, QTextList *> lists = listsOf(*block.document(), item.numId);
    join(block, lists, wordListFormat(item.definition, item.abstractKey, item.numId, level, item.starts));
    setItemIndents(block, listDefinition(item.definition)->levels[size_t(level)]);
}

void removeFromList(const QTextBlock &block, bool keepIndent)
{
    if (!block.isValid() || block.textList() == nullptr) {
        return;
    }
    QTextBlockFormat f = block.blockFormat();
    f.setObjectIndex(-1);
    f.clearProperty(kListHanging);
    f.clearProperty(QTextFormat::ListStyle);
    f.setTextIndent(0);
    if (!keepIndent) {
        f.setLeftMargin(0);
    }
    if (f.hasProperty(kTabStops)) {
        setTabStops(f, tabStops(f));
    }
    QTextCursor(block).setBlockFormat(f);
}

int restartNumbering(const QTextBlock &block, int value)
{
    const WordListItem item = wordListItem(block);
    if (!item.isValid() || block.document() == nullptr) {
        return 0;
    }
    const int fresh = nextNumId(*block.document());
    QList<int> starts = kNoStarts;
    starts[item.level] = qMax(0, value);
    QHash<int, QTextList *> lists;
    for (QTextBlock b = block; b.isValid(); b = b.next()) {
        const WordListItem other = wordListItem(b);
        if (other.isValid() && other.numId == item.numId) {
            join(b, lists, wordListFormat(item.definition, item.abstractKey, fresh, other.level, starts));
        }
    }
    return fresh;
}

int continuableNumId(const QTextBlock &block)
{
    const WordListItem item = wordListItem(block);
    if (!item.isValid()) {
        return 0;
    }
    for (QTextBlock b = block.previous(); b.isValid(); b = b.previous()) {
        const WordListItem other = wordListItem(b);
        if (other.isValid() && other.numId != item.numId
            && (other.definition == item.definition || other.abstractKey == item.abstractKey)) {
            return other.numId;
        }
    }
    return 0;
}

bool continueNumbering(const QTextBlock &block)
{
    const WordListItem item = wordListItem(block);
    const int target = continuableNumId(block);
    if (target == 0 || block.document() == nullptr) {
        return false;
    }
    const QList<QTextBlock> targets = listItems(*block.document(), target);
    const WordListItem into = wordListItem(targets.value(0));
    QHash<int, QTextList *> lists = listsOf(*block.document(), target);
    const std::shared_ptr<const ListDefinition> parsed = listDefinition(into.definition);
    for (const QTextBlock &b : listItems(*block.document(), item.numId)) {
        const int level = wordListItem(b).level;
        join(b, lists, wordListFormat(into.definition, into.abstractKey, target, level, into.starts));
        setItemIndents(b, parsed->levels[size_t(level)]);
    }
    return true;
}

QList<QTextBlock> listItems(const QTextDocument &document, int numId)
{
    QList<QTextBlock> out;
    for (QTextBlock b = document.begin(); b.isValid(); b = b.next()) {
        if (const WordListItem item = wordListItem(b); item.isValid() && item.numId == numId) {
            out.append(b);
        }
    }
    return out;
}

} // namespace QindaOffice::Docx
