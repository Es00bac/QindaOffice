// SPDX-License-Identifier: GPL-3.0-or-later
// Paragraphs and lists of a .docx body (runs: docx_reader_runs.cpp).

#include "docx_controls.h"
#include "docx_format.h"
#include "docx_properties.h"
#include "docx_reader_p.h"
#include "docx_sections.h"
#include "docx_text.h"
#include "ooxml_namespaces.h"
#include "ooxml_units.h"

#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextTable>

#include <algorithm>

namespace QindaOffice::Docx::detail {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);
const QString kM = QString::fromUtf16(Ooxml::Ns::M);
const QString kR = QString::fromUtf16(Ooxml::Ns::R);
const QString kMC = QString::fromUtf16(Ooxml::Ns::MC);

QTextListFormat::Style bulletStyle(const QString &text)
{
    if (text == u"o" || text == u"◦" || text == u"○" || text == QString(QChar(0xF06F))) {
        return QTextListFormat::ListCircle;
    }
    if (text == u"▪" || text == u"■" || text == QString(QChar(0xF0A7)) || text == QString(QChar(0xF06E))
        || text == QString(QChar(0xF071))) {
        return QTextListFormat::ListSquare;
    }
    return QTextListFormat::ListDisc;
}

QTextListFormat::Style numberStyle(const QString &numFmt)
{
    if (numFmt == u"upperRoman") {
        return QTextListFormat::ListUpperRoman;
    }
    if (numFmt == u"lowerRoman") {
        return QTextListFormat::ListLowerRoman;
    }
    if (numFmt == u"upperLetter") {
        return QTextListFormat::ListUpperAlpha;
    }
    if (numFmt == u"lowerLetter") {
        return QTextListFormat::ListLowerAlpha;
    }
    return QTextListFormat::ListDecimal;
}

} // namespace

BodyReader::BodyReader(ReadEnv &env, QTextDocument *document, QString part)
    : m_env(env)
    , m_document(document)
    , m_part(std::move(part))
{
}

void BodyReader::readDocument(const XmlElement &container)
{
    QTextCursor cursor(m_document);
    ContainerState state;
    readBlocks(container, cursor, state);
}

void BodyReader::readBlocks(const XmlElement &container, QTextCursor &cursor, ContainerState &state)
{
    const bool lastWasTable = readBlockItems(container, cursor, state) == 1;
    if (state.hasPending) {
        finishParagraph(state, true);
    }
    if (lastWasTable && state.blockFresh) {
        // The block Qt keeps after a closing table has no w:p behind it.
        QTextBlockFormat synthetic = cursor.blockFormat();
        synthetic.setProperty(kParagraphSynthetic, true);
        cursor.setBlockFormat(synthetic);
    }
}

QString BodyReader::openControl(const XmlElement &sdt, bool block)
{
    const QString key = QString::number(m_env.nextControl++);
    if (m_env.parts != nullptr) {
        m_env.parts->contentControls.insert(key, parseControl(sdt, block));
    }
    return key;
}

int BodyReader::readBlockItems(const XmlElement &container, QTextCursor &cursor, ContainerState &state)
{
    int last = -1;
    for (const XmlElement *e : container.elements()) {
        if (e->ns == kW && e->name == u"p") {
            readParagraph(*e, cursor, state);
            last = 0;
        } else if (e->ns == kW && e->name == u"tbl") {
            state.tableTopMargin = 0;
            if (state.collapsedExtra > 0) {
                state.tableTopMargin = state.collapsedExtra + (state.hasPending ? state.pendingAfter : 0);
                state.collapsedExtra = 0;
            }
            if (state.hasPending) {
                finishParagraph(state, true);
            }
            readTable(*e, cursor, state);
            last = 1;
        } else if (e->ns == kW && e->name == u"sdt") {
            // A block-level control: its paragraphs and tables carry its key
            // and flow on as the container's own (spacing, page breaks).
            if (const XmlElement *content = e->child(kW, u"sdtContent")) {
                m_blockControls.append(openControl(*e, true));
                if (const int inner = readBlockItems(*content, cursor, state); inner >= 0) {
                    last = inner;
                }
                m_blockControls.removeLast();
            }
        } else if (e->ns == kW && e->name == u"customXml") {
            if (const int inner = readBlockItems(*e, cursor, state); inner >= 0) {
                last = inner;
            }
        } else if (e->ns == kW && e->name == u"bookmarkStart") {
            const QString name = wAttr(e, u"name");
            if (!name.isEmpty() && name != u"_GoBack") {
                state.pendingAnchors.append(name);
            }
        } else if (e->ns == kW && (e->name == u"commentRangeStart" || e->name == u"commentRangeEnd")) {
            commentRange(*e); // a comment can span paragraphs
        } else if (e->ns == kW && e->name == u"sectPr") {
            m_sections.append(e);
        } else if (e->ns == kM && e->name == u"oMathPara") {
            // Display math between paragraphs: give it its own paragraph.
            QTextBlockFormat bf;
            bf.setAlignment(Qt::AlignHCenter);
            if (!m_blockControls.isEmpty()) {
                bf.setProperty(kBlockControls, m_blockControls);
            }
            bf.setProperty(kParagraphSynthetic, false);
            if (state.blockFresh) {
                cursor.setBlockFormat(bf);
            } else {
                cursor.insertBlock(bf);
            }
            state.blockFresh = false;
            InlineState inl;
            readMath(*e, cursor, inl, state);
            last = 0;
        }
    }
    return last;
}

