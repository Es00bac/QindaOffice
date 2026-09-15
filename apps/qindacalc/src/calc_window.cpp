// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc_window.h"

#include "spreadsheet_document.h"
#include "spreadsheet_view.h"
#include "template_loader.h"

#ifdef QQO_WITH_CHARTS
#include "chart_dialog.h"
#endif

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTabBar>
#include <QVBoxLayout>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

CalcWindow::CalcWindow()
    : m_document(new SpreadsheetDocument)
    , m_view(new SpreadsheetView(m_document))
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *bar = new QWidget(central);
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(4, 2, 4, 2);
    m_nameBox = new QLineEdit(bar);
    m_nameBox->setFixedWidth(72);
    m_nameBox->setAlignment(Qt::AlignCenter);
    m_formulaBar = new QLineEdit(bar);
    barLayout->addWidget(m_nameBox);
    barLayout->addWidget(m_formulaBar, 1);
    layout->addWidget(bar);

    layout->addWidget(m_view, 1);

    m_tabs = new QTabBar(central);
    m_tabs->setDrawBase(false);
    m_tabs->setExpanding(false);
    layout->addWidget(m_tabs);

    setDocumentHost(central, m_document);

    connect(m_view, &SpreadsheetView::cursorChanged, this,
            [this](const CellAddr &addr, const QString &formulaText, const QString &) {
                m_syncingBar = true;
                m_nameBox->setText(addrText(addr));
                m_formulaBar->setText(formulaText);
                m_syncingBar = false;
            });
    connect(m_view, &SpreadsheetView::editCommitted, this, &CalcWindow::commitInput);
    connect(m_formulaBar, &QLineEdit::returnPressed, this, [this] {
        if (!m_syncingBar) {
            m_view->commitFormulaBar(m_formulaBar->text());
        }
    });
    connect(m_nameBox, &QLineEdit::returnPressed, this, [this] {
        if (m_syncingBar) {
            return;
        }
        // Name box navigation: type a reference to jump.
        ParseResult parsed = parseFormula(m_nameBox->text());
        if (parsed.root && parsed.root->kind == Node::Kind::Ref && parsed.root->ref.isSingleCell()) {
            m_view->setCursorCell(parsed.root->ref.range.topLeft);
        }
    });
    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (index >= 0) {
            m_document->setActiveSheet(index);
            m_view->refreshAll();
        }
    });
    rebuildSheetTabs();
}

CalcWindow::~CalcWindow() = default;

void CalcWindow::loadWorkbookPath(const QString &path)
{
    QString error;
    if (m_document->load(path, &error)) {
        m_document->adoptPath(path);
        rebuildSheetTabs();
        m_view->refreshAll();
        statusBar()->showMessage(tr("Opened %1").arg(path), 5000);
    } else {
        statusBar()->showMessage(tr("Open failed: %1").arg(error), 8000);
    }
}

void CalcWindow::commitInput(const CellAddr &addr, const QString &input)
{
    m_document->applyInput(addr, input);
    m_view->refreshAll();
}

void CalcWindow::rebuildSheetTabs()
{
    QSignalBlocker blocker(m_tabs);
    while (m_tabs->count() > 0) {
        m_tabs->removeTab(0);
    }
    const Workbook *wb = m_document->workbook();
    for (qint32 i = 0; i < wb->sheetCount(); ++i) {
        m_tabs->addTab(wb->sheetName(i));
    }
    m_tabs->setCurrentIndex(int(m_document->activeSheet()));
}

