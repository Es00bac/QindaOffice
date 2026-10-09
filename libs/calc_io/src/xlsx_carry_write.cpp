// SPDX-License-Identifier: GPL-3.0-or-later
// The save side of the carry stage (xlsx_carry.h, M66, D-075): runs the
// phases of xlsx_carry_phases.h over the package the post-pass built.

#include "xlsx_carry.h"

#include "xlsx_carry_phases.h"

#include <QObject>

namespace QindaOffice::Calc::IO {

bool applyCarry(QHash<QString, QByteArray> *parts, const PatchManifest &manifest,
                const QList<QPair<QString, QString>> &tableSources, int *drawingPartCount, QString *error)
{
    if (manifest.source.carried.isEmpty()) {
        return true;
    }
    QString loadError;
    const std::shared_ptr<const Ooxml::Package> source = Ooxml::Package::fromData(manifest.source.carried, &loadError);
    if (!source) {
        // The copy rides undo and recovery; one that cannot be read carries
        // nothing, and the save says so rather than failing.
        if (manifest.warnings != nullptr) {
            manifest.warnings->append(QObject::tr("Parts kept from the original file could not be read back; "
                                                  "they were not saved (%1).")
                                          .arg(loadError));
        }
        return true;
    }
    Carry::Context ctx(parts, manifest, source);
    Carry::CarryState state;
    for (const auto &[written, from] : tableSources) {
        if (!from.isEmpty() && source->contains(from)) {
            state.sourceToDestTable.insert(from, written);
        }
    }
    Carry::carryPivotTables(ctx, state);
    Carry::carryWorkbookParts(ctx, state);
    if (!Carry::carrySheets(ctx, state, drawingPartCount, error)) {
        return false;
    }
    return ctx.flush(error);
}

} // namespace QindaOffice::Calc::IO
