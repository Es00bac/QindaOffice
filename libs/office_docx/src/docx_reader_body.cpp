// SPDX-License-Identifier: GPL-3.0-or-later
// Paragraphs, runs, fields, hyperlinks, revisions and lists of a .docx body.

#include "docx_format.h"
#include "docx_properties.h"
#include "docx_reader_p.h"
#include "docx_text.h"
#include "ooxml_namespaces.h"
#include "ooxml_units.h"

#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextTable>

namespace QindaOffice::Docx::detail {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);
const QString kM = QString::fromUtf16(Ooxml::Ns::M);
const QString kR = QString::fromUtf16(Ooxml::Ns::R);
const QString kMC = QString::fromUtf16(Ooxml::Ns::MC);

// Field types whose result runs keep the field on save; others (TOC,
// HYPERLINK, form fields) are read as their plain result.
bool keepsField(const QString &type)
{
    static const QStringList kTypes{
        QStringLiteral("PAGE"), QStringLiteral("NUMPAGES"), QStringLiteral("SECTIONPAGES"),
        QStringLiteral("DATE"), QStringLiteral("TIME"), QStringLiteral("CREATEDATE"),
        QStringLiteral("SAVEDATE"), QStringLiteral("PRINTDATE"), QStringLiteral("AUTHOR"),
        QStringLiteral("TITLE"), QStringLiteral("SUBJECT"), QStringLiteral("FILENAME"),
        QStringLiteral("MERGEFIELD"), QStringLiteral("REF"), QStringLiteral("PAGEREF"),
        QStringLiteral("NOTEREF"), QStringLiteral("SEQ"), QStringLiteral("DOCPROPERTY"),
        QStringLiteral("NUMWORDS"), QStringLiteral("NUMCHARS"), QStringLiteral("USERNAME"),
        QStringLiteral("STYLEREF"), QStringLiteral("LASTSAVEDBY"),
    };
    return kTypes.contains(type);
}

QString fieldType(const QString &instruction)
{
    return instruction.trimmed().section(QLatin1Char(' '), 0, 0).toUpper();
}

// HYPERLINK "url" \l "anchor" → url#anchor
QString hyperlinkTarget(const QString &instruction)
{
    QStringList quoted;
    QString current;
    bool inQuote = false;
    bool anchorNext = false;
    QString url;
    QString anchor;
    const QStringList tokens = instruction.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (qsizetype i = 1; i < tokens.size(); ++i) {
        QString token = tokens.at(i);
        if (inQuote) {
            current += QLatin1Char(' ') + token;
            if (token.endsWith(QLatin1Char('"'))) {
                inQuote = false;
                token = current;
            } else {
                continue;
            }
        } else if (token.startsWith(QLatin1Char('"')) && (token.size() == 1 || !token.endsWith(QLatin1Char('"')))) {
            inQuote = true;
            current = token;
            continue;
        }
        if (token == u"\\l") {
            anchorNext = true;
            continue;
        }
        if (token.startsWith(QLatin1Char('\\'))) {
            anchorNext = false;
            ++i; // switch argument (\o "tip", \t "frame")
            continue;
        }
        const QString value = token.remove(QLatin1Char('"'));
        if (anchorNext) {
            anchor = value;
            anchorNext = false;
        } else if (url.isEmpty()) {
            url = value;
        }
    }
    return anchor.isEmpty() ? url : url + QLatin1Char('#') + anchor;
}

