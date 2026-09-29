// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc_agent.h"
#include "calc_host.h"
#include "document_tools.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickItem>
#include <algorithm>
#include <type_traits>
namespace QindaOffice::Apps::QindaCalc {
using namespace CalcEng;
namespace {
QString rangeText(const CellRange &r) {
  return r.topLeft == r.bottomRight
             ? addrText(r.topLeft)
             : addrText(r.topLeft) + QLatin1Char(':') + addrText(r.bottomRight);
}
QJsonValue valueJson(const Value &value) {
  return std::visit(
      [](const auto &v) -> QJsonValue {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate>)
          return QJsonValue::Null;
        else if constexpr (std::is_same_v<T, Err>)
          return QJsonObject{{QStringLiteral("error"), errText(v)}};
        else
          return QJsonValue(v);
      },
      value);
}
} // namespace
CalcAgent::CalcAgent(CalcHost *host, SpreadsheetDocument *document,
                     Ai::DocumentTools *tools)
    : AgentProvider(host), m_host(host), m_document(document), m_tools(tools) {
  connect(document, &Core::Document::contentChanged, this,
          &AgentProvider::touch);
  connect(document, &Core::Document::dirtyChanged, this, &AgentProvider::changed);
  connect(host, &CalcHost::cursorChanged, this, &AgentProvider::changed);
  // Selection, focus and draft input are already in the revision hash.
  // Only content mutations advance generation: moving the cursor must
  // not invalidate the named undo step for an otherwise unchanged sheet.
}
QJsonObject CalcAgent::state() const {
  const auto *book = m_document->workbook();
  const int active = m_document->activeSheet();
  const auto &sheet = book->sheetAt(active);
  const auto *grid = m_host->grid();
  QSet<qint32> visibleRows, visibleColumns;
  QJsonArray rowLabels, columnLabels;
  if (grid) {
    for (qint32 row : grid->visibleRows()) {
      visibleRows.insert(row);
      rowLabels.append(row + 1);
    }
    for (qint32 column : grid->visibleColumns()) {
      visibleColumns.insert(column);
      QString label = addrText({0, column});
      label.chop(1);
      columnLabels.append(label);
    }
  }
  const auto onScreen = [&](const CellAddr &address) {
    return visibleRows.contains(address.row) &&
           visibleColumns.contains(address.col);
  };
  QJsonArray sheets;
  for (int i = 0; i < book->sheetCount(); ++i)
    sheets.append(QJsonObject{{QStringLiteral("index"), i},
                              {QStringLiteral("name"), book->sheetName(i)}});
  QList<CellAddr> addresses;
  for (auto it = sheet.cells().begin(); it != sheet.cells().end(); ++it)
    addresses.append({qint32(it.key() & 0xffffffffu), qint32(it.key() >> 32)});
  std::sort(addresses.begin(), addresses.end(),
            [&](const auto &a, const auto &b) {
              if (onScreen(a) != onScreen(b))
                return onScreen(a);
              return a.row == b.row ? a.col < b.col : a.row < b.row;
            });
  QJsonObject cells;
  int shown = 0, filled = 0, bytes = 0;
  // Keep the receipt below the transport limit even for text-heavy sheets.
  // Omit whole cells, never silently shorten a value the agent may act on.
  constexpr int byteBudget = 1048576;
  for (const auto &address : addresses) {
    const auto *cell = sheet.cellAt(address);
    const auto input = m_document->inputTextAt(address);
    if (input.isEmpty())
      continue;
    ++filled;
    if (shown >= 3000 || input.size() > 60000)
      continue;
    QJsonObject entry{
        {QStringLiteral("input"), input},
        {QStringLiteral("value"), valueJson(cell->value)},
        {QStringLiteral("display"), m_document->displayTextAt(address)},
        {QStringLiteral("on_screen"), onScreen(address)}};
    if (const auto *style = book->style(cell->styleId))
      entry.insert(QStringLiteral("number_format"), style->numberFormat);
    const int entryBytes =
        int(QJsonDocument(entry).toJson(QJsonDocument::Compact).size());
    if (bytes + entryBytes > byteBudget)
      continue;
    bytes += entryBytes;
    cells.insert(addrText(address), entry);
    ++shown;
  }
  QJsonValue draft = QJsonValue::Null;
  if (grid && grid->isEditing()) {
    const auto *editor =
        grid->findChild<QObject *>(QStringLiteral("cellEditor"));
    draft = QJsonObject{
        {QStringLiteral("target"), addrText(grid->cursorCell())},
        {QStringLiteral("text"),
         editor ? editor->property("text").toString() : grid->editorText()}};
  }
  if (grid && m_host->window()) {
    const auto *field = m_host->window()->findChild<QObject *>(QStringLiteral("formulaField"));
    if (field && field->property("text").toString() !=
            m_document->inputTextAt(grid->cursorCell()))
      draft = QJsonObject{
          {QStringLiteral("target"), addrText(grid->cursorCell())},
          {QStringLiteral("text"), field->property("text").toString()}};
  }
  QJsonArray modals;
  if (m_host->window())
    for (const auto *object : m_host->window()->findChildren<QObject *>())
      if (object->property("modal").toBool() &&
          object->property("visible").toBool())
        modals.append(object->objectName());
  QJsonObject allowed;
  const auto filter = sheet.autoFilter();
  for (auto it = filter.allowed.begin(); it != filter.allowed.end(); ++it)
    allowed.insert(QString::number(it.key()),
                   QJsonArray::fromStringList(it.value()));
  QJsonArray validations;
  for (const auto &rule : sheet.validations())
    validations.append(QJsonObject{
        {QStringLiteral("range"), rangeText(rule.range)},
        {QStringLiteral("description"), validationDescription(rule)}});
  return {
      {QStringLiteral("type"), QStringLiteral("spreadsheet")},
      {QStringLiteral("title"), m_document->displayTitle()},
      {QStringLiteral("dirty"), m_document->isDirty()},
      {QStringLiteral("loading"), m_host->isLoading()},
      {QStringLiteral("active_sheet"),
       QJsonObject{{QStringLiteral("index"), active},
                   {QStringLiteral("name"), book->sheetName(active)}}},
      {QStringLiteral("sheets"), sheets},
      {QStringLiteral("cursor"), grid ? QJsonValue(addrText(grid->cursorCell()))
                                      : QJsonValue(QJsonValue::Null)},
      {QStringLiteral("selection"),
       grid ? QJsonValue(rangeText(grid->selectionRange()))
            : QJsonValue(QJsonValue::Null)},
      {QStringLiteral("draft"), draft},
      {QStringLiteral("modals"), modals},
      {QStringLiteral("cells"), cells},
      {QStringLiteral("snapshot_limit"), 3000},
      {QStringLiteral("snapshot_bytes_limit"), byteBudget},
      {QStringLiteral("truncated"), filled > shown},
      {QStringLiteral("omitted_cells"), filled - shown},
      {QStringLiteral("focus"),
       m_host->window() && m_host->window()->activeFocusItem()
           ? m_host->window()->activeFocusItem()->objectName()
           : QString()},
      {QStringLiteral("view"),
       grid ? QJsonValue(QJsonObject{
                  {QStringLiteral("surface"), QStringLiteral("worksheet")},
                  {QStringLiteral("visible_rows"), rowLabels},
                  {QStringLiteral("visible_columns"), columnLabels},
                  {QStringLiteral("zoom"), grid->zoom()},
                  {QStringLiteral("scroll_x"), grid->scrollX()},
                  {QStringLiteral("scroll_y"), grid->scrollY()},
                  {QStringLiteral("viewport_width"), grid->viewportWidth()},
                  {QStringLiteral("viewport_height"), grid->viewportHeight()}})
            : QJsonValue(QJsonValue::Null)},
      {QStringLiteral("filter"),
       QJsonObject{{QStringLiteral("active"), filter.active},
                   {QStringLiteral("range"), filter.range.isValid()
                                                 ? rangeText(filter.range)
                                                 : QString()},
                   {QStringLiteral("allowed_columns"), allowed}}},
      {QStringLiteral("validations"), validations}};
}
QString CalcAgent::blockedReason() const {
  const auto snapshot = state();
  if (m_host->isLoading())
    return QStringLiteral("Document load is in progress");
  if (!snapshot.value(QStringLiteral("draft")).isNull())
    return QStringLiteral(
        "Finish or cancel the current input before an agent edits");
  if (!snapshot.value(QStringLiteral("modals")).toArray().isEmpty())
    return QStringLiteral("Close the active dialog before an agent edits");
  return {};
}
QString CalcAgent::undoRevisionToken() const
{
    return QString::number(generation()) + QLatin1Char('/')
        + QString::number(m_document->undoStack()->index()) + QLatin1Char('/') + blockedReason();
}
} // namespace QindaOffice::Apps::QindaCalc
