// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_mailmerge.h"

#include "docx_styles.h"
#include "ooxml_namespaces.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

namespace QindaOffice::Docx {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);
const QString kR = QString::fromUtf16(Ooxml::Ns::R);
const QString kMergeSourceRel =
    QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/mailMergeSource");
// Where QindaWrite keeps a source Word has no relationship for (QindaMail's
// People): a w:docVar beside the excluded records.
const QString kSourceDocVar = QStringLiteral("QindaWriteMergeSource");

struct OpName {
    const char *op;
    const char *sql;
};
// Word's comparison words in its generated queries; "blank" and "contains"
// read as IS NULL / LIKE.
constexpr OpName kOps[] = {{"equal", "="},      {"notEqual", "<>"},     {"lessEqual", "<="}, {"greaterEqual", ">="},
                           {"less", "<"},       {"greater", ">"},       {"blank", "IS NULL"},
                           {"notBlank", "IS NOT NULL"}, {"contains", "LIKE"}, {"notContains", "NOT LIKE"}};

QString quoted(const QString &value)
{
    QString out = value;
    return QLatin1Char('\'') + out.replace(QLatin1Char('\''), QStringLiteral("''")) + QLatin1Char('\'');
}

// The data source path as a relationship target Word reads ("file:///…").
QString sourceTarget(const QString &path)
{
    return QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()).toString(QUrl::FullyEncoded);
}

QString localPath(const QString &target)
{
    const QUrl url(target);
    if (url.isLocalFile()) {
        return url.toLocalFile();
    }
    // Word on Windows writes "file:///C:\Users\…" or a bare path.
    QString path = target;
    if (path.startsWith(u"file:///", Qt::CaseInsensitive)) {
        path = path.mid(8);
    }
    return QUrl::fromPercentEncoding(path.toUtf8());
}

void setVal(XmlElement &parent, const QString &name, const QString &value)
{
    XmlElement &e = parent.appendElement(kW, name);
    e.setAttribute(kW, QStringLiteral("val"), value);
}

QString docVar(const XmlElement *settings, const QString &name)
{
    const XmlElement *vars = settings != nullptr ? settings->child(kW, u"docVars") : nullptr;
    if (vars == nullptr) {
        return {};
    }
    for (const XmlElement *var : vars->elements(kW, u"docVar")) {
        if (wAttr(var, u"name") == name) {
            return wAttr(var, u"val");
        }
    }
    return {};
}

void setDocVar(XmlElement &settings, const QString &name, const QString &value)
{
    XmlElement *vars = settings.child(kW, u"docVars");
    if (vars != nullptr) {
        for (qsizetype i = vars->children.size() - 1; i >= 0; --i) {
            const XmlElementPtr &e = vars->children.at(i).element;
            if (e && e->ns == kW && e->name == u"docVar" && wAttr(e.get(), u"name") == name) {
                vars->children.removeAt(i);
            }
        }
    }
    if (!value.isEmpty()) {
        if (vars == nullptr) {
            vars = &settings.appendElement(kW, QStringLiteral("docVars"));
        }
        XmlElement &var = vars->appendElement(kW, QStringLiteral("docVar"));
        var.setAttribute(kW, QStringLiteral("name"), name);
        var.setAttribute(kW, QStringLiteral("val"), value);
    }
    if (vars != nullptr && vars->elements().isEmpty()) {
        settings.removeChildren(kW, u"docVars");
    }
}

} // namespace

QString wordDocumentType(const QString &kind)
{
    if (kind == u"letters") {
        return QStringLiteral("formLetters");
    }
    if (kind == u"labels") {
        return QStringLiteral("mailingLabels");
    }
    if (kind == u"directory") {
        return QStringLiteral("catalog");
    }
    if (kind == u"email" || kind == u"envelopes") {
        return kind;
    }
    return {};
}

QString mergeKind(const QString &wordType)
{
    if (wordType == u"formLetters" || wordType == u"fax") {
        return QStringLiteral("letters");
    }
    if (wordType == u"mailingLabels") {
        return QStringLiteral("labels");
    }
    if (wordType == u"catalog") {
        return QStringLiteral("directory");
    }
    return wordType;
}

