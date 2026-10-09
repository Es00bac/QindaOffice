// SPDX-License-Identifier: GPL-3.0-or-later
// QindaBase interchange (M65, D-074) through BaseService: Excel import with
// types read from the cells, typed Excel export that reads back as the
// types it left as, file fields (insert, preview, save, remove, undo, the
// datasheet's text, icon and filter), and the Access import as one undo
// step with its report.

#include "base_app_test_support.h"

#include "base_file_field.h"
#include "base_records.h"
#include "qindabase/src/base_xlsx_import.h"

#include <cell.h>
#include <workbook.h>
#include <xlsx_bridge.h>

#include <qindatk/editable_grid_model.h>

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QUndoStack>

using namespace BaseAppTest;
namespace Engine = QindaOffice::Base;
namespace Calc = QindaOffice::Calc;
using QindaOffice::Apps::Base::readXlsxTable;

namespace {

struct Fixture {
    QTemporaryDir dir;
    BaseDocument document;
    BaseService service{&document};
    QString error;
    Fixture() { document.createAt(dir.filePath(str("club.qbase")), str("donors"), &error); }
    [[nodiscard]] QString path(const char *name) const { return dir.filePath(str(name)); }
    [[nodiscard]] QUrl url(const char *name) const { return QUrl::fromLocalFile(path(name)); }
    [[nodiscard]] QUndoStack *undo() const { return document.undoStack(); }
    [[nodiscard]] QVariant scalar(const char *sql) const { return service.file()->db().scalar(str(sql)); }
};

// A "Cover" sheet first (one title), then "Members" with every kind of cell.
bool writeMembersWorkbook(const QString &path)
{
    Calc::Workbook book;
    Calc::Sheet &cover = book.addSheet(str("Cover"));
    Calc::Sheet &sheet = book.addSheet(str("Members"));
    const auto style = [&](const char *format) {
        Calc::CellStyle s;
        s.numberFormat = str(format);
        return book.addStyle(s);
    };
    const qint32 date = style("m/d/yyyy");
    const qint32 stamp = style("yyyy-mm-dd hh:mm");
    const qint32 money = style("\"$\"#,##0.00");
    book.beginBulkLoad();
    book.setCellValue(0, {0, 0}, Calc::Value(str("Club members 2026")));
    Q_UNUSED(cover);
    const QStringList header{str("id"), str("Name"), str("Zip"), str("Joined"), str("Last seen"), str("Dues"),
                             str("Rating"), str("Active"), str("Notes"), str("Mixed")};
    for (int c = 0; c < header.size(); ++c) {
        book.setCellValue(1, {1, c + 1}, Calc::Value(header.at(c))); // the table starts at B2
    }
    const auto put = [&](int row, int col, const Calc::Value &v, qint32 styleId = 0) {
        book.setCellValue(1, {row, col + 1}, v);
        if (styleId != 0) {
            sheet.setStyleIdAt({row, col + 1}, styleId);
        }
    };
    put(2, 0, 10.0);
    put(2, 1, str("Ada Lovelace"));
    put(2, 2, str("02134"));
    put(2, 3, 46095.0, date);
    put(2, 4, 46095.5, stamp);
    put(2, 5, 1250.5, money);
    put(2, 6, 4.5);
    put(2, 7, true);
    put(2, 8, QString(300, QLatin1Char('n')));
    put(2, 9, 7.0);
    put(4, 0, 20.0); // row 4: a blank row 3 above is skipped
    put(4, 1, str("Grace Hopper"));
    put(4, 2, str("94016"));
    put(4, 3, 46027.0, date);
    put(4, 4, 46027.25, stamp);
    put(4, 5, -20.0, money);
    put(4, 6, 3.0);
    put(4, 7, false);
    put(4, 8, str("short"));
    put(4, 9, str("seven"));
    book.endBulkLoad();
    return Calc::IO::writeXlsxFile(book, path);
}

QByteArray pngBytes()
{
    QImage image(48, 32, QImage::Format_ARGB32);
    image.fill(QColor(30, 120, 200));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

} // namespace

class BaseInterchangeTest final : public QObject {
    Q_OBJECT
private slots:
    void formatCodesAreRead()
    {
        using namespace QindaOffice::Apps::Base;
        QVERIFY(isDateFormatCode(str("yyyy-mm-dd")));
        QVERIFY(isDateFormatCode(str("m/d/yyyy")));
        QVERIFY(isDateFormatCode(str("[$-409]mmmm d, yyyy;@")));
        QVERIFY(isDateFormatCode(str("[h]:mm:ss")));
        QVERIFY(!isDateFormatCode(str("General")));
        QVERIFY(!isDateFormatCode(str("0.00E+00")));
        QVERIFY(!isDateFormatCode(str("#,##0.00 \"days\""))); // a quoted literal is not a token
        QVERIFY(hasTimeCode(str("yyyy-mm-dd hh:mm")));
        QVERIFY(hasTimeCode(str("h:mm AM/PM")));
        QVERIFY(!hasTimeCode(str("mmm d, yyyy")));
        QVERIFY(isCurrencyFormatCode(str("\"$\"#,##0.00"), str("$")));
        QVERIFY(isCurrencyFormatCode(str("[$€-407]#,##0.00"), str("$")));
        QVERIFY(isCurrencyFormatCode(str("#,##0.00 \"kr\""), str("kr")));
        QVERIFY(!isCurrencyFormatCode(str("[$-409]#,##0"), str("$")));
        QVERIFY(!isCurrencyFormatCode(str("#,##0.00"), str("$")));
        QCOMPARE(dateTimeFromSerial(46095.5), QDateTime(QDate(2026, 3, 14), QTime(12, 0)));
        QCOMPARE(serialFromDateTime(QDateTime(QDate(2026, 1, 5), QTime(6, 0))), 46027.25);
    }

