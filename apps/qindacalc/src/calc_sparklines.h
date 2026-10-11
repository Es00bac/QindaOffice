// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Sparklines in QindaCalc (M90, D-098): Insert ▸ Sparklines (Line, Column,
// Win/Loss — Create Sparklines asks for the data and location ranges) and
// the contextual Sparkline tab Excel shows while the cursor is in a
// sparkline: Edit Data (the group's ranges, or one sparkline's data),
// Hidden & Empty Cells, Type, Show (high, low, negative, first, last
// points, markers), Sparkline Color and Marker Color (theme roles with
// their shades, or a colour of one's own), Axis (general or date axis,
// show axis, right to left, minimum and maximum — automatic, same for all
// sparklines, custom), Group, Ungroup and Clear. Every change is one undo
// step through SpreadsheetDocument::setSparklineGroups; the look is
// calc_engine's sparkline.h, drawn by calc_sparkline_look.h.

#include "sparkline.h"

#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>

namespace QindaOffice::Quick {
class ActionModel;
class MenuTree;
}

namespace QindaOffice::Apps::QindaCalc {

class CalcHost;

class CalcSparklines final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by CalcHost.")
public:
    explicit CalcSparklines(CalcHost *host);

    void createActions(Quick::ActionModel *model);
    void insertMenus(Quick::MenuTree *tree);
    // Enabled and checked states for the cursor; true when the cursor is
    // in a sparkline (the "sparkline" ribbon context).
    bool sync(Quick::ActionModel *model);

    // Create Sparklines: {data (the selection), location ("")}; the answer
    // ("" or Excel's message). `type`: 0 line, 1 column, 2 win/loss.
    Q_INVOKABLE QVariantMap createState() const;
    Q_INVOKABLE QString create(int type, const QString &data, const QString &location);
    // Edit Sparklines (`single`: Edit Single Sparkline's Data): {data, location}.
    Q_INVOKABLE QVariantMap editState(bool single) const;
    Q_INVOKABLE QString applyEdit(bool single, const QString &data, const QString &location);
    // Hidden and Empty Cell Settings: {empty: 0 gaps, 1 zero, 2 connect; hidden}.
    Q_INVOKABLE QVariantMap hiddenEmptyState() const;
    Q_INVOKABLE void applyHiddenEmpty(int empty, bool hidden);
    // The colours: {series, negative, markers, high, low, first, last, axis},
    // each {role (index into colorRoles(), the last "Custom"), shade (index
    // into shades()), color "#rrggbb" (what it looks like now)}.
    Q_INVOKABLE QVariantMap colorsState() const;
    Q_INVOKABLE void applyColors(const QVariantMap &colors);
    Q_INVOKABLE QStringList colorRoles() const;
    Q_INVOKABLE QStringList shades() const;
    // Custom Value (minimum or maximum) and Date Axis Type: "" or what is wrong.
    Q_INVOKABLE QString axisValue(bool max) const;
    Q_INVOKABLE QString applyAxisValue(bool max, const QString &value);
    Q_INVOKABLE QString dateRange() const;
    Q_INVOKABLE QString applyDateAxis(const QString &range);

signals:
    void createRequested(int type);
    void editRequested(bool single);
    void hiddenEmptyRequested();
    void colorsRequested();
    void axisValueRequested(bool max);
    void dateAxisRequested();

private:
    // The groups the selection (or the cursor) touches, changed together:
    // one undo step. False when there is none.
    bool change(const std::function<void(QindaOffice::Calc::SparklineGroup &)> &edit, const QString &undoText);
    [[nodiscard]] QList<qint32> touchedGroups() const;
    [[nodiscard]] QindaOffice::Calc::SparklineRef current() const;
    void group();
    void ungroup();
    void clear(bool wholeGroups);

    CalcHost *m_host;
};

} // namespace QindaOffice::Apps::QindaCalc
