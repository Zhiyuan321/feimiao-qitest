#pragma once
#include <QString>

namespace qitest {
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