// Private-use glyphs Word stores for Symbol / Wingdings characters.
QString symbolText(const XmlElement &sym)
{
    const QString font = wAttr(&sym, u"font").toLower();
    bool ok = false;
    uint code = wAttr(&sym, u"char").toUInt(&ok, 16);
    if (!ok) {
        return {};
    }
    if (code >= 0xF000) {
        code -= 0xF000;
    }
    if (font.contains(u"wingdings")) {
        switch (code) {
        case 0xFC: return QStringLiteral("✓");
        case 0xFB: return QStringLiteral("✗");
        case 0xFE: return QStringLiteral("☒");
        case 0xA8: return QStringLiteral("☐");
        case 0x6F: return QStringLiteral("☐");
        case 0x78: return QStringLiteral("☒");
        case 0x9F: case 0x6C: return QStringLiteral("●");
        case 0xA7: case 0x6E: return QStringLiteral("■");
        case 0xD8: return QStringLiteral("➢");
        case 0xE0: return QStringLiteral("→");
        default: break;
        }
    }
    if (font.contains(u"symbol") && code == 0xB7) {
        return QStringLiteral("•");
    }
    return QString(QChar(char16_t(code)));
}

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
    bool lastWasTable = false;
    for (const XmlElement *e : container.elements()) {
        if (e->ns == kW && e->name == u"p") {
            readParagraph(*e, cursor, state);
            lastWasTable = false;
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
            lastWasTable = true;
        } else if (e->ns == kW && (e->name == u"sdt" || e->name == u"customXml")) {
            const XmlElement *content = e->name == u"sdt" ? e->child(kW, u"sdtContent") : e;
            if (content != nullptr) {
                readBlocks(*content, cursor, state);
            }
        } else if (e->ns == kW && e->name == u"bookmarkStart") {
            const QString name = wAttr(e, u"name");
            if (!name.isEmpty() && name != u"_GoBack") {
                state.pendingAnchors.append(name);
            }
        } else if (e->ns == kW && e->name == u"sectPr") {
            m_sections.append(e);
        } else if (e->ns == kM && e->name == u"oMathPara") {
            // Display math between paragraphs: give it its own paragraph.
            QTextBlockFormat bf;
            bf.setAlignment(Qt::AlignHCenter);
            bf.setProperty(kParagraphSynthetic, false);
            if (state.blockFresh) {
                cursor.setBlockFormat(bf);
            } else {
                cursor.insertBlock(bf);
            }
            state.blockFresh = false;
            InlineState inl;
            readMath(*e, cursor, inl, state);
            lastWasTable = false;
        }
    }
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

void BodyReader::finishParagraph(ContainerState &state, bool keepAfter)
{
    if (!state.hasPending || !state.pending.isValid()) {
        state.hasPending = false;
        return;
    }
    QTextCursor c(state.pending);
    QTextBlockFormat f = state.pending.blockFormat();
    f.setBottomMargin(keepAfter ? state.pendingAfter : 0.0);
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
    qreal gap = (spacing.contextual && sameStyle) ? 0.0 : spacing.before;
    if (state.hasPending) {
        if (!(state.previousContextual && sameStyle)) {
            gap += state.pendingAfter;
        }
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
        // Qt draws the marker left of the first line; Word's hanging indent
        // puts the number there already, so the first line starts at the
        // left indent.
        bf.setTextIndent(0);
    }
    if (state.pageBreakPending) {
        bf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
        bf.setProperty(kPageBreakRun, true);
        state.pageBreakPending = false;
    }
    if (const XmlElement *sectPr = pPr != nullptr ? pPr->child(kW, u"sectPr") : nullptr) {
        m_sections.append(sectPr);
        bf.setProperty(kSectionXml, Ooxml::serializeFragment(*sectPr));
        if (childVal(sectPr, u"type", QStringLiteral("nextPage")) != u"continuous") {
            bf.setPageBreakPolicy(bf.pageBreakPolicy() | QTextFormat::PageBreak_AlwaysAfter);
        }
    }
    const XmlElement *markRPr = pPr != nullptr ? pPr->child(kW, u"rPr") : nullptr;
    QTextCharFormat mark = charFormatFromRPr(mergeProperties(runBase.get(), markRPr).get(), m_env.theme);

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
    state.first = false;
}

