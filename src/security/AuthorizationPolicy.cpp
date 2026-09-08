#include "security/AuthorizationPolicy.h"

namespace qitest {

bool AuthorizationPolicy::allows(SessionRole role, Permission permission) {
    if (permission == Permission::HardwareCriticalCommand)
        return role == SessionRole::Engineer || role == SessionRole::Administrator;
    if (permission == Permission::ManageMethods)
        return role == SessionRole::OfflineDemo || role == SessionRole::Engineer || role == SessionRole::Administrator;
    if (permission == Permission::ReviewResult)
        return role == SessionRole::OfflineDemo || role == SessionRole::Operator
            || role == SessionRole::Engineer || role == SessionRole::Administrator;
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
    case SessionRole::OfflineDemo: return "本地操作";
    case SessionRole::Operator: return "操作员";
    case SessionRole::Engineer: return "工程师";
    case SessionRole::Administrator: return "管理员";
    }
    return "本地操作";
}

} // namespace qitest
