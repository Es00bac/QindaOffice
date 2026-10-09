// SPDX-License-Identifier: GPL-3.0-or-later
// MailMergeController (write_mailmerge.h): merge fields, rules, Address
// Block, Greeting Line, Match Fields, Update Labels, Check for Errors and
// Preview Results.

#include "write_mailmerge.h"

#include "docx_float.h"
#include "docx_table_format.h"
#include "page_text_control.h"
#include "page_text_item.h"
#include "write_document.h"

#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextFrame>
#include <QTextTable>

namespace QindaOffice::Apps::Write {

namespace {

Docx::AddressBlockOptions addressOptions(const QVariantMap &m)
{
    Docx::AddressBlockOptions o;
    o.nameFormat = m.value(QStringLiteral("nameFormat"), o.nameFormat).toString();
    o.company = m.value(QStringLiteral("company"), o.company).toBool();
    o.postal = m.value(QStringLiteral("postal"), o.postal).toBool();
    o.country = m.value(QStringLiteral("country"), o.country).toInt();
    o.homeCountry = m.value(QStringLiteral("homeCountry"), o.homeCountry).toString();
    o.formatByCountry = m.value(QStringLiteral("formatByCountry"), o.formatByCountry).toBool();
    return o;
}

Docx::GreetingLineOptions greetingOptions(const QVariantMap &m)
{
    Docx::GreetingLineOptions o;
    o.salutation = m.value(QStringLiteral("salutation"), o.salutation).toString();
    o.nameFormat = m.value(QStringLiteral("nameFormat"), o.nameFormat).toString();
    o.punctuation = m.value(QStringLiteral("punctuation"), o.punctuation).toString();
    o.fallback = m.value(QStringLiteral("fallback"), o.fallback).toString();
    return o;
}

QVariantList formatList(const QList<std::pair<QString, QString>> &formats)
{
    QVariantList out;
    for (const auto &[format, sample] : formats) {
        out.append(QVariantMap{{QStringLiteral("format"), format}, {QStringLiteral("sample"), sample}});
    }
    return out;
}

} // namespace

void MailMergeController::insertCode(const QString &instruction)
{
    // The result Word shows for a rule that has one (an IF evaluates for
    // the current recipient); the field's label otherwise.
    QString display;
    if (Docx::mergeFieldKind(instruction) == Docx::MergeFieldKind::If && !m_order.isEmpty()) {
        Docx::MergeCursor cursor;
        cursor.data = &m_data;
        cursor.order = m_order;
        cursor.position = m_position;
        cursor.fieldMap = settings().fieldMap;
        display = Docx::evaluateMergeField(instruction, cursor);
    }
    QTextCursor cursor = m_control->textCursor();
    cursor.beginEditBlock();
    cursor.removeSelectedText();
    const QTextCharFormat next = Docx::insertMergeField(cursor, instruction, display);
    cursor.endEditBlock();
    m_control->setTextCursor(cursor);
    m_control->setCurrentCharFormat(next);
    refreshPreview();
}

void MailMergeController::insertField(const QString &column)
{
    if (!column.isEmpty()) {
        insertCode(Docx::mergeFieldCode(column));
    }
}

QVariantMap MailMergeController::addressBlockInfo() const
{
    const Docx::AddressBlockOptions defaults;
    const QVariantList formats = formatList(Docx::addressNameFormats());
    return {{QStringLiteral("formats"), formats},
            {QStringLiteral("nameFormat"), Docx::addressNameFormats().at(5).first},
            {QStringLiteral("company"), defaults.company},
            {QStringLiteral("postal"), defaults.postal},
            {QStringLiteral("country"), defaults.country},
            {QStringLiteral("homeCountry"), defaults.homeCountry},
            {QStringLiteral("formatByCountry"), defaults.formatByCountry},
            {QStringLiteral("records"), int(m_order.size())}};
}

QString MailMergeController::addressBlockPreview(const QVariantMap &options) const
{
    Docx::MergeCursor cursor;
    cursor.data = &m_data;
    cursor.order = m_order;
    cursor.position = m_position;
    cursor.fieldMap = settings().fieldMap;
    return Docx::evaluateMergeField(Docx::addressBlockCode(addressOptions(options)), cursor);
}

void MailMergeController::insertAddressBlock(const QVariantMap &options)
{
    insertCode(Docx::addressBlockCode(addressOptions(options)));
}

void MailMergeController::insertDefaultAddressBlock(QTextCursor &cursor)
{
    Docx::insertMergeField(cursor, Docx::addressBlockCode({}));
}

QVariantMap MailMergeController::greetingLineInfo() const
{
    return {{QStringLiteral("formats"), formatList(Docx::greetingNameFormats())},
            {QStringLiteral("salutations"), QStringList{QStringLiteral("Dear"), QStringLiteral("To"), QString()}},
            {QStringLiteral("punctuations"), QStringList{QStringLiteral(","), QStringLiteral(":"), QStringLiteral("!"), QString()}},
            {QStringLiteral("fallbacks"),
             QStringList{QStringLiteral("Dear Sir or Madam,"), QStringLiteral("To Whom It May Concern:"), QString()}},
            {QStringLiteral("nameFormat"), Docx::greetingNameFormats().first().first}};
}

QString MailMergeController::greetingLinePreview(const QVariantMap &options) const
{
    Docx::MergeCursor cursor;
    cursor.data = &m_data;
    cursor.order = m_order;
    cursor.position = m_position;
    cursor.fieldMap = settings().fieldMap;
    return Docx::evaluateMergeField(Docx::greetingLineCode(greetingOptions(options)), cursor);
}

void MailMergeController::insertGreetingLine(const QVariantMap &options)
{
    insertCode(Docx::greetingLineCode(greetingOptions(options)));
}

void MailMergeController::insertRule(const QVariantMap &answer)
{
    const QString rule = answer.value(QStringLiteral("rule")).toString();
    const QString field = answer.value(QStringLiteral("field")).toString();
    const QString op = answer.value(QStringLiteral("op"), QStringLiteral("=")).toString();
    const QString value = answer.value(QStringLiteral("value")).toString();
    if (field.isEmpty()) {
        return;
    }
    if (rule == u"if") {
        insertCode(Docx::ifCode(field, op, value, answer.value(QStringLiteral("then")).toString(),
                                answer.value(QStringLiteral("otherwise")).toString()));
    } else if (rule == u"skipif") {
        insertCode(Docx::skipIfCode(field, op, value));
    } else if (rule == u"nextif") {
        insertCode(Docx::nextIfCode(field, op, value));
    }
}

QVariantMap MailMergeController::matchFieldsInfo() const
{
    QVariantList fields;
    const QMap<QString, QString> map = settings().fieldMap;
    for (const QString &name : Docx::addressFields()) {
        fields.append(QVariantMap{{QStringLiteral("name"), name},
                                  {QStringLiteral("column"), Docx::matchedColumn(name, m_data, map)}});
    }
    return {{QStringLiteral("fields"), fields}, {QStringLiteral("columns"), m_data.fields}};
}

void MailMergeController::setMatchFields(const QVariantMap &map)
{
    Docx::MailMergeSettings s = settings();
    s.fieldMap.clear();
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        const QString column = it.value().toString();
        // Only what differs from Word's own matching is kept (w:odso).
        if (column != Docx::matchedColumn(it.key(), m_data, {})) {
            s.fieldMap.insert(it.key(), column);
        }
    }
    store(s);
}

