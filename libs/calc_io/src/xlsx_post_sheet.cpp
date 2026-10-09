// SPDX-License-Identifier: GPL-3.0-or-later
// Post-pass work for a sheet's M66 features (xlsx_post_sheet.h, D-075).
//
// AGENT-NOTE: like the rest of the post-pass this edits QXlsx's own output
// as text: QXlsx writes unprefixed elements, escapes all character data
// and writes "<c r=\"", "<row " and "<xf " only as start tags, so the
// searches below can only ever find the elements they are after.

#include "xlsx_post_sheet.h"

#include "xlsx_xml.h"

namespace QindaOffice::Calc::IO {

namespace {

bool fail(QString *error, const QString &message)
{
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

QString relsPathFor(const QString &part)
{
    const int slash = int(part.lastIndexOf(QLatin1Char('/')));
    return part.left(slash) + QStringLiteral("/_rels") + part.mid(slash) + QStringLiteral(".rels");
}

// Drops one attribute (` name="…"`) from a start tag.
void stripAttribute(QByteArray *tag, const QByteArray &name)
{
    const QByteArray probe = ' ' + name + QByteArrayLiteral("=\"");
    const qsizetype at = tag->indexOf(probe);
    if (at < 0) {
        return;
    }
    const qsizetype end = tag->indexOf('"', at + probe.size());
    tag->remove(at, end + 1 - at);
}

// Adds attributes just before a start tag's ">" or "/>".
void appendToTag(QByteArray *tag, const QByteArray &attributes)
{
    tag->insert(tag->size() - (tag->endsWith("/>") ? 2 : 1), attributes);
}

// cm="1" on each dynamic-array anchor: the XLDAPR block of xl/metadata.xml.
bool addCellMetadata(QByteArray *xml, const QHash<QString, QString> &anchors)
{
    qsizetype done = 0;
    for (qsizetype pos = xml->indexOf("<c r=\""); pos >= 0 && !anchors.isEmpty(); pos = xml->indexOf("<c r=\"", pos + 1)) {
        const qsizetype refStart = pos + 6;
        const qsizetype refEnd = xml->indexOf('"', refStart);
        if (refEnd < 0) {
            return false;
        }
        if (anchors.contains(QString::fromLatin1(xml->mid(refStart, refEnd - refStart)))) {
            xml->insert(refEnd + 1, QByteArrayLiteral(" cm=\"1\""));
            ++done;
        }
    }
    return done == anchors.size();
}

QByteArray outlineAttributes(const LineOutline &line)
{
    QByteArray out;
    if (line.hidden) {
        out += QByteArrayLiteral(" hidden=\"1\"");
    }
    if (line.level > 0) {
        out += QByteArrayLiteral(" outlineLevel=\"") + QByteArray::number(line.level) + '"';
    }
    if (line.collapsed) {
        out += QByteArrayLiteral(" collapsed=\"1\"");
    }
    return out;
}

// Outline state onto QXlsx's <row> start tags, in row order; a row QXlsx
// did not write (an empty grouped row) is added as <row r=… …/>.
bool patchRows(QByteArray *xml, const QMap<qint32, LineOutline> &rows)
{
    if (rows.isEmpty()) {
        return true;
    }
    const qsizetype open = xml->indexOf("<sheetData>");
    const qsizetype close = xml->indexOf("</sheetData>");
    if (open < 0 || close < 0) {
        return false;
    }
    const qsizetype bodyStart = open + 11;
    const QByteArray body = xml->mid(bodyStart, close - bodyStart);
    QByteArray out;
    out.reserve(body.size() + rows.size() * 48);
    auto pending = rows.constBegin();
    const auto emitBefore = [&](qint64 rowNumber) {
        for (; pending != rows.constEnd() && pending.key() + 1 < rowNumber; ++pending) {
            out += QByteArrayLiteral("<row r=\"") + QByteArray::number(pending.key() + 1) + '"'
                + outlineAttributes(pending.value()) + QByteArrayLiteral("/>");
        }
    };
    qsizetype copied = 0;
    for (qsizetype pos = body.indexOf("<row "); pos >= 0; pos = body.indexOf("<row ", pos + 1)) {
        const qsizetype tagEnd = body.indexOf('>', pos);
        const qsizetype r = body.indexOf("r=\"", pos);
        if (tagEnd < 0 || r < 0 || r > tagEnd) {
            return false;
        }
        const qint32 rowNumber = body.mid(r + 3, body.indexOf('"', r + 3) - r - 3).toInt();
        out += body.mid(copied, pos - copied);
        emitBefore(rowNumber);
        QByteArray tag = body.mid(pos, tagEnd + 1 - pos);
        if (pending != rows.constEnd() && pending.key() + 1 == rowNumber) {
            for (const char *name : {"hidden", "outlineLevel", "collapsed"}) {
                stripAttribute(&tag, name);
            }
            appendToTag(&tag, outlineAttributes(pending.value()));
            ++pending;
        }
        out += tag;
        copied = tagEnd + 1;
    }
    out += body.mid(copied);
    emitBefore(qint64(Sheet::kMaxRows) + 2);
    xml->replace(bodyStart, close - bodyStart, out);
    return true;
}

// The sheet's outline depth on <sheetFormatPr> (Excel sizes its gutter by it).
void setFormatLevel(QByteArray *xml, const QByteArray &attribute, quint8 level)
{
    const qsizetype at = xml->indexOf("<sheetFormatPr");
    if (level == 0 || at < 0) {
        return;
    }
    const qsizetype end = xml->indexOf('>', at);
    QByteArray tag = xml->mid(at, end + 1 - at);
    const qsizetype length = tag.size();
    stripAttribute(&tag, attribute);
    appendToTag(&tag, ' ' + attribute + QByteArrayLiteral("=\"") + QByteArray::number(level) + '"');
    xml->replace(at, length, tag);
}

bool replaceCols(QByteArray *xml, const QByteArray &cols)
{
    if (cols.isEmpty()) {
        return true;
    }
    const qsizetype open = xml->indexOf("<cols>");
    if (open >= 0) {
        const qsizetype close = xml->indexOf("</cols>", open);
        if (close < 0) {
            return false;
        }
        xml->replace(open, close + 7 - open, cols);
        return true;
    }
    return insertBeforeAny(xml, {QStringLiteral("sheetData")}, cols);
}

QByteArray sheetPrXml(const PatchManifest::SheetPatch &patch)
{
    const bool fitToPage = patch.fitToPage; // M78: Page Setup's "Fit to"
    if (patch.source.codeName.isEmpty() && patch.summaryBelow && patch.summaryRight && !fitToPage) {
        return {};
    }
    QByteArray xml = QByteArrayLiteral("<sheetPr");
    if (!patch.source.codeName.isEmpty()) {
        xml += QByteArrayLiteral(" codeName=\"") + xmlAttribute(patch.source.codeName) + '"';
    }
    xml += '>';
    if (!patch.summaryBelow || !patch.summaryRight) {
        xml += QByteArrayLiteral("<outlinePr") + (patch.summaryBelow ? QByteArray() : QByteArrayLiteral(" summaryBelow=\"0\""))
            + (patch.summaryRight ? QByteArray() : QByteArrayLiteral(" summaryRight=\"0\"")) + QByteArrayLiteral("/>");
    }
    if (fitToPage) {
        xml += QByteArrayLiteral("<pageSetUpPr fitToPage=\"1\"/>");
    }
    return xml + QByteArrayLiteral("</sheetPr>");
}

bool writeHyperlinks(QHash<QString, QByteArray> *parts, const PatchManifest::SheetPatch &patch, QString *error)
{
    if (patch.hyperlinks.isEmpty()) {
        return true;
    }
    const QString relsPath = relsPathFor(patch.sheetPart);
    QByteArray rels = parts->value(relsPath);
    QByteArray xml = QByteArrayLiteral("<hyperlinks>");
    for (const PatchManifest::SheetPatch::Link &link : patch.hyperlinks) {
        xml += QByteArrayLiteral("<hyperlink ref=\"") + link.ref.toLatin1() + '"';
        if (!link.target.isEmpty()) {
            const QString rid = appendExternalRelationship(
                &rels, QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink"),
                link.target);
            if (rid.isEmpty()) {
                return fail(error, QStringLiteral("post-pass: broken rels part %1").arg(relsPath));
            }
            xml += QByteArrayLiteral(" r:id=\"") + rid.toLatin1() + '"';
        }
        for (const auto &[name, value] : {std::pair{"location", link.location}, std::pair{"display", link.display},
                                          std::pair{"tooltip", link.tooltip}}) {
            if (!value.isEmpty()) {
                xml += ' ' + QByteArray(name) + QByteArrayLiteral("=\"") + xmlAttribute(value) + '"';
            }
        }
        xml += QByteArrayLiteral("/>");
    }
    xml += QByteArrayLiteral("</hyperlinks>");
    parts->insert(relsPath, rels);
    // Schema order: hyperlinks follow dataValidations and precede the print
    // settings, the drawings, the table parts and extLst.
    if (!insertBeforeAny(&(*parts)[patch.sheetPart],
                         {QStringLiteral("printOptions"), QStringLiteral("pageMargins"), QStringLiteral("pageSetup"),
                          QStringLiteral("headerFooter"), QStringLiteral("rowBreaks"), QStringLiteral("colBreaks"),
                          QStringLiteral("customProperties"), QStringLiteral("cellWatches"),
                          QStringLiteral("ignoredErrors"), QStringLiteral("smartTags"), QStringLiteral("drawing"),
                          QStringLiteral("legacyDrawing"), QStringLiteral("legacyDrawingHF"),
                          QStringLiteral("drawingHF"), QStringLiteral("picture"), QStringLiteral("oleObjects"),
                          QStringLiteral("controls"), QStringLiteral("webPublishItems"), QStringLiteral("tableParts"),
                          QStringLiteral("extLst"), QStringLiteral("/worksheet")},
                         xml)) {
        return fail(error, QStringLiteral("post-pass: no hyperlinks anchor in %1").arg(patch.sheetPart));
    }
    return true;
}

} // namespace

void openSheetData(QByteArray *sheetXml)
{
    sheetXml->replace(QByteArrayLiteral("<sheetData/>"), QByteArrayLiteral("<sheetData></sheetData>"));
}

bool applySheetFeatures(QHash<QString, QByteArray> *parts, const PatchManifest::SheetPatch &patch, QString *error)
{
    QByteArray &xml = (*parts)[patch.sheetPart];
    if (!addCellMetadata(&xml, patch.arrayRefs)) {
        return fail(error, QStringLiteral("post-pass: a dynamic-array cell is missing from %1").arg(patch.sheetPart));
    }
    // Schema order: sheetProtection directly follows sheetData (it lands
    // before the autoFilter inserted there earlier).
    if (!patch.sheetProtectionXml.isEmpty() && !insertAfterTag(&xml, QStringLiteral("sheetData"), patch.sheetProtectionXml)) {
        return fail(error, QStringLiteral("post-pass: no </sheetData> in %1").arg(patch.sheetPart));
    }
    if (!patchRows(&xml, patch.rowOutline) || !replaceCols(&xml, patch.colsXml)) {
        return fail(error, QStringLiteral("post-pass: no row or column anchor in %1").arg(patch.sheetPart));
    }
    setFormatLevel(&xml, QByteArrayLiteral("outlineLevelRow"), patch.outlineLevelRow);
    setFormatLevel(&xml, QByteArrayLiteral("outlineLevelCol"), patch.outlineLevelCol);
    if (const QByteArray sheetPr = sheetPrXml(patch); !sheetPr.isEmpty()
        && !insertBeforeAny(&xml, {QStringLiteral("dimension"), QStringLiteral("sheetViews")}, sheetPr)) {
        return fail(error, QStringLiteral("post-pass: no sheetPr anchor in %1").arg(patch.sheetPart));
    }
    return writeHyperlinks(parts, patch, error);
}

void collectXfProtection(const QByteArray &sheetXml, const QHash<QString, quint8> &cells, QHash<int, quint8> *xfs)
{
    for (qsizetype pos = sheetXml.indexOf("<c r=\""); pos >= 0 && !cells.isEmpty(); pos = sheetXml.indexOf("<c r=\"", pos + 1)) {
        const qsizetype refEnd = sheetXml.indexOf('"', pos + 6);
        const auto found = cells.constFind(QString::fromLatin1(sheetXml.mid(pos + 6, refEnd - pos - 6)));
        if (found == cells.constEnd()) {
            continue;
        }
        const QByteArray tag = sheetXml.mid(pos, sheetXml.indexOf('>', pos) + 1 - pos);
        const qsizetype s = tag.indexOf(" s=\"");
        const int xf = s < 0 ? 0 : tag.mid(s + 4, tag.indexOf('"', s + 4) - s - 4).toInt();
        xfs->insert(xf, found.value());
    }
}

bool applyXfProtection(QByteArray *stylesXml, const QHash<int, quint8> &xfs)
{
    if (xfs.isEmpty()) {
        return true;
    }
    const qsizetype open = stylesXml->indexOf("<cellXfs");
    const qsizetype close = stylesXml->indexOf("</cellXfs>", open);
    if (open < 0 || close < 0) {
        return false;
    }
    QList<QPair<qsizetype, qsizetype>> spans; // each <xf> element: start, end (exclusive)
    for (qsizetype pos = stylesXml->indexOf("<xf ", open); pos >= 0 && pos < close; pos = stylesXml->indexOf("<xf ", pos + 1)) {
        const qsizetype tagEnd = stylesXml->indexOf('>', pos);
        const bool selfClosing = stylesXml->at(tagEnd - 1) == '/';
        const qsizetype end = selfClosing ? tagEnd + 1 : stylesXml->indexOf("</xf>", tagEnd) + 5;
        spans.append({pos, end});
    }
    for (qsizetype i = spans.size() - 1; i >= 0; --i) {
        const auto found = xfs.constFind(int(i));
        if (found == xfs.constEnd()) {
            continue;
        }
        const auto [start, end] = spans.at(i);
        QByteArray element = stylesXml->mid(start, end - start);
        const qsizetype tagEnd = element.indexOf('>');
        QByteArray tag = element.left(tagEnd + 1);
        QByteArray inner = tag.endsWith("/>") ? QByteArray() : element.mid(tagEnd + 1, element.size() - tagEnd - 1 - 5);
        if (tag.endsWith("/>")) {
            tag.chop(2);
            tag += '>';
        }
        stripAttribute(&tag, "applyProtection");
        appendToTag(&tag, QByteArrayLiteral(" applyProtection=\"1\""));
        const QByteArray protection = QByteArrayLiteral("<protection")
            + ((found.value() & 1) != 0 ? QByteArrayLiteral(" locked=\"0\"") : QByteArray())
            + ((found.value() & 2) != 0 ? QByteArrayLiteral(" hidden=\"1\"") : QByteArray()) + QByteArrayLiteral("/>");
        stylesXml->replace(start, end - start, tag + inner + protection + QByteArrayLiteral("</xf>"));
    }
    return true;
}

} // namespace QindaOffice::Calc::IO
