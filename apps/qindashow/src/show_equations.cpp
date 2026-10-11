// SPDX-License-Identifier: GPL-3.0-or-later
#include "show_equations.h"
#include "show_host.h"
#include "show_equation.h"
#include <QUuid>
namespace QindaOffice::Apps::Show {
Quick::EquationSession *ShowHost::equations() const { return m_equations->session(); }
ShowEquations::ShowEquations(ShowHost *host)
    : QObject(host), m_host(host), m_session(host->actions(), this)
{
    auto *insert = host->actions()->add(QStringLiteral("showInsertEquationAction"), tr("Insert equation…"),
                                        QKeySequence(QStringLiteral("Alt+=")), QStringLiteral("sigma"));
    auto *edit = host->actions()->add(QStringLiteral("showEditEquationAction"), tr("Edit equation…"), {}, QStringLiteral("sigma"));
    connect(insert, &Quick::ActionEntry::triggered, this, [this] { begin(false); });
    connect(edit, &Quick::ActionEntry::triggered, this, [this] { begin(true); });
    connect(host->canvas(), &SlideCanvasItem::selectionChanged, this, &ShowEquations::sync);
    connect(host, &ShowHost::currentSlideChanged, this, &ShowEquations::sync);
    connect(host->canvas(), &SlideCanvasItem::equationEditRequested, this, [this](const QString &id) {
        m_host->canvas()->clearSelection(); m_host->canvas()->setSelected(id, true); begin(true);
    });
    connect(&m_session, &Quick::EquationSession::accepted, this, &ShowEquations::commit);
    sync();
}
void ShowEquations::sync()
{
    const auto *shape = m_host->canvas()->selectedShape();
    m_host->actions()->setEnabled(QStringLiteral("showInsertEquationAction"), m_host->canvas()->slide() != nullptr);
    m_host->actions()->setEnabled(QStringLiteral("showEditEquationAction"), shape && shape->kind == ShowModel::ShapeKind::Equation);
}
void ShowEquations::begin(bool existing)
{
    auto *canvas = m_host->canvas();
    if (!canvas->slide()) return;
    canvas->commitTextEdit(false);
    const auto *shape = existing ? canvas->selectedShape() : nullptr;
    if (existing && (!shape || shape->kind != ShowModel::ShapeKind::Equation)) return;
    m_slideId = canvas->slide()->id;
    m_shapeId = shape ? shape->id : QString();
    if (!m_session.begin(shape ? shape->equationXml : QString(), shape ? shape->equationSize : 24))
        emit m_host->messageRequested(tr("Equation"), m_session.error(), QStringLiteral("danger"));
}
void ShowEquations::commit(const QString &xml)
{
    auto *canvas = m_host->canvas();
    if (!canvas->slide() || canvas->slide()->id != m_slideId) return;
    if (!m_shapeId.isEmpty()) {
        canvas->editShape(m_shapeId, tr("Edit equation"), [xml](ShowModel::Shape &shape) {
            if (shape.equationXml == xml) return false;
            shape.equationXml = xml; return true;
        });
    } else {
        ShowModel::Shape shape;
        shape.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        shape.kind = ShowModel::ShapeKind::Equation;
        shape.equationXml = xml;
        shape.name = tr("Equation"); shape.strokeWidth = 0;
        const QSizeF size = ShowModel::equationSize(shape);
        shape.rect = QRectF(QPointF((canvas->slideSize().width() - size.width()) / 2,
                                   (canvas->slideSize().height() - size.height()) / 2), size);
        canvas->insertShape(shape);
    }
}
}
