// SPDX-License-Identifier: GPL-3.0-or-later
// BaseService (M80, D-089 §6–7): reports evaluated by the engine and laid
// out by QindaTK, and mailing labels.
//
// AGENT-NOTE: a report's pages used to come from QindaTK's
// LocalReportDocumentModel, which totals in double precision. The engine
// now evaluates the same report map (base_report.h) with exact money and
// hands the bands to the toolkit's own layout (LocalReport::documentJson),
// so the pages, page bands, preview, print and PDF look as before while
// every total is exact. The ReportDesigner still edits the design document
// over the snapshot (openReport), which only feeds the designer now.
#include "base_document.h"
#include "base_labels.h"
#include "base_objects.h"
#include "base_report.h"
#include "base_service.h"
#include "label_sheet.h"

#include <qindatk/local_report_spec.h>
#include <qindatk/rich_text_codec.h>
#include <qindatk/rich_text_document.h>

namespace QindaOffice::Apps::Base {

namespace Engine = ::QindaOffice::Base;
namespace Report = ::QindaTK::LocalReport;

namespace {

QVariantMap failed(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}

QString s(const char *text)
{
    return QString::fromLatin1(text);
}

Report::Band::Kind toolkitKind(Engine::ReportBand::Kind kind)
{
    using K = Engine::ReportBand::Kind;
    switch (kind) {
    case K::ReportHeader: return Report::Band::Kind::ReportHeader;
    case K::ReportInfo: return Report::Band::Kind::ReportInfo;
    case K::GroupHeader: return Report::Band::Kind::GroupHeader;
    case K::Detail: return Report::Band::Kind::Detail;
    case K::GroupFooter: return Report::Band::Kind::GroupFooter;
    case K::ReportFooter: return Report::Band::Kind::ReportFooter;
    case K::Empty: return Report::Band::Kind::Empty;
    }
    return Report::Band::Kind::Detail;
}

QString kindName(Engine::ReportBand::Kind kind)
{
    static const char *const names[] = {"reportHeader", "reportInfo", "groupHeader", "detail",
                                        "groupFooter",  "reportFooter", "empty"};
    return s(names[int(kind)]);
}

} // namespace

QStringList BaseService::reportNames() const
{
    QStringList out;
    if (file() != nullptr) {
        for (const Engine::SavedObject &o : Engine::ObjectStore::list(*file(), s("report"))) {
            out.append(o.name);
        }
    }
    return out;
}

QVariantList BaseService::labelProducts() const
{
    QVariantList out;
    for (const Engine::LabelProduct &p : Engine::labelProducts()) {
        out.append(p.toMap());
    }
    return out;
}

QVariantMap BaseService::reportInfo(const QString &name) const
{
    Engine::BaseFile *f = file();
    const std::optional<Engine::SavedObject> o = f != nullptr ? Engine::ObjectStore::load(*f, s("report"), name)
                                                              : std::nullopt;
    if (!o) {
        return failed(tr("There is no report named %1.").arg(name));
    }
    const QString type = o->definition.value(s("type")).toString();
    return {{s("ok"), true},
            {s("type"), type == QLatin1String("labels") ? type : s("report")},
            {s("source"), o->definition.value(s("source"))}};
}

QVariantMap BaseService::buildReport(QObject *document, const QVariantMap &report)
{
    auto *pages = qobject_cast<QindaTK::RichTextDocument *>(document);
    Engine::BaseFile *f = file();
    if (pages == nullptr || f == nullptr) {
        return failed(tr("There are no pages to build."));
    }
    if (!m_reportSource) {
        return failed(tr("Open the report first."));
    }
    const Engine::ReportSpec spec = Engine::ReportSpec::fromReportMap(report);
    const Engine::ReportRun run = Engine::runReport(*m_reportSource, spec, f->currency());
    if (!run.ok) {
        return failed(run.error);
    }
    Report::Output output;
    output.columnLabels = run.columnLabels;
    output.detailRows = run.detailRows;
    output.groupCount = run.groupCount;
    QVariantList bands;
    for (const Engine::ReportBand &b : run.bands) {
        output.bands.append({toolkitKind(b.kind), b.level, b.text, b.cells, b.pageBreakBefore, b.strong});
        bands.append(QVariantMap{{s("kind"), kindName(b.kind)}, {s("level"), b.level}, {s("text"), b.text},
                                 {s("cells"), b.cells}, {s("pageBreakBefore"), b.pageBreakBefore}});
    }
    // AGENT-CONTRACT (QindaTK local_report_spec.h): documentJson reads the
    // spec's paper, orientation and page bands only; the bands carry the rest.
    Report::Spec layout;
    layout.title = spec.title;
    layout.footer = spec.footer;
    layout.pageHeader = spec.pageHeader;
    layout.pageFooter = spec.pageFooter;
    layout.paperId = spec.paperId;
    layout.landscape = spec.landscape;
    const QJsonObject json = Report::documentJson(output, layout, pages->documentIdString());
    QindaTK::OperationError error;
    if (!QindaTK::RichTextCodec::fromJsonObject(json, *pages, &error)) {
        return failed(error.message);
    }
    return {{s("ok"), true},
            {s("rowCount"), run.detailRows},
            {s("groupCount"), run.groupCount},
            {s("columns"), run.columnLabels},
            {s("bands"), bands},
            {s("truncated"), m_reportSource->truncated}};
}

QVariantMap BaseService::createLabels(const QString &name, const QVariantMap &spec)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    if (!f->hasSystemTables()) {
        return needsSystemTables();
    }
    const Engine::LabelSpec labels = Engine::LabelSpec::fromMap(spec);
    if (labels.sourceName.isEmpty()) {
        return failed(tr("Choose the table or query the labels print."));
    }
    // The wizard's preview run checks the lines and the sort first, so a bad
    // one is refused before anything is saved (a source that asks for
    // parameters is checked when the labels open).
    const QVariantMap check = previewLabels(spec);
    if (!check.value(s("ok")).toBool() && !check.value(s("needsParameters")).toBool()) {
        return check;
    }
    const QString objectName = name.trimmed().isEmpty()
                                   ? Engine::ObjectStore::uniqueName(*f, s("report"), tr("Labels %1").arg(labels.sourceName))
                                   : name.trimmed();
    QVariantMap def = labels.toMap();
    def.insert(s("title"), objectName);
    Engine::Change change;
    const Engine::Result r = Engine::ObjectStore::save(*f, s("report"), objectName, def, false, &change);
    if (!r.ok) {
        return failed(r.error);
    }
    m_document->pushChange(change);
    return {{s("ok"), true}, {s("name"), objectName}};
}