    void excelImportTakesTypesFromTheCells()
    {
        Fixture f;
        QVERIFY2(f.error.isEmpty(), qPrintable(f.error));
        QVERIFY(writeMembersWorkbook(f.path("members.xlsx")));
        QString error;
        const auto read = readXlsxTable(f.path("members.xlsx"), str("Members"), true, Engine::CurrencyFormat{}, &error);
        QVERIFY2(read, qPrintable(error));
        QCOMPARE(read->sheets, (QStringList{str("Cover"), str("Members")}));
        QCOMPARE(read->table.header.first(), str("id"));
        QCOMPARE(read->table.rows.size(), 2); // the blank row is skipped
        using T = Engine::FieldType;
        const QList<std::optional<T>> stated{T::Integer, std::nullopt, std::nullopt, T::Date, T::DateTime,
                                             T::Currency, T::Number, T::YesNo, std::nullopt, std::nullopt};
        QCOMPARE(read->table.types, stated);
        // "" picks the first sheet holding data.
        QCOMPARE(readXlsxTable(f.path("members.xlsx"), QString(), true, {}, &error)->sheet, str("Cover"));
        // The service proposes from it and imports as one step.
        const QVariantMap proposal = f.service.xlsxProposal(f.url("members.xlsx"), str("Members"), QString(), true);
        QVERIFY2(ok(proposal), qPrintable(proposal.value(str("error")).toString()));
        QCOMPARE(proposal.value(str("sheets")).toStringList().size(), 2);
        QCOMPARE(proposal.value(str("preview")).toList().size(), 2);
        const QVariantMap plan = proposal.value(str("plan")).toMap();
        QCOMPARE(plan.value(str("table")).toString(), str("Members"));
        QStringList types;
        for (const QVariant &c : plan.value(str("columns")).toList()) {
            types.append(c.toMap().value(str("type")).toString());
        }
        QCOMPARE(types, (QStringList{str("autoNumber"), str("text"), str("text"), str("date"), str("dateTime"),
                                     str("currency"), str("number"), str("yesNo"), str("longText"), str("text")}));
        const int steps = f.undo()->count();
        const QVariantMap imported = f.service.importXlsx(f.url("members.xlsx"), plan, str("Members"), true);
        QVERIFY2(ok(imported), qPrintable(imported.value(str("error")).toString()));
        QCOMPARE(f.undo()->count(), steps + 1);
        QCOMPARE(f.scalar("SELECT Joined FROM Members WHERE id = 10").toString(), str("2026-03-14"));
        QCOMPARE(f.scalar("SELECT \"Last seen\" FROM Members WHERE id = 20").toString(), str("2026-01-05 06:00:00"));
        QCOMPARE(f.scalar("SELECT Dues FROM Members WHERE id = 10").toLongLong(), qint64(125050));
        QCOMPARE(f.scalar("SELECT Zip FROM Members WHERE id = 10").toString(), str("02134"));
        QCOMPARE(f.scalar("SELECT Active FROM Members WHERE id = 20").toLongLong(), qint64(0));
        QCOMPARE(f.scalar("SELECT Mixed FROM Members WHERE id = 10").toString(), str("7"));
        // An Excel 97 file is refused with what to do instead.
        QFile::copy(f.path("members.xlsx"), f.path("old.xls"));
        QVERIFY(f.service.xlsxProposal(f.url("old.xls"), QString(), QString(), true).value(str("error")).toString()
                    .contains(str(".xlsx")));
    }

