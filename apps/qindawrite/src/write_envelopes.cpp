// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_envelopes.h"

#include "docx_envelopes.h"
#include "docx_merge.h"
#include "docx_properties.h"
#include "docx_table_format.h"
#include "docx_writer.h"
#include "menu_tree.h"
#include "page_text_control.h"
#include "write_document.h"

#include <QSettings>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>

namespace QindaOffice::Apps::Write {

using Quick::ActionEntry;
using Quick::MenuNode;

namespace {

// The user's mailing address and last label, remembered as Word remembers
// them (per user, not per document).
QSettings settings()
{
    return QSettings(QStringLiteral("qindaoffice"), QStringLiteral("write"));
}

void configure(QTextDocument *document, const Docx::PageLayout &layout, const QFont &font)
{
    document->setUseDesignMetrics(true);
    document->setDefaultFont(font);
    document->setPageSize(layout.pageSize);
    QTextFrameFormat root = document->rootFrame()->frameFormat();
    root.setLeftMargin(layout.margins.left() + layout.gutter);
    root.setRightMargin(layout.margins.right());
    root.setTopMargin(layout.margins.top());
    root.setBottomMargin(layout.margins.bottom());
    document->rootFrame()->setFrameFormat(root);
}

// Address lines as paragraphs in a label cell (the cell's first paragraph
// keeps the cell's indents).
void writeLines(QTextCursor &cursor, const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            cursor.insertBlock();
        }
        cursor.insertText(lines.at(i));
    }
}

Docx::EnvelopeSpec specFrom(const QVariantMap &answer)
{
    Docx::EnvelopeSpec spec;
    spec.delivery = answer.value(QStringLiteral("delivery")).toString();
    spec.returnAddress = answer.value(QStringLiteral("returnAddress")).toString();
    spec.omitReturn = answer.value(QStringLiteral("omitReturn")).toBool();
    const QString size = answer.value(QStringLiteral("size"), QStringLiteral("10")).toString();
    spec.paperCode = 0;
    for (const Docx::EnvelopeSize &s : Docx::envelopeSizes()) {
        if (s.id == size) {
            spec.size = s.size;
            spec.paperCode = s.paperCode;
        }
    }
    if (size == u"custom") {
        const qreal w = answer.value(QStringLiteral("width")).toReal();
        const qreal h = answer.value(QStringLiteral("height")).toReal();
        if (w > 48 && h > 48) {
            spec.size = QSizeF(qMax(w, h), qMin(w, h));
        }
    }
    const qreal left = answer.value(QStringLiteral("deliveryLeft"), -1).toReal();
    const qreal top = answer.value(QStringLiteral("deliveryTop"), -1).toReal();
    if (left >= 0 && top >= 0) {
        spec.deliveryAt = QPointF(left, top);
    }
    return spec;
}

} // namespace

EnvelopesController::EnvelopesController(PageTextControl *control, WriteDocument *document,
                                         Quick::ActionModel *model, QObject *parent)
    : QObject(parent)
    , m_control(control)
    , m_document(document)
{
    m_envelopes = model->add(QStringLiteral("writeEnvelopesAction"), tr("&Envelopes…"), {}, QStringLiteral("mail"));
    m_labels = model->add(QStringLiteral("writeLabelsAction"), tr("&Labels…"), {}, QStringLiteral("tag"));
    connect(m_envelopes, &ActionEntry::triggered, this, [this] { emit envelopeDialogRequested(false); });
    connect(m_labels, &ActionEntry::triggered, this, [this] { emit labelsDialogRequested(false); });
}

QList<MenuNode> EnvelopesController::menuItems() const
{
    return {MenuNode::action(m_envelopes->id()), MenuNode::action(m_labels->id())};
}

