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
    };
}

QIcon iconFor(const AppEntry &app)
{
    // Theme first (AGENTS.md rule 3: icons via QIcon::fromTheme). The
    // suite installs its mascots into hicolor, which every theme falls back
    // to — but a run from a build tree has no installed theme at all, so
    // fall back to the file itself rather than showing four blank squares.
    QIcon icon = QIcon::fromTheme(app.desktopId);
    if (!icon.isNull()) {
        return icon;
    }
    const QString leaf =
        QStringLiteral("icons/hicolor/scalable/apps/%1.svg").arg(app.desktopId);
    QStringList candidates =
        QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, leaf);
    const QDir here(QCoreApplication::applicationDirPath());
    candidates << here.filePath(QStringLiteral("../../data/") + leaf)
               << here.filePath(QStringLiteral("../data/") + leaf);
    for (const QString &candidate : std::as_const(candidates)) {
        if (QFileInfo::exists(candidate)) {
            return QIcon(candidate);
        }
    }
    return {};
}

// Every document type any of the four apps can open, so "Open a
// document…" means what it says and the launcher picks the right app.
QString openFilter()
{
    return QObject::tr(
        "All office documents (*.odt *.docx *.doc *.rtf *.html *.htm *.md *.txt "
        "*.xlsx *.ods *.xls *.xlsm *.csv *.tsv *.odp *.pptx *.fodp);;"
        "Documents (*.odt *.docx *.doc *.rtf *.html *.htm *.md *.txt);;"
        "Spreadsheets (*.xlsx *.ods *.xls *.xlsm *.csv *.tsv);;"
        "Presentations (*.odp *.pptx *.fodp);;"
        "All files (*)");
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
    // A build tree: the four binaries sit beside (or near) the launcher, so
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
    // the only ordering the four separate lists can be merged on.
    std::sort(entries.begin(), entries.end(), [](const RecentEntry &a, const RecentEntry &b) {
        return QFileInfo(a.path).lastModified() > QFileInfo(b.path).lastModified();
    });
    if (entries.size() > limit) {
        entries.erase(entries.begin() + limit, entries.end());
    }
    return entries;
}

} // namespace QindaOffice::Launcher
