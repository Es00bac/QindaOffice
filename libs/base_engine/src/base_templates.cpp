// SPDX-License-Identifier: GPL-3.0-or-later
// The five starter templates, as data: tables (with relationships and
// lookups), saved queries, forms and reports.
#include "base_templates.h"

#include "base_objects.h"
#include "base_query.h"
#include "base_schema.h"

namespace QindaOffice::Base {

namespace {

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

struct F {
    const char *name;
    FieldType type;
    bool required = false;
    const char *defaultValue = "";
    const char *caption = "";
};

struct Link {
    const char *field;
    const char *parent;
    const char *display;
    RefAction onDelete;
};

TableDef makeTable(const char *name, std::initializer_list<F> fields, std::initializer_list<Link> links = {})
{
    TableDef t;
    t.name = s(name);
    t.autoIncrement = true;
    FieldDef id;
    id.name = s("id");
    id.type = FieldType::AutoNumber;
    id.primaryKey = true;
    id.caption = s("ID");
    t.fields.append(id);
    for (const F &f : fields) {
        FieldDef d;
        d.name = s(f.name);
        d.type = f.type;
        d.required = f.required;
        d.defaultValue = s(f.defaultValue);
        d.caption = s(f.caption);
        if (d.caption.isEmpty()) {
            // "first_gift" reads "First gift" in datasheets, forms and reports.
            d.caption = d.name;
            d.caption.replace(QLatin1Char('_'), QLatin1Char(' '));
            d.caption[0] = d.caption.at(0).toUpper();
        }
        t.fields.append(d);
    }
    for (const Link &l : links) {
        Relationship r;
        r.childTable = t.name;
        r.childField = s(l.field);
        r.parentTable = s(l.parent);
        r.parentField = s("id");
        r.onDelete = l.onDelete;
        t.relationships.append(r);
        const int i = t.fieldIndex(r.childField);
        t.fields[i].indexed = true;
        t.fields[i].lookup = {r.parentTable, s("id"), s(l.display)};
    }
    return t;
}

QVariantMap form(const char *table, const char *title, std::initializer_list<const char *> fields)
{
    QVariantList list;
    for (const char *f : fields) {
        list.append(QVariantMap{{s("name"), s(f)}, {s("label"), QString()}, {s("hidden"), false}});
    }
    return {{s("version"), 1},
            {s("source"), QVariantMap{{s("kind"), s("table")}, {s("name"), s(table)}}},
            {s("title"), s(title)},
            {s("fields"), list}};
}

QVariantMap report(const char *kind, const char *source, const char *title,
                   std::initializer_list<std::pair<const char *, const char *>> columns, const char *groupField,
                   const char *totalField, const char *function = "sum")
{
    QVariantList cols;
    for (const auto &[field, label] : columns) {
        cols.append(QVariantMap{{s("field"), s(field)}, {s("label"), s(label)}});
    }
    QVariantList groups;
    if (*groupField != '\0') {
        QVariantList totals;
        if (*totalField != '\0') {
            totals.append(QVariantMap{{s("field"), s(totalField)}, {s("function"), s(function)}});
        }
        groups.append(QVariantMap{{s("field"), s(groupField)}, {s("label"), QString()}, {s("pageBreakBefore"), false},
                                  {s("totals"), totals}});
    }
    QVariantList totals;
    if (*totalField != '\0') {
        totals.append(QVariantMap{{s("field"), s(totalField)}, {s("function"), s(function)}});
    }
    return {{s("version"), 1},
            {s("source"), QVariantMap{{s("kind"), s(kind)}, {s("name"), s(source)}}},
            {s("title"), s(title)},
            {s("columns"), cols},
            {s("groups"), groups},
            {s("totals"), totals},
            {s("detail"), true},
            {s("footer"), QString()},
            {s("orientation"), s("portrait")}};
}

QueryColumn col(const char *table, const char *field, int sort = 0, const char *criteria = "",
                TotalKind total = TotalKind::GroupBy, const char *alias = "")
{
    QueryColumn c;
    c.table = s(table);
    c.field = s(field);
    c.sort = sort;
    c.criteria = s(criteria);
    c.total = total;
    c.alias = s(alias);
    return c;
}

QVariantMap query(std::initializer_list<const char *> tables, std::initializer_list<QueryJoin> joins,
                  std::initializer_list<QueryColumn> columns, bool totals = false, const char *description = "")
{
    QueryDesign d;
    for (const char *t : tables) {
        d.tables.append({s(t), QString()});
    }
    d.joins = joins;
    d.columns = columns;
    d.totals = totals;
    QVariantMap map = d.toMap();
    map.insert(s("description"), s(description));
    return map;
}

QVariantMap sqlQuery(const char *sql, const char *description)
{
    QueryDesign d;
    d.kind = QueryKind::Sql;
    d.sql = s(sql);
    QVariantMap map = d.toMap();
    map.insert(s("description"), s(description));
    return map;
}

QueryJoin join(const char *left, const char *leftField, const char *right, const char *rightField)
{
    return {s(left), s(leftField), s(right), s(rightField), s("inner")};
}

struct Template {
    TemplateInfo info;
    QList<TableDef> tables;
    QList<std::pair<QString, QVariantMap>> queries, forms, reports;
};

QList<Template> all()
{
    const FieldType T = FieldType::Text, L = FieldType::LongText, I = FieldType::Integer, N = FieldType::Number,
                    C = FieldType::Currency, Y = FieldType::YesNo, D = FieldType::Date;
    QList<Template> out;

    Template contacts;
    contacts.info = TemplateInfo{s("contacts"), s("Contacts"), s("People and organizations, with categories and notes."),
                       s("contact")};
    contacts.tables = {makeTable("contacts", {{"first_name", T, true, "", "First name"}, {"last_name", T, true, "", "Last name"},
                                              {"organization", T}, {"email", T}, {"phone", T}, {"street", T},
                                              {"city", T}, {"region", T}, {"postal_code", T, false, "", "Postal code"},
                                              {"category", T}, {"notes", L}, {"added", D, false, "Date()"}})};
    contacts.queries = {{s("Contacts by Category"),
                         query({"contacts"}, {}, {col("contacts", "category", 1), col("contacts", "last_name", 1),
                                                  col("contacts", "first_name"), col("contacts", "email"),
                                                  col("contacts", "phone")},
                               false, "Everyone, sorted by category and last name")}};
    contacts.forms = {{s("Contact Details"), form("contacts", "Contact details",
                                                  {"first_name", "last_name", "organization", "email", "phone", "street",
                                                   "city", "region", "postal_code", "category", "notes", "added"})}};
    contacts.reports = {{s("Contact Directory"),
                         report("table", "contacts", "Contact directory",
                                {{"last_name", "Last name"}, {"first_name", "First name"}, {"organization", "Organization"},
                                 {"email", "Email"}, {"phone", "Phone"}},
                                "category", "")}};
    out.append(contacts);

    Template donors;
    donors.info = TemplateInfo{s("donors"), s("Donors & Donations"),
                     s("Donors, campaigns and every gift, with totals by donor and by campaign."), s("hand-coins")};
    donors.tables = {
        makeTable("donors", {{"name", T, true}, {"email", T}, {"phone", T}, {"address", T}, {"city", T},
                             {"donor_type", T, false, "Individual", "Donor type"}, {"first_gift", D, false, "", "First gift"},
                             {"notes", L}}),
        makeTable("campaigns", {{"name", T, true}, {"start_date", D, false, "", "Start"}, {"goal", C}}),
        makeTable("donations", {{"donor_id", I, true, "", "Donor"}, {"campaign_id", I, false, "", "Campaign"},
                                {"amount", C, true}, {"given", D, true, "Date()"}, {"method", T, false, "Check"},
                                {"receipt_sent", Y, false, "No", "Receipt sent"}, {"notes", L}},
                  {{"donor_id", "donors", "name", RefAction::Restrict},
                   {"campaign_id", "campaigns", "name", RefAction::SetNull}})};
    donors.queries = {
        {s("Donation Details"),
         query({"donations", "donors"}, {join("donations", "donor_id", "donors", "id")},
               {col("donors", "name", 1, "", TotalKind::GroupBy, "Donor"), col("donations", "given", 1),
                col("donations", "amount"), col("donations", "method"), col("donations", "receipt_sent")},
               false, "Every donation with its donor")},
        {s("Totals by Donor"),
         query({"donations", "donors"}, {join("donations", "donor_id", "donors", "id")},
               {col("donors", "name", 1, "", TotalKind::GroupBy, "Donor"),
                col("donations", "amount", 0, "", TotalKind::Sum, "Total given"),
                col("donations", "id", 0, "", TotalKind::Count, "Gifts"),
                col("donations", "given", 0, "", TotalKind::Max, "Last gift")},
               true, "Total, count and last gift per donor")},
        {s("Donations in a Period"),
         query({"donations", "donors"}, {join("donations", "donor_id", "donors", "id")},
               {col("donations", "given", 1, "Between [Start date] And [End date]"),
                col("donors", "name", 0, "", TotalKind::GroupBy, "Donor"), col("donations", "amount")},
               false, "Asks for a start and an end date")}};
    donors.forms = {{s("Donor"), form("donors", "Donor",
                                      {"name", "donor_type", "email", "phone", "address", "city", "first_gift", "notes"})},
                    {s("Donation Entry"), form("donations", "Donation",
                                               {"donor_id", "campaign_id", "amount", "given", "method", "receipt_sent",
                                                "notes"})}};
    donors.reports = {{s("Donations by Donor"),
                       report("query", "Donation Details", "Donations by donor",
                              {{"given", "Date"}, {"amount", "Amount"}, {"method", "Method"}}, "Donor", "amount")}};
    out.append(donors);

    Template volunteers;
    volunteers.info = TemplateInfo{s("volunteers"), s("Volunteers & Shifts"),
                         s("Volunteers, the shifts you need covered, and who signed up."), s("users")};
    volunteers.tables = {
        makeTable("volunteers", {{"name", T, true}, {"email", T}, {"phone", T}, {"skills", L},
                                 {"active", Y, false, "Yes"}, {"joined", D, false, "Date()"}}),
        makeTable("shifts", {{"title", T, true}, {"shift_date", D, true, "", "Date"}, {"start_time", T, false, "", "Starts"},
                             {"hours", N, false, "2"}, {"location", T}, {"needed", I, false, "2", "Volunteers needed"}}),
        makeTable("signups", {{"shift_id", I, true, "", "Shift"}, {"volunteer_id", I, true, "", "Volunteer"},
                              {"confirmed", Y, false, "No"}, {"hours_worked", N, false, "", "Hours worked"}},
                  {{"shift_id", "shifts", "title", RefAction::Cascade},
                   {"volunteer_id", "volunteers", "name", RefAction::Cascade}})};
    volunteers.queries = {
        {s("Volunteer Hours"),
         query({"signups", "volunteers"}, {join("signups", "volunteer_id", "volunteers", "id")},
               {col("volunteers", "name", 1, "", TotalKind::GroupBy, "Volunteer"),
                col("signups", "hours_worked", 0, "", TotalKind::Sum, "Hours"),
                col("signups", "id", 0, "", TotalKind::Count, "Shifts")},
               true, "Hours worked per volunteer")},
        {s("Shift Roster"),
         sqlQuery("SELECT shifts.shift_date AS \"Date\", shifts.title AS \"Shift\", shifts.location AS \"Location\", "
                  "volunteers.name AS \"Volunteer\", signups.confirmed AS \"Confirmed\"\n"
                  "FROM signups\n  JOIN shifts ON shifts.id = signups.shift_id\n"
                  "  JOIN volunteers ON volunteers.id = signups.volunteer_id\nORDER BY shifts.shift_date, shifts.title",
                  "Who is on which shift (three tables, written in SQL)")}};
    volunteers.forms = {{s("Volunteer"), form("volunteers", "Volunteer",
                                              {"name", "email", "phone", "skills", "active", "joined"})}};
    volunteers.reports = {{s("Shift Roster"),
                           report("query", "Shift Roster", "Shift roster",
                                  {{"Volunteer", "Volunteer"}, {"Location", "Location"}, {"Confirmed", "Confirmed"}},
                                  "Shift", "")}};
    out.append(volunteers);

    Template inventory;
    inventory.info = TemplateInfo{s("inventory"), s("Inventory"),
                        s("Items, suppliers and stock movements, with a reorder list."), s("package")};
    inventory.tables = {
        makeTable("suppliers", {{"name", T, true}, {"contact", T}, {"email", T}, {"phone", T}}),
        makeTable("items", {{"sku", T, true, "", "SKU"}, {"name", T, true}, {"category", T},
                            {"supplier_id", I, false, "", "Supplier"}, {"unit_cost", C, false, "", "Unit cost"},
                            {"price", C}, {"on_hand", I, false, "0", "On hand"},
                            {"reorder_level", I, false, "0", "Reorder at"}, {"location", T}},
                  {{"supplier_id", "suppliers", "name", RefAction::SetNull}}),
        makeTable("stock_moves", {{"item_id", I, true, "", "Item"}, {"moved", D, true, "Date()", "Date"},
                                  {"quantity", I, true}, {"reason", T}},
                  {{"item_id", "items", "name", RefAction::Cascade}})};
    inventory.tables[1].fields[1].unique = true;
    inventory.queries = {{s("Items to Reorder"),
                          query({"items"}, {},
                                {col("items", "sku", 1), col("items", "name"), col("items", "on_hand", 0, "<= [reorder_level]"),
                                 col("items", "reorder_level")},
                                false, "Items at or below their reorder level")}};
    inventory.forms = {{s("Item"), form("items", "Item",
                                        {"sku", "name", "category", "supplier_id", "unit_cost", "price", "on_hand",
                                         "reorder_level", "location"})}};
    inventory.reports = {{s("Inventory by Category"),
                          report("table", "items", "Inventory by category",
                                 {{"sku", "SKU"}, {"name", "Item"}, {"on_hand", "On hand"}, {"price", "Price"}}, "category",
                                 "on_hand")}};
    out.append(inventory);

    Template projects;
    projects.info = TemplateInfo{s("projects"), s("Projects & Tasks"),
                       s("Clients, their projects, and the tasks that finish them."), s("list-checks")};
    projects.tables = {
        makeTable("clients", {{"name", T, true}, {"contact", T}, {"email", T}, {"phone", T}}),
        makeTable("projects", {{"name", T, true}, {"client_id", I, false, "", "Client"}, {"status", T, false, "Planned"},
                               {"start_date", D, false, "", "Start"}, {"due_date", D, false, "", "Due"}, {"budget", C}},
                  {{"client_id", "clients", "name", RefAction::Restrict}}),
        makeTable("tasks", {{"project_id", I, true, "", "Project"}, {"title", T, true}, {"assigned_to", T, false, "", "Assigned to"},
                            {"due", D}, {"hours_estimated", N, false, "", "Estimate (h)"}, {"done", Y, false, "No"}},
                  {{"project_id", "projects", "name", RefAction::Cascade}})};
    projects.queries = {{s("Open Tasks"),
                         query({"tasks", "projects"}, {join("tasks", "project_id", "projects", "id")},
                               {col("projects", "name", 0, "", TotalKind::GroupBy, "Project"), col("tasks", "title"),
                                col("tasks", "assigned_to"), col("tasks", "due", 1), col("tasks", "done", 0, "No")},
                               false, "Tasks not done yet, soonest first")}};
    projects.forms = {{s("Project"), form("projects", "Project",
                                          {"name", "client_id", "status", "start_date", "due_date", "budget"})}};
    projects.reports = {{s("Tasks by Project"),
                         report("query", "Open Tasks", "Open tasks by project",
                                {{"title", "Task"}, {"assigned_to", "Assigned to"}, {"due", "Due"}}, "Project", "")}};
    out.append(projects);
    return out;
}

} // namespace

QVariantMap TemplateInfo::toMap() const
{
    return {{s("id"), id}, {s("name"), name}, {s("description"), description}, {s("iconName"), iconName}};
}

QList<TemplateInfo> templates()
{
    QList<TemplateInfo> out;
    for (const Template &t : all()) {
        out.append(t.info);
    }
    return out;
}

Result applyTemplate(BaseFile &file, const QString &id)
{
    for (const Template &t : all()) {
        if (t.info.id != id) {
            continue;
        }
        Result r = file.ensureSystemTables();
        for (const TableDef &table : t.tables) {
            if (r.ok) {
                r = file.applyPlan(planCreateTable(file, table), nullptr);
            }
        }
        const auto save = [&](const QString &kind, const QList<std::pair<QString, QVariantMap>> &objects) {
            for (const auto &[name, definition] : objects) {
                if (r.ok) {
                    r = ObjectStore::save(file, kind, name, definition, true, nullptr);
                }
            }
        };
        save(QStringLiteral("query"), t.queries);
        save(QStringLiteral("form"), t.forms);
        save(QStringLiteral("report"), t.reports);
        if (r.ok) {
            r = file.setSetting(QStringLiteral("template"), id);
        }
        return r;
    }
    return Result::failure(QStringLiteral("There is no template %1.").arg(id));
}

} // namespace QindaOffice::Base
