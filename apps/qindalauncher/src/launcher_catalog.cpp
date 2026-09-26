// SPDX-License-Identifier: GPL-3.0-or-later
#include "launcher_catalog.h"

#include "paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QHash>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace QindaOffice::Launcher {

QList<AppEntry> officeApps()
{
    return {
        {QStringLiteral("QindaWrite"), QStringLiteral("org.qindaqt.QindaWrite"),
         QStringLiteral("qindawrite"), QStringLiteral("QindaWrite"),
         QObject::tr("Word processor"),
         QObject::tr("Letters, reports and anything with paragraphs in it."),
         QObject::tr("New document")},
        {QStringLiteral("QindaCalc"), QStringLiteral("org.qindaqt.QindaCalc"),
         QStringLiteral("qindacalc"), QStringLiteral("QindaCalc"),
         QObject::tr("Spreadsheet"),
         QObject::tr("Numbers that add themselves up, and charts of them."),
         QObject::tr("New spreadsheet")},
        {QStringLiteral("QindaShow"), QStringLiteral("org.qindaqt.QindaShow"),
         QStringLiteral("qindashow"), QStringLiteral("QindaShow"),
         QObject::tr("Presentations"),
         QObject::tr("Slides to build, and a full-screen player to give them in."),
         QObject::tr("New deck")},
        {QStringLiteral("QindaNote"), QStringLiteral("org.qindaqt.QindaNote"),
         QStringLiteral("qindanote"), QStringLiteral("QindaNote"),
         QObject::tr("Notes"),
         QObject::tr("Handwriting, typing and checklists on an endless page."),
         QObject::tr("New notebook")},
        {QStringLiteral("QindaMail"), QStringLiteral("org.qindaqt.QindaMail"),
         QStringLiteral("qindamail"), QStringLiteral("QindaMail"),
         QObject::tr("Mail and calendar"),
         QObject::tr("Every inbox and calendar you have, and your tasks, in one place."),
         QObject::tr("New message")},
        {QStringLiteral("QindaBooks"), QStringLiteral("org.qindaqt.QindaBooks"),
         QStringLiteral("qindabooks"), QStringLiteral("QindaBooks"),
         QObject::tr("Bookkeeping"),
         QObject::tr("Invoices, bills, the bank and the reports that add it all up."),
         QObject::tr("New company")},
        {QStringLiteral("QindaDiagram"), QStringLiteral("org.qindaqt.QindaDiagram"),
         QStringLiteral("qindadiagram"), QStringLiteral("QindaDiagram"),
         QObject::tr("Diagrams"),
         QObject::tr("Flowcharts, org charts and network maps whose lines stay attached."),
         QObject::tr("New diagram")},
        {QStringLiteral("QindaPlan"), QStringLiteral("org.qindaqt.QindaPlan"),
         QStringLiteral("qindaplan"), QStringLiteral("QindaPlan"),
         QObject::tr("Project plans"),
         QObject::tr("Tasks, dates and people on a Gantt chart that reschedules itself."),
         QObject::tr("New project")},
        {QStringLiteral("QindaBase"), QStringLiteral("org.qindaqt.QindaBase"),
         QStringLiteral("qindabase"), QStringLiteral("QindaBase"),
         QObject::tr("Database"),
         QObject::tr("Tables, queries, forms and reports, all in one file."),
         QObject::tr("New database")},
    };
}

QString iconSourceFor(const AppEntry &app)
{
    // The mascots live in hicolor, which is where a theme lookup would
    // have found them; a run from a build tree has no installed theme at
    // all, so the data directory beside the binary is tried too rather
    // than showing blank squares.
    const QString leaf = QStringLiteral("icons/hicolor/scalable/apps/%1.svg").arg(app.desktopId);
    QStringList candidates = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, leaf);
    const QDir here(QCoreApplication::applicationDirPath());
    candidates << here.filePath(QStringLiteral("../../data/") + leaf)
               << here.filePath(QStringLiteral("../data/") + leaf);
    for (const QString &candidate : std::as_const(candidates)) {
        if (QFileInfo::exists(candidate)) {
            return QUrl::fromLocalFile(QFileInfo(candidate).absoluteFilePath()).toString();
        }
    }
    return {};
}

// Every document type any of the suite's apps can open, so "Open a
// document…" means what it says and the launcher picks the right app.
QStringList openNameFilters()
{
    return {QObject::tr("All office documents (*.odt *.docx *.doc *.rtf *.html *.htm *.md *.txt "
                        "*.xlsx *.ods *.xls *.xlsm *.csv *.tsv *.odp *.pptx *.fodp *.qbooks "
                        "*.qdiagram *.drawio *.qplan *.qbase *.ics *.eml)"),
            QObject::tr("Documents (*.odt *.docx *.doc *.rtf *.html *.htm *.md *.txt)"),
            QObject::tr("Spreadsheets (*.xlsx *.ods *.xls *.xlsm *.csv *.tsv)"),
            QObject::tr("Presentations (*.odp *.pptx *.fodp)"),
            QObject::tr("Company books (*.qbooks)"),
            QObject::tr("Diagrams (*.qdiagram *.drawio)"),
            QObject::tr("Project plans (*.qplan)"),
            QObject::tr("Databases (*.qbase)"),
            QObject::tr("Calendars and messages (*.ics *.eml)"),
            QObject::tr("All files (*)")};
}