QString mergeQuery(const MailMergeSettings &settings)
{
    QString query = QStringLiteral("SELECT * FROM ");
    query += settings.table.isEmpty() ? settings.dataSource : QStringLiteral("`%1$`").arg(settings.table);
    if (!settings.filters.isEmpty()) {
        QStringList terms;
        for (const MergeFilter &f : settings.filters) {
            QString sql = QStringLiteral("=");
            for (const OpName &op : kOps) {
                if (f.op == QLatin1String(op.op)) {
                    sql = QString::fromLatin1(op.sql);
                }
            }
            QString term = QStringLiteral("(`%1` %2").arg(f.field, sql);
            if (f.op == u"contains" || f.op == u"notContains") {
                term += QLatin1Char(' ') + quoted(QStringLiteral("%") + f.value + QStringLiteral("%"));
            } else if (f.op != u"blank" && f.op != u"notBlank") {
                term += QLatin1Char(' ') + quoted(f.value);
            }
            term += QLatin1Char(')');
            if (!terms.isEmpty()) {
                terms.append(f.join == u"or" ? QStringLiteral("OR") : QStringLiteral("AND"));
            }
            terms.append(term);
        }
        query += QStringLiteral(" WHERE (") + terms.join(QLatin1Char(' ')) + QLatin1Char(')');
    }
    if (!settings.sorts.isEmpty()) {
        QStringList keys;
        for (const MergeSort &s : settings.sorts) {
            keys.append(QStringLiteral("`%1`").arg(s.field) + (s.descending ? QStringLiteral(" DESC") : QString()));
        }
        query += QStringLiteral(" ORDER BY ") + keys.join(QStringLiteral(", "));
    }
    return query;
}

bool parseMergeQuery(const QString &query, QList<MergeFilter> *filters, QList<MergeSort> *sorts)
{
    QList<MergeFilter> outFilters;
    QList<MergeSort> outSorts;
    const qsizetype where = query.indexOf(QStringLiteral(" WHERE ("));
    const qsizetype order = query.indexOf(QStringLiteral(" ORDER BY "));
    if (where >= 0) {
        const qsizetype end = order >= 0 ? order : query.size();
        const QString body = query.mid(where + 8, end - where - 9); // inside the outer ( )
        static const QRegularExpression term(
            QStringLiteral(R"(^\s*(?:(AND|OR)\s+)?\(`([^`]+)`\s+(=|<>|<=|>=|<|>|IS NULL|IS NOT NULL|LIKE|NOT LIKE)\s*('((?:[^']|'')*)')?\))"));
        QString rest = body;
        while (!rest.trimmed().isEmpty()) {
            const QRegularExpressionMatch m = term.match(rest);
            if (!m.hasMatch()) {
                return false;
            }
            MergeFilter f;
            f.join = m.captured(1) == u"OR" ? QStringLiteral("or") : QStringLiteral("and");
            f.field = m.captured(2);
            for (const OpName &op : kOps) {
                if (m.captured(3) == QLatin1String(op.sql)) {
                    f.op = QString::fromLatin1(op.op);
                }
            }
            f.value = m.captured(5).replace(QStringLiteral("''"), QStringLiteral("'"));
            if (f.op == u"contains" || f.op == u"notContains") {
                f.value = f.value.mid(1, f.value.size() - 2);
            }
            outFilters.append(f);
            rest = rest.mid(m.capturedLength());
        }
    }
    if (order >= 0) {
        static const QRegularExpression key(QStringLiteral(R"(^`([^`]+)`(\s+DESC)?$)"));
        for (const QString &part : query.mid(order + 10).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QRegularExpressionMatch m = key.match(part.trimmed());
            if (!m.hasMatch()) {
                return false;
            }
            outSorts.append({m.captured(1), m.hasCaptured(2)});
        }
    }
    *filters = outFilters;
    *sorts = outSorts;
    return true;
}

MailMergeSettings readMailMerge(const Ooxml::Package &package, const QString &settingsPart, const XmlElement *settings)
{
    MailMergeSettings out;
    const XmlElement *merge = settings != nullptr ? settings->child(kW, u"mailMerge") : nullptr;
    if (merge != nullptr) {
        out.documentType = childVal(merge, u"mainDocumentType");
        out.dataType = childVal(merge, u"dataType", out.dataType);
        out.query = childVal(merge, u"query");
        if (const XmlElement *source = merge->child(kW, u"dataSource")) {
            if (const auto rel = package.relationship(settingsPart, source->attribute(kR, u"id"))) {
                out.dataSource = localPath(rel->target);
            }
        }
        static const QRegularExpression sheet(QStringLiteral("FROM `([^`]+)\\$`"));
        if (const QRegularExpressionMatch m = sheet.match(out.query); m.hasMatch()) {
            out.table = m.captured(1);
        }
        static_cast<void>(parseMergeQuery(out.query, &out.filters, &out.sorts));
        out.destination = childVal(merge, u"destination");
        out.addressField = childVal(merge, u"addressFieldName");
        out.mailSubject = childVal(merge, u"mailSubject");
        out.mailAsAttachment = onOff(merge, u"mailAsAttachment");
        out.viewMergedData = onOff(merge, u"viewMergedData");
        out.activeRecord = childVal(merge, u"activeRecord").toInt();
        if (const XmlElement *odso = merge->child(kW, u"odso")) {
            for (const XmlElement *map : odso->elements(kW, u"fieldMapData")) {
                const QString word = childVal(map, u"mappedName");
                const QString column = childVal(map, u"name");
                if (!word.isEmpty() && !column.isEmpty() && childVal(map, u"type") != u"null") {
                    out.fieldMap.insert(word, column);
                }
            }
        }
    }
    if (const QString people = docVar(settings, kSourceDocVar); !people.isEmpty()) {
        out.dataSource = people;
    }
    for (const QString &n : docVar(settings, QString::fromUtf16(kExcludedDocVar)).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool ok = false;
        const int index = n.toInt(&ok);
        if (ok && index >= 0) {
            out.excluded.append(index);
        }
    }
    return out;
}