QVariantMap BaseService::previewLabels(const QVariantMap &spec) const
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    const Engine::LabelSpec labels = Engine::LabelSpec::fromMap(spec);
    const QList<Engine::QueryParameter> parameters = Engine::labelParameters(*f, labels);
    if (!parameters.isEmpty()) {
        // The wizard previews without asking; the labels ask when they open.
        return {{s("ok"), false}, {s("needsParameters"), true},
                {s("error"), tr("These labels ask for %1 when they open.").arg(parameters.first().prompt)}};
    }
    const Engine::LabelRun run = Engine::runLabels(*f, labels, {}, 6);
    if (!run.ok) {
        return failed(run.error);
    }
    QVariantList texts;
    for (const Engine::LabelPlacement &p : run.labels) {
        texts.append(p.lines.join(QLatin1Char('\n')));
    }
    return {{s("ok"), true}, {s("labels"), texts}, {s("product"), run.product.toMap()}};
}

QVariantMap BaseService::openLabels(const QString &name, const QVariantMap &parameters)
{
    Engine::BaseFile *f = file();
    const std::optional<Engine::SavedObject> o = f != nullptr ? Engine::ObjectStore::load(*f, s("report"), name)
                                                              : std::nullopt;
    if (!o) {
        return failed(tr("There is no report named %1.").arg(name));
    }
    const Engine::LabelSpec spec = Engine::LabelSpec::fromMap(o->definition);
    const Engine::LabelRun run = Engine::runLabels(*f, spec, parameters);
    if (!run.ok) {
        return failed(run.error);
    }
    m_labels->setRun(o->name, run, spec.fontSize);
    return {{s("ok"), true},
            {s("pageCount"), run.pageCount},
            {s("records"), run.records},
            {s("truncated"), run.truncated},
            {s("product"), run.product.toMap()},
            {s("table"), spec.sourceName}};
}

} // namespace QindaOffice::Apps::Base