QVariantMap EnvelopesController::envelopeInfo() const
{
    QVariantList sizes;
    for (const Docx::EnvelopeSize &s : Docx::envelopeSizes()) {
        sizes.append(QVariantMap{{QStringLiteral("id"), s.id},
                                 {QStringLiteral("name"), s.name},
                                 {QStringLiteral("width"), s.size.width()},
                                 {QStringLiteral("height"), s.size.height()}});
    }
    QString delivery = m_control->textCursor().selectedText();
    delivery.replace(QChar::ParagraphSeparator, QLatin1Char('\n')).replace(QChar::LineSeparator, QLatin1Char('\n'));
    QSettings stored = settings();
    return {{QStringLiteral("sizes"), sizes},
            {QStringLiteral("delivery"), delivery},
            {QStringLiteral("returnAddress"), stored.value(QStringLiteral("envelopes/returnAddress")).toString()},
            {QStringLiteral("omitReturn"), stored.value(QStringLiteral("envelopes/omitReturn"), false).toBool()},
            {QStringLiteral("size"), stored.value(QStringLiteral("envelopes/size"), QStringLiteral("10")).toString()}};
}

bool EnvelopesController::applyEnvelope(const QVariantMap &answer)
{
    const QString mode = answer.value(QStringLiteral("mode"), QStringLiteral("add")).toString();
    const Docx::EnvelopeSpec spec = specFrom(answer);
    QSettings stored = settings();
    stored.setValue(QStringLiteral("envelopes/returnAddress"), spec.returnAddress);
    stored.setValue(QStringLiteral("envelopes/omitReturn"), spec.omitReturn);
    stored.setValue(QStringLiteral("envelopes/size"), answer.value(QStringLiteral("size"), QStringLiteral("10")));
    const Docx::PageLayout layout = Docx::envelopeLayout(spec);
    if (mode == u"merge") {
        replaceContent(layout, [&](QTextCursor &cursor) {
            Docx::insertEnvelope(cursor, spec, QString(), [this](QTextCursor &at) {
                if (mergeAddress) {
                    mergeAddress(at);
                }
            });
        });
        return true;
    }
    if (mode == u"new" || mode == u"print") {
        return deliver(mode, layout, [&](QTextCursor &cursor) { Docx::insertEnvelope(cursor, spec, QString()); });
    }
    // Add to Document: an empty document becomes the envelope; otherwise
    // the envelope is a section of its own before the text, as in Word.
    QTextDocument *text = m_document->textDocument();
    QTextCursor cursor(text);
    cursor.beginEditBlock();
    if (text->isEmpty()) {
        m_document->setPageLayoutUndoable(layout);
        Docx::insertEnvelope(cursor, spec, QString());
    } else {
        cursor.movePosition(QTextCursor::Start);
        cursor.insertBlock();
        cursor.movePosition(QTextCursor::PreviousBlock);
        cursor.setBlockFormat(QTextBlockFormat());
        cursor.setBlockCharFormat(QTextCharFormat());
        Docx::insertEnvelope(cursor, spec, Docx::envelopeSectionXml(spec));
    }
    cursor.endEditBlock();
    emit message(tr("Envelope added to the document"));
    return true;
}

QVariantMap EnvelopesController::labelInfo() const
{
    QVariantList products;
    QStringList vendors;
    for (const Docx::LabelProduct &p : Docx::labelProducts()) {
        if (!vendors.contains(p.vendor)) {
            vendors.append(p.vendor);
        }
        const bool inches = p.vendor.contains(u"US");
        const qreal unit = inches ? 96.0 : 96.0 / 25.4;
        const QString suffix = inches ? QStringLiteral("\"") : QStringLiteral(" mm");
        auto measure = [&](qreal px) { return QString::number(px / unit, 'f', inches ? 3 : 1) + suffix; };
        products.append(QVariantMap{
            {QStringLiteral("id"), p.id()},
            {QStringLiteral("vendor"), p.vendor},
            {QStringLiteral("product"), p.product},
            {QStringLiteral("description"), p.description},
            {QStringLiteral("across"), p.across},
            {QStringLiteral("down"), p.down},
            {QStringLiteral("details"),
             tr("%1 — %2 high × %3 wide, %4 across × %5 down; top margin %6, side margin %7, pitch %8 × %9")
                 .arg(p.description, measure(p.height), measure(p.width))
                 .arg(p.across)
                 .arg(p.down)
                 .arg(measure(p.top), measure(p.side), measure(p.verticalPitch), measure(p.horizontalPitch))}});
    }
    QSettings stored = settings();
    QString address = m_control->textCursor().selectedText();
    address.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    return {{QStringLiteral("vendors"), vendors},
            {QStringLiteral("products"), products},
            {QStringLiteral("address"), address},
            {QStringLiteral("product"),
             stored.value(QStringLiteral("labels/product"), Docx::labelProducts().first().id()).toString()}};
}