void BodyReader::finishParagraph(ContainerState &state, bool keepAfter)
{
    if (!state.hasPending || !state.pending.isValid()) {
        state.hasPending = false;
        return;
    }
    QTextCursor c(state.pending);
    QTextBlockFormat f = state.pending.blockFormat();
    // Auto spacing ends at a table cell's edge (HTML: a cell's last block).
    const bool cellEdge = state.cell != nullptr && state.previousAutoAfter;
    f.setBottomMargin(keepAfter && !cellEdge ? state.pendingAfter : 0.0);
    f.setProperty(kParagraphSnapshot, blockSnapshot(f));
    c.setBlockFormat(f);
    state.hasPending = false;
}

void BodyReader::readParagraph(const XmlElement &p, QTextCursor &cursor, ContainerState &state)
{
    const XmlElement *pPr = p.child(kW, u"pPr");
    const StyleSheet &styles = m_env.styles;
    QString styleId = childVal(pPr, u"pStyle");
    if (styles.style(styleId) == nullptr) {
        styleId = styles.defaultParagraphStyle();
    }
    // The style stack (docx_styles.h), with the cell's table-style layer.
    XmlElementPtr effective = styles.paragraphProperties(styleId);
    XmlElementPtr runBase = styles.paragraphRunProperties(styleId);
    if (state.cell != nullptr) {
        const bool tableWins = styles.tableStyleBeatsParagraphStyle(childVal(pPr, u"pStyle"));
        if (tableWins) {
            effective = mergeProperties(effective.get(), state.cell->pPr.get());
            runBase = mergeProperties(runBase.get(), state.cell->rPr.get());
        } else {
            effective = mergeProperties(state.cell->pPr.get(), effective.get());
            runBase = mergeProperties(state.cell->rPr.get(), runBase.get());
        }
    }
    // Numbering: direct w:numPr, or the style's.
    const XmlElement *numPr = pPr != nullptr ? pPr->child(kW, u"numPr") : nullptr;
    const XmlElement *styleNumPr = styles.styleNumbering(styleId);
    const int numId = childVal(numPr, u"numId", childVal(styleNumPr, u"numId", QStringLiteral("0"))).toInt();
    const int ilvl = qBound(0, childVal(numPr, u"ilvl", childVal(styleNumPr, u"ilvl", QStringLiteral("0"))).toInt(), 8);
    const NumberingLevel *level = m_env.numbering.level(numId, ilvl);
    if (level != nullptr && level->pPr) {
        if (numPr == nullptr && effective->child(kW, u"ind") != nullptr) {
            // Numbering from the style: the style's own indent wins.
            effective = mergeProperties(level->pPr.get(), effective.get());
        } else {
            effective = mergeProperties(effective.get(), level->pPr.get());
        }
    }
    effective = mergeProperties(effective.get(), pPr);

    QTextBlockFormat bf = blockFormatFromPPr(effective.get(), m_env.theme);
    if (const XmlElement *pBdr = effective->child(kW, u"pBdr")) {
        const QString borders = paragraphBordersSpec(*pBdr, m_env.theme);
        if (!borders.isEmpty()) {
            bf.setProperty(kParagraphBorders, borders);
        }
    }
    const ParagraphSpacing spacing = spacingOf(effective.get());
    const bool sameStyle = !state.first && state.previousStyle == styleId;
    // AGENT-NOTE: Word's HTML auto spacing (14 pt, D-068): no space between
    // the items of one list, none before a table cell's first paragraph, and
    // two adjacent auto spaces collapse to the larger as in HTML (Word's
    // default; LibreOffice lays it out the same). Lists of auto-spaced items
    // had grown documents by half.
    const int listId = level != nullptr ? numId : 0;
    const bool sameList = listId > 0 && state.previousNumId == listId;
    qreal before = (spacing.contextual && sameStyle) ? 0.0 : spacing.before;
    if (spacing.autoBefore && (sameList || (state.first && state.cell != nullptr))) {
        before = 0;
    }
    qreal gap = before;
    if (state.hasPending) {
        qreal after = (state.previousContextual && sameStyle) ? 0.0 : state.pendingAfter;
        if (state.previousAutoAfter && sameList) {
            after = 0;
        }
        gap = spacing.autoBefore && state.previousAutoAfter ? qMax(before, after) : before + after;
        finishParagraph(state, false);
    }
    gap += state.collapsedExtra; // an empty paragraph Qt hid beside a table
    state.collapsedExtra = 0;
    const bool directlyAfterTable = state.afterTable;
    state.afterTable = false;
    bf.setTopMargin(gap);
    bf.setBottomMargin(0);
    bf.setProperty(kParagraphStyleId, styleId);
    if (pPr != nullptr) {
        bf.setProperty(kParagraphXml, Ooxml::serializeFragment(*pPr));
    }
    const int heading = styles.headingLevel(styleId);
    if (heading >= 1 && heading <= 6) {
        bf.setHeadingLevel(heading);
        bf.setProperty(kWriteStyleIdProperty, 1 + heading); // StyleCatalog: Title 1, Heading1 2 … Heading6 7
    } else if (const Style *style = styles.style(styleId); style != nullptr) {
        const QString name = style->name.toLower();
        if (name == u"title") {
            bf.setProperty(kWriteStyleIdProperty, 1);
        } else if (name == u"quote" || name == u"intense quote") {
            bf.setProperty(kWriteStyleIdProperty, 8); // StyleCatalog::Quote
        }
    }
    if (level != nullptr) {
        // Word puts the number at left − hanging and the text at the left
        // indent: the hanging indent is where the marker goes (drawn by
        // docx_list_labels.h; Qt's own marker is off), not the first line's.
        bf.setProperty(kListHanging, -bf.textIndent());
        bf.setProperty(QTextFormat::ListStyle, int(QTextListFormat::ListStyleUndefined));
        bf.setTextIndent(0);
    }
    if (state.pageBreakPending) {
        bf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
        bf.setProperty(kPageBreakRun, true);
        state.pageBreakPending = false;
    }
    if (const XmlElement *sectPr = pPr != nullptr ? pPr->child(kW, u"sectPr") : nullptr) {
        // M88 (D-096): the paragraph ends a section (docx_sections.h). The
        // page the next section starts on is the layout's to give
        // (QindaWrite's write_sections.h, from the NEXT section's w:type),
        // never a page-break flag of this paragraph's.
        m_sections.append(sectPr);
        markSectionEnd(bf, QStringLiteral("r%1").arg(m_sections.size()), Ooxml::serializeFragment(*sectPr));
    }
    const XmlElement *markRPr = pPr != nullptr ? pPr->child(kW, u"rPr") : nullptr;
    QTextCharFormat mark = charFormatFromRPr(mergeProperties(runBase.get(), markRPr).get(), m_env.theme);

    if (!m_blockControls.isEmpty()) {
        bf.setProperty(kBlockControls, m_blockControls);
    }
    if (state.blockFresh) {
        cursor.setBlockFormat(bf);
        setBlockCharFormatKeepingCell(cursor, mark);
    } else {
        cursor.insertBlock(bf, mark);
    }
    state.blockFresh = false;
    if (level != nullptr) {
        joinList(numId, ilvl, *level, cursor);
    }
    InlineState inl;
    inl.blockFormat = bf;
    inl.markFormat = mark;
    inl.paragraphRunProperties = runBase;
    readInline(p, cursor, inl, state);

    if (directlyAfterTable && cursor.block().text().isEmpty()) {
        QFont font = mark.font();
        const qreal natural = QFontMetricsF(font).height();
        const int type = bf.lineHeightType();
        const qreal line = type == QTextBlockFormat::FixedHeight ? bf.lineHeight()
            : type == QTextBlockFormat::MinimumHeight            ? qMax(natural, bf.lineHeight())
            : type == QTextBlockFormat::ProportionalHeight       ? natural * bf.lineHeight() / 100.0
                                                                 : natural;
        state.collapsedExtra = gap + line;
    }
    state.pending = cursor.block();
    state.pendingAfter = spacing.after;
    state.hasPending = true;
    state.previousStyle = styleId;
    state.previousContextual = spacing.contextual;
    state.previousAutoAfter = spacing.autoAfter;
    state.previousNumId = listId;
    state.first = false;
}

