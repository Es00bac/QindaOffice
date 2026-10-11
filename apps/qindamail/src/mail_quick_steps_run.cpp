// SPDX-License-Identifier: GPL-3.0-or-later
#include "mail_quick_steps.h"
#include "mail_service.h"
#include "mail_organizer.h"
#include "provider_calls.h"
#include <qindatk/local_productivity_store.h>
#include <QTimer>
namespace QindaOffice::Apps::Mail {
using namespace Qt::StringLiterals;
QString MailQuickSteps::execute(const QString &id, const QStringList &messageIds)
{
    const QString request = ProviderCalls::newRequestId(u"quickstep"_s);
    QVariantMap step;
    for (const auto &value : m_steps) if (value.toMap().value(u"id"_s).toString() == id) step=value.toMap();
    QStringList errors = step.isEmpty() ? QStringList{tr("The Quick Step no longer exists.")} : validate(step);
    if (busy()) errors.append(tr("A Quick Step is already running."));
    if (messageIds.isEmpty() || messageIds.size() > 1000) errors.append(tr("Select between one and 1000 messages."));
    QStringList ids = messageIds; ids.removeDuplicates();
    for (const auto &message : ids) {
        const auto record = m_service->messageById(message);
        if (record.isEmpty() || record.value(u"isDraft"_s).toBool()) errors.append(tr("Select received messages, not drafts."));
        if (m_service->organizer()->accountOf(u"message"_s,message) != step.value(u"mailboxId"_s).toString())
            errors.append(tr("Every selected message must belong to this Quick Step's mailbox."));
    }
    if (!errors.isEmpty()) {
        const QVariantMap rejected{{u"state"_s,u"rejected"_s},{u"error"_s,errors.join(u'\n')},{u"completed"_s,0}};
        QTimer::singleShot(0,this,[this,request,rejected] { emit finished(request,rejected); });
        return request;
    }
    m_requestId=request; m_messages=ids; m_actions=step.value(u"actions"_s).toList();
    m_results.clear(); m_messageIndex=0; m_actionIndex=0; m_cancel=false;
    m_outcome={{u"state"_s,u"running"_s},{u"name"_s,step.value(u"name"_s)},{u"completed"_s,0},
               {u"total"_s,m_messages.size()*m_actions.size()},{u"results"_s,QVariantList()}};
    emit runChanged(); QTimer::singleShot(0,this,&MailQuickSteps::next); return request;
}
void MailQuickSteps::cancel()
{
    if (!busy() || m_cancel) return;
    m_cancel=true; emit runChanged();
}
void MailQuickSteps::next()
{
    if (m_cancel) { complete(u"cancelled"_s,tr("Stopped after the current action; completed actions remain applied.")); return; }
    if (m_messageIndex >= m_messages.size()) { complete(u"accepted"_s); return; }
    const QString request=m_requestId, message=m_messages[m_messageIndex];
    const QVariantMap action=m_actions[m_actionIndex].toMap();
    QPointer<MailQuickSteps> self(this);
    perform(message,action,[self,request,message,action](QVariantMap result) {
        if (!self || self->m_requestId != request) return;
        result.insert(u"messageId"_s,message); result.insert(u"action"_s,action.value(u"kind"_s));
        self->m_results.append(result); self->m_outcome.insert(u"results"_s,self->m_results);
        const QString state=result.value(u"state"_s,u"rejected"_s).toString();
        if (state != "accepted"_L1) { self->complete(state,result.value(u"error"_s).toString()); return; }
        self->m_outcome.insert(u"completed"_s,self->m_results.size());
        if (++self->m_actionIndex >= self->m_actions.size()) { self->m_actionIndex=0; ++self->m_messageIndex; }
        emit self->runChanged(); QTimer::singleShot(0,self,&MailQuickSteps::next);
    });
}
void MailQuickSteps::complete(const QString &state, const QString &error)
{
    const QString request=m_requestId;
    m_requestId.clear(); m_cancel=false;
    m_outcome.insert(u"state"_s,state); m_outcome.insert(u"error"_s,error);
    emit runChanged(); emit finished(request,m_outcome);
}
void MailQuickSteps::waitFor(const QString &requestId, Done done) { m_waiting.insert(requestId,std::move(done)); }
void MailQuickSteps::answered(const QString &requestId, const QVariantMap &result)
{
    const auto done=m_waiting.take(requestId); if (done) done(result);
}
QVariantMap MailQuickSteps::draft(const QString &id) const
{
    for (const auto &value : m_service->store()->drafts()) if (value.toMap().value(u"id"_s).toString() == id) return value.toMap();
    return {};
}
void MailQuickSteps::perform(const QString &message, const QVariantMap &action, Done done)
{
    const QString kind=action.value(u"kind"_s).toString();
    auto *organizer=m_service->organizer();
    if (kind == "move"_L1) waitFor(m_service->moveTo({message},action.value(u"folderKey"_s).toString()),std::move(done));
    else if (kind == "copy"_L1) organizer->copyMessage(message,action.value(u"folderKey"_s).toString(),std::move(done));
    else if (kind == "markRead"_L1) waitFor(m_service->markRead({message},action.value(u"read"_s,true).toBool()),std::move(done));
    else if (kind == "flag"_L1) waitFor(organizer->followUp({message},action.value(u"preset"_s).toString()),std::move(done));
    else if (kind == "categories"_L1) {
        QStringList names=action.value(u"names"_s).toStringList();
        const QString mode=action.value(u"mode"_s,u"add"_s).toString();
        if (mode == "clear"_L1) names.clear();
        else if (mode == "add"_L1) names += organizer->categoriesOf(u"message"_s,message);
        waitFor(organizer->setCategories(u"message"_s,{message},names),std::move(done));
    } else if (kind == "replyDraft"_L1 || kind == "forwardDraft"_L1) {
        QVariantMap prepared = kind == "replyDraft"_L1 ? m_service->replyDraft(message,action.value(u"replyAll"_s).toBool())
                                                       : m_service->forwardDraft(message);
        if (prepared.isEmpty()) { done(ProviderCalls::result({},u"rejected"_s,tr("A draft could not be created."))); return; }
        if (!action.value(u"recipients"_s).toString().isEmpty()) prepared.insert(u"to"_s,action.value(u"recipients"_s));
        if (!action.value(u"body"_s).toString().isEmpty()) prepared.insert(u"body"_s,action.value(u"body"_s).toString()+u"\n\n"_s+prepared.value(u"body"_s).toString());
        prepared.insert(u"queued"_s,false);
        const auto saved=m_service->saveDraft(prepared);
        QVariantMap result=ProviderCalls::result({},saved.value(u"ok"_s).toBool()?u"accepted"_s:u"rejected"_s,saved.value(u"error"_s).toString());
        result.insert(u"draftId"_s,saved.value(u"id"_s)); done(result);
    } else done(ProviderCalls::result({},u"rejected"_s,tr("Unsupported Quick Step action.")));
}
}
