// SPDX-License-Identifier: GPL-3.0-or-later
#include "launcher_window.h"

#include "app_card.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

namespace QindaOffice::Launcher {

LauncherWindow::LauncherWindow(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("launcherWindow"));
    setWindowTitle(tr("QindaQt Office"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(16);

    auto *heading = new QLabel(tr("QindaQt Office"), this);
    heading->setObjectName(QStringLiteral("launcherHeading"));
    QFont headingFont = heading->font();
    headingFont.setPointSizeF(headingFont.pointSizeF() * 1.8);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    layout->addWidget(heading);

    auto *subtitle = new QLabel(tr("Pick something to work in, or carry on where you left off."),
                                this);
    subtitle->setObjectName(QStringLiteral("launcherSubtitle"));
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);

    auto *grid = new QGridLayout;
    grid->setSpacing(14);
    buildCards(grid);
    layout->addLayout(grid);

    auto *recentHeading = new QLabel(tr("Recent documents"), this);
    recentHeading->setObjectName(QStringLiteral("launcherRecentHeading"));
    QFont recentFont = recentHeading->font();
    recentFont.setBold(true);
    recentHeading->setFont(recentFont);
    layout->addWidget(recentHeading);

    m_recent = new QListWidget(this);
    m_recent->setObjectName(QStringLiteral("launcherRecentList"));
    m_recent->setAlternatingRowColors(true);
    m_recent->setIconSize(QSize(24, 24));
    m_recent->setMinimumHeight(140);
    layout->addWidget(m_recent, 1);
    connect(m_recent, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *item) { openRecent(m_recent->row(item)); });
    // One click opens: this is a launcher, and a list whose rows need a
    // double-click reads as a list to browse rather than a list to use.
    connect(m_recent, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) { openRecent(m_recent->row(item)); });

    auto *actions = new QHBoxLayout;
    auto *open = new QPushButton(tr("Open a Document…"), this);
    open->setObjectName(QStringLiteral("launcherOpenButton"));
    open->setIcon(QIcon::fromTheme(QStringLiteral("document-open")));
    connect(open, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Open"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), openFilter());
        if (path.isEmpty()) {
            return;
        }
        for (const AppEntry &app : officeApps()) {
            if (app.id == appIdForPath(path)) {
                openApp(app, path);
                return;
            }
        }
    });
    actions->addWidget(open);
    actions->addStretch(1);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("launcherStatus"));
    actions->addWidget(m_status);
    layout->addLayout(actions);

    refreshRecents();
    resize(760, 620);
}

void LauncherWindow::buildCards(QGridLayout *grid)
{
    const QList<AppEntry> apps = officeApps();
    for (int i = 0; i < apps.size(); ++i) {
        auto *card = new AppCard(apps.at(i), this);
        // A card for an app that is not installed says so rather than
        // looking clickable and doing nothing.
        card->setAvailable(!resolveBinary(apps.at(i)).isEmpty());
        connect(card, &AppCard::activated, this,
                [this](const AppEntry &app) { openApp(app); });
        // Two columns so the cards stay legible when the window is narrow;
        // the grid stretches them evenly at any width.
        grid->addWidget(card, i / 2, i % 2);
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
}

void LauncherWindow::refreshRecents()
{
    m_recentEntries = recentDocuments();
    m_recent->clear();
    if (m_recentEntries.isEmpty()) {
        auto *empty = new QListWidgetItem(tr("Nothing yet — open or create a document and it "
                                             "will show up here."),
                                          m_recent);
        empty->setFlags(Qt::NoItemFlags);
        return;
    }
    for (const RecentEntry &entry : m_recentEntries) {
        auto *item = new QListWidgetItem(m_recent);
        item->setText(QStringLiteral("%1\n%2").arg(entry.fileName, entry.folder));
        item->setToolTip(entry.path);
        for (const AppEntry &app : officeApps()) {
            if (app.id == entry.appId) {
                item->setIcon(iconFor(app));
                break;
            }
        }
    }
}

void LauncherWindow::openRecent(int row)
{
    if (row < 0 || row >= m_recentEntries.size()) {
        return;
    }
    const RecentEntry entry = m_recentEntries.at(row);
    for (const AppEntry &app : officeApps()) {
        if (app.id == entry.appId) {
            openApp(app, entry.path);
            return;
        }
    }
}

void LauncherWindow::openApp(const AppEntry &app, const QString &path)
{
    if (launch(app, path)) {
        report(path.isEmpty() ? tr("Starting %1…").arg(app.name)
                              : tr("Opening %1…").arg(QFileInfo(path).fileName()));
        return;
    }
    // Fail visibly: a launcher that silently does nothing is the worst
    // possible front door.
    report(tr("Could not start %1 — is it installed?").arg(app.name));
}

void LauncherWindow::report(const QString &text)
{
    m_status->setText(text);
    // The message is a receipt, not a state: clear it so a stale "Starting
    // QindaWrite…" does not sit there for the rest of the session.
    QTimer::singleShot(6000, m_status, [this] { m_status->clear(); });
}

} // namespace QindaOffice::Launcher
