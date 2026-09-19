// SPDX-License-Identifier: GPL-3.0-or-later
// SpreadsheetDocument's floating-object commands: charts and pictures are
// workbook-level data rather than cell content, but they mutate the document
// exactly like everything else does — snapshot, one undo step, dirty, and a
// structureChanged so the views re-read. Split from spreadsheet_document.cpp
// for the line budget.

#include "spreadsheet_document.h"

#include "calc_undo.h"

#include "workbook.h"

#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>

namespace QindaOffice::Apps::QindaCalc {

namespace CalcEng = QindaOffice::Calc;

void SpreadsheetDocument::addChart(const CalcEng::ChartSpec &spec)
{
    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    m_workbook.charts.append(spec);
    pushUndo(before, tr("Insert chart"));
    setDirty(true);
    emit contentChanged();
    emit structureChanged();
}

bool SpreadsheetDocument::addImageFromFile(const QString &path, int row, int col)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit inputRejected({row, col}, tr("Cannot read %1").arg(QFileInfo(path).fileName()));
        return false;
    }
    const QByteArray bytes = file.readAll();
    const QImage probe = QImage::fromData(bytes);
    if (probe.isNull()) {
        emit inputRejected({row, col}, tr("%1 is not an image this build can read")
                                            .arg(QFileInfo(path).fileName()));
        return false;
    }
    CalcEng::ImageSpec spec;
    spec.bytes = QSharedPointer<const QByteArray>::create(bytes);
    // The suffix is the media part's extension, so it decides the content
    // type other readers see; QImageReader knows the real format when the
    // name lies, and that spelling is the one to store.
    const QByteArray detected = QImageReader(path).format();
    spec.format = detected.isEmpty() ? QFileInfo(path).suffix().toLower()
                                     : QString::fromLatin1(detected).toLower();
    if (spec.format.isEmpty()) {
        spec.format = QStringLiteral("png");
    }
    spec.sheet = m_activeSheet;
    spec.anchorRow = qMax(0, row);
    spec.anchorCol = qMax(0, col);
    spec.widthPx = probe.width();
    spec.heightPx = probe.height();

    const WorkbookSnapshot before = takeSnapshot(m_workbook, m_activeSheet);
    m_workbook.images.append(spec);
    pushUndo(before, tr("Insert image"));
    setDirty(true);
    emit contentChanged();
    emit structureChanged();
    return true;
}

} // namespace QindaOffice::Apps::QindaCalc
