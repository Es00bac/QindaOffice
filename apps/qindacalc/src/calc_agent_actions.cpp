// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc_agent.h"
#include "calc_host.h"
#include "document_tools.h"
#include <QUuid>
namespace QindaOffice::Apps::QindaCalc {
using namespace CalcEng;
namespace {
QJsonObject objectSchema(const QJsonObject &properties,
                         const QJsonArray &required) {
  return {{QStringLiteral("type"), QStringLiteral("object")},
          {QStringLiteral("properties"), properties},
          {QStringLiteral("required"), required},
          {QStringLiteral("additionalProperties"), false}};
}
QJsonObject refused(const QString &message) {
  return {{QStringLiteral("status"), QStringLiteral("validation_failed")},
          {QStringLiteral("message"), message},
          {QStringLiteral("changed"), QJsonArray{}}};
}
QString inputText(const QJsonValue &v) {
  return v.isDouble() ? QString::number(v.toDouble(), 'g', 15)
         : v.isBool()
             ? (v.toBool() ? QStringLiteral("TRUE") : QStringLiteral("FALSE"))
             : v.toString();
}
CellAddr cellAddress(const QString &text) {
  const auto r = parseChartRange(text);
  return r.isValid() && r.topLeft == r.bottomRight ? r.topLeft : CellAddr{};
}
bool bounded(const CellAddr &a) {
  return a.isValid() && a.row < 1048576 && a.col < 16384;
}
} // namespace
QJsonArray CalcAgent::actions() const {
  const QString reason = blockedReason();
  const bool enabled = reason.isEmpty();
  const QJsonObject text{{QStringLiteral("type"), QStringLiteral("string")},
                         {QStringLiteral("maxLength"), 60000}};
  const QJsonObject input{
      {QStringLiteral("anyOf"),
       QJsonArray{
           text,
           QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}},
           QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}}}};
  const auto descriptor = [&](const QString &id, const QString &description,
                              const QJsonObject &schema,
                              bool available = true) {
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("description"), description},
        {QStringLiteral("inputSchema"), schema},
        {QStringLiteral("enabled"), enabled && available},
        {QStringLiteral("disabled_reason"),
         !enabled    ? reason
         : available ? QString()
                     : QStringLiteral("No current agent edit can be undone")}};
  };
  const QJsonObject address{
      {QStringLiteral("type"), QStringLiteral("string")},
      {QStringLiteral("pattern"), QStringLiteral("^[A-Za-z]+[1-9][0-9]*$")},
      {QStringLiteral("maxLength"), 20}};
  return {
      descriptor(
          QStringLiteral("calc.set_cells"),
          QStringLiteral("Set inputs on the active sheet as one validated undo "
                         "step; formulas recalculate"),
          objectSchema(
              {{QStringLiteral("cells"),
                QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("minProperties"), 1},
                            {QStringLiteral("maxProperties"), 20000},
                            {QStringLiteral("additionalProperties"), input}}}},
              QJsonArray{QStringLiteral("cells")})),
      descriptor(
          QStringLiteral("calc.set_range"),
          QStringLiteral("Set rows starting at an active-sheet cell; inputs "
                         "validated before mutation"),
          objectSchema(
              {{QStringLiteral("start"), address},
               {QStringLiteral("rows"),
                QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                            {QStringLiteral("minItems"), 1},
                            {QStringLiteral("maxItems"), 20000},
                            {QStringLiteral("items"),
                             QJsonObject{{QStringLiteral("type"),
                                          QStringLiteral("array")},
                                         {QStringLiteral("minItems"), 1},
                                         {QStringLiteral("maxItems"), 20000},
                                         {QStringLiteral("items"), input}}}}}},
              QJsonArray{QStringLiteral("start"), QStringLiteral("rows")})),
      descriptor(QStringLiteral("calc.clear"),
                 QStringLiteral("Clear active-sheet inputs as one undo step"),
                 objectSchema({{QStringLiteral("range"), text}},
                              QJsonArray{QStringLiteral("range")})),
      descriptor(QStringLiteral("calc.undo"),
                 QStringLiteral("Undo the named agent edit only if no document "
                                "edit has intervened"),
                 objectSchema({{QStringLiteral("undo_ref"), text}},
                              QJsonArray{QStringLiteral("undo_ref")}),
                 !m_undoRef.isEmpty() && m_undoGeneration == generation() &&
                     m_undoIndex == m_document->undoStack()->index())};
}
QJsonObject CalcAgent::execute(const QString &action,
                               const QJsonObject &arguments) {
  if (!blockedReason().isEmpty())
    return refused(blockedReason());
  if (action == QLatin1String("calc.undo")) {
    if (m_undoRef.isEmpty() ||
        arguments.value(QStringLiteral("undo_ref")).toString() != m_undoRef ||
        m_undoGeneration != generation() ||
        m_undoIndex != m_document->undoStack()->index())
      return refused(
          QStringLiteral("The named edit is no longer the current undo step"));
    m_tools->undo();
    m_undoRef.clear();
    return {{QStringLiteral("status"), QStringLiteral("undone")},
            {QStringLiteral("message"), QStringLiteral("Agent edit undone")},
            {QStringLiteral("result"), state()}};
  }
  QList<QPair<CellAddr, QString>> inputs;
  QString op;
  if (action == QLatin1String("calc.set_cells")) {
    op = QStringLiteral("set_cells");
    const auto cells = arguments.value(QStringLiteral("cells")).toObject();
    for (auto it = cells.begin(); it != cells.end(); ++it)
      inputs.append({cellAddress(it.key()), inputText(it.value())});
  } else if (action == QLatin1String("calc.set_range")) {
    op = QStringLiteral("set_range");
    const auto start =
        cellAddress(arguments.value(QStringLiteral("start")).toString());
    if (!bounded(start))
      return refused(QStringLiteral("Invalid start cell"));
    const auto rows = arguments.value(QStringLiteral("rows")).toArray();
    for (qsizetype r = 0; r < rows.size(); ++r) {
      const auto row = rows.at(r).toArray();
      for (qsizetype c = 0; c < row.size(); ++c) {
        if (inputs.size() >= 20000)
          return refused(QStringLiteral("At most 20,000 cells per edit"));
        inputs.append({{start.row + qint32(r), start.col + qint32(c)},
                       inputText(row.at(c))});
      }
    }
  } else if (action == QLatin1String("calc.clear")) {
    op = QStringLiteral("clear");
    const auto range =
        parseChartRange(arguments.value(QStringLiteral("range")).toString());
    if (!range.isValid() || !bounded(range.topLeft) ||
        !bounded(range.bottomRight) || range.cellCount() > 20000)
      return refused(QStringLiteral("Invalid or oversized range"));
    for (qint32 r = range.topLeft.row; r <= range.bottomRight.row; ++r)
      for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c)
        inputs.append({{r, c}, QString()});
  } else
    return refused(QStringLiteral("Unknown Calc action"));
  const auto &sheet =
      m_document->workbook()->sheetAt(m_document->activeSheet());
  QJsonArray changed;
  for (const auto &[address, input] : inputs) {
    if (!bounded(address))
      return refused(
          QStringLiteral("Cell address is outside the worksheet bounds"));
    if (const auto *rule = sheet.validationAt(address);
        rule && !validationAccepts(*rule, input))
      return refused(addrText(address) + QStringLiteral(": ") +
                     validationDescription(*rule));
    changed.append(addrText(address));
  }
  QJsonObject edit = arguments;
  edit.insert(QStringLiteral("op"), op);
  const auto outcome = m_tools->apply(QJsonArray{edit});
  if (outcome.applied == 0)
    return refused(outcome.errors.isEmpty()
                       ? QStringLiteral("No cells changed")
                       : outcome.errors.join(QStringLiteral("; ")));
  m_undoRef = QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_undoGeneration = generation();
  m_undoIndex = m_document->undoStack()->index();
  return {
      {QStringLiteral("status"), outcome.errors.isEmpty()
                                     ? QStringLiteral("completed")
                                     : QStringLiteral("partial")},
      {QStringLiteral("message"), outcome.changes.join(QStringLiteral("; "))},
      {QStringLiteral("changed"), changed},
      {QStringLiteral("errors"), QJsonArray::fromStringList(outcome.errors)},
      {QStringLiteral("undo_ref"), m_undoRef},
      {QStringLiteral("result"), state()}};
}
} // namespace QindaOffice::Apps::QindaCalc
