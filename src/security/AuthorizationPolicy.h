#pragma once

#include <QString>

namespace qitest {

enum class SessionRole { OfflineDemo, Operator, Engineer, Administrator };
enum class Permission { RunAcquisition, ReviewResult, ExportData, ManageMethods, HardwareCriticalCommand };

class AuthorizationPolicy final {
public:
    static bool allows(SessionRole role, Permission permission);
    static SessionRole roleFromString(const QString &name);
    static QString roleName(SessionRole role);
};

} // namespace qitest
