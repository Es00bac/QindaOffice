// SPDX-License-Identifier: GPL-3.0-or-later
// The stencil catalogue: ids, categories, default sizes, text, roles and
// connection points. Geometry is in stencil_paths.cpp, glyphs in
// stencil_glyphs.cpp.
#include "stencils.h"

#include <QCoreApplication>
#include <QHash>

namespace QindaOffice::Diagram {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("Stencils", text);
}

// Side midpoints: top, right, bottom, left (the org chart and layout code
// rely on this order: 0 top, 2 bottom).
QVector<QPointF> sides()
{
    return {{0.5, 0}, {1, 0.5}, {0.5, 1}, {0, 0.5}};
}

// Sides plus the four corners (rectangular shapes users align to corners).
QVector<QPointF> sidesAndCorners()
{
    return sides() + QVector<QPointF>{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
}

struct Row {
    const char *id;
    const char *category;
    const char *title;
    const char *keywords;
    double width;
    double height;
    const char *text;
    StyleRole role;
    TextPlacement placement;
    int flags; // kContainer | kAspect | kHidden
};

constexpr int kContainer = 1;
constexpr int kAspect = 2;
constexpr int kHidden = 4;

using SR = StyleRole;
using TP = TextPlacement;

// AGENT-CONTRACT: ids are persisted (stencils.h); append, never rename.
constexpr Row kRows[] = {
    // Basic
    {"rectangle", "basic", QT_TRANSLATE_NOOP("Stencils", "Rectangle"), QT_TRANSLATE_NOOP("Stencils", "box square"), 120, 72, "", SR::Primary, TP::Inside, 0},
    {"roundedRectangle", "basic", QT_TRANSLATE_NOOP("Stencils", "Rounded rectangle"), QT_TRANSLATE_NOOP("Stencils", "rounded box"), 120, 72, "", SR::Primary, TP::Inside, 0},
    {"ellipse", "basic", QT_TRANSLATE_NOOP("Stencils", "Ellipse"), QT_TRANSLATE_NOOP("Stencils", "circle oval"), 110, 72, "", SR::Primary, TP::Inside, 0},
    {"circle", "basic", QT_TRANSLATE_NOOP("Stencils", "Circle"), QT_TRANSLATE_NOOP("Stencils", "round"), 72, 72, "", SR::Primary, TP::Inside, kAspect},
    {"diamond", "basic", QT_TRANSLATE_NOOP("Stencils", "Diamond"), QT_TRANSLATE_NOOP("Stencils", "rhombus"), 96, 96, "", SR::Decision, TP::Inside, 0},
    {"triangle", "basic", QT_TRANSLATE_NOOP("Stencils", "Triangle"), QT_TRANSLATE_NOOP("Stencils", "delta"), 96, 84, "", SR::Primary, TP::Inside, 0},
    {"hexagon", "basic", QT_TRANSLATE_NOOP("Stencils", "Hexagon"), "", 110, 80, "", SR::Primary, TP::Inside, 0},
    {"text", "basic", QT_TRANSLATE_NOOP("Stencils", "Text"), QT_TRANSLATE_NOOP("Stencils", "label caption title"), 140, 36, QT_TRANSLATE_NOOP("Stencils", "Text"), SR::Plain, TP::Inside, 0},
    {"note", "basic", QT_TRANSLATE_NOOP("Stencils", "Note"), QT_TRANSLATE_NOOP("Stencils", "sticky comment"), 130, 96, QT_TRANSLATE_NOOP("Stencils", "Note"), SR::Note, TP::Inside, 0},
    // Flowchart
    {"process", "flowchart", QT_TRANSLATE_NOOP("Stencils", "Process"), QT_TRANSLATE_NOOP("Stencils", "step activity task"), 132, 66, QT_TRANSLATE_NOOP("Stencils", "Process"), SR::Primary, TP::Inside, 0},
    {"decision", "flowchart", QT_TRANSLATE_NOOP("Stencils", "Decision"), QT_TRANSLATE_NOOP("Stencils", "choice question branch if"), 132, 88, QT_TRANSLATE_NOOP("Stencils", "Decision?"), SR::Decision, TP::Inside, 0},
    {"terminator", "flowchart", QT_TRANSLATE_NOOP("Stencils", "Start / End"), QT_TRANSLATE_NOOP("Stencils", "terminator begin finish"), 132, 56, QT_TRANSLATE_NOOP("Stencils", "Start"), SR::Terminal, TP::Inside, 0},
    {"data", "flowchart", QT_TRANSLATE_NOOP("Stencils", "Data"), QT_TRANSLATE_NOOP("Stencils", "input output parallelogram"), 132, 66, QT_TRANSLATE_NOOP("Stencils", "Data"), SR::Data, TP::Inside, 0},
    {"document", "flowchart", QT_TRANSLATE_NOOP("Stencils", "Document"), QT_TRANSLATE_NOOP("Stencils", "report paper form"), 132, 76, QT_TRANSLATE_NOOP("Stencils", "Document"), SR::Data, TP::Inside, 0},
    {"predefinedProcess", "flowchart", QT_TRANSLATE_NOOP("Stencils", "Predefined process"), QT_TRANSLATE_NOOP("Stencils", "subroutine subprocess"), 132, 66, QT_TRANSLATE_NOOP("Stencils", "Subprocess"), SR::Primary, TP::Inside, 0},
    {"manualInput", "flowchart", QT_TRANSLATE_NOOP("Stencils", "Manual input"), QT_TRANSLATE_NOOP("Stencils", "keyboard entry form"), 132, 66, QT_TRANSLATE_NOOP("Stencils", "Manual input"), SR::Data, TP::Inside, 0},
    {"onPage", "flowchart", QT_TRANSLATE_NOOP("Stencils", "Connector"), QT_TRANSLATE_NOOP("Stencils", "on-page reference circle"), 44, 44, "A", SR::Neutral, TP::Inside, kAspect},
    {"offPage", "flowchart", QT_TRANSLATE_NOOP("Stencils", "Off-page reference"), QT_TRANSLATE_NOOP("Stencils", "continue link"), 56, 56, "1", SR::Neutral, TP::Inside, 0},
    {"database", "flowchart", QT_TRANSLATE_NOOP("Stencils", "Database"), QT_TRANSLATE_NOOP("Stencils", "stored data cylinder"), 88, 96, QT_TRANSLATE_NOOP("Stencils", "Database"), SR::Data, TP::Inside, 0},
    {"preparation", "flowchart", QT_TRANSLATE_NOOP("Stencils", "Preparation"), QT_TRANSLATE_NOOP("Stencils", "setup hexagon"), 132, 66, QT_TRANSLATE_NOOP("Stencils", "Prepare"), SR::Neutral, TP::Inside, 0},
    // Org chart
    {"orgPerson", "org", QT_TRANSLATE_NOOP("Stencils", "Person"), QT_TRANSLATE_NOOP("Stencils", "position employee staff volunteer card"), 196, 68, QT_TRANSLATE_NOOP("Stencils", "Name\nTitle"), SR::Primary, TP::Card, 0},
    {"orgExecutive", "org", QT_TRANSLATE_NOOP("Stencils", "Executive"), QT_TRANSLATE_NOOP("Stencils", "director manager lead card"), 196, 68, QT_TRANSLATE_NOOP("Stencils", "Name\nExecutive Director"), SR::Accent, TP::Card, 0},
    {"orgVacancy", "org", QT_TRANSLATE_NOOP("Stencils", "Vacancy"), QT_TRANSLATE_NOOP("Stencils", "open position hiring"), 196, 68, QT_TRANSLATE_NOOP("Stencils", "Open position\nTitle"), SR::Neutral, TP::Card, 0},
    {"orgTeam", "org", QT_TRANSLATE_NOOP("Stencils", "Team"), QT_TRANSLATE_NOOP("Stencils", "department group container"), 440, 220, QT_TRANSLATE_NOOP("Stencils", "Team"), SR::Container, TP::Header, kContainer},
    // Network
    {"server", "network", QT_TRANSLATE_NOOP("Stencils", "Server"), QT_TRANSLATE_NOOP("Stencils", "host rack computer"), 64, 64, QT_TRANSLATE_NOOP("Stencils", "Server"), SR::Primary, TP::Below, kAspect},
    {"desktop", "network", QT_TRANSLATE_NOOP("Stencils", "Desktop"), QT_TRANSLATE_NOOP("Stencils", "pc workstation monitor computer"), 64, 64, QT_TRANSLATE_NOOP("Stencils", "Desktop"), SR::Primary, TP::Below, kAspect},
    {"laptop", "network", QT_TRANSLATE_NOOP("Stencils", "Laptop"), QT_TRANSLATE_NOOP("Stencils", "notebook computer"), 64, 64, QT_TRANSLATE_NOOP("Stencils", "Laptop"), SR::Primary, TP::Below, kAspect},
    {"router", "network", QT_TRANSLATE_NOOP("Stencils", "Router"), QT_TRANSLATE_NOOP("Stencils", "gateway wifi modem"), 64, 64, QT_TRANSLATE_NOOP("Stencils", "Router"), SR::Terminal, TP::Below, kAspect},
    {"switch", "network", QT_TRANSLATE_NOOP("Stencils", "Switch"), QT_TRANSLATE_NOOP("Stencils", "hub ethernet"), 64, 64, QT_TRANSLATE_NOOP("Stencils", "Switch"), SR::Terminal, TP::Below, kAspect},
    {"firewall", "network", QT_TRANSLATE_NOOP("Stencils", "Firewall"), QT_TRANSLATE_NOOP("Stencils", "security wall"), 64, 64, QT_TRANSLATE_NOOP("Stencils", "Firewall"), SR::Accent, TP::Below, kAspect},
    {"cloud", "network", QT_TRANSLATE_NOOP("Stencils", "Cloud"), QT_TRANSLATE_NOOP("Stencils", "internet wan"), 120, 76, QT_TRANSLATE_NOOP("Stencils", "Internet"), SR::Neutral, TP::Inside, 0},
    {"networkDatabase", "network", QT_TRANSLATE_NOOP("Stencils", "Database server"), QT_TRANSLATE_NOOP("Stencils", "storage sql nas"), 64, 64, QT_TRANSLATE_NOOP("Stencils", "Database"), SR::Data, TP::Below, kAspect},
    {"printer", "network", QT_TRANSLATE_NOOP("Stencils", "Printer"), QT_TRANSLATE_NOOP("Stencils", "print copier"), 64, 64, QT_TRANSLATE_NOOP("Stencils", "Printer"), SR::Neutral, TP::Below, kAspect},
    {"phone", "network", QT_TRANSLATE_NOOP("Stencils", "Phone"), QT_TRANSLATE_NOOP("Stencils", "mobile smartphone tablet"), 64, 64, QT_TRANSLATE_NOOP("Stencils", "Phone"), SR::Neutral, TP::Below, kAspect},
    // Containers and swimlanes
    {"container", "containers", QT_TRANSLATE_NOOP("Stencils", "Container"), QT_TRANSLATE_NOOP("Stencils", "group frame box"), 360, 240, QT_TRANSLATE_NOOP("Stencils", "Container"), SR::Container, TP::Header, kContainer},
    {"laneHorizontal", "containers", QT_TRANSLATE_NOOP("Stencils", "Swimlane (horizontal)"), QT_TRANSLATE_NOOP("Stencils", "pool row function band"), 720, 150, QT_TRANSLATE_NOOP("Stencils", "Lane"), SR::Container, TP::Header, kContainer},
    {"laneVertical", "containers", QT_TRANSLATE_NOOP("Stencils", "Swimlane (vertical)"), QT_TRANSLATE_NOOP("Stencils", "pool column function band"), 190, 520, QT_TRANSLATE_NOOP("Stencils", "Lane"), SR::Container, TP::Header, kContainer},
    {"phase", "containers", QT_TRANSLATE_NOOP("Stencils", "Phase"), QT_TRANSLATE_NOOP("Stencils", "milestone stage separator"), 240, 480, QT_TRANSLATE_NOOP("Stencils", "Phase"), SR::Neutral, TP::Header, kContainer},
    // UML-lite
    {"umlClass", "uml", QT_TRANSLATE_NOOP("Stencils", "Class"), QT_TRANSLATE_NOOP("Stencils", "uml object type"), 170, 120, QT_TRANSLATE_NOOP("Stencils", "ClassName\n--\n+ name: String\n--\n+ save()"), SR::Primary, TP::Inside, 0},
    {"umlActor", "uml", QT_TRANSLATE_NOOP("Stencils", "Actor"), QT_TRANSLATE_NOOP("Stencils", "user role person stick"), 40, 72, QT_TRANSLATE_NOOP("Stencils", "Actor"), SR::Plain, TP::Below, 0},
    {"umlUseCase", "uml", QT_TRANSLATE_NOOP("Stencils", "Use case"), QT_TRANSLATE_NOOP("Stencils", "uml ellipse goal"), 150, 64, QT_TRANSLATE_NOOP("Stencils", "Use case"), SR::Primary, TP::Inside, 0},
    {"umlPackage", "uml", QT_TRANSLATE_NOOP("Stencils", "Package"), QT_TRANSLATE_NOOP("Stencils", "namespace module folder"), 170, 110, QT_TRANSLATE_NOOP("Stencils", "package"), SR::Neutral, TP::Inside, 0},
    // Arrows and callouts
    {"arrowRight", "arrows", QT_TRANSLATE_NOOP("Stencils", "Arrow right"), QT_TRANSLATE_NOOP("Stencils", "block arrow next"), 120, 64, "", SR::Accent, TP::Inside, 0},
    {"arrowLeft", "arrows", QT_TRANSLATE_NOOP("Stencils", "Arrow left"), QT_TRANSLATE_NOOP("Stencils", "block arrow back"), 120, 64, "", SR::Accent, TP::Inside, 0},
    {"arrowUp", "arrows", QT_TRANSLATE_NOOP("Stencils", "Arrow up"), QT_TRANSLATE_NOOP("Stencils", "block arrow"), 64, 110, "", SR::Accent, TP::Inside, 0},
    {"arrowDown", "arrows", QT_TRANSLATE_NOOP("Stencils", "Arrow down"), QT_TRANSLATE_NOOP("Stencils", "block arrow"), 64, 110, "", SR::Accent, TP::Inside, 0},
    {"arrowDouble", "arrows", QT_TRANSLATE_NOOP("Stencils", "Double arrow"), QT_TRANSLATE_NOOP("Stencils", "both ways exchange"), 140, 64, "", SR::Accent, TP::Inside, 0},
    {"chevron", "arrows", QT_TRANSLATE_NOOP("Stencils", "Chevron"), QT_TRANSLATE_NOOP("Stencils", "step stage process arrow"), 130, 60, "", SR::Primary, TP::Inside, 0},
    {"calloutRect", "arrows", QT_TRANSLATE_NOOP("Stencils", "Callout"), QT_TRANSLATE_NOOP("Stencils", "speech comment balloon"), 150, 90, QT_TRANSLATE_NOOP("Stencils", "Callout"), SR::Note, TP::Inside, 0},
    {"calloutOval", "arrows", QT_TRANSLATE_NOOP("Stencils", "Speech bubble"), QT_TRANSLATE_NOOP("Stencils", "callout talk oval"), 150, 90, QT_TRANSLATE_NOOP("Stencils", "Say something"), SR::Note, TP::Inside, 0},
    // Inserted by commands, not offered in the panel.
    {"picture", "basic", QT_TRANSLATE_NOOP("Stencils", "Picture"), "", 240, 160, "", SR::Plain, TP::Below, kAspect | kHidden},
};

struct Catalogue {
    QVector<Stencil> list;
    QHash<QString, int> index;
};

const Catalogue &catalogue()
{
    static const Catalogue value = [] {
        Catalogue c;
        for (const Row &row : kRows) {
            Stencil s;
            s.id = QString::fromLatin1(row.id);
            s.category = QString::fromLatin1(row.category);
            s.title = tr(row.title);
            s.keywords = *row.keywords ? tr(row.keywords) : QString();
            s.size = QSizeF(row.width, row.height);
            s.defaultText = *row.text ? tr(row.text) : QString();
            s.role = row.role;
            s.text = row.placement;
            s.container = row.flags & kContainer;
            s.aspectLocked = row.flags & kAspect;
            s.palette = !(row.flags & kHidden);
            if (s.id == QLatin1String("triangle")) {
                s.ports = {{0.5, 0}, {0.75, 0.5}, {0.5, 1}, {0.25, 0.5}};
            } else if (s.id == QLatin1String("laneHorizontal") || s.id == QLatin1String("laneVertical")
                       || s.id == QLatin1String("phase")) {
                s.ports = {};
            } else if (s.id == QLatin1String("rectangle") || s.id == QLatin1String("process")
                       || s.id == QLatin1String("container") || s.id == QLatin1String("orgTeam")) {
                s.ports = sidesAndCorners();
            } else {
                s.ports = sides();
            }
            c.index.insert(s.id, int(c.list.size()));
            c.list.append(s);
        }
        return c;
    }();
    return value;
}

} // namespace

