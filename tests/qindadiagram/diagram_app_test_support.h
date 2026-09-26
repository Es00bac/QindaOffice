// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared fixture for the QindaDiagram app tests: the host, and the three
// drawings the UI test seeds through the real DiagramService — a volunteer
// onboarding flowchart across swimlanes, the org chart of a small nonprofit
// (from CSV, as a user would build it) and a small office network.

#include "qindadiagram/src/diagram_host.h"
#include "qindadiagram/src/diagram_service.h"

#include <QGuiApplication>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

namespace DiagramAppTest {

using QindaOffice::Apps::Diagram::DiagramHost;
using QindaOffice::Apps::Diagram::DiagramService;

inline QString str(const char *text)
{
    return QString::fromUtf8(text);
}

inline QVariantMap end(const QString &shape, int port)
{
    return {{str("shapeId"), shape}, {str("port"), port}, {str("x"), 0}, {str("y"), 0}};
}

inline QString link(DiagramService *d, const QString &a, int pa, const QString &b, int pb, const char *label = nullptr)
{
    const QString id = d->connectItems(end(a, pa), end(b, pb));
    if (label != nullptr && !id.isEmpty()) {
        d->setText(id, str(label));
    }
    return id;
}

inline QString shape(DiagramService *d, const char *type, double x, double y, const char *text)
{
    const QString id = d->addShape(str(type), x, y);
    if (text != nullptr) {
        d->setText(id, str(text));
    }
    return id;
}

// Page 1: onboarding a volunteer, across three swimlanes.
inline void seedFlowchart(DiagramService *d)
{
    d->renamePage({}, str("Volunteer onboarding"));
    const char *lanes[] = {"Volunteer", "Coordinator", "Program lead"};
    for (int i = 0; i < 3; ++i) {
        const QString lane = d->addShape(str("laneHorizontal"), 528, 180 + i * 210);
        d->setText(lane, str(lanes[i]));
        d->setSelection({lane});
        d->applyGeometry({{lane, QRectF(40, 75 + i * 210, 976, 210), 0}}, {}, {}, {}, str("Lane"));
    }
    const QString title = shape(d, "text", 528, 40, "Volunteer onboarding — Harbor Lights Community Center");
    d->setSelection({title});
    d->setTextStyle({{str("size"), 15}, {str("bold"), true}});
    d->applyGeometry({{title, QRectF(228, 22, 600, 36), 0}}, {}, {}, {}, str("Title"));
    const QString start = shape(d, "terminator", 150, 180, "Interested in volunteering");
    const QString form = shape(d, "process", 340, 180, "Submit interest form online");
    const QString review = shape(d, "process", 340, 390, "Review application");
    const QString check = shape(d, "decision", 530, 390, "Background check clear?");
    const QString refer = shape(d, "process", 530, 600, "Refer to a partner program");
    const QString schedule = shape(d, "process", 720, 390, "Schedule orientation");
    const QString kit = shape(d, "document", 720, 600, "Welcome kit & mentor match");
    const QString attend = shape(d, "process", 720, 180, "Attend orientation");
    const QString shift = shape(d, "terminator", 910, 180, "First shift!");
    const QString note = shape(d, "note", 912, 380, "Checks take 3–5 business days; call if longer.");
    d->setSelection({note});
    d->applyGeometry({{note, QRectF(840, 340, 150, 84), 0}}, {}, {}, {}, str("Note"));
    link(d, start, 1, form, 3);
    link(d, form, 2, review, 0);
    link(d, review, 1, check, 3);
    link(d, check, 1, schedule, 3, "Yes");
    link(d, check, 2, refer, 0, "No");
    link(d, schedule, 0, attend, 2);
    link(d, schedule, 2, kit, 0);
    link(d, attend, 1, shift, 3);
    link(d, kit, 1, shift, 2);
    d->setSelection({});
}

// Page 2: the org chart, imported from CSV the way a user would.
inline void seedOrgChart(DiagramService *d)
{
    d->addPage();
    d->renamePage({}, str("Org chart"));
    d->importOrgChartText(str("Name,Title,Manager,Email\n"
                              "Maya Ortiz,Executive Director,,maya@harborlights.org\n"
                              "Sam Lee,Program Director,Maya Ortiz,sam@harborlights.org\n"
                              "Dana Brooks,Operations Manager,Maya Ortiz,dana@harborlights.org\n"
                              "Ravi Patel,Development Lead,Maya Ortiz,ravi@harborlights.org\n"
                              "Priya Nair,Volunteer Coordinator,Sam Lee,\n"
                              "Jo Park,Youth Programs,Sam Lee,\n"
                              "Ana Diaz,Tutoring Lead,Sam Lee,\n"
                              "Leo Chen,Food Pantry Lead,Sam Lee,\n"
                              "Grace Kim,Finance & HR,Dana Brooks,\n"
                              "Tom Reyes,Facilities,Dana Brooks,\n"
                              "Nia Evans,Grants Writer,Ravi Patel,\n"));
    d->setSelection({});
}

// Page 3: a small office network.
inline void seedNetwork(DiagramService *d)
{
    d->addPage();
    d->renamePage({}, str("Office network"));
    const QString office = shape(d, "container", 528, 600, "Main office — 2nd floor");
    d->setSelection({office});
    d->applyGeometry({{office, QRectF(90, 500, 876, 200), 0}}, {}, {}, {}, str("Office"));
    const QString internet = shape(d, "cloud", 528, 80, "Internet");
    const QString firewall = shape(d, "firewall", 528, 200, "Firewall");
    const QString router = shape(d, "router", 528, 310, "Router");
    const QString wifi = shape(d, "phone", 760, 310, "Guest Wi-Fi");
    const QString sw = shape(d, "switch", 528, 420, "Core switch");
    const QString server = shape(d, "server", 190, 590, "File server");
    const QString backups = shape(d, "networkDatabase", 330, 590, "Backups");
    const QString desk = shape(d, "desktop", 470, 590, "Front desk");
    const QString office1 = shape(d, "desktop", 610, 590, "Office PCs");
    const QString printer = shape(d, "printer", 750, 590, "Printer");
    const QString laptops = shape(d, "laptop", 890, 590, "Staff laptops");
    d->setData(server, str("IP address"), str("10.0.10.5"));
    d->setData(server, str("OS"), str("Debian 13"));
    const QStringList links{link(d, internet, 2, firewall, 0), link(d, firewall, 2, router, 0), link(d, router, 1, wifi, 3),
                            link(d, router, 2, sw, 0)};
    QStringList all = links;
    for (const QString &device : {server, backups, desk, office1, printer, laptops}) {
        all.append(link(d, sw, 2, device, 0));
    }
    d->setSelection(all);
    d->setConnectorStyle({{str("endArrow"), str("none")}});
    d->setSelection({});
}

} // namespace DiagramAppTest

#define DIAGRAM_APP_TEST_MAIN(TestClass)                                                                               \
    int main(int argc, char *argv[])                                                                                   \
    {                                                                                                                  \
        qputenv("QT_QPA_PLATFORM", "offscreen");                                                                       \
        qputenv("QT_QUICK_BACKEND", "software");                                                                       \
        QTemporaryDir home;                                                                                            \
        qputenv("XDG_CONFIG_HOME", home.filePath(QStringLiteral("config")).toUtf8());                                  \
        qputenv("XDG_STATE_HOME", home.filePath(QStringLiteral("state")).toUtf8());                                    \
        qputenv("XDG_DATA_HOME", home.filePath(QStringLiteral("data")).toUtf8());                                      \
        qputenv("XDG_CACHE_HOME", home.filePath(QStringLiteral("cache")).toUtf8());                                    \
        QGuiApplication app(argc, argv);                                                                               \
        QGuiApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaDiagram"));                               \
        QGuiApplication::setApplicationDisplayName(QStringLiteral("QindaDiagram"));                                    \
        TestClass test;                                                                                                \
        return QTest::qExec(&test, argc, argv);                                                                        \
    }
