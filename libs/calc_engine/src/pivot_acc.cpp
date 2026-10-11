// SPDX-License-Identifier: GPL-3.0-or-later
// Independent summaries shared by ordinary and calculated pivot fields.
#include "pivot_compute.h"
#include <cmath>
namespace QindaOffice::Calc {
void PivotAcc::add(const Value &value)
{
    if (isBlank(value)) {
        return;
    }
    ++count;
    if (const auto *e = std::get_if<Err>(&value)) {
        if (!hasError) {
            hasError = true;
            error = *e;
        }
        return;
    }
    const auto *n = std::get_if<double>(&value);
    if (n == nullptr) {
        return; // text and booleans are counted, never summed
    }
    ++nums;
    sum += *n;
    product *= *n;
    min = nums == 1 ? *n : qMin(min, *n);
    max = nums == 1 ? *n : qMax(max, *n);
    const double delta = *n - mean;
    mean += delta / double(nums);
    m2 += delta * (*n - mean);
}

Value pivotFinalize(const PivotAcc &acc, PivotFunction function)
{
    if (function == PivotFunction::Count) {
        return double(acc.count);
    }
    if (function == PivotFunction::CountNums) {
        return double(acc.nums);
    }
    if (acc.hasError) {
        return acc.error;
    }
    const double n = double(acc.nums);
    switch (function) {
    case PivotFunction::Sum: return acc.sum;
    case PivotFunction::Average: return acc.nums == 0 ? Value(Err::Div0) : Value(acc.sum / n);
    case PivotFunction::Max: return acc.nums == 0 ? 0.0 : acc.max;
    case PivotFunction::Min: return acc.nums == 0 ? 0.0 : acc.min;
    case PivotFunction::Product: return acc.nums == 0 ? 0.0 : acc.product;
    case PivotFunction::StdDev: return acc.nums < 2 ? Value(Err::Div0) : Value(std::sqrt(acc.m2 / (n - 1)));
    case PivotFunction::StdDevP: return acc.nums < 1 ? Value(Err::Div0) : Value(std::sqrt(acc.m2 / n));
    case PivotFunction::Var: return acc.nums < 2 ? Value(Err::Div0) : Value(acc.m2 / (n - 1));
    case PivotFunction::VarP: return acc.nums < 1 ? Value(Err::Div0) : Value(acc.m2 / n);
    default: return acc.sum;
    }
}

} // namespace QindaOffice::Calc