void CalcWindow::insertChart()
{
#ifdef QQO_WITH_CHARTS
    Workbook *wb = m_document->workbook();
    const CalcEng::CellAddr cursor = m_view->cursorCell();
    auto colName = [](int col) {
        QString name;
        for (++col; col > 0; col /= 26) {
            name.prepend(QChar(QLatin1Char('A' + (col - 1) % 26)));
        }
        return name;
    };
    const QString initial = QStringLiteral("%1%2:%3%4")
        .arg(colName(cursor.col), QString::number(cursor.row + 1),
             colName(cursor.col + 1), QString::number(cursor.row + 6));
    ChartDialog dialog(wb, initial, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    CalcEng::ChartSpec spec = dialog.spec();
    spec.sheet = m_document->activeSheet();
    spec.anchorRow = cursor.row;
    spec.anchorCol = cursor.col;
    wb->charts.append(spec);
    m_document->setDirty(true);
    rebuildChartWindows();
#endif
}

void CalcWindow::rebuildChartWindows()
{
#ifdef QQO_WITH_CHARTS
    qDeleteAll(m_chartWindows);
    m_chartWindows.clear();
    const Workbook *wb = m_document->workbook();
    for (const CalcEng::ChartSpec &spec : wb->charts) {
        auto *floating = new QWidget(this, Qt::Tool | Qt::WindowStaysOnTopHint);
        floating->setWindowTitle(spec.title.isEmpty() ? tr("Chart") : spec.title);
        auto *layout = new QVBoxLayout(floating);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(chartWidgetFor(spec, *wb, floating));
        floating->resize(spec.widthPx, spec.heightPx);
        floating->move(140 + spec.anchorCol * 24, 120 + spec.anchorRow * 22);
        floating->show();
        m_chartWindows.append(floating);
    }
#endif
}

void CalcWindow::insertFileMenuActions(QMenu *fileMenu)
{
    const QList<Shell::TemplateEntry> templates = Shell::templatesFor(QStringLiteral("QindaCalc"));
    if (templates.isEmpty()) {
        return;
    }
    QMenu *submenu = fileMenu->addMenu(tr("New from &Template"));
    for (const Shell::TemplateEntry &entry : templates) {
        submenu->addAction(entry.name, this, [this, path = entry.path] {
            QString error;
            if (m_document->load(path, &error)) {
                m_document->adoptPath(QString());
                m_document->setDirty(true);
                rebuildSheetTabs();
                m_view->refreshAll();
                rebuildChartWindows();
            }
        });
    }
}

void CalcWindow::insertApplicationMenus(QMenu *before)
{
#ifdef QQO_WITH_CHARTS
    auto *insertMenu = new QMenu(tr("&Insert"), this);
    menuBar()->insertMenu(before->menuAction(), insertMenu);
    QAction *chart = insertMenu->addAction(tr("&Chart…"));
    connect(chart, &QAction::triggered, this, [this] { insertChart(); });
#endif
    auto *sheetMenu = new QMenu(tr("&Sheet"), this);
    menuBar()->insertMenu(before->menuAction(), sheetMenu);
    QAction *insertSheet = sheetMenu->addAction(tr("&Insert Sheet"));
    connect(insertSheet, &QAction::triggered, this, [this] {
        Workbook *wb = m_document->workbook();
        wb->addSheet(tr("Sheet%1").arg(wb->sheetCount() + 1));
        rebuildSheetTabs();
        emit m_view->sheetCountChanged();
    });
    auto *formatMenu = new QMenu(tr("F&ormat"), this);
    menuBar()->insertMenu(before->menuAction(), formatMenu);
    QAction *thousands = formatMenu->addAction(tr("Number format: #,##0.00"));
    connect(thousands, &QAction::triggered, this, [this] {
        // v1: applies to the cursor cell only.
        Workbook *wb = m_document->workbook();
        Sheet &sheet = wb->sheetAt(m_document->activeSheet());
        const CellAddr addr = m_view->cursorCell();
        CellStyle style = [wb, &sheet, addr] {
            const qint32 id = sheet.styleIdAt(addr);
            return id != 0 && wb->style(id) != nullptr ? *wb->style(id) : CellStyle{};
        }();
        style.numberFormat = QStringLiteral("#,##0.00");
        sheet.setStyleIdAt(addr, wb->addStyle(style));
        m_document->setDirty(true);
        m_view->viewport()->update();
    });
}

QString CalcWindow::filterString() const
{
    return tr("Spreadsheets (*.xlsx);;CSV (*.csv);;All files (*)");
}

} // namespace QindaOffice::Apps::QindaCalc
