// SPDX-License-Identifier: GPL-3.0-or-later
#include "mail_quick_steps.h"
#include "mail_service.h"
#include "mail_organizer.h"
#include "provider_calls.h"
#include "json_settings.h"
#include <qindatk/mail_address_validator.h>
#include <QUuid>
namespace QindaOffice::Apps::Mail {
using namespace Qt::StringLiterals;
namespace {
const QString schema = u"qindaoffice.mail.quicksteps/1"_s;
QString key(const QVariantMap &map, const char *name) { return map.value(QLatin1String(name)).toString(); }
}
MailQuickSteps::MailQuickSteps(MailService *service, const QString &path, QObject *parent)
    : QObject(parent), m_service(service), m_file(std::make_unique<Core::JsonSettings>(path))
{
    if (!m_file->load(&m_error)) m_readOnly = !m_error.isEmpty();
    const QString version = m_file->value(u"schema"_s).toString();
    if (!version.isEmpty() && version != schema) {
        m_readOnly = true; m_error = tr("Quick Steps were saved by a newer version; this file is read-only.");
    }
    connect(service, &MailService::operationFinished, this,
            [this](const QString &id, const QString &state, const QString &error) { answered(id,ProviderCalls::result(id,state,error)); });
    connect(service->organizer(), &MailOrganizer::tagsFinished, this, &MailQuickSteps::answered);
    m_steps = m_file->value(u"steps"_s).toList();
    if (m_steps.size() > 256) { m_readOnly = true; m_error = tr("Too many saved Quick Steps."); }
}
MailQuickSteps::~MailQuickSteps() = default;
QVariantList MailQuickSteps::mailboxes() const
{
    QVariantList result{{QVariantMap{{u"id"_s, u"local"_s}, {u"name"_s, tr("On this computer")}}}};
    for (const auto &value : m_service->accounts()) {
        const auto account = value.toMap();
        result.append(QVariantMap{{u"id"_s, account.value(u"id"_s)},
                                  {u"name"_s, account.value(u"displayName"_s, account.value(u"emailAddress"_s))}});
    }
    return result;
}
QVariantList MailQuickSteps::forMailbox(const QString &mailboxId) const
{
    QVariantList result;
    for (const auto &value : m_steps) if (key(value.toMap(), "mailboxId") == mailboxId) result.append(value);
    return result;
}
QVariantMap MailQuickSteps::newStep(const QString &mailboxId) const
{
    return {{u"id"_s, QUuid::createUuid().toString(QUuid::WithoutBraces)}, {u"mailboxId"_s, mailboxId},
            {u"name"_s, tr("New Quick Step")},
            {u"actions"_s, QVariantList{QVariantMap{{u"kind"_s, u"markRead"_s}, {u"read"_s, true}}}}};
}
QStringList MailQuickSteps::validate(const QVariantMap &step) const
{
    QStringList errors;
    const QString mailbox = key(step, "mailboxId");
    bool known = false;
    for (const auto &value : mailboxes()) known |= key(value.toMap(), "id") == mailbox;
    if (!known) errors.append(tr("Choose an available mailbox."));
    if (key(step, "id").isEmpty() || key(step, "id").size() > 128) errors.append(tr("Invalid Quick Step identity."));
    if (key(step, "name").trimmed().isEmpty() || key(step, "name").size() > 200) errors.append(tr("Enter a name of at most 200 characters."));
    const auto actions = step.value(u"actions"_s).toList();
    if (actions.isEmpty() || actions.size() > 16) errors.append(tr("Choose between one and sixteen actions."));
    const QStringList allowed{u"move"_s,u"copy"_s,u"markRead"_s,u"flag"_s,u"categories"_s,u"replyDraft"_s,u"forwardDraft"_s};
    for (qsizetype i = 0; i < actions.size(); ++i) {
        const auto action = actions[i].toMap(); const QString kind = key(action, "kind");
        if (!allowed.contains(kind)) errors.append(tr("Unsupported action: %1").arg(kind));
        if (kind == "move"_L1 && i != actions.size() - 1) errors.append(tr("Move must be the final action, after drafts and copies."));
        if (kind == "move"_L1 || kind == "copy"_L1) {
            bool found = false;
            for (const auto &folder : m_service->folderTargets(mailbox)) found |= key(folder.toMap(), "key") == key(action,"folderKey");
            if (!found) errors.append(tr("Choose a destination in this mailbox."));
        }
        if (kind == "flag"_L1 && !QStringList{u"today"_s,u"tomorrow"_s,u"thisWeek"_s,u"nextWeek"_s,u"noDate"_s,u"complete"_s,u"clear"_s}.contains(key(action,"preset")))
            errors.append(tr("Choose a follow-up flag."));
        if (kind == "categories"_L1) {
            const auto names = action.value(u"names"_s).toStringList();
            if (names.size() > 64 || (key(action,"mode") != "clear"_L1 && names.isEmpty())) errors.append(tr("Choose categories, or Clear categories."));
            for (const auto &name : names) if (name.trimmed().isEmpty() || name.size() > 200) errors.append(tr("Invalid category name."));
        }
        if (kind == "replyDraft"_L1 || kind == "forwardDraft"_L1) {
            const QString recipients = key(action, "recipients");
            if (recipients.size() > 8192 || key(action,"body").size() > 65536) errors.append(tr("Draft text is too long."));
            if (!recipients.isEmpty()) {
                QindaTK::MailAddressValidator validator;
                for (const auto &entry : validator.parseList(recipients).value(u"recipients"_s).toList())
                    if (!entry.toMap().value(u"valid"_s).toBool()) errors.append(tr("A draft recipient is invalid."));
            }
        }
    }
    return errors;
}
bool MailQuickSteps::persist(const QVariantList &steps)
{
    if (m_readOnly) { emit changed(); return false; }
    const QVariantList before = m_steps;
    m_file->setValue(u"schema"_s, schema); m_file->setValue(u"steps"_s, steps);
    if (!m_file->save(&m_error)) { m_file->setValue(u"steps"_s, before); emit changed(); return false; }
    m_steps = steps; m_error.clear(); ++m_revision; emit changed(); return true;
}
QVariantMap MailQuickSteps::saveStep(const QVariantMap &step)
{
    QStringList errors = validate(step);
    QVariantList next = m_steps;
    int at = -1;
    for (int i=0; i<next.size(); ++i) if (key(next[i].toMap(),"id") == key(step,"id")) at=i;
    if (at < 0 && (next.size() >= 256 || forMailbox(key(step,"mailboxId")).size() >= 32)) errors.append(tr("This mailbox already has 32 Quick Steps."));
    if (!errors.isEmpty()) return {{u"ok"_s,false},{u"errors"_s,errors},{u"error"_s,errors.join(u'\n')}};
    if (at < 0) next.append(step); else next[at] = step;
    const bool ok = persist(next);
    return {{u"ok"_s,ok},{u"id"_s,step.value(u"id"_s)},{u"error"_s,m_error}};
}
bool MailQuickSteps::removeStep(const QString &id)
{
    QVariantList next;
    for (const auto &value : m_steps) if (key(value.toMap(),"id") != id) next.append(value);
    return next.size() != m_steps.size() && persist(next);
}
bool MailQuickSteps::moveStep(const QString &id, int index)
{
    int old = -1;
    for (int i=0; i<m_steps.size(); ++i) if (key(m_steps[i].toMap(),"id") == id) old=i;
    if (old < 0) return false;
    const QString mailbox = key(m_steps[old].toMap(),"mailboxId");
    QVariantList reordered = forMailbox(mailbox);
    int local = -1;
    for (int i=0; i<reordered.size(); ++i) if (key(reordered[i].toMap(),"id") == id) local=i;
    if (index < 0 || index >= reordered.size() || local == index) return false;
    const auto moving = reordered.takeAt(local); reordered.insert(index,moving);
    QVariantList next=m_steps; int cursor=0;
    for (int i=0; i<next.size(); ++i) if (key(next[i].toMap(),"mailboxId") == mailbox) next[i]=reordered[cursor++];
    return persist(next);
}
}