void writeMailMerge(XmlElement &settings, const MailMergeSettings &merge, const XmlElement *source,
                    Ooxml::PackageWriter &writer, const QString &settingsPart)
{
    settings.removeChildren(kW, u"mailMerge");
    const bool qindaSource = merge.dataSource.startsWith(u"qindamail:");
    setDocVar(settings, kSourceDocVar, qindaSource && merge.isMerge() ? merge.dataSource : QString());
    QStringList excluded;
    for (const int index : merge.excluded) {
        excluded.append(QString::number(index));
    }
    setDocVar(settings, QString::fromUtf16(kExcludedDocVar), merge.isMerge() ? excluded.join(QLatin1Char(',')) : QString());
    if (!merge.isMerge()) {
        return;
    }
    auto e = std::make_shared<XmlElement>(kW, QStringLiteral("mailMerge"));
    auto kept = [source](QStringView name) { return source != nullptr ? source->child(kW, name) : nullptr; };
    // CT_MailMerge sequence (ECMA-376 Part 1 §17.14.20).
    setVal(*e, QStringLiteral("mainDocumentType"), merge.documentType);
    const bool hasSource = !merge.dataSource.isEmpty() && !qindaSource;
    if (hasSource && merge.table.isEmpty() && kept(u"linkToQuery") != nullptr) {
        e->appendElement(kW, QStringLiteral("linkToQuery"));
    }
    if (hasSource) {
        setVal(*e, QStringLiteral("dataType"), merge.table.isEmpty() ? merge.dataType : QStringLiteral("native"));
        if (!merge.table.isEmpty()) {
            setVal(*e, QStringLiteral("connectString"),
                   QStringLiteral("Provider=Microsoft.ACE.OLEDB.12.0;User ID=Admin;Data Source=%1;Mode=Read;"
                                  "Extended Properties=\"HDR=YES;IMEX=1;\";")
                       .arg(merge.dataSource));
        } else if (const XmlElement *connect = kept(u"connectString")) {
            e->appendElement(connect->clone());
        }
        setVal(*e, QStringLiteral("query"), mergeQuery(merge));
        const QString id = writer.addRelationship(settingsPart, kMergeSourceRel, sourceTarget(merge.dataSource), true);
        e->appendElement(kW, QStringLiteral("dataSource")).setAttribute(kR, QStringLiteral("id"), id);
    } else {
        setVal(*e, QStringLiteral("dataType"), merge.dataType);
    }
    if (const XmlElement *blank = kept(u"doNotSuppressBlankLines")) {
        e->appendElement(blank->clone());
    }
    if (!merge.destination.isEmpty()) {
        setVal(*e, QStringLiteral("destination"), merge.destination);
    }
    if (!merge.addressField.isEmpty()) {
        setVal(*e, QStringLiteral("addressFieldName"), merge.addressField);
    }
    if (!merge.mailSubject.isEmpty()) {
        setVal(*e, QStringLiteral("mailSubject"), merge.mailSubject);
    }
    if (merge.mailAsAttachment) {
        e->appendElement(kW, QStringLiteral("mailAsAttachment"));
    }
    if (merge.viewMergedData) {
        e->appendElement(kW, QStringLiteral("viewMergedData"));
    }
    if (merge.activeRecord > 0) {
        setVal(*e, QStringLiteral("activeRecord"), QString::number(merge.activeRecord));
    }
    if (const XmlElement *check = kept(u"checkErrors")) {
        e->appendElement(check->clone());
    }
    if (!merge.fieldMap.isEmpty()) {
        XmlElement &odso = e->appendElement(kW, QStringLiteral("odso"));
        for (auto it = merge.fieldMap.cbegin(); it != merge.fieldMap.cend(); ++it) {
            XmlElement &map = odso.appendElement(kW, QStringLiteral("fieldMapData"));
            setVal(map, QStringLiteral("type"), QStringLiteral("dbColumn"));
            setVal(map, QStringLiteral("name"), it.value());
            setVal(map, QStringLiteral("mappedName"), it.key());
        }
    } else if (const XmlElement *odso = kept(u"odso")) {
        e->appendElement(odso->clone());
    }
    // w:mailMerge sits after w:documentType in CT_Settings; the caller sorts.
    settings.appendElement(e);
}

} // namespace QindaOffice::Docx
