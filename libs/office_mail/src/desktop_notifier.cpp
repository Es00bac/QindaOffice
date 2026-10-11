// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop_notifier.h"
#include <qindatk/desktop_notifications.h>
#include <QCoreApplication>
#include <QPointer>
namespace QindaOffice::Mail {
using namespace Qt::StringLiterals;
namespace {
DesktopNotifier::Sink &sink() { static DesktopNotifier::Sink instance; return instance; }
QindaTK::DesktopNotifications *notifier()
{
    static QPointer<QindaTK::DesktopNotifications> instance;
    if (!instance && QCoreApplication::instance()) {
        instance = new QindaTK::DesktopNotifications(QCoreApplication::instance());
        instance->setProperty("applicationName",u"QindaMail"_s);
    }
    return instance;
}
}
void DesktopNotifier::prepare() { static_cast<void>(notifier()); }
void DesktopNotifier::setSink(Sink replacement) { sink()=std::move(replacement); }
bool DesktopNotifier::notify(const QString &title,const QString &body)
{
    if (sink()) { sink()(title,body); return true; }
    auto *helper=notifier();
    const QVariantMap hints{{u"desktop-entry"_s,u"org.qindaqt.QindaMail"_s},
                            {u"category"_s,u"email.arrived"_s},{u"suppress-sound"_s,true}};
    return helper && helper->post({},title.left(200),body.left(1000),u"org.qindaqt.QindaMail"_s,{},hints,-1,0);
}
}
