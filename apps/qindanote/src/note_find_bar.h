// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaNote find bar: searches page titles and typed text blocks across the
// whole notebook in order, wraps around, and selects the matching page.
//
// Edit > Find/Find Next/Find Previous were created disabled and never wired
// in QindaNote. Same shape as QindaCalc's and QindaShow's bars.

#include <QList>
#include <QWidget>

class QCheckBox;
class QLineEdit;

namespace QindaOffice::Apps::Notes {

class NoteDocument;

class NoteFindBar final : public QWidget {
    Q_OBJECT
public:
    NoteFindBar(NoteDocument *document, QWidget *parent);

    void openFind();
    void findNext();
    void findPrevious();
    // The page the caller is currently on, so next/previous advance from it.
    void setCurrentPage(const QString &sectionId, const QString &pageId);

signals:
    void message(const QString &text);
    void pageRequested(const QString &sectionId, const QString &pageId);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    struct Hit {
        QString sectionId;
        QString pageId;
    };

    [[nodiscard]] bool findIn(bool backward);
    // AGENT-GUARD: reads pages that were never opened from disk first —
    // their envelopes are empty in memory, so a search that skipped them
    // would quietly report "not found" for text that is really there.
    [[nodiscard]] QList<Hit> matches() const;

    NoteDocument *m_document;
    QLineEdit *m_findEdit;
    QCheckBox *m_caseCheck;
    QString m_currentSectionId;
    QString m_currentPageId;
};

} // namespace QindaOffice::Apps::Notes
