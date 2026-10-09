// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The style the New Style / Modify Style dialog is editing (M73, D-082):
// its name, type, base, next style and gallery flag, and its look — kept
// as a one-paragraph sample document whose paragraph and run carry the
// style's own w:pPr / w:rPr as their imported markup. M70's Font,
// Paragraph, Tabs and Borders and Shading dialogs work on the sample
// through the same functions they use on the page (write_font.h,
// write_paragraph.h, write_borders.h), so the dialogs need no second code
// path; at OK the style's markup is what the .docx writer would write for
// the sample (docx_style_edit.h paragraphFormatting / runFormatting): the
// style's own markup with exactly what the user changed re-derived.
//
// Threading: GUI thread. The sample is private to the draft and has no
// undo history; StylesController commits the result as one undo step.

#include "docx_restyle.h"
#include "ooxml_xml.h"

#include <QObject>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextFormat>
#include <QVariantMap>

class QTextCursor;

namespace QindaOffice::Apps::Write {

class WriteDocument;

class StyleDraft final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode NOTIFY changed)        // "new" | "modify"
    Q_PROPERTY(QString styleId READ styleId NOTIFY changed)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    Q_PROPERTY(QString type READ type WRITE setType NOTIFY changed) // paragraph, character, linked
    Q_PROPERTY(QString basedOn READ basedOn WRITE setBasedOn NOTIFY changed)
    Q_PROPERTY(QString next READ next WRITE setNext NOTIFY changed)
    Q_PROPERTY(bool quick READ quick WRITE setQuick NOTIFY changed)
    // "" (as it is), "none", "bullet", "number": Format ▸ Numbering.
    Q_PROPERTY(QString numbering READ numbering WRITE setNumbering NOTIFY changed)
    // [{id, name}] for the Based On and Next Style lists.
    Q_PROPERTY(QVariantList paragraphStyles READ paragraphStyles NOTIFY changed)
    Q_PROPERTY(QVariantList characterStyles READ characterStyles NOTIFY changed)
    // {family, size, bold, italic, underline, color, alignment, before,
    // after, lineRule}: the quick controls and the preview.
    Q_PROPERTY(QVariantMap look READ look NOTIFY changed)
    Q_PROPERTY(QString description READ description NOTIFY changed)
public:
    StyleDraft(WriteDocument *document, QObject *parent);

    // Modify Style for `id`, or New Style from what `selection` shows.
    bool loadStyle(const QString &id);
    void loadSelection(const QTextCursor &selection);

    [[nodiscard]] QString mode() const { return m_mode; }
    [[nodiscard]] QString styleId() const { return m_id; }
    [[nodiscard]] QString name() const { return m_name; }
    void setName(const QString &name);
    [[nodiscard]] QString type() const { return m_type; }
    void setType(const QString &type);
    [[nodiscard]] QString basedOn() const { return m_basedOn; }
    void setBasedOn(const QString &id);
    [[nodiscard]] QString next() const { return m_next; }
    void setNext(const QString &id);
    [[nodiscard]] bool quick() const { return m_quick; }
    void setQuick(bool quick);
    [[nodiscard]] QString numbering() const { return m_numbering; }
    void setNumbering(const QString &numbering);
    [[nodiscard]] QVariantList paragraphStyles() const { return m_paragraphStyles; }
    [[nodiscard]] QVariantList characterStyles() const { return m_characterStyles; }
    [[nodiscard]] QVariantMap look() const;
    [[nodiscard]] QString description() const;
    // The w:name to write: Word's own spelling of a built-in kept while the
    // user leaves the name alone.
    [[nodiscard]] QString wordName() const;

    // M70's dialogs, on the sample (same keys as on the page).
    Q_INVOKABLE QVariantMap paragraphInfo() const;
    Q_INVOKABLE bool applyParagraph(const QVariantMap &answer);
    Q_INVOKABLE QVariantMap tabsInfo() const;
    Q_INVOKABLE bool applyTabs(const QVariantMap &answer);
    Q_INVOKABLE QVariantMap fontInfo() const;
    Q_INVOKABLE bool applyFont(const QVariantMap &answer);
    Q_INVOKABLE QVariantMap dialogInfo() const;
    Q_INVOKABLE bool applyDialog(const QVariantMap &answer);

    // The style's own w:pPr / w:rPr now (either may be null).
    [[nodiscard]] Ooxml::XmlElementPtr paragraphMarkup() const;
    [[nodiscard]] Ooxml::XmlElementPtr runMarkup() const;

signals:
    void changed();

private:
    void listStyles(const Docx::StyleSheet &sheet);
    // The sample paragraph and its run, shown and marked up as `pPr`/`rPr`.
    void setSample(const QTextBlockFormat &block, const QTextCharFormat &run);
    [[nodiscard]] QTextCursor sample() const;

    WriteDocument *m_document;
    QTextDocument m_sample;
    QString m_mode = QStringLiteral("new");
    QString m_id;
    QString m_name;
    QString m_sourceName;
    QString m_type = QStringLiteral("paragraph");
    QString m_basedOn;
    QString m_next;
    QString m_numbering;
    bool m_quick = true;
    QVariantList m_paragraphStyles;
    QVariantList m_characterStyles;
};

} // namespace QindaOffice::Apps::Write
