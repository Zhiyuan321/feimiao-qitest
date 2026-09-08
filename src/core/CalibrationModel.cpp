#include "core/CalibrationModel.h"
#include <algorithm>
#include <cmath>

namespace qitest {
CalibrationEvaluation CalibrationCalculator::evaluate(const CalibrationModel &model) {
    CalibrationEvaluation result;
    const auto fail = [&](const QString &message) { result.fit = {false, message}; return result; };
    if (model.observations.size() > 10000) return fail("最多 10000 个校准点");
    if (model.standard != CalibrationModel::Standard::External && model.standard != CalibrationModel::Standard::Internal)
        return fail("未知校准类型");
    if (model.weight != QuantitationEngine::Weight::None && model.weight != QuantitationEngine::Weight::InverseXSquared)
        return fail("未知权重");
    if (model.standard == CalibrationModel::Standard::Internal && model.internalStandard.trimmed().isEmpty())
        return fail("请明确内标名称，不能自动猜测配对");
    for (const auto &row : model.observations) {
        // Even excluded rows must remain valid data; zero ISTD is allowed only
        // in an explicitly excluded row and never participates in division.
        for (double value : {row.concentration, row.response, row.internalConcentration, row.internalResponse})
            if (!std::isfinite(value) || value < 0) return fail("校准数据存在非有限值或负数");
        if (!row.included) continue;
        double x = row.concentration, y = row.response;
        if (model.standard == CalibrationModel::Standard::Internal) {
            if (row.internalConcentration <= 0 || row.internalResponse <= 0)
                return fail("参与拟合的每个点都需要正的内标浓度和响应");
            x /= row.internalConcentration; y /= row.internalResponse;
        }
        result.points.append({x, y});
    }
    result.fit = QuantitationEngine::fitLinear(result.points, model.weight);
    if (!result.fit.valid) return result;
    if (result.fit.slope <= 0) return fail("响应随浓度未正向增加，不能作为定量校准");
    const auto bounds = std::minmax_element(result.points.cbegin(), result.points.cend(),
        [](const CalibrationPoint &a, const CalibrationPoint &b) { return a.concentration < b.concentration; });
    result.minimum = bounds.first->concentration;
    result.maximum = bounds.second->concentration;
    return result;
}

bool CalibrationCalculator::calculate(const CalibrationModel &model, double response,
        double internalResponse, double internalConcentration, double *concentration, QString *error) {
    const auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    const auto evaluation = evaluate(model);
    if (!evaluation.fit.valid) return fail(evaluation.fit.error);
    if (!concentration || !std::isfinite(response) || response < 0) return fail("样品响应无效");
    double scale = 1;
    if (model.standard == CalibrationModel::Standard::Internal) {
        if (!std::isfinite(internalResponse) || !std::isfinite(internalConcentration)
            || internalResponse <= 0 || internalConcentration <= 0) return fail("需要同一样品的内标响应和已知内标浓度");
        response /= internalResponse;
        scale = internalConcentration;
    }
    double x = 0;
    if (!QuantitationEngine::backCalculate(evaluation.fit, response, evaluation.minimum, evaluation.maximum, &x, error)) return false;
    const double value = x * scale;
    if (!std::isfinite(value)) return fail("浓度计算溢出");
    *concentration = value;
    return true;
}
}
