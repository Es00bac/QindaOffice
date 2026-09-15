// SPDX-License-Identifier: GPL-3.0-or-later
#include "pdf_export.h"

#include "slide_renderer.h"

#include <QPainter>
#include <QPdfWriter>
#include <QTextDocument>

namespace QindaOffice::Apps::Show {

bool exportDeckPdf(const ShowModel::Presentation &deck, const QString &path, bool withNotesPages,
                   QString *error)
{
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(deck.slideSize, QPageSize::Point));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Point);
    writer.setTitle(deck.title.isEmpty() ? QStringLiteral("QindaShow deck") : deck.title);
    QPainter painter(&writer);
    if (!painter.isActive()) {
        if (error != nullptr) {
            *error = QStringLiteral("Could not open %1 for writing.").arg(path);
        }
        return false;
    }
    for (int i = 0; i < deck.slides.size(); ++i) {
        ShowModel::renderSlide(&painter, deck.slides.at(i), deck.slideSize);
        if (i + 1 < deck.slides.size()) {
            writer.newPage();
        }
    }
    if (withNotesPages && !deck.slides.isEmpty()) {
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.newPage();
        for (int i = 0; i < deck.slides.size(); ++i) {
            const ShowModel::Slide &slide = deck.slides.at(i);
            const QRectF page = painter.viewport();
            const QRectF slideRect = QRectF(page.topLeft(), QSizeF(page.width(), page.height() * 0.55));
            const qreal scale = qMin(slideRect.width() / deck.slideSize.width(),
                                     slideRect.height() / deck.slideSize.height());
            painter.save();
            painter.translate(slideRect.center()
                              - QPointF(deck.slideSize.width() * scale / 2.0,
                                        deck.slideSize.height() * scale / 2.0));
            painter.scale(scale, scale);
            ShowModel::renderSlide(&painter, slide, deck.slideSize);
            painter.restore();
            QTextDocument notes;
            notes.setHtml(slide.notes.toHtmlEscaped().replace(QLatin1String("\n"),
                                                               QStringLiteral("<br/>")));
            notes.setTextWidth(page.width());
            const QRectF notesRect(QPointF(page.left(), slideRect.bottom() + 12),
                                   QSizeF(page.width(), page.bottom() - slideRect.bottom() - 24));
            painter.save();
            painter.translate(notesRect.topLeft());
            notes.drawContents(&painter, QRectF(QPointF(0, 0), notesRect.size()));
            painter.restore();
            if (i + 1 < deck.slides.size()) {
                writer.newPage();
            }
        }
    }
    return true;
}

} // namespace QindaOffice::Apps::Show
