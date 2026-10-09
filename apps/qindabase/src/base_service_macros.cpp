// SPDX-License-Identifier: GPL-3.0-or-later
// BaseService (M80, D-089 §5): macros — the action catalog, a macro's
// design, the checks it passes, saving it as one undo step — and the where
// conditions OpenForm and ApplyFilter carry. The window runs the actions
// (MacroRunner.qml); nothing here runs one.
#include "base_document.h"
#include "base_macro.h"
#include "base_objects.h"
#include "base_service.h"

namespace QindaOffice::Apps::Base {

namespace Engine = ::QindaOffice::Base;

namespace {

QVariantMap failed(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}

QString s(const char *text)
{
    return QString::fromLatin1(text);
}

} // namespace

QStringList BaseService::macroNames() const
{
    QStringList out;
    if (file() != nullptr) {
        for (const Engine::SavedObject &o : Engine::ObjectStore::list(*file(), s("macro"))) {
            out.append(o.name);
        }
    }
    return out;
}

QStringList BaseService::formNames() const
{
    QStringList out;
    if (file() != nullptr) {
        for (const Engine::SavedObject &o : Engine::ObjectStore::list(*file(), s("form"))) {
            out.append(o.name);
        }
    }
    return out;
}

QVariantList BaseService::macroActions() const
{
    QVariantList out;
    for (const Engine::MacroActionInfo &a : Engine::macroActions()) {
        out.append(a.toMap());
    }
    return out;
}

QVariantMap BaseService::macroDesign(const QString &name) const
{
    Engine::BaseFile *f = file();
    const std::optional<Engine::SavedObject> o = f != nullptr ? Engine::ObjectStore::load(*f, s("macro"), name)
                                                              : std::nullopt;
    if (!o) {
        return failed(tr("There is no macro named %1.").arg(name));
    }
    QVariantMap map = Engine::Macro::fromMap(o->definition).toMap();
    map.insert(s("ok"), true);
    map.insert(s("name"), o->name);
    return map;
}

QVariantMap BaseService::newMacroDesign() const
{
    QVariantMap map = Engine::Macro().toMap();
    map.insert(s("ok"), true);
    map.insert(s("name"), QString());
    return map;
}

QVariantMap BaseService::checkMacro(const QVariantMap &macro, const QString &name) const
{
    if (file() == nullptr) {
        return failed(tr("No database is open."));
    }
    const QStringList problems = Engine::checkMacro(*file(), Engine::Macro::fromMap(macro), name);
    return {{s("ok"), problems.isEmpty()}, {s("problems"), problems},
            {s("error"), problems.isEmpty() ? QString() : problems.first()}};
}

QVariantMap BaseService::saveMacro(const QString &name, const QVariantMap &macro)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    if (!f->hasSystemTables()) {
        return needsSystemTables();
    }
    const QString target = name.trimmed();
    // A macro is saved even with problems (Access saves an unfinished
    // macro too); running it reports them. A loop onto itself is refused.
    const Engine::Macro parsed = Engine::Macro::fromMap(macro);
    for (const Engine::MacroStep &step : parsed.steps) {
        if (step.action == QLatin1String("runMacro")
            && step.arguments.value(s("macro")).toString().compare(target, Qt::CaseInsensitive) == 0) {
            return failed(tr("%1 cannot run itself.").arg(target));
        }
    }
    QVariantMap definition;
    if (const auto existing = Engine::ObjectStore::load(*f, s("macro"), target)) {
        definition = existing->definition; // members a newer version wrote survive
    }
    const QVariantMap known = parsed.toMap();
    for (auto it = known.cbegin(); it != known.cend(); ++it) {
        definition.insert(it.key(), it.value());
    }
    Engine::Change change;
    const Engine::Result r = Engine::ObjectStore::save(*f, s("macro"), target, definition, true, &change);
    if (!r.ok) {
        return failed(r.error);
    }
    m_document->pushChange(change);
    return {{s("ok"), true}, {s("name"), target}};
}

QVariantMap BaseService::whereRowIds(const QString &table, const QString &condition) const
{
    if (file() == nullptr) {
        return failed(tr("No database is open."));
    }
    QString error;
    const std::optional<QList<qint64>> rows = Engine::filteredRowIds(*file(), table, condition, &error);
    if (!rows) {
        return failed(tr("The condition \"%1\" cannot be used: %2").arg(condition, error));
    }
    QVariantList ids;
    for (const qint64 id : *rows) {
        ids.append(id);
    }
    return {{s("ok"), true}, {s("rowIds"), ids}};
}

} // namespace QindaOffice::Apps::Base