void BodyReader::joinList(int numId, int ilvl, const NumberingLevel &level, QTextCursor &cursor)
{
    QString key = m_env.numbering.abstractIdOf(numId);
    if (m_env.numbering.start(numId, ilvl) != level.start || key.isEmpty()) {
        key = QStringLiteral("num:%1").arg(numId); // an override keeps its own count
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
    QVariantMap snapshot{{QStringLiteral("style"), int(lf.style())},
                         {QStringLiteral("prefix"), lf.numberPrefix()},
                         {QStringLiteral("suffix"), lf.numberSuffix()},
                         {QStringLiteral("start"), lf.start()}};
    lf.setProperty(kListSnapshot, snapshot);
    counters.list[ilvl] = cursor.createList(lf);
}

QString BodyReader::currentHref(const InlineState &inl, const ContainerState &state) const
{
    if (!inl.href.isEmpty()) {
        return inl.href;
    }
    for (auto it = state.fields.crbegin(); it != state.fields.crend(); ++it) {
        if (it->inResult && fieldType(it->instruction) == u"HYPERLINK") {
            return hyperlinkTarget(it->instruction);
        }
    }
    return {};
}

QString BodyReader::currentField(const InlineState &inl, const ContainerState &state) const
{
    if (!inl.simpleFields.isEmpty()) {
        const QString instr = inl.simpleFields.last();
        return keepsField(fieldType(instr)) ? instr : QString();
    }
    if (!state.fields.isEmpty() && state.fields.last().inResult) {
        const QString instr = state.fields.last().instruction.trimmed();
        return keepsField(fieldType(instr)) ? instr : QString();
    }
    return {};
}

bool BodyReader::inFieldInstruction(const ContainerState &state) const
{
    for (const ContainerState::Field &f : state.fields) {
        if (!f.inResult) {
            return true;
        }
    }
    return false;
}

void BodyReader::readInline(const XmlElement &parent, QTextCursor &cursor, InlineState &inl, ContainerState &state)
{
    for (const XmlElement *e : parent.elements()) {
        if (e->ns == kM && (e->name == u"oMath" || e->name == u"oMathPara")) {
            readMath(*e, cursor, inl, state);
            continue;
        }
        if (e->ns != kW) {
            continue;
        }
        const QStringView n = e->name;
        if (n == u"r") {
            readRun(*e, cursor, inl, state);
        } else if (n == u"hyperlink") {
            const QString saved = inl.href;
            QString target;
            if (const QString id = e->attribute(kR, u"id"); !id.isEmpty()) {
                if (const auto rel = m_env.package->relationship(m_part, id)) {
                    target = rel->target;
                }
            }
            if (const QString anchor = wAttr(e, u"anchor"); !anchor.isEmpty()) {
                target += QLatin1Char('#') + anchor;
            }
            inl.href = target;
            readInline(*e, cursor, inl, state);
            inl.href = saved;
        } else if (n == u"fldSimple") {
            const QString instr = wAttr(e, u"instr");
            const QString saved = inl.href;
            if (fieldType(instr) == u"HYPERLINK") {
                inl.href = hyperlinkTarget(instr);
            }
            inl.simpleFields.append(instr);
            readInline(*e, cursor, inl, state);
            inl.simpleFields.removeLast();
            inl.href = saved;
        } else if (n == u"ins" || n == u"moveTo") {
            const QString saved = inl.insertAuthor;
            inl.insertAuthor = wAttr(e, u"author", QStringLiteral("Unknown"));
            inl.revisionDate = QDateTime::fromString(wAttr(e, u"date"), Qt::ISODate);
            readInline(*e, cursor, inl, state);
            inl.insertAuthor = saved;
        } else if (n == u"del" || n == u"moveFrom") {
            const QString saved = inl.deleteAuthor;
            inl.deleteAuthor = wAttr(e, u"author", QStringLiteral("Unknown"));
            inl.revisionDate = QDateTime::fromString(wAttr(e, u"date"), Qt::ISODate);
            readInline(*e, cursor, inl, state);
            inl.deleteAuthor = saved;
        } else if (n == u"sdt") {
            if (const XmlElement *content = e->child(kW, u"sdtContent")) {
                readInline(*content, cursor, inl, state);
            }
        } else if (n == u"smartTag" || n == u"customXml" || n == u"dir" || n == u"bdo") {
            readInline(*e, cursor, inl, state);
        } else if (n == u"bookmarkStart") {
            const QString name = wAttr(e, u"name");
            if (!name.isEmpty() && name != u"_GoBack") {
                state.pendingAnchors.append(name);
            }
        }
        // pPr, proofErr, bookmarkEnd, permStart/End, comment ranges: nothing to show.
    }
}

void BodyReader::readFieldChar(const XmlElement &fldChar, ContainerState &state)
{
    const QString type = wAttr(&fldChar, u"fldCharType");
    if (type == u"begin") {
        state.fields.append({QString(), false});
    } else if (type == u"separate") {
        if (!state.fields.isEmpty()) {
            state.fields.last().inResult = true;
        }
    } else if (type == u"end") {
        if (!state.fields.isEmpty()) {
            state.fields.removeLast();
        }
    }
}

void BodyReader::readRun(const XmlElement &r, QTextCursor &cursor, InlineState &inl, ContainerState &state)
{
    const XmlElement *rPr = r.child(kW, u"rPr");
    const QString rStyle = childVal(rPr, u"rStyle");
    XmlElementPtr effective = mergeProperties(inl.paragraphRunProperties.get(),
                                              m_env.styles.characterRunProperties(rStyle).get());
    effective = mergeProperties(effective.get(), rPr);
    QTextCharFormat cf = charFormatFromRPr(effective.get(), m_env.theme);
    if (!rStyle.isEmpty()) {
        cf.setProperty(kRunStyleId, rStyle);
    }
    if (rPr != nullptr) {
        cf.setProperty(kRunXml, Ooxml::serializeFragment(*rPr));
    }
    for (const XmlElement *e : r.elements()) {
        if (e->ns == kMC && e->name == u"AlternateContent") {
            readObject(r, *e, cursor, cf, inl, state);
            continue;
        }
        if (e->ns != kW) {
            continue;
        }
        const QStringView n = e->name;
        if (n == u"fldChar") {
            readFieldChar(*e, state);
        } else if (n == u"instrText" || n == u"delInstrText") {
            if (!state.fields.isEmpty() && !state.fields.last().inResult) {
                state.fields.last().instruction += e->text();
            }
        } else if (inFieldInstruction(state)) {
            continue; // everything between begin and separate is the code
        } else if (n == u"t" || n == u"delText") {
            insertText(cursor, e->text(), cf, inl, state);
        } else if (n == u"tab" || n == u"ptab") {
            insertText(cursor, QStringLiteral("\t"), cf, inl, state);
        } else if (n == u"br") {
            const QString type = wAttr(e, u"type");
            if (type == u"page") {
                pageBreak(cursor, inl, state);
            } else {
                insertText(cursor, QString(QChar::LineSeparator), cf, inl, state);
            }
        } else if (n == u"cr") {
            insertText(cursor, QString(QChar::LineSeparator), cf, inl, state);
        } else if (n == u"noBreakHyphen") {
            insertText(cursor, QString(QChar(0x2011)), cf, inl, state);
        } else if (n == u"softHyphen") {
            insertText(cursor, QString(QChar(0x00AD)), cf, inl, state);
        } else if (n == u"sym") {
            insertText(cursor, symbolText(*e), cf, inl, state);
        } else if (n == u"pgNum") {
            QTextCharFormat field = cf;
            field.setProperty(kFieldInstruction, QStringLiteral("PAGE"));
            insertText(cursor, QStringLiteral("1"), field, inl, state);
        } else if (n == u"footnoteReference") {
            footnoteMarker(cursor, wAttr(e, u"id"), cf, inl, state);
        } else if (n == u"endnoteReference") {
            preservedText(cursor, r, QStringLiteral("†"), cf, inl, state);
            m_env.approximations[QStringLiteral("endnote")] += 1;
        } else if (n == u"commentReference") {
            preservedText(cursor, r, QString(QChar(0x200B)), cf, inl, state);
            m_env.approximations[QStringLiteral("comment")] += 1;
        } else if (n == u"ruby") {
            if (const XmlElement *base = e->child(kW, u"rubyBase")) {
                readInline(*base, cursor, inl, state);
            }
        } else if (n == u"drawing" || n == u"pict" || n == u"object" || n == u"contentPart") {
            readObject(r, *e, cursor, cf, inl, state);
        }
        // footnoteRef/endnoteRef (inside notes), separators,
        // lastRenderedPageBreak, annotationRef: nothing to show.
    }
}

void BodyReader::insertText(QTextCursor &cursor, const QString &text, QTextCharFormat format, InlineState &inl,
                            ContainerState &state)
{
    if (text.isEmpty()) {
        return;
    }
    const QString href = currentHref(inl, state);
    if (!href.isEmpty()) {
        format.setAnchor(true);
        format.setAnchorHref(href);
    }
    if (!state.pendingAnchors.isEmpty()) {
        format.setAnchorNames(state.pendingAnchors);
        state.pendingAnchors.clear();
    }
    const QString field = currentField(inl, state);
    if (!field.isEmpty()) {
        format.setProperty(kFieldInstruction, field);
    }
    // Tracked changes look the way QindaWrite shows its own
    // (write_revisions.cpp): set before the snapshot, so the marks never
    // reach the file as real formatting.
    if (!inl.insertAuthor.isEmpty()) {
        format.setProperty(kRevisionInsertProperty, inl.insertAuthor);
        format.setFontUnderline(true);
        format.setForeground(QColor::fromRgb(kRevisionInsertColor));
    }
    if (!inl.deleteAuthor.isEmpty()) {
        format.setProperty(kRevisionDeleteProperty, inl.deleteAuthor);
        format.setFontStrikeOut(true);
        format.setForeground(QColor::fromRgb(kRevisionDeleteColor));
    }
    if ((!inl.insertAuthor.isEmpty() || !inl.deleteAuthor.isEmpty()) && inl.revisionDate.isValid()) {
        format.setProperty(kRevisionDate, inl.revisionDate);
    }
    if (!format.hasProperty(kRunSnapshot)) {
        format.setProperty(kRunSnapshot, charSnapshot(format));
    }
    cursor.insertText(text, format);
    inl.blockHasContent = true;
}

void BodyReader::insertObject(QTextCursor &cursor, QTextCharFormat format, InlineState &inl, ContainerState &state)
{
    insertText(cursor, QString(QChar::ObjectReplacementCharacter), std::move(format), inl, state);
}

void BodyReader::pageBreak(QTextCursor &cursor, InlineState &inl, ContainerState &state)
{
    if (!inl.blockHasContent) {
        QTextBlockFormat f = cursor.blockFormat();
        f.setPageBreakPolicy(f.pageBreakPolicy() | QTextFormat::PageBreak_AlwaysBefore);
        f.setProperty(kPageBreakRun, true);
        cursor.setBlockFormat(f);
        return;
    }
    // Mid-paragraph: what follows starts a new page, in a block with the
    // same paragraph formatting.
    QTextBlockFormat f = inl.blockFormat;
    f.setTopMargin(0);
    f.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    f.setProperty(kPageBreakRun, true);
    f.clearProperty(kSectionXml);
    f.setProperty(kParagraphSnapshot, blockSnapshot(f));
    cursor.insertBlock(f, inl.markFormat);
    inl.blockHasContent = false;
    Q_UNUSED(state);
}

void BodyReader::footnoteMarker(QTextCursor &cursor, const QString &id, QTextCharFormat format, InlineState &inl,
                                ContainerState &state)
{
    const QString noteId = QStringLiteral("docx-") + id;
    format.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    format.setProperty(kFootnoteIdProperty, noteId);
    WordParts &parts = *m_env.parts;
    const QString text = m_env.footnoteText.value(id);
    parts.footnotes.insert(noteId, text);
    parts.footnoteImportedText.insert(noteId, text);
    insertText(cursor, QString::number(++m_footnoteNumber), format, inl, state);
}

void BodyReader::preservedText(QTextCursor &cursor, const XmlElement &element, const QString &display,
                               QTextCharFormat format, InlineState &inl, ContainerState &state)
{
    format.setProperty(kInlineXml, Ooxml::serializeFragment(element));
    format.setProperty(kInlineSourcePart, m_part);
    format.setProperty(kInlineSerial, m_env.nextIsland++);
    format.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    insertText(cursor, display, format, inl, state);
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
