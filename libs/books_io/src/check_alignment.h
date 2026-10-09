// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Per-printer check alignment (M68, D-077; docs/books.md "Check
// printing"). Printers do not feed paper to the same hundredth of an inch,
// so QuickBooks lets the user move every check field by a horizontal and a
// vertical offset per printer, checked on an alignment test page. So does
// QindaBooks: CheckRenderer shifts the whole sheet — check third and stubs —
// by the offset of the printer the job goes to.
//
// AGENT-NOTE (where it is stored): per user, per printer NAME, in
// $XDG_CONFIG_HOME/qindaoffice/books/check-alignment.json — not in the
// company file. Alignment belongs to the printer on this computer, and one
// printer serves every company the user keeps (two businesses' books, one
// check printer). A PDF has no printer and is never shifted.
//
// Offsets are typed in inches (two decimals, as QuickBooks' 1/100 in steps)
// or millimetres (one decimal), at most one inch / 25.4 mm either way, and
// kept as the text the user typed; only painting turns them into points.

#include <QHash>
#include <QString>
#include <QStringList>

#include <optional>

namespace QindaOffice::Books {

struct CheckOffset {
    qreal dx = 0; // points; positive moves the print right
    qreal dy = 0; // points; positive moves the print down
};

struct CheckAlignment {
    enum class Unit { Inch, Millimetre };
    QString printer;      // the printer's name as the print dialog lists it
    QString x = QStringLiteral("0");
    QString y = QStringLiteral("0");
    Unit unit = Unit::Inch;

    [[nodiscard]] CheckOffset offset() const;
    [[nodiscard]] bool isZero() const;
    // "0.25 in right, 0.5 in down", "1.5 mm left", "none".
    [[nodiscard]] QString describe() const;
    // "in" | "mm".
    [[nodiscard]] QString unitKey() const { return unit == Unit::Millimetre ? QStringLiteral("mm") : QStringLiteral("in"); }
    // Validated: `x`/`y` decimal text within the unit's limits. nullopt
    // with `error` when they do not read.
    [[nodiscard]] static std::optional<CheckAlignment> make(const QString &printer, const QString &x, const QString &y,
                                                            const QString &unitKey, QString *error = nullptr);
};

class CheckAlignmentStore {
public:
    // `path` empty = defaultPath() (tests pass a file in a temp dir).
    explicit CheckAlignmentStore(const QString &path = {});
    [[nodiscard]] static QString defaultPath();

    // The printer's alignment; a zero offset in inches when none is stored.
    [[nodiscard]] CheckAlignment alignment(const QString &printer) const;
    // Replaces the printer's entry; a zero offset removes it. Written
    // atomically (QSaveFile); false with `error` when it cannot be.
    bool save(const CheckAlignment &alignment, QString *error = nullptr);
    [[nodiscard]] QStringList printers() const;

private:
    [[nodiscard]] QHash<QString, CheckAlignment> load() const;
    QString m_path;
};

} // namespace QindaOffice::Books
