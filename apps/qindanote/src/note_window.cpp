// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_window.h"

#include "note_page_view.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace QindaOffice::Apps::Notes {

NoteDocument::NoteDocument()
{
    // A pristine untitled notebook: lazily created on first save.
}

bool NoteDocument::load(const QString &path, QString *error)
{
    if (!m_notebook.load(path, error)) {
        return false;
    }
    setCanonicalPath(path);
    refreshByteRevision();
    setDirty(false);
    emit contentChanged();
    return true;
}

bool NoteDocument::save(const QString &path, QString *error)
{
    if (m_notebook.directory().isEmpty()) {
        if (!m_notebook.create(path, error)) {
            return false;
        }
    }
    if (!m_notebook.save(error)) {
        return false;
    }
    setCanonicalPath(m_notebook.directory());
    refreshByteRevision();
    setDirty(false);
    return true;
}

QStringList NoteDocument::mimeTypes() const
{
    return {QStringLiteral("application/x-qinda-notebook")};
}

NoteWindow::NoteWindow()
    : m_document(new NoteDocument)
    , m_view(new NotePageView)
    , m_navigator(new QTreeWidget)
{
    m_navigator->setHeaderHidden(true);
    auto *central = new QWidget(this);
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_navigator, 1);
    layout->addWidget(m_view, 4);

    auto *tools = new QToolBar(tr("Note tools"), this);
    const QList<std::pair<QString, NoteTool>> entries = {
        {tr("Pen"), NoteTool::Pen},         {tr("Highlight"), NoteTool::Highlighter},
        {tr("Eraser"), NoteTool::Eraser},   {tr("Text"), NoteTool::Text},
    };
    for (const auto &[label, tool] : entries) {
        QAction *action = tools->addAction(label);
        action->setCheckable(true);
        connect(action, &QAction::triggered, this, [this, tool] { m_view->setTool(tool); });
    }
    tools->addSeparator();
    QAction *undo = tools->addAction(tr("Undo"));
    connect(undo, &QAction::triggered, m_view, &NotePageView::undo);
    QAction *text = tools->addAction(tr("Add text"));
    connect(text, &QAction::triggered, m_view, &NotePageView::addTextBlock);

    setDocumentHost(central, m_document);
    addToolBar(Qt::LeftToolBarArea, tools);

    connect(m_view, &NotePageView::pageEdited, this, [this] {
        m_document->setDirty(true);
        persistCurrentPage();
    });
    connect(m_navigator, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
                if (item == nullptr) {
                    return;
                }
                NoteSection *section = static_cast<NoteSection *>(
                    item->data(0, Qt::UserRole).value<void *>());
                if (section != nullptr) {
                    return; // section header
                }
                NotePage *page = static_cast<NotePage *>(
                    item->data(0, Qt::UserRole + 1).value<void *>());
                NoteSection *parentSection = static_cast<NoteSection *>(
                    item->data(0, Qt::UserRole + 2).value<void *>());
                if (page == nullptr) {
                    return;
                }
                persistCurrentPage();
                QString error;
                m_document->notebook()->loadPage(*parentSection, *page, &error);
                m_currentSection = parentSection;
                m_currentPage = page;
                m_view->setPage(page);
            });
    rebuildNavigator();
}

NoteWindow::~NoteWindow() = default;

void NoteWindow::persistCurrentPage()
{
    if (m_currentSection == nullptr || m_currentPage == nullptr) {
        return;
    }
    QString error;
    m_document->notebook()->persistPage(*m_currentSection, *m_currentPage, &error);
}

void NoteWindow::openNotebookPath(const QString &path)
{
    QString error;
    if (m_document->load(path, &error)) {
        rebuildNavigator();
        statusBar()->showMessage(tr("Opened %1").arg(path), 5000);
    } else {
        statusBar()->showMessage(tr("Open failed: %1").arg(error), 8000);
    }
}

void NoteWindow::rebuildNavigator()
{
    m_navigator->clear();
    Notebook *notebook = m_document->notebook();
    for (NoteSection &section : const_cast<QVector<NoteSection> &>(notebook->sections())) {
        auto *sectionItem = new QTreeWidgetItem(m_navigator, {section.name});
        sectionItem->setData(0, Qt::UserRole, QVariant::fromValue<void *>(&section));
        for (NotePage &page : section.pages) {
            auto *pageItem = new QTreeWidgetItem(sectionItem, {page.title});
            pageItem->setData(0, Qt::UserRole + 1, QVariant::fromValue<void *>(&page));
            pageItem->setData(0, Qt::UserRole + 2, QVariant::fromValue<void *>(&section));
        }
        sectionItem->setExpanded(true);
    }
    if (m_navigator->topLevelItemCount() > 0
        && m_navigator->topLevelItem(0)->childCount() > 0) {
        m_navigator->setCurrentItem(m_navigator->topLevelItem(0)->child(0));
    }
}

void NoteWindow::insertApplicationMenus(QMenu *before)
{
    auto *pageMenu = new QMenu(tr("&Page"), this);
    menuBar()->insertMenu(before->menuAction(), pageMenu);
    QAction *addPage = pageMenu->addAction(tr("&New Page"));
    connect(addPage, &QAction::triggered, this, [this] {
        if (m_currentSection == nullptr) {
            Notebook *notebook = m_document->notebook();
            if (notebook->sections().isEmpty()) {
                m_currentSection = &notebook->addSection(QStringLiteral("Notes"));
            } else {
                m_currentSection =
                    &const_cast<QVector<NoteSection> &>(notebook->sections()).first();
            }
        }
        NotePage &page = m_document->notebook()->addPage(*m_currentSection, QStringLiteral("Page"));
        persistCurrentPage();
        rebuildNavigator();
        Q_UNUSED(page);
        m_document->setDirty(true);
    });
    QAction *background = pageMenu->addAction(tr("&Background…"));
    connect(background, &QAction::triggered, this, [this] {
        if (m_currentPage == nullptr) {
            return;
        }
        const QString choice = QInputDialog::getItem(
            this, tr("Background"), tr("Pattern"),
            {QStringLiteral("blank"), QStringLiteral("ruled"), QStringLiteral("grid"),
             QStringLiteral("dotted")});
        if (!choice.isEmpty()) {
            m_currentPage->envelope.background = choice;
            m_view->update();
            m_document->setDirty(true);
            persistCurrentPage();
        }
    });
}

QString NoteWindow::filterString() const
{
    return tr("QindaNote notebooks (notebook.json);;All files (*)");
}

} // namespace QindaOffice::Apps::Notes
