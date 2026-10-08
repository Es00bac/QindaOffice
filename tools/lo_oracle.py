#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Dev-only expectation oracle for calc_engine function fixtures (plan
section 7.2). Reads tests/fixtures/functions/exprs.txt (one formula per
line, no leading '='), lays the shared DATA BLOCK (below) plus the formulas
into a CSV grid, converts CSV -> XLSX -> CSV through headless LibreOffice
(which evaluates the formulas), and writes
tests/fixtures/functions/functions.tsv as  expr<TAB>expected.

The DATA BLOCK here must stay identical to kDataBlock in
tests/calc_engine/tst_calc_functions.cpp.
"""
import csv
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
EXPRS = REPO / "tests/fixtures/functions/exprs.txt"
OUT = REPO / "tests/fixtures/functions/functions.tsv"

DATA = {
    "A1": 1, "A2": 2, "A3": 3, "A4": 4, "A5": 5,
    "B1": 10, "B2": 20, "B3": 30, "B4": 40, "B5": 50,
    "C1": "apple", "C2": "Apple", "C3": "banana", "C4": "", "C5": "10",
    "D1": True, "D2": False, "D3": True, "D4": False, "D5": False,
    "G1": "aa", "G2": "bb", "G3": "cc", "G4": "dd", "G5": "ee",
    "H1": 100, "H2": 200, "H3": 300, "H4": 400, "H5": 500,
    "I1": "k1", "J1": "k2", "K1": "v1", "L1": "v2",
    "I2": "a", "J2": "b", "K2": 1, "L2": 2,
    "I3": "c", "J3": "d", "K3": 3, "L3": 4,
    "I4": "e", "J4": "f", "K4": 5, "L4": 6,
    # M61: dated cash flows (Microsoft's XNPV/XIRR example), MIRR flows,
    # and columns with ties for MODE/RANK.AVG/XMATCH.
    "M1": -10000, "M2": 2750, "M3": 4250, "M4": 3250, "M5": 2750,
    "N1": 39448, "N2": 39508, "N3": 39751, "N4": 39859, "N5": 39904,
    "O1": -120000, "O2": 39000, "O3": 30000, "O4": 21000, "O5": 37000, "O6": 46000,
    "P1": 7, "P2": 3, "P3": 7, "P4": 1, "P5": 7,
    "Q1": 2, "Q2": 2, "Q3": 5, "Q4": 5, "Q5": 9,
}
FORMULA_COL = 25  # column Z


def col_to_num(col):
    n = 0
    for ch in col:
        n = n * 26 + (ord(ch) - 64)
    return n - 1


def split_addr(addr):
    i = 0
    while addr[i].isalpha():
        i += 1
    return col_to_num(addr[:i]), int(addr[i:]) - 1


def main():
    # Optional: lo_oracle.py EXPRS OUT — evaluate another list, e.g.
    # tests/fixtures/functions/exprs_m61.txt → functions_m61.tsv (M61 rows;
    # the rows LibreOffice gets wrong or lacks are asserted by hand instead).
    exprs_path = Path(sys.argv[1]) if len(sys.argv) > 1 else EXPRS
    out_path = Path(sys.argv[2]) if len(sys.argv) > 2 else OUT
    exprs = [ln.strip() for ln in exprs_path.read_text().splitlines()
             if ln.strip() and not ln.startswith("#")]
    max_row = max([r for _, r in (split_addr(a) for a in DATA)] + [len(exprs) - 1])
    max_col = max([c for c, _ in (split_addr(a) for a in DATA)] + [FORMULA_COL])
    grid = [["" for _ in range(max_col + 1)] for _ in range(max_row + 1)]
    for addr, value in DATA.items():
        c, r = split_addr(addr)
        if value is True:
            grid[r][c] = "=TRUE()"
        elif value is False:
            grid[r][c] = "=FALSE()"
        else:
            grid[r][c] = str(value)
    for i, expr in enumerate(exprs):
        grid[i][FORMULA_COL] = "=" + expr

    tmp = Path(tempfile.mkdtemp(prefix="lo_oracle_"))
    csv_in = tmp / "in.csv"
    with csv_in.open("w", newline="") as fh:
        csv.writer(fh).writerows(grid)

    def convert(target, src):
        subprocess.run(["soffice", "--headless", "--norestore", "--nologo",
                        "-env:UserInstallation=file://" + str(tmp / "profile"),
                        "--convert-to", target, "--outdir", str(tmp), str(src)],
                       check=True, capture_output=True, timeout=300)

    convert("xlsx", csv_in)
    convert("csv", tmp / "in.xlsx")

    with (tmp / "in.csv").open(newline="") as fh:
        rows = list(csv.reader(fh))
    lines = []
    for i, expr in enumerate(exprs):
        value = rows[i][FORMULA_COL] if i < len(rows) and FORMULA_COL < len(rows[i]) else ""
        lines.append(expr + "\t" + value)
    out_path.write_text("\n".join(lines) + "\n")
    print(f"wrote {len(lines)} rows to {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