void BodyReader::joinList(int numId, int ilvl, const NumberingLevel &level, QTextCursor &cursor)
{
    QString key = m_env.numbering.abstractIdOf(numId);
    const QList<int> overrides = m_env.numbering.startOverrides(numId);
    if (std::any_of(overrides.cbegin(), overrides.cend(), [](int v) { return v >= 0; }) || key.isEmpty()) {
        key = QStringLiteral("num:%1").arg(numId); // an override keeps its own count (Word's restart)
    }
    ListCounters &counters = m_counters[key];
    QTextList *list = counters.list[ilvl].data();
    if (list == nullptr) {
        counters.value[ilvl] = m_env.numbering.start(numId, ilvl) - 1;
    }
    counters.value[ilvl] += 1;
    // A higher level restarts every deeper one (w:lvlRestart refines that).
    for (int deeper = ilvl + 1; deeper < 9; ++deeper) {
        const NumberingLevel *d = m_env.numbering.level(numId, deeper);
        const int restartAfter = d != nullptr ? d->restartAfterLevel : -2;
        if (restartAfter == -1) {
            continue; // lvlRestart 0: never restarts
        }
        if (restartAfter == -2 || restartAfter >= ilvl) {
            counters.list[deeper] = nullptr;
        }
    }
    if (list != nullptr) {
        list->add(cursor.block());
        return;
    }
    QTextListFormat lf;
    lf.setIndent(0);
    if (level.isBullet()) {
        lf.setStyle(bulletStyle(level.text));
    } else if (level.format == u"none") {
        lf.setStyle(QTextListFormat::ListStyleUndefined);
    } else {
        lf.setStyle(numberStyle(level.format));
        // "%1.%2." at level 1: Qt draws this level's own number; the
        // ancestors' numbers are fixed for this list instance, so they
        // become its prefix ("3." + n + ".").
        QString text = level.text;
        const QString own = QStringLiteral("%%1").arg(ilvl + 1);
        const qsizetype at = text.indexOf(own);
        QString prefix = at >= 0 ? text.left(at) : QString();
        const QString suffix = at >= 0 ? text.mid(at + own.size()) : QString();
        for (int up = 0; up < ilvl; ++up) {
            const NumberingLevel *ancestor = m_env.numbering.level(numId, up);
            const QString fmt = (level.legal || ancestor == nullptr) ? QStringLiteral("decimal") : ancestor->format;
            prefix.replace(QStringLiteral("%%1").arg(up + 1), formatListNumber(counters.value[up], fmt));
        }
        lf.setNumberPrefix(prefix);
        lf.setNumberSuffix(suffix);
        lf.setStart(counters.value[ilvl]);
    }
    lf.setProperty(kListNumId, numId);
    lf.setProperty(kListLevel, ilvl);
    // M73 (D-082): Word's own description of the list (docx_lists.h); the
    // writer keeps this w:num and its w:abstractNum while they are as read.
    const QString definition = m_env.numbering.definitionXml(numId);
    QVariantList starts;
    for (const int v : overrides) {
        starts.append(v);
    }
    if (!definition.isEmpty()) {
        lf.setProperty(kListDefinition, definition);
        lf.setProperty(kListAbstractKey, QStringLiteral("src:") + m_env.numbering.abstractIdOf(numId));
        lf.setProperty(kListStarts, starts);
    }
    QVariantMap snapshot{{QStringLiteral("style"), int(lf.style())},
                         {QStringLiteral("prefix"), lf.numberPrefix()},
                         {QStringLiteral("suffix"), lf.numberSuffix()},
                         {QStringLiteral("start"), lf.start()},
                         {QStringLiteral("num"), numId},
                         {QStringLiteral("def"), definition},
                         {QStringLiteral("starts"), starts}};
    lf.setProperty(kListSnapshot, snapshot);
    counters.list[ilvl] = cursor.createList(lf);
}

QString formatListNumber(int value, const QString &numFmt)
{
    if (numFmt == u"upperRoman" || numFmt == u"lowerRoman") {
        static constexpr std::pair<int, const char *> kRoman[] = {
            {1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"}, {90, "XC"},
            {50, "L"}, {40, "XL"}, {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}};
        QString out;
        int n = qBound(1, value, 3999);
        for (const auto &[v, s] : kRoman) {
            while (n >= v) {
                out += QLatin1String(s);
                n -= v;
            }
        }
        return numFmt == u"lowerRoman" ? out.toLower() : out;
    }
    if (numFmt == u"upperLetter" || numFmt == u"lowerLetter") {
        const int n = qMax(1, value);
        const QChar letter(char16_t(u'A' + (n - 1) % 26));
        const QString out = QString(1 + (n - 1) / 26, letter);
        return numFmt == u"lowerLetter" ? out.toLower() : out;
    }
    if (numFmt == u"decimalZero" && value < 10) {
        return QStringLiteral("0") + QString::number(value);
    }
    return QString::number(value);
}

} // namespace QindaOffice::Docx::detail
