// SPDX-License-Identifier: GPL-3.0-or-later
#include "xlsx_print_assets.h"
#include "ooxml_package.h"
#include "sheet.h"
#include <QRegularExpression>
namespace QindaOffice::Calc::IO {
namespace {
const QString sml = QStringLiteral("http://schemas.openxmlformats.org/spreadsheetml/2006/main");
const QString rel = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships");
const QString vml = QStringLiteral("urn:schemas-microsoft-com:vml");
const QString office = QStringLiteral("urn:schemas-microsoft-com:office:office");
const QString excel = QStringLiteral("urn:schemas-microsoft-com:office:excel");
qreal fraction(QString text)
{
    const bool fixed = text.endsWith(QLatin1Char('f'));
    if (fixed) text.chop(1);
    return qBound(0.0, text.toDouble() / (fixed ? 65536.0 : 1.0), 1.0);
}
qreal dimension(const QString &style, const QString &name)
{
    const QRegularExpression re(QStringLiteral("(?:^|;)\\s*%1\\s*:\\s*([0-9.]+)(pt|px|in|cm|mm)?").arg(name));
    const auto match = re.match(style);
    const QString unit = match.captured(2);
    const qreal multiplier = unit == QLatin1String("in") ? 72 : unit == QLatin1String("cm") ? 72 / 2.54
        : unit == QLatin1String("mm") ? 72 / 25.4 : unit == QLatin1String("px") ? 0.75 : 1;
    return match.captured(1).toDouble() * multiplier;
}
}
LegacyPrintAssets legacyPrintAssets(const Workbook &book, qint32 index)
{
    LegacyPrintAssets out;
    if (index < 0 || index >= book.sheetCount() || book.source.carried.isEmpty()) return out;
    const Sheet &sheet = book.sheetAt(index);
    const auto package = Ooxml::Package::fromData(book.source.carried);
    if (!package) return out;
    const auto xml = package->xml(sheet.source().part);
    if (!xml) return out;
    for (const auto name : {u"legacyDrawingHF", u"legacyDrawing"}) {
        const auto *drawing = xml->child(sml, name);
        if (drawing == nullptr) continue;
        const auto link = package->relationship(sheet.source().part, drawing->attribute(rel, u"id"));
        if (!link || link->external) continue;
        const QString part = package->targetPart(sheet.source().part, *link);
        const auto shapes = package->xml(part);
        if (!shapes) continue;
        for (const auto *shape : shapes->elements(vml, u"shape")) {
            const QString style = shape->attr(u"style");
            const QSizeF size(dimension(style, QStringLiteral("width")), dimension(style, QStringLiteral("height")));
            if (QStringView(name) == u"legacyDrawingHF") {
                const auto *image = shape->child(vml, u"imagedata");
                if (!image) continue;
                const QString id = image->attribute(office, u"relid", image->attribute(rel, u"id"));
                const auto imageLink = package->relationship(part, id);
                if (imageLink && !imageLink->external)
                    out.pictures.insert(shape->attr(u"id"), {package->data(package->targetPart(part, *imageLink)), size,
                        QMarginsF(fraction(image->attr(u"cropleft")), fraction(image->attr(u"croptop")),
                                  fraction(image->attr(u"cropright")), fraction(image->attr(u"cropbottom")))});
            } else {
                const auto *data = shape->child(excel, u"ClientData");
                if (!data || data->attr(u"ObjectType") != QLatin1String("Note")) continue;
                const auto *row = data->child(excel, u"Row");
                const auto *col = data->child(excel, u"Column");
                const bool visible = data->child(excel, u"Visible") != nullptr || style.contains(QLatin1String("visibility:visible"));
                if (!visible || !row || !col) continue;
                const auto cell = mapSourceCell(sheet.source().steps, {row->text().toInt(), col->text().toInt()});
                if (cell && sheet.commentAt(*cell)) {
                    PrintedNote note{*cell, size, std::nullopt, 0, 0};
                    if (const auto *anchor = data->child(excel, u"Anchor")) {
                        const auto a = anchor->text().split(QLatin1Char(','));
                        if (a.size() == 8) {
                            note.anchor = mapSourceCell(sheet.source().steps, {a.at(2).trimmed().toInt(), a.at(0).trimmed().toInt()});
                            note.dx = a.at(1).trimmed().toDouble() / 1024;
                            note.dy = a.at(3).trimmed().toDouble() / 256;
                        }
                    }
                    out.notes.append(note);
                }
            }
        }
    }
    return out;
}
}
