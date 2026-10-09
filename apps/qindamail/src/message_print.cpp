// SPDX-License-Identifier: GPL-3.0-or-later
#include "message_print.h"

#include "mail_service.h"
#include "message_content.h"
#include "message_document.h"
#include "message_render.h"

#include <QAbstractTextDocumentLayout>
#include <QLocale>
#include <QPainter>
#include <QPrinter>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>

#include <algorithm>

namespace QindaOffice::Apps::Mail {

namespace {

// Paper margins of the printed memo, on top of what the printer reserves.
constexpr qreal kMarginMm = 15.0;

QString sizeText(qint64 bytes)
{
    const QLocale locale;
    return locale.formattedDataSize(bytes, 0, QLocale::DataSizeTraditionalFormat);
}

QString row(const QString &label, const QString &value)
{
    if (value.trimmed().isEmpty()) return {};
    return QStringLiteral("<tr><td style=\"padding-right: 12px; vertical-align: top\"><b>%1</b></td><td>%2</td></tr>")
        .arg(label.toHtmlEscaped(), value.toHtmlEscaped());
}

QString headerBlock(const QindaOffice::Mail::MessageContent &content, const QVariantMap &record)
{
    QString from = content.fromName.isEmpty() ? content.fromAddress
                                              : content.fromName + QStringLiteral(" <") + content.fromAddress + QLatin1Char('>');
    if (content.fromAddress.isEmpty()) from = record.value(QStringLiteral("sender")).toString();
    const QDateTime date = content.date.isValid()
        ? content.date
        : QDateTime::fromString(record.value(QStringLiteral("date")).toString(), Qt::ISODate);
    QStringList attachments;
    for (const QVariant &value : record.value(QStringLiteral("attachments")).toList()) {
        const QVariantMap attachment = value.toMap();
        const qint64 size = attachment.value(QStringLiteral("size")).toLongLong();
        attachments.append(attachment.value(QStringLiteral("name")).toString()
                           + (size > 0 ? QStringLiteral(" (") + sizeText(size) + QLatin1Char(')') : QString()));
    }
    QString subject = content.subject.isEmpty() ? QObject::tr("(no subject)") : content.subject;
    return QStringLiteral("<h2>%1</h2><table cellspacing=\"0\" cellpadding=\"1\">").arg(subject.toHtmlEscaped())
        + row(QObject::tr("From:"), from)
        + row(QObject::tr("Sent:"), date.isValid() ? QLocale().toString(date.toLocalTime(), QLocale::LongFormat) : QString())
        + row(QObject::tr("To:"), content.to) + row(QObject::tr("Cc:"), content.cc)
        + row(QObject::tr("Attachments:"), attachments.join(QStringLiteral("; ")))
        + QStringLiteral("</table><hr/>");
}

// The header block goes inside the body's own <body> when it has one (its
// <head> styles stay in front of everything).
QString combine(const QString &header, const QString &body)
{
    static const QRegularExpression bodyTag(QStringLiteral("<body\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = bodyTag.match(body);
    if (!match.hasMatch()) return header + body;
    QString out = body;
    out.insert(match.capturedEnd(), header);
    return out;
}

} // namespace

std::unique_ptr<QTextDocument> MessagePrinter::document(MailService *service, const QString &messageId)
{
    if (service == nullptr) return nullptr;
    const QVariantMap record = service->messageById(messageId);
    if (record.isEmpty()) return nullptr;
    const QindaOffice::Mail::MessageContent content = service->contentOf(messageId);
    const QindaOffice::Mail::RenderedBody body = QindaOffice::Mail::renderBody(content, service->renderPolicy(messageId));
    const MessageImages images = decodeMessageImages(body);
    auto document = std::make_unique<QTextDocument>();
    QindaOffice::Mail::RenderedBody memo = body;
    memo.html = combine(headerBlock(content, record), body.html);
    // The same guard as the reading pane: every image URL registered before
    // the HTML (message_document.h); resources travel with clone().
    setBody(document.get(), memo, images);
    return document;
}

Quick::PrintRenderCallback MessagePrinter::callback(MailService *service, const QString &messageId)
{
    std::shared_ptr<QTextDocument> snapshot = document(service, messageId);
    if (snapshot == nullptr) return {};
    return [snapshot](QPrinter *printer, QPainter *painter) {
        // AGENT-NOTE: laid out at screen resolution (96 px per inch, the CSS
        // pixel mail is written in) and painted through a scaled painter.
        // Binding the layout to the printer instead would print every
        // "font-size: 14px" in printer pixels — unreadably small.
        std::unique_ptr<QTextDocument> doc(snapshot->clone());
        const qreal scale = printer->logicalDpiX() / 96.0;
        const QSizeF page(printer->width() / scale, printer->height() / scale);
        const QMarginsF reserved = printer->pageLayout().marginsPixels(printer->resolution()) / scale;
        const qreal wanted = kMarginMm / 25.4 * 96.0;
        QTextFrameFormat frame = doc->rootFrame()->frameFormat();
        frame.setLeftMargin(std::max<qreal>(0, wanted - reserved.left()));
        frame.setRightMargin(std::max<qreal>(0, wanted - reserved.right()));
        frame.setTopMargin(std::max<qreal>(0, wanted - reserved.top()));
        frame.setBottomMargin(std::max<qreal>(0, wanted - reserved.bottom()));
        doc->rootFrame()->setFrameFormat(frame);
        doc->setPageSize(page);
        // Pictures fit the text column, as on screen.
        MessageImages images;
        for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                if (!it.fragment().charFormat().isImageFormat()) continue;
                const QUrl url(it.fragment().charFormat().toImageFormat().name());
                images.insert(url, doc->resource(QTextDocument::ImageResource, url).value<QImage>());
            }
        }
        constrainImages(doc.get(), images, page.width() - frame.leftMargin() - frame.rightMargin());
        QAbstractTextDocumentLayout *layout = doc->documentLayout();
        const int pages = std::max(1, doc->pageCount());
        for (int index = 0; index < pages; ++index) {
            if (index > 0) printer->newPage();
            painter->save();
            painter->scale(scale, scale);
            painter->translate(0, -index * page.height());
            const QRectF view(0, index * page.height(), page.width(), page.height());
            painter->setClipRect(view);
            QAbstractTextDocumentLayout::PaintContext context;
            context.clip = view;
            context.palette.setColor(QPalette::Text, Qt::black);
            layout->draw(painter, context);
            painter->restore();
        }
    };
}

} // namespace QindaOffice::Apps::Mail