const QVector<Stencil> &stencils()
{
    return catalogue().list;
}

const Stencil *stencil(const QString &id)
{
    const Catalogue &c = catalogue();
    const auto it = c.index.constFind(id);
    return it == c.index.constEnd() ? nullptr : &c.list.at(*it);
}

QStringList categoryIds()
{
    return {QStringLiteral("basic"),   QStringLiteral("flowchart"),  QStringLiteral("org"),
            QStringLiteral("network"), QStringLiteral("containers"), QStringLiteral("uml"),
            QStringLiteral("arrows")};
}

QString categoryTitle(const QString &categoryId)
{
    static const QHash<QString, const char *> titles = {
        {QStringLiteral("basic"), QT_TRANSLATE_NOOP("Stencils", "Basic shapes")},
        {QStringLiteral("flowchart"), QT_TRANSLATE_NOOP("Stencils", "Flowchart")},
        {QStringLiteral("org"), QT_TRANSLATE_NOOP("Stencils", "Org chart")},
        {QStringLiteral("network"), QT_TRANSLATE_NOOP("Stencils", "Network")},
        {QStringLiteral("containers"), QT_TRANSLATE_NOOP("Stencils", "Containers & swimlanes")},
        {QStringLiteral("uml"), QT_TRANSLATE_NOOP("Stencils", "UML")},
        {QStringLiteral("arrows"), QT_TRANSLATE_NOOP("Stencils", "Arrows & callouts")},
    };
    const auto it = titles.constFind(categoryId);
    return it == titles.constEnd() ? categoryId : tr(*it);
}

QVector<QPointF> portsOf(const Shape &shape)
{
    const Stencil *s = stencil(shape.type);
    return s != nullptr ? s->ports : sides();
}

bool isContainer(const Shape &shape)
{
    const Stencil *s = stencil(shape.type);
    return s != nullptr && s->container;
}

} // namespace QindaOffice::Diagram