bool MailMergeController::updateLabels()
{
    QTextDocument *document = m_document->textDocument();
    QTextTable *table = nullptr;
    for (QTextFrame *frame : document->rootFrame()->childFrames()) {
        if ((table = qobject_cast<QTextTable *>(frame)) != nullptr) {
            break;
        }
    }
    if (table == nullptr || table->rows() * table->columns() < 2) {
        emit message(tr("This document has no label sheet: Start Mail Merge ▸ Labels makes one."));
        return false;
    }
    const QList<QTextLength> widths = table->format().columnWidthConstraints();
    const qreal label = widths.isEmpty() ? 0 : widths.first().rawValue();
    const QTextTableCell first = table->cellAt(0, 0);
    QTextCursor source = first.firstCursorPosition();
    source.setPosition(first.lastCursorPosition().position(), QTextCursor::KeepAnchor);
    const QTextDocumentFragment content = source.selection();
    QTextCursor edit(document);
    edit.beginEditBlock();
    for (int r = 0; r < table->rows(); ++r) {
        for (int c = 0; c < table->columns(); ++c) {
            // Spacer columns (the gap between labels) are narrow and stay empty.
            const qreal width = c < widths.size() ? widths.at(c).rawValue() : label;
            if ((r == 0 && c == 0) || width < label / 2) {
                continue;
            }
            const QTextTableCell cell = table->cellAt(r, c);
            QTextCursor at = cell.firstCursorPosition();
            at.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
            at.removeSelectedText();
            Docx::insertMergeField(at, QStringLiteral("NEXT"));
            if (!content.isEmpty()) {
                at.insertFragment(content);
            }
        }
    }
    edit.endEditBlock();
    // Each label row as tall as the sheet says, its labels centred: measured
    // once the block has been laid out, joined to it as one undo step.
    edit.joinPreviousEditBlock();
    Docx::refreshRowHeights(table);
    edit.endEditBlock();
    refreshPreview();
    return true;
}

