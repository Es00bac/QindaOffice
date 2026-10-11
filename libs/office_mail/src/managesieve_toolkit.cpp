// SPDX-License-Identifier: GPL-3.0-or-later
#include "managesieve_toolkit.h"
#include "sieve_script.h"
#include <qindatk/manage_sieve_client.h>
#include <QTimer>
namespace QindaOffice::Mail {
using namespace Qt::StringLiterals;
namespace {
class Vacation : public QObject {
public:
    Vacation(QObject *owner,const ManageSieveProvider::Server &server,const QString &secret,bool writing,
             const AutoReply &reply,const QStringList &addresses,std::function<void(QVariantMap)> done)
        : QObject(owner),m_write(writing),m_reply(reply),m_addresses(addresses),m_done(std::move(done))
    {
        m_client.setProperty("host",server.host);m_client.setProperty("port",server.port);
        m_client.setProperty("security",server.security);m_client.setProperty("userName",server.userName);
        m_client.setProperty("timeoutMs",server.timeoutMs);m_client.setProperty("allowInsecureAuth",server.allowInsecureAuth);
        m_client.setSecret(secret);
        connect(&m_client,&QindaTK::ManageSieveClient::finished,this,[this](const QString &,const QVariantMap &result){ receive(result); });
        QTimer::singleShot(0,this,[this]{ command(Capabilities,u"capabilities"_s); });
    }
private:
    enum Stage { Capabilities,List,Get,Check,Put,Activate,Deactivate,Delete } m_stage=Capabilities;
    void command(Stage stage,const QString &name,const QString &scriptName={},const QByteArray &script={}) {
        m_stage=stage; static_cast<void>(m_client.request(u"vacation"_s,name,scriptName,script));
    }
    void finish(QString error={},QVariantMap failure={}) {
        QVariantMap out=m_info;
        if(!failure.isEmpty())out.insert(failure);
        const bool ok=error.isEmpty()&&(failure.isEmpty()||failure.value(u"ok"_s).toBool());
        out.insert(u"ok"_s,ok);out.insert(u"state"_s,ok?u"accepted"_s:failure.value(u"state"_s,u"rejected"_s));
        out.insert(u"error"_s,error.isEmpty()?failure.value(u"error"_s,QString()):error);
        out.insert(u"available"_s,m_available);out.insert(u"changed"_s,m_changed);
        out.insert(u"retryable"_s,out.value(u"state"_s)=="offline"_L1);
        m_client.clearSecret(); const auto done=std::move(m_done); deleteLater(); done(out);
    }
    void receive(const QVariantMap &result) {
        if(!result.value(u"ok"_s).toBool()){finish({},result);return;}
        const auto caps=result.value(u"capabilities"_s).toMap();
        switch(m_stage) {
        case Capabilities:
            m_extensions=caps.value(u"SIEVE"_s).toString().split(u' ',Qt::SkipEmptyParts);
            m_check=caps.value(u"VERSION"_s).toString().toDouble()>=1;
            m_info={{u"extensions"_s,m_extensions},{u"implementation"_s,caps.value(u"IMPLEMENTATION"_s)}};
            if(!m_extensions.contains(u"vacation"_s)){m_available=false;finish(tr("The server's Sieve filters have no vacation extension."));return;}
            command(List,u"list"_s);return;
        case List:
            for(const auto &value:result.value(u"scripts"_s).toList())if(value.toMap().value(u"active"_s).toBool())m_name=value.toMap().value(u"name"_s).toString();
            m_info.insert(u"activeScript"_s,m_name);
            if(m_name.isEmpty())prepare();else command(Get,u"get"_s,m_name);return;
        case Get:m_script=QString::fromUtf8(result.value(u"script"_s).toByteArray());prepare();return;
        case Check:command(Put,u"put"_s,m_target,m_updated.toUtf8());return;
        case Put:
            m_changed=true;
            if(m_name.isEmpty())command(Activate,u"activate"_s,m_target);else finish();return;
        case Activate:m_changed=true;finish();return;
        case Deactivate:command(Delete,u"delete"_s,m_name);return;
        case Delete:m_changed=true;finish();return;
        }
    }
    void prepare() {
        if(!m_write){
            bool found=false; auto reply=Sieve::readManaged(m_script,&found);if(!found)reply.enabled=false;
            m_info.insert(u"settings"_s,AutoReplies::toVariantMap(reply));
            m_info.insert(u"foreignVacation"_s,Sieve::hasForeignVacation(m_script));finish();return;
        }
        m_target=m_name.isEmpty()?QString::fromLatin1(ManageSieveProvider::kScriptName):m_name;
        if(!m_reply.enabled){
            if(m_name.isEmpty()||!Sieve::hasManagedVacation(m_script)){finish();return;}
            m_updated=Sieve::withoutVacation(m_script);
            if(m_name==QLatin1String(ManageSieveProvider::kScriptName)&&m_updated.trimmed().isEmpty()){command(Deactivate,u"activate"_s);return;}
        }else{
            if(m_reply.scheduled&&!(m_extensions.contains(u"date"_s)&&m_extensions.contains(u"relational"_s))){
                finish(tr("This server cannot limit automatic replies to a time range; its Sieve lacks date and relational extensions."));return;
            }
            m_updated=Sieve::withVacation(m_script,m_reply,m_addresses);
            if(m_updated==m_script){finish();return;}
        }
        if(m_check)command(Check,u"check"_s,{},m_updated.toUtf8());else command(Put,u"put"_s,m_target,m_updated.toUtf8());
    }
    QindaTK::ManageSieveClient m_client;
    bool m_write=false,m_check=false,m_available=true,m_changed=false;
    AutoReply m_reply;
    QStringList m_addresses,m_extensions;
    QString m_name,m_target,m_script,m_updated;
    QVariantMap m_info;
    std::function<void(QVariantMap)> m_done;
};
}
void startToolkitVacation(QObject *owner,const ManageSieveProvider::Server &server,const QString &secret,bool write,
                          const AutoReply &reply,const QStringList &addresses,std::function<void(QVariantMap)> done)
{
    new Vacation(owner,server,secret,write,reply,addresses,std::move(done));
}
}
