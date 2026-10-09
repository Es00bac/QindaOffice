// SPDX-License-Identifier: GPL-3.0-or-later
// ListController's dialogs (write_lists.h): Define New Bullet, Define New
// Number Format, Define New Multilevel List, Set Numbering Value and
// Adjust List Indents — each question read from the caret's list level,
// each answer one command through docx_list_edit.h.

#include "write_lists.h"

#include "docx_list_edit.h"
#include "docx_list_labels.h"
#include "docx_lists.h"
#include "ooxml_units.h"
#include "page_text_control.h"
#include "write_document.h"

#include <QTextBlock>
#include <QTextCursor>

namespace QindaOffice::Apps::Write {

namespace Units = Ooxml::Units;

namespace {

// "%3." at level 3 reads "%1." in the Number Format dialog, and back.
QString ownToOne(const QString &text, int level)
{
    QString out = text;
    return out.replace(QStringLiteral("%%1").arg(level + 1), QStringLiteral("%1"));
}

QString oneToOwn(const QString &text, int level)
{
    QString out = text;
    return out.replace(QStringLiteral("%1"), QStringLiteral("%%1").arg(level + 1));
}

// AGENT-GUARD: a level that also shows other levels' numbers ("%1.%2." at
// level 2) stays in Word's own %1 … %9 in the Number Format dialog (its
// "levelText"): mapped to "%1" its own number and level 1's would read
// "%1.%1." and be written back as "%2.%2.".
bool namesOtherLevels(const QString &text, int level)
{
    for (qsizetype i = text.indexOf(QLatin1Char('%')); i >= 0 && i + 1 < text.size();
         i = text.indexOf(QLatin1Char('%'), i + 1)) {
        const int digit = text.at(i + 1).digitValue();
        if (digit >= 1 && digit <= 9 && digit != level + 1) {
            return true;
        }
    }
    return false;
}

QString colorName(const QString &rrggbb)
{
    return rrggbb.isEmpty() ? QString() : QLatin1Char('#') + rrggbb.toLower();
}

// The definition a level spec list describes, over `base`.
QString definitionFrom(const QString &base, const QVariantList &levels)
{
    QString definition = base;
    for (int k = 0; k < 9 && k < levels.size(); ++k) {
        QVariantMap spec = levels.at(k).toMap();
        spec.remove(QStringLiteral("defined"));
        if (!spec.isEmpty()) {
            definition = Docx::withLevel(definition, k, spec);
        }
    }
    return definition;
}

} // namespace

QVariantMap ListController::caretLevel() const
{
    const Docx::WordListItem item = Docx::wordListItem(m_control->textCursor().block());
    if (!item.isValid()) {
        return {};
    }
    QVariantMap spec = Docx::levelSpec(item.definition, item.level);
    spec.insert(QStringLiteral("level"), item.level);
    return spec;
}

QVariantMap ListController::bulletInfo() const
{
    const QVariantMap spec = caretLevel();
    if (spec.value(QStringLiteral("format")).toString() == u"bullet") {
        const QString glyph = spec.value(QStringLiteral("text")).toString();
        const QString font = spec.value(QStringLiteral("font")).toString();
        return {{QStringLiteral("glyph"), glyph}, {QStringLiteral("font"), font},
                {QStringLiteral("shown"), Docx::displayGlyph(glyph, font)},
                {QStringLiteral("color"), colorName(spec.value(QStringLiteral("color")).toString())},
                {QStringLiteral("justification"), spec.value(QStringLiteral("justification"))}};
    }
    return {{QStringLiteral("glyph"), QString(QChar(0xF0B7))}, {QStringLiteral("font"), QStringLiteral("Symbol")},
            {QStringLiteral("shown"), QString(QChar(0x2022))}, {QStringLiteral("color"), QString()},
            {QStringLiteral("justification"), QStringLiteral("left")}};
}

bool ListController::applyDefineBullet(const QVariantMap &answer)
{
    const QString glyph = answer.value(QStringLiteral("glyph")).toString().left(2);
    if (glyph.isEmpty()) {
        return false;
    }
    const QString font = answer.value(QStringLiteral("font")).toString();
    const QString color = answer.value(QStringLiteral("color")).toString().remove(QLatin1Char('#')).toUpper();
    const QString jc = answer.value(QStringLiteral("justification"), QStringLiteral("left")).toString();
    QString definition = Docx::bulletDefinition(glyph, font, color);
    definition = Docx::withLevel(definition, 0, {{QStringLiteral("justification"), jc}});
    return applyDefinition(definition, true,
                           {{QStringLiteral("format"), QStringLiteral("bullet")}, {QStringLiteral("text"), glyph},
                            {QStringLiteral("font"), font}, {QStringLiteral("color"), color},
                            {QStringLiteral("justification"), jc}});
}

QVariantMap ListController::numberFormatInfo() const
{
    const QVariantMap spec = caretLevel();
    if (!spec.isEmpty() && spec.value(QStringLiteral("format")).toString() != u"bullet") {
        const int level = spec.value(QStringLiteral("level")).toInt();
        const QString text = spec.value(QStringLiteral("text")).toString();
        const bool levelText = namesOtherLevels(text, level);
        return {{QStringLiteral("format"), spec.value(QStringLiteral("format"))},
                {QStringLiteral("text"), levelText ? text : ownToOne(text, level)},
                {QStringLiteral("start"), spec.value(QStringLiteral("start"))},
                {QStringLiteral("justification"), spec.value(QStringLiteral("justification"))},
                {QStringLiteral("level"), level},
                {QStringLiteral("levelText"), levelText}};
    }
    return {{QStringLiteral("format"), QStringLiteral("decimal")}, {QStringLiteral("text"), QStringLiteral("%1.")},
            {QStringLiteral("start"), 1}, {QStringLiteral("justification"), QStringLiteral("left")},
            {QStringLiteral("level"), 0}, {QStringLiteral("levelText"), false}};
}

bool ListController::applyNumberFormat(const QVariantMap &answer)
{
    const QString format = answer.value(QStringLiteral("format"), QStringLiteral("decimal")).toString();
    const Docx::WordListItem item = Docx::wordListItem(m_control->textCursor().block());
    const int level = qMax(0, item.level);
    // The answer's text names this level "%1", or — levelText — "%<level>"
    // among the other levels' numbers, as Word stores it.
    const bool levelText = item.isValid() && answer.value(QStringLiteral("levelText")).toBool();
    const QString own = levelText ? QStringLiteral("%%1").arg(level + 1) : QStringLiteral("%1");
    QString text = answer.value(QStringLiteral("text")).toString();
    if (!text.contains(own) && format != u"none") {
        text += own;
    }
    const int start = qMax(0, answer.value(QStringLiteral("start"), 1).toInt());
    const QString jc = answer.value(QStringLiteral("justification"), QStringLiteral("left")).toString();
    const QVariantMap change{{QStringLiteral("format"), format},
                             {QStringLiteral("text"), levelText ? text : oneToOwn(text, level)},
                             {QStringLiteral("start"), start},
                             {QStringLiteral("justification"), jc},
                             {QStringLiteral("font"), QString()}};
    // Paragraphs of other lists in the selection take the caret's list with
    // this level changed when the level shows its parents' numbers.
    return applyDefinition(levelText ? Docx::withLevel(item.definition, level, change)
                                     : Docx::numberingDefinition(format, text, jc, start),
                           false, change);
}

QVariantMap ListController::multilevelInfo() const
{
    const Docx::WordListItem item = Docx::wordListItem(m_control->textCursor().block());
    const QString definition = item.isValid() ? item.definition : Docx::multilevelDefinition(0);
    QVariantList levels;
    for (int k = 0; k < 9; ++k) {
        levels.append(Docx::levelSpec(definition, k));
    }
    return {{QStringLiteral("levels"), levels}, {QStringLiteral("level"), item.isValid() ? item.level : 0},
            {QStringLiteral("inList"), item.isValid()}};
}

bool ListController::applyMultilevelDefinition(const QVariantMap &answer)
{
    const Docx::WordListItem item = Docx::wordListItem(m_control->textCursor().block());
    const QString base = item.isValid() ? item.definition : Docx::multilevelDefinition(0);
    const QString definition = definitionFrom(base, answer.value(QStringLiteral("levels")).toList());
    return applyDefinition(definition, false, {});
}

QVariantMap ListController::numberingValueInfo() const
{
    const QTextBlock block = m_control->textCursor().block();
    const Docx::ListMarker marker = Docx::listMarker(block);
    return {{QStringLiteral("value"), marker.isValid() ? marker.value : 1},
            {QStringLiteral("canContinue"), Docx::continuableNumId(block) != 0},
            {QStringLiteral("inList"), marker.isValid()}};
}

bool ListController::applyNumberingValue(const QVariantMap &answer)
{
    if (answer.value(QStringLiteral("mode")).toString() == u"continue") {
        return continueNumbering();
    }
    const QTextBlock block = m_control->textCursor().block();
    const int value = qMax(0, answer.value(QStringLiteral("value"), 1).toInt());
    return command([&] { return Docx::restartNumbering(block, value) > 0; });
}

QVariantMap ListController::indentsInfo() const
{
    const QVariantMap spec = caretLevel();
    if (spec.isEmpty()) {
        return {};
    }
    const int tab = spec.value(QStringLiteral("tab")).toInt();
    return {{QStringLiteral("numberPosition"), Units::twipsToPx(spec.value(QStringLiteral("numberPosition")).toInt())},
            {QStringLiteral("textIndent"), Units::twipsToPx(spec.value(QStringLiteral("textIndent")).toInt())},
            {QStringLiteral("suffix"), spec.value(QStringLiteral("suffix"))},
            {QStringLiteral("tab"), tab >= 0 ? Units::twipsToPx(tab) : -1.0}};
}

bool ListController::applyIndents(const QVariantMap &answer)
{
    const QTextBlock block = m_control->textCursor().block();
    const Docx::WordListItem item = Docx::wordListItem(block);
    if (!item.isValid()) {
        return false;
    }
    const qreal tab = answer.value(QStringLiteral("tab"), -1).toReal();
    const QVariantMap change{
        {QStringLiteral("numberPosition"), qRound(Units::pxToTwips(answer.value(QStringLiteral("numberPosition")).toReal()))},
        {QStringLiteral("textIndent"), qRound(Units::pxToTwips(answer.value(QStringLiteral("textIndent")).toReal()))},
        {QStringLiteral("suffix"), answer.value(QStringLiteral("suffix"), QStringLiteral("tab"))},
        {QStringLiteral("tab"), tab >= 0 ? qRound(Units::pxToTwips(tab)) : -1}};
    return command([&] {
        return Docx::redefineList(m_control->document(), item.numId, Docx::withLevel(item.definition, item.level, change)) > 0;
    });
}

QString ListController::previewLabel(const QVariantList &levels, int level, int value) const
{
    const QString definition = definitionFrom(Docx::multilevelDefinition(0), levels);
    const int at = qBound(0, level, 8);
    std::array<int, 9> counters{};
    counters.fill(1);
    counters[size_t(at)] = qMax(0, value);
    const auto parsed = Docx::listDefinition(definition);
    const QString label = Docx::levelLabel(*parsed, at, counters);
    if (parsed->levels[size_t(at)].isBullet()) {
        return Docx::displayGlyph(label, levels.value(at).toMap().value(QStringLiteral("font")).toString());
    }
    return label;
}

QVariantList ListController::numberFormats() const
{
    static const char *const kFormats[][2] = {
        {"decimal", QT_TR_NOOP("1, 2, 3, …")},           {"upperRoman", QT_TR_NOOP("I, II, III, …")},
        {"lowerRoman", QT_TR_NOOP("i, ii, iii, …")},      {"upperLetter", QT_TR_NOOP("A, B, C, …")},
        {"lowerLetter", QT_TR_NOOP("a, b, c, …")},        {"ordinal", QT_TR_NOOP("1st, 2nd, 3rd, …")},
        {"cardinalText", QT_TR_NOOP("One, Two, Three, …")}, {"ordinalText", QT_TR_NOOP("First, Second, Third, …")},
        {"decimalZero", QT_TR_NOOP("01, 02, 03, …")},     {"bullet", QT_TR_NOOP("Bullet")},
        {"none", QT_TR_NOOP("(none)")}};
    QVariantList out;
    for (const auto &f : kFormats) {
        out.append(QVariantMap{{QStringLiteral("format"), QString::fromLatin1(f[0])}, {QStringLiteral("label"), tr(f[1])}});
    }
    return out;
}

} // namespace QindaOffice::Apps::Write