// Which app owns a path. The suffix is the only thing the launcher can go
// on, and it is the same thing every app's own load() dispatches on.
QString appIdForPath(const QString &path)
{
    const QString suffix = path.section(QLatin1Char('.'), -1).toLower();
    static const QHash<QString, QString> kOwners{
        {QStringLiteral("odt"), QStringLiteral("QindaWrite")},
        {QStringLiteral("docx"), QStringLiteral("QindaWrite")},
        {QStringLiteral("doc"), QStringLiteral("QindaWrite")},
        {QStringLiteral("rtf"), QStringLiteral("QindaWrite")},
        {QStringLiteral("html"), QStringLiteral("QindaWrite")},
        {QStringLiteral("htm"), QStringLiteral("QindaWrite")},
        {QStringLiteral("md"), QStringLiteral("QindaWrite")},
        {QStringLiteral("txt"), QStringLiteral("QindaWrite")},
        {QStringLiteral("xlsx"), QStringLiteral("QindaCalc")},
        {QStringLiteral("ods"), QStringLiteral("QindaCalc")},
        {QStringLiteral("xls"), QStringLiteral("QindaCalc")},
        {QStringLiteral("xlsm"), QStringLiteral("QindaCalc")},
        {QStringLiteral("csv"), QStringLiteral("QindaCalc")},
        {QStringLiteral("tsv"), QStringLiteral("QindaCalc")},
        {QStringLiteral("odp"), QStringLiteral("QindaShow")},
        {QStringLiteral("pptx"), QStringLiteral("QindaShow")},
        {QStringLiteral("fodp"), QStringLiteral("QindaShow")},
        {QStringLiteral("qbooks"), QStringLiteral("QindaBooks")},
        {QStringLiteral("qdiagram"), QStringLiteral("QindaDiagram")},
        {QStringLiteral("drawio"), QStringLiteral("QindaDiagram")},
        {QStringLiteral("qplan"), QStringLiteral("QindaPlan")},
        {QStringLiteral("qbase"), QStringLiteral("QindaBase")},
        {QStringLiteral("ics"), QStringLiteral("QindaMail")},
        {QStringLiteral("eml"), QStringLiteral("QindaMail")},
    };
    // A notebook is a DIRECTORY, so anything without a known suffix that is
    // not a file at all belongs to QindaNote.
    return kOwners.value(suffix, QStringLiteral("QindaNote"));
}

QString resolveBinary(const AppEntry &app)
{
    const QString installed = QStandardPaths::findExecutable(app.binary);
    if (!installed.isEmpty()) {
        return installed;
    }
    // A build tree: the app binaries sit beside (or near) the launcher, so
    // the suite is runnable before it is installed.
    const QDir here(QCoreApplication::applicationDirPath());
    for (const QString &candidate :
         {app.binary, QStringLiteral("../%1/%2").arg(app.binary.toLower(), app.binary)}) {
        const QFileInfo info(here.filePath(candidate));
        if (info.isExecutable() && info.isFile()) {
            return info.absoluteFilePath();
        }
    }
    return {};
}

bool launch(const AppEntry &app, const QString &path)
{
    const QString binary = resolveBinary(app);
    if (binary.isEmpty()) {
        return false;
    }
    QStringList arguments;
    if (!path.isEmpty()) {
        arguments << path;
    }
    // Detached: the launcher is a front door, not a parent process — closing
    // it must not take the documents with it.
    return QProcess::startDetached(binary, arguments);
}

QList<RecentEntry> recentDocuments(int limit)
{
    QList<RecentEntry> entries;
    for (const AppEntry &app : officeApps()) {
        const QString path = Core::configDirFor(app.id) + QStringLiteral("/recent.json");
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        // RecentFiles stores a flat array; a file we cannot parse is simply
        // skipped, never rewritten (it is another app's state).
        const QJsonArray array = document.isArray()
            ? document.array()
            : document.object().value(QStringLiteral("paths")).toArray();
        for (const QJsonValue &value : array) {
            const QString recent = value.toString();
            const QFileInfo info(recent);
            // Offering a document that is gone is worse than a shorter list.
            if (recent.isEmpty() || !info.exists()) {
                continue;
            }
            entries.append({recent, info.fileName(), info.absolutePath(), app.id});
        }
    }
    // Newest first across the suite: the files' own modification times are
    // the only ordering the separate per-app lists can be merged on.
    std::sort(entries.begin(), entries.end(), [](const RecentEntry &a, const RecentEntry &b) {
        return QFileInfo(a.path).lastModified() > QFileInfo(b.path).lastModified();
    });
    if (entries.size() > limit) {
        entries.erase(entries.begin() + limit, entries.end());
    }
    return entries;
}

} // namespace QindaOffice::Launcher