QStringList MailMergeController::checkErrors() const
{
    QStringList errors;
    if (m_order.isEmpty()) {
        errors.append(tr("No recipients are selected (Select Recipients, then Edit Recipient List)."));
        return errors;
    }
    for (const QString &column : Docx::missingMergeColumns(*m_document->textDocument(), m_data)) {
        errors.append(tr("«%1» — the data source has no field %1.").arg(column));
    }
    if (Docx::mergeFieldSpans(*m_document->textDocument()).isEmpty()) {
        errors.append(tr("The document has no merge fields: every copy would be the same."));
    }
    if (kind() == u"email") {
        const QString field = emailInfo().value(QStringLiteral("to")).toString();
        int missing = 0;
        for (const int record : m_order) {
            missing += m_data.value(record, field).trimmed().isEmpty() ? 1 : 0;
        }
        if (field.isEmpty()) {
            errors.append(tr("The recipients have no e-mail address field."));
        } else if (missing > 0) {
            errors.append(tr("%n recipient(s) have no address in %1.", "", missing).arg(field));
        }
    }
    return errors;
}

void MailMergeController::setPreviewing(bool on)
{
    on = on && !m_order.isEmpty();
    if (m_previewing != on) {
        m_previewing = on;
        refreshPreview();
        emit previewChanged();
    }
    sync();
}

void MailMergeController::setPreviewRecord(int record)
{
    const int position = qBound(0, record - 1, qMax(0, int(m_order.size()) - 1));
    if (position != m_position) {
        m_position = position;
        refreshPreview();
        emit previewChanged();
    }
}

void MailMergeController::refreshPreview()
{
    if (m_page == nullptr) {
        return;
    }
    if (!m_previewing || m_order.isEmpty()) {
        m_page->setPreviewDocument(nullptr);
        m_preview.reset();
        return;
    }
    auto preview = std::make_unique<QTextDocument>();
    preview->setUndoRedoEnabled(false);
    Docx::MergeRequest request;
    request.main = m_document->textDocument();
    request.kind = kind();
    request.data = &m_data;
    request.order = m_order;
    request.fieldMap = settings().fieldMap;
    request.first = m_position;
    request.singleCopy = true;
    m_document->configureLike(preview.get()); // first: rows are padded for this page (mergeDocument)
    Docx::mergeDocument(preview.get(), request);
    if (Docx::hasFloatingPictures(*preview)) {
        Docx::installPictureHandler(preview.get());
    }
    m_page->setPreviewDocument(preview.get());
    m_preview = std::move(preview);
}

} // namespace QindaOffice::Apps::Write
