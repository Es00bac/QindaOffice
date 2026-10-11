// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "docx_bibliography.h"
#include "docx_reference_fields.h"
#include <QTextCursor>
namespace QindaOffice::Apps::Write {
// GUI-thread document edits; caller owns the edit block and cursor signals.
// Unsupported or locked imported fields keep their code and cached result.
[[nodiscard]] QStringList citationOrder(const QTextDocument &document, const Docx::Bibliography &data);
[[nodiscard]] QString bibliographyResult(const QTextDocument &document, const Docx::Bibliography &data);
[[nodiscard]] bool referenceLocked(const QVariantMap &field);
[[nodiscard]] bool simpleReferenceCode(const QString &code);
void insertReference(QTextCursor &cursor, const QString &code, const QString &result,
                     QTextCharFormat format = QTextCharFormat());
void replaceReference(QTextDocument &document, const Docx::ReferenceSpan &span, const QString &text);
// Returns how many supported cached fields changed; leaves unsupported ones alone.
int refreshReferences(QTextDocument &document, const Docx::Bibliography &data, qreal pageHeight, bool pagesOnly = false);
[[nodiscard]] QVariantList referenceTargets(const QTextDocument &document);
bool followReference(QTextCursor &cursor, const QString &bookmark);
} // namespace QindaOffice::Apps::Write