    void excelExportReadsBackAsItsTypes()
    {
        Fixture f;
        QVERIFY2(f.error.isEmpty(), qPrintable(f.error));
        Engine::BaseFile &file = *f.service.file();
        Engine::TableDef t;
        t.name = str("Ledger");
        t.autoIncrement = true;
        const auto add = [&](const char *name, Engine::FieldType type) {
            Engine::FieldDef d;
            d.name = str(name);
            d.type = type;
            d.primaryKey = type == Engine::FieldType::AutoNumber;
            t.fields.append(d);
        };
        using T = Engine::FieldType;
        add("id", T::AutoNumber);
        add("Label", T::Text);
        add("Memo", T::LongText);
        add("Count", T::Integer);
        add("Ratio", T::Number);
        add("Amount", T::Currency);
        add("Done", T::YesNo);
        add("Day", T::Date);
        add("At", T::DateTime);
        QVERIFY(file.applyPlan(Engine::planCreateTable(file, t), nullptr).ok);
        QVERIFY(file.db().run(str("INSERT INTO Ledger(Label, Memo, Count, Ratio, Amount, Done, Day, At) VALUES"
                                  "('rent', ?, 3, 2.75, 125050, 1, '2026-03-14', '2026-03-14 18:30:00'),"
                                  "('fees', 'x', -4, 0.5, -2000, 0, '2025-12-31', '2026-01-05 06:00:00')"),
                              {QString(300, QLatin1Char('m'))})
                    .ok);
        QVERIFY(ok(f.service.exportObject(str("table"), str("Ledger"), f.url("ledger.xlsx"), str("xlsx"))));
        QString error;
        const auto read = readXlsxTable(f.path("ledger.xlsx"), QString(), true, file.currency(), &error);
        QVERIFY2(read, qPrintable(error));
        const QVariantMap proposal = f.service.xlsxProposal(f.url("ledger.xlsx"), QString(), str("Ledger copy"), true);
        QStringList types;
        for (const QVariant &c : proposal.value(str("plan")).toMap().value(str("columns")).toList()) {
            types.append(c.toMap().value(str("type")).toString());
        }
        QCOMPARE(types, (QStringList{str("autoNumber"), str("text"), str("longText"), str("integer"), str("number"),
                                     str("currency"), str("yesNo"), str("date"), str("dateTime")}));
        QVERIFY(ok(f.service.importXlsx(f.url("ledger.xlsx"), proposal.value(str("plan")).toMap(), QString(), true)));
        // Every value comes back exactly.
        const QString columns = str("Label, Memo, Count, Ratio, Amount, Done, Day, At");
        QCOMPARE(file.db().rows(str("SELECT id, %1 FROM \"Ledger copy\" ORDER BY id").arg(columns)),
                 file.db().rows(str("SELECT id, %1 FROM Ledger ORDER BY id").arg(columns)));
    }

