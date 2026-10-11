// SPDX-License-Identifier: GPL-3.0-or-later
// Excel's two note-printing modes share the document's current text.
// End-of-sheet notes wrap and paginate without dropping long comments.
#include "calc_print_notes.h"
#include <QTextLayout>
#include <QFontMetricsF>
#include <QObject>
#include <algorithm>
namespace QindaOffice::Apps::QindaCalc::PrintDetail {
using namespace CalcEng;
namespace {
QString noteText(const CellComment &note)
{
    return note.author.isEmpty() ? note.text : note.author + QStringLiteral(": ") + note.text;
}
QStringList wrap(const QString &text, const QFont &font, QPaintDevice *device, qreal width)
{
    QStringList out;
    for (const QString &paragraph : text.split(QLatin1Char('\n'))) {
        if (paragraph.isEmpty()) { out.append(QString()); continue; }
        QTextLayout layout(paragraph, font, device);
        QTextOption options;
        options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(options);
        layout.beginLayout();
        while (true) {
            auto line = layout.createLine();
            if (!line.isValid()) break;
            line.setLineWidth(width);
            out.append(paragraph.mid(line.textStart(), line.textLength()));
        }
        layout.endLayout();
    }
    return out;
}
}
NotePages endNotePages(const Canvas &canvas, const PrintContext &ctx, const PrintLayout &layout)
{
    NotePages pages;
    if (ctx.setup->cellComments != 1) return pages;
    const Sheet &sheet = ctx.book->sheetAt(ctx.sheet);
    QList<CellAddr> cells;
    for (auto it = sheet.comments().cbegin(); it != sheet.comments().cend(); ++it)
        cells.append({qint32(it.key() & 0xffffffffu), qint32(it.key() >> 32)});
    std::sort(cells.begin(), cells.end(), [](const auto &a, const auto &b) { return a.row == b.row ? a.col < b.col : a.row < b.row; });
    const qreal lineHeight = QFontMetricsF(ctx.base, canvas.p->device()).height() / canvas.dpp;
    const int capacity = qMax(1, int(layout.contentHeightPt / lineHeight) - 2);
    QStringList current;
    for (const auto &cell : cells) {
        QStringList lines = wrap(addrText(cell) + QStringLiteral(": ") + noteText(*sheet.commentAt(cell)), ctx.base,
                                 canvas.p->device(), layout.contentWidthPt * canvas.dpp);
        lines.append(QString());
        for (const auto &line : lines) {
            if (current.size() == capacity) { pages.append(current); current.clear(); }
            current.append(line);
        }
    }
    if (!current.isEmpty()) pages.append(current);
    return pages;
}
void paintEndNotes(const Canvas &canvas, const PrintContext &ctx, const PrintLayout &layout, const QStringList &lines)
{
    auto &p = *canvas.p;
    p.save();
    p.setFont(ctx.base);
    p.setPen(kInk);
    const qreal height = QFontMetricsF(ctx.base, p.device()).height() / canvas.dpp;
    qreal y = layout.contentTopPt;
    QStringList all{QObject::tr("Notes — %1").arg(ctx.book->sheetName(ctx.sheet)), QString()};
    all += lines;
    for (const auto &line : all) {
        p.drawText(canvas.rect(QRectF(layout.contentLeftPt, y, layout.contentWidthPt, height)), Qt::AlignLeft | Qt::AlignVCenter, line);
        y += height;
    }
    p.restore();
}
void paintDisplayedNotes(const Canvas &canvas, const PrintContext &ctx, const QList<Line> &rows, const QList<Line> &cols,
                         const QList<qreal> &xs, const QList<qreal> &ys, const IO::LegacyPrintAssets &assets)
{
    const Sheet &sheet = ctx.book->sheetAt(ctx.sheet);
    for (const auto &note : assets.notes) {
        const CellAddr anchor = note.anchor.value_or(note.cell);
        const auto r = std::find_if(rows.begin(), rows.end(), [&](const auto &line) { return line.index == anchor.row; });
        const auto c = std::find_if(cols.begin(), cols.end(), [&](const auto &line) { return line.index == anchor.col; });
        if (r == rows.end() || c == cols.end() || !sheet.commentAt(note.cell)) continue;
        const QSizeF size = (note.points.isEmpty() ? QSizeF(108, 59) : note.points) * ctx.scale;
        const QRectF box(xs.at(c - cols.begin()) + c->sizePt * (note.anchor ? note.dx : 1.0),
                         ys.at(r - rows.begin()) + r->sizePt * note.dy, size.width(), size.height());
        auto &p = *canvas.p;
        p.save();
        p.setPen(kInk);
        p.setBrush(ctx.setup->blackAndWhite ? Qt::white : QColor(255, 255, 225));
        p.drawRect(canvas.rect(box));
        QFont font = ctx.base;
        font.setPointSizeF(qMax(1.0, font.pointSizeF() * ctx.scale));
        p.setFont(font);
        p.drawText(canvas.rect(box.adjusted(3, 3, -3, -3)), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   noteText(*sheet.commentAt(note.cell)));
        p.restore();
    }
}
}
