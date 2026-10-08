// SPDX-License-Identifier: GPL-3.0-or-later
// Post-pass work inside one worksheet part (xlsx_post_cells.h, M61, D-069).

#include "xlsx_post_cells.h"

#include "xlsx_write_xml.h"
#include "xlsx_xml.h"

namespace QindaOffice::Calc::IO {

namespace {

QByteArray escaped(const QString &text)
{
    QString out = text;
    out.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    out.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    out.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    return out.toUtf8();
}

} // namespace

bool retypeCells(QByteArray *sheetXml, const QHash<QString, PatchManifest::CellType> &cells)
{
    if (cells.isEmpty()) {
        return true;
    }
    // AGENT-NOTE: QXlsx writes every cell as <c r="A1" s="3" t="n"> with an
    // optional <f>…</f> (or <f …/>) and <v>…</v>; text inside is escaped, so
    // "<c r=\"" can only ever start a cell element. One linear pass rebuilds
    // the listed cells and copies everything else untouched.
    const QByteArray &xml = *sheetXml;
    QByteArray out;
    out.reserve(xml.size() + cells.size() * 24);
    qsizetype copied = 0;
    qsizetype done = 0;
    for (qsizetype start = xml.indexOf("<c r=\""); start >= 0; start = xml.indexOf("<c r=\"", start + 1)) {
        const qsizetype refStart = start + 6;
        const qsizetype refEnd = xml.indexOf('"', refStart);
        const qsizetype tagEnd = xml.indexOf('>', refEnd);
        if (refEnd < 0 || tagEnd < 0) {
            return false;
        }
        const auto found = cells.constFind(QString::fromLatin1(xml.mid(refStart, refEnd - refStart)));
        if (found == cells.constEnd()) {
            continue;
        }
        const bool selfClosing = xml.at(tagEnd - 1) == '/';
        const qsizetype close = selfClosing ? -1 : xml.indexOf("</c>", tagEnd);
        if (!selfClosing && close < 0) {
            return false;
        }
        const qsizetype end = selfClosing ? tagEnd + 1 : close + 4;
        const QByteArray startTag = xml.mid(start, tagEnd - start);
        QByteArray rebuilt = xml.mid(start, refEnd + 1 - start); // <c r="A1"
        if (const qsizetype style = startTag.indexOf(" s=\""); style >= 0) {
            rebuilt += startTag.mid(style, startTag.indexOf('"', style + 4) + 1 - style);
        }
        rebuilt += QByteArrayLiteral(" t=\"") + found->type + QByteArrayLiteral("\">");
        if (!selfClosing) {
            const QByteArray inner = xml.mid(tagEnd + 1, close - tagEnd - 1);
            if (const qsizetype f = inner.indexOf("<f"); f >= 0) {
                const qsizetype selfF = inner.indexOf("/>", f);
                const qsizetype openEnd = inner.indexOf('>', f);
                const qsizetype fEnd = selfF >= 0 && selfF + 1 == openEnd ? selfF + 2
                                                                        : inner.indexOf("</f>", f) + 4;
                rebuilt += inner.mid(f, fEnd - f);
            }
        }
        rebuilt += QByteArrayLiteral("<v>") + escaped(found->value) + QByteArrayLiteral("</v></c>");
        out += xml.mid(copied, start - copied);
        out += rebuilt;
        copied = end;
        ++done;
    }
    out += xml.mid(copied);
    *sheetXml = out;
    return done == cells.size();
}

bool addCommentsVml(QHash<QString, QByteArray> *parts, const PatchManifest::SheetPatch &patch, QString *error)
{
    if (patch.commentCells.isEmpty()) {
        return true;
    }
    const QString vmlPart = QStringLiteral("xl/drawings/vmlDrawing%1.vml").arg(patch.vmlNumber);
    parts->insert(vmlPart, commentsVmlXml(patch.commentCells, patch.vmlNumber));
    appendContentTypeDefault(&(*parts)[QStringLiteral("[Content_Types].xml")], QStringLiteral("vml"),
                             QStringLiteral("application/vnd.openxmlformats-officedocument.vmlDrawing"));
    const int slash = int(patch.sheetPart.lastIndexOf(QLatin1Char('/')));
    const QString relsPath = patch.sheetPart.left(slash) + QStringLiteral("/_rels")
        + patch.sheetPart.mid(slash) + QStringLiteral(".rels");
    QByteArray relsXml = parts->value(relsPath);
    const QString rid = appendRelationship(
        &relsXml, QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing"),
        QStringLiteral("../drawings/vmlDrawing%1.vml").arg(patch.vmlNumber));
    if (rid.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("post-pass: broken rels part %1").arg(relsPath);
        }
        return false;
    }
    parts->insert(relsPath, relsXml);
    // Schema order: legacyDrawing follows drawing and precedes
    // legacyDrawingHF, picture, oleObjects, controls, webPublishItems,
    // tableParts and extLst (a <drawing> added later goes before it).
    if (!insertBeforeAny(&(*parts)[patch.sheetPart],
                         {QStringLiteral("legacyDrawingHF"), QStringLiteral("picture"),
                          QStringLiteral("oleObjects"), QStringLiteral("controls"),
                          QStringLiteral("webPublishItems"), QStringLiteral("tableParts"),
                          QStringLiteral("extLst")},
                         QByteArrayLiteral("<legacyDrawing r:id=\"") + rid.toUtf8() + QByteArrayLiteral("\"/>"))
        && !insertBeforeAny(&(*parts)[patch.sheetPart], {QStringLiteral("/worksheet")},
                            QByteArrayLiteral("<legacyDrawing r:id=\"") + rid.toUtf8() + QByteArrayLiteral("\"/>"))) {
        if (error != nullptr) {
            *error = QStringLiteral("post-pass: no legacyDrawing anchor in %1").arg(patch.sheetPart);
        }
        return false;
    }
    return true;
}

} // namespace QindaOffice::Calc::IO
