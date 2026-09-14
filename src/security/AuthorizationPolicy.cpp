#include "security/AuthorizationPolicy.h"

namespace qitest {

bool AuthorizationPolicy::allows(SessionRole role, Permission permission) {
    Q_UNUSED(role)
    Q_UNUSED(permission)
    // 单机仪器交付不再设置账户层级。危险操作仍由连接状态、设备互锁、
    // 参数范围、人工确认、回执匹配和审计记录共同约束。
    return true;
}

SessionRole AuthorizationPolicy::roleFromString(const QString &name) {
    const QString normalized = name.trimmed().toLower();
    if (normalized == "administrator" || normalized == "admin") return SessionRole::Administrator;
    if (normalized == "engineer") return SessionRole::Engineer;
    if (normalized == "operator") return SessionRole::Operator;
    return SessionRole::OfflineDemo;
}

QString AuthorizationPolicy::roleName(SessionRole role) {
    switch (role) {
    case SessionRole::OfflineDemo: return "工作站";
    case SessionRole::Operator: return "操作员";
    case SessionRole::Engineer: return "工程师";
    case SessionRole::Administrator: return "管理员";
    }
    return "本地操作";
}

} // namespace qitest
