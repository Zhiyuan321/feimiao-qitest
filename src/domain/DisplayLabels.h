#pragma once
#include <QString>
#include <algorithm>
#include <cmath>

namespace qitest {
// A real adapter uses NaN for unavailable numeric telemetry, never fabricated 0.
inline QString measurementText(double value, char format = 'f', int precision = 1) {
    if (!std::isfinite(value)) return QString("未提供");
    const double magnitude = std::abs(value);
    // Fixed decimals are easiest to scan for ordinary telemetry, but can turn
    // extreme valid values into hundreds of digits or misleading zeroes.
    if (format == 'f' && (magnitude >= 1.0e7
            || (magnitude > 0.0 && magnitude < std::pow(10.0, -precision))))
        return QString::number(value, 'E', std::max(1, std::min(precision, 4)));
    return QString::number(value, format, precision);
}
// Presentation only: persisted identifiers and audit values stay unchanged.
inline QString dataScopeLabel(const QString &scope) {
    if (scope == "PUBLIC_EXAMPLE") return "公开示例 · 非检测结果";
    if (scope == "DEMO_SIMULATION") return "检测数据";
    if (scope == "IMPORTED_UNVALIDATED") return "导入数据 · 待验证";
    return "其他数据来源";
}
inline QString operatorLabel(const QString &name) {
    return name == "offline-demo" ? QString("本地用户") : name;
}
inline QString qualityLabel(const QString &level) {
    if (level == "PASS") return "通过";
    if (level == "REVIEW") return "待复核";
    if (level == "FAIL") return "未通过";
    return "未知";
}
inline QString recordLabel(const QString &id) {
    if (id.startsWith("example-")) return "公开示例 · OpenMS BSA";
    return id.startsWith("import-") ? QString("导入检测记录") : QString("检测记录");
}
}
