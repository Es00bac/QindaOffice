// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A structural dump of a QtQuick window for fixture tests (conversion plan
// §6 M27): every item with an objectName, as `Type#objectName` with its
// visibility, nested by the named ancestors. Text content and geometry are
// deliberately left out — fonts differ between the build hosts and the
// desktop, and a fixture that broke on every font change would be muted
// within a week. Compare with QQO_UPDATE_FIXTURES=1 to rewrite.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace QindaOffice::Test {

inline QString plainClassName(const QObject *object)
{
    QString name = QString::fromLatin1(object->metaObject()->className());
    name = name.section(QStringLiteral("_QMLTYPE_"), 0, 0).section(QStringLiteral("_QML_"), 0, 0);
    return name.section(QStringLiteral("::"), -1);
}

inline QString normalisedName(const QString &name)
{
    static const QRegularExpression id(QStringLiteral("[0-9a-f]{8}-[0-9a-f]{3,}"));
    // Calendar dates are real command targets, but the structural fixture
    // must not become stale every week. Behavior tests verify actual dates.
    static const QRegularExpression day(QStringLiteral("(?<=calendarGridDay_)[0-9]{4}-[0-9]{2}-[0-9]{2}"));
    QString out = name;
    return out.replace(id, QStringLiteral("<id>")).replace(day, QStringLiteral("<date>"));
}

struct DumpNode {
    QString line;
    QList<DumpNode> children;
};

inline void collect(QQuickItem *item, QList<DumpNode> &into)
{
    const QString name = item->objectName();
    if (name.isEmpty()) {
        for (QQuickItem *child : item->childItems()) {
            collect(child, into);
        }
        return;
    }
    DumpNode node;
    node.line = plainClassName(item) + QLatin1Char('#') + normalisedName(name)
        + (item->isVisible() ? QString() : QStringLiteral(" [hidden]"));
    for (QQuickItem *child : item->childItems()) {
        collect(child, node.children);
    }
    into.append(node);
}

inline void flatten(QList<DumpNode> nodes, int depth, QStringList &out)
{
    std::stable_sort(nodes.begin(), nodes.end(),
                     [](const DumpNode &a, const DumpNode &b) { return a.line < b.line; });
    for (const DumpNode &node : nodes) {
        out.append(QString(depth * 2, QLatin1Char(' ')) + node.line);
        flatten(node.children, depth + 1, out);
    }
}

inline QString dumpWindow(QQuickWindow *window)
{
    QList<DumpNode> nodes;
    collect(window->contentItem(), nodes);
    QStringList out;
    flatten(nodes, 0, out);
    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

// True when the dump matches the fixture (or the fixture was (re)written:
// a missing fixture is created on first run, QQO_UPDATE_FIXTURES=1 rewrites
// it). `diff` receives the first differing lines otherwise.
inline bool matchesFixture(QQuickWindow *window, const QString &path, QString *diff)
{
    const QString actual = dumpWindow(window);
    QFile file(path);
    const bool update = qEnvironmentVariableIsSet("QQO_UPDATE_FIXTURES") || !file.exists();
    if (update) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            *diff = QStringLiteral("cannot write ") + path;
            return false;
        }
        file.write(actual.toUtf8());
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        *diff = QStringLiteral("cannot read ") + path;
        return false;
    }
    const QString expected = QString::fromUtf8(file.readAll());
    if (expected == actual) {
        return true;
    }
    const QStringList a = expected.split(QLatin1Char('\n'));
    const QStringList b = actual.split(QLatin1Char('\n'));
    for (int i = 0; i < qMax(a.size(), b.size()); ++i) {
        const QString left = i < a.size() ? a.at(i) : QString();
        const QString right = i < b.size() ? b.at(i) : QString();
        if (left != right) {
            *diff = QStringLiteral("%1: line %2\n  fixture: %3\n  actual:  %4")
                        .arg(path, QString::number(i + 1), left, right);
            return false;
        }
    }
    *diff = QStringLiteral("dumps differ in length");
    return false;
}

// QQO_GRAB_DIR=<dir> saves a PNG of the window for review, BEFORE any
// fixture comparison (a failing layout is exactly the one to look at).
// QQO_GRAB_PREFIX (e.g. "M40-") is prepended so a run can be dropped
// straight into the validation gallery inbox (tools/validation_gallery.py).
inline void saveGrab(QQuickWindow *window, const QString &name)
{
    const QString dir = qEnvironmentVariable("QQO_GRAB_DIR");
    if (dir.isEmpty() || window == nullptr) {
        return;
    }
    QDir().mkpath(dir);
    window->grabWindow().save(QStringLiteral("%1/%2%3.png")
                                  .arg(dir, qEnvironmentVariable("QQO_GRAB_PREFIX"), name));
}

} // namespace QindaOffice::Test