bool EnvelopesController::applyLabels(const QVariantMap &answer)
{
    const QString mode = answer.value(QStringLiteral("mode"), QStringLiteral("new")).toString();
    const Docx::LabelProduct product = Docx::labelProduct(answer.value(QStringLiteral("product")).toString());
    settings().setValue(QStringLiteral("labels/product"), product.id());
    const Docx::PageLayout layout = Docx::labelLayout(product);
    if (mode == u"merge") {
        replaceContent(layout, [&](QTextCursor &cursor) {
            Docx::insertLabelSheet(cursor, product, [](QTextCursor &cell, int index) {
                if (index > 0) {
                    Docx::insertMergeField(cell, QStringLiteral("NEXT"));
                }
            });
        });
        return true;
    }
    const QString address = answer.value(QStringLiteral("address")).toString();
    const bool fullPage = answer.value(QStringLiteral("fullPage"), true).toBool();
    const int single = (answer.value(QStringLiteral("row"), 1).toInt() - 1) * product.across
        + (answer.value(QStringLiteral("column"), 1).toInt() - 1);
    return deliver(mode, layout, [&](QTextCursor &cursor) {
        Docx::insertLabelSheet(cursor, product, [&](QTextCursor &cell, int index) {
            if (fullPage || index == single) {
                writeLines(cell, address);
            }
        });
    });
}

bool EnvelopesController::deliver(const QString &mode, const Docx::PageLayout &layout,
                                  const std::function<void(QTextCursor &)> &fill)
{
    auto document = std::make_shared<QTextDocument>();
    document->setUndoRedoEnabled(false);
    configure(document.get(), layout, m_document->textDocument()->defaultFont());
    QTextCursor cursor(document.get());
    fill(cursor);
    if (mode == u"print") {
        if (printDocument) {
            printDocument(document);
        }
        return true;
    }
    Docx::WordParts parts;
    parts.layout = layout;
    parts.defaultTabStop = m_document->parts().defaultTabStop;
    QString error;
    const QByteArray bytes = Docx::docxBytes(*document, parts, {}, &error);
    if (bytes.isEmpty()) {
        emit message(tr("Could not make the document: %1").arg(error));
        return false;
    }
    if (openDocument) {
        openDocument(bytes);
    }
    return true;
}

void EnvelopesController::replaceContent(const Docx::PageLayout &layout, const std::function<void(QTextCursor &)> &fill)
{
    QTextCursor cursor(m_document->textDocument());
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.removeSelectedText();
    cursor.setBlockFormat(QTextBlockFormat());
    cursor.setBlockCharFormat(QTextCharFormat());
    m_document->setPageLayoutUndoable(layout);
    fill(cursor);
    cursor.endEditBlock();
    // A label sheet's rows are padded to their pitch once Qt has laid them
    // out, which it does when the edit block ends; joined to it, so the
    // whole change stays one undo step (docx_table_format.h).
    cursor.joinPreviousEditBlock();
    Docx::refreshRowHeights(m_document->textDocument());
    cursor.endEditBlock();
    QTextCursor home(m_document->textDocument());
    // Word puts the insertion point in the first label, not in the block Qt
    // keeps before the sheet (written as nothing, docx_envelopes.cpp).
    if (home.blockFormat().boolProperty(Docx::kParagraphSynthetic) && home.block().next().isValid()) {
        home.movePosition(QTextCursor::NextBlock);
    }
    m_control->setTextCursor(home);
}

} // namespace QindaOffice::Apps::Write
