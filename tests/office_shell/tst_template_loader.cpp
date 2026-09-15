// SPDX-License-Identifier: GPL-3.0-or-later
// Template loader gates (plan M10): a templates directory surfaces its
// files with display names; missing roots yield an empty list.

#include "template_loader.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Shell;

class TemplateLoaderTest : public QObject {
    Q_OBJECT
private slots:
    void listsReadableFilesWithDisplayNames();
    void missingDirectoryIsEmpty();
};

void TemplateLoaderTest::listsReadableFilesWithDisplayNames()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(dir.path() + QStringLiteral("/Meeting-Notes.html"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("template");
    QFile second(dir.path() + QStringLiteral("/Invoice.csv"));
    QVERIFY(second.open(QIODevice::WriteOnly));

    const QList<TemplateEntry> entries = templatesIn(dir.path());
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.first().name, QStringLiteral("Invoice"));
    QCOMPARE(entries.at(1).name, QStringLiteral("Meeting Notes"));
    QVERIFY(entries.at(1).path.endsWith(QStringLiteral(".html")));
}

void TemplateLoaderTest::missingDirectoryIsEmpty()
{
    QVERIFY(templatesIn(QStringLiteral("/nonexistent/qindaoffice")).isEmpty());
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    TemplateLoaderTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_template_loader.moc"
