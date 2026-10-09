// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private (books_io): a paycheck printed on voucher check stock (M71,
// D-081). check_print.cpp asks fillPayStub() for the payee, the window
// address and the pay stub; check_renderer.cpp draws the stub with
// drawPayStub() in place of the ordinary stub, on both voucher stubs.

#include "check_print.h"

class QPainter;

namespace QindaOffice::Books {

struct BusinessDocument;

// The geometry check_renderer.cpp lays the stubs out on (points from the
// sheet's corner), passed in so there is one source of it.
struct StubGeometry {
    qreal left = 0;
    qreal right = 0;
    qreal pad = 0;
    qreal height = 0;
};

// Fills the employee's name and address, the printed memo and the pay
// stub — this check and the year to date — from the paycheck `doc`.
bool fillPayStub(CompanyFile &file, const BusinessDocument &doc, PrintableCheck *check, QString *error);

// Draws the pay stub in the stub whose top edge is `top`.
void drawPayStub(QPainter *painter, const PrintableCheck &check, qreal top, const StubGeometry &geometry);

} // namespace QindaOffice::Books