    void fileFieldsThroughTheService()
    {
        Fixture f;
        QVERIFY2(f.error.isEmpty(), qPrintable(f.error));
        Engine::BaseFile &file = *f.service.file();
        Engine::TableDef t;
        t.name = str("People");
        t.autoIncrement = true;
        Engine::FieldDef id;
        id.name = str("id");
        id.type = Engine::FieldType::AutoNumber;
        id.primaryKey = true;
        Engine::FieldDef name;
        name.name = str("Name");
        Engine::FieldDef photo;
        photo.name = str("Photo");
        photo.type = Engine::FieldType::File;
        t.fields = {id, name, photo};
        QVERIFY(file.applyPlan(Engine::planCreateTable(file, t), nullptr).ok);
        QVERIFY(file.db().run(str("INSERT INTO People(Name) VALUES('Ada'), ('Grace')")).ok);
        const QByteArray png = pngBytes();
        QFile source(f.path("ada.png"));
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write(png);
        source.close();
        const int steps = f.undo()->count();
        QVERIFY(ok(f.service.insertFile(str("People"), 1, str("Photo"), f.url("ada.png"))));
        QCOMPARE(f.undo()->count(), steps + 1);
        const QVariantMap info = f.service.fileInfo(str("People"), 1, str("Photo"));
        QVERIFY(info.value(str("hasFile")).toBool());
        QCOMPARE(info.value(str("kind")).toString(), str("PNG image"));
        QVERIFY(info.value(str("image")).toBool());
        QCOMPARE(info.value(str("suggestedName")).toString(), str("People-1-Photo.png"));
        QFile preview(info.value(str("previewUrl")).toUrl().toLocalFile());
        QVERIFY(preview.open(QIODevice::ReadOnly));
        QCOMPARE(preview.readAll(), png);
        QVERIFY(ok(f.service.saveFile(str("People"), 1, str("Photo"), f.url("out.png"))));
        QFile saved(f.path("out.png"));
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), png);
        // The datasheet: text, an icon, read-only, filter by having a file.
        DatasheetModel *sheet = f.service.datasheet();
        QVERIFY(sheet->openTable(str("People")));
        QVERIFY(sheet->cellText(str("r1"), str("Photo")).startsWith(str("PNG image · ")));
        const auto *styled = dynamic_cast<const QindaTK::GridCellSourceExtension *>(sheet->grid()->source());
        QVERIFY(styled != nullptr);
        QCOMPARE(styled->cellStyle(str("r1"), str("Photo")).value(str("icon")).toString(), str("image"));
        QVERIFY(!ok(sheet->setCell(str("r1"), str("Photo"), str("x"))));
        QVERIFY(sheet->filterBySelection(str("r1"), str("Photo"), false));
        QCOMPARE(sheet->recordCount(), 1);
        sheet->clearFilters();
        // Remove, then undo puts it back; files over the limit are refused.
        QVERIFY(ok(f.service.removeFile(str("People"), 1, str("Photo"))));
        QVERIFY(!f.service.fileInfo(str("People"), 1, str("Photo")).value(str("hasFile")).toBool());
        f.undo()->undo();
        QVERIFY(f.service.fileInfo(str("People"), 1, str("Photo")).value(str("hasFile")).toBool());
        QFile big(f.path("big.bin"));
        QVERIFY(big.open(QIODevice::WriteOnly));
        QVERIFY(big.resize(Engine::kFileFieldLimit + 1)); // sparse: costs no disk
        big.close();
        QVERIFY(f.service.insertFile(str("People"), 2, str("Photo"), f.url("big.bin")).value(str("error")).toString()
                    .contains(str("32 MB")));
        // Exports show what the cell holds.
        QVERIFY(ok(f.service.exportObject(str("table"), str("People"), f.url("people.csv"), str("csv"))));
        QFile csv(f.path("people.csv"));
        QVERIFY(csv.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(csv.readAll()).contains(str("PNG image")));
    }
};

BASE_APP_TEST_MAIN(BaseInterchangeTest)

#include "tst_qindabase_interchange.moc"
