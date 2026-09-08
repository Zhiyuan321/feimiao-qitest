#include "core/QuantitationEngine.h"
#include <cmath>

namespace qitest {
LinearCalibrationFit QuantitationEngine::fitLinear(const QVector<CalibrationPoint> &points, Weight weighting) {
    if (points.size() < 3 || points.size() > 10000) return {false, "需要 3–10000 个校准点"};
    double sumW = 0, meanX = 0, meanY = 0;
    QVector<double> weights;
    for (const auto &p : points) {
        if (!std::isfinite(p.concentration) || !std::isfinite(p.response) || p.concentration < 0 || p.response < 0)
            return {false, "校准点包含无效或负数值"};
        if (weighting == Weight::InverseXSquared && p.concentration <= 0)
            return {false, "1/x² 不能使用零浓度点；请明确排除空白点或改用无权重，不自动修改数据"};
        const double w = weighting == Weight::None ? 1.0 : std::pow(1.0 / p.concentration, 2);
        if (!std::isfinite(w) || w <= 0) return {false, "权重超出数值范围"};
        weights.append(w); sumW += w; meanX += w*p.concentration; meanY += w*p.response;
    }
    meanX /= sumW; meanY /= sumW;
    double xx = 0, xy = 0, total = 0;
    for (int i=0; i<points.size(); ++i) {
        const double dx = points[i].concentration-meanX, dy = points[i].response-meanY;
        xx += weights[i]*dx*dx; xy += weights[i]*dx*dy; total += weights[i]*dy*dy;
    }
    if (!std::isfinite(xx) || !std::isfinite(xy) || xx <= 0 || total <= 0)
        return {false, "浓度或响应没有有效跨度，或数值溢出"};
    const double slope = xy/xx, intercept = meanY-slope*meanX;
    double residual = 0;
    for (int i=0; i<points.size(); ++i)
        residual += weights[i]*std::pow(points[i].response - (slope*points[i].concentration+intercept), 2);
    const double r2 = 1-residual/total;
    if (!std::isfinite(slope) || !std::isfinite(intercept) || !std::isfinite(r2)) return {false, "拟合数值溢出"};
    return {true, {}, slope, intercept, r2};
}

bool QuantitationEngine::backCalculate(const LinearCalibrationFit &fit, double response,
        double minimum, double maximum, double *concentration, QString *error) {
    const auto fail = [error](const QString &message) { if(error) *error=message; return false; };
    if (!fit.valid || !std::isfinite(fit.slope) || fit.slope <= 0 || !std::isfinite(fit.intercept))
        return fail("需要有效正斜率校准曲线");
    if (!std::isfinite(response) || response < 0 || !std::isfinite(minimum) || !std::isfinite(maximum)
            || minimum < 0 || maximum <= minimum || !concentration) return fail("响应或校准范围无效");
    const double value = (response-fit.intercept)/fit.slope;
    if (!std::isfinite(value) || value < minimum || value > maximum) return fail("反算值超出校准范围，不进行外推");
    *concentration = value;
    return true;
}
}
