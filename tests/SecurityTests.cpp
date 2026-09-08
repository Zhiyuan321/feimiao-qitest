#include "security/AuthorizationPolicy.h"
#include <QtTest>

using namespace qitest;

class SecurityTests final : public QObject {
    Q_OBJECT
private slots:
    void enforcesRoleBoundaries();
};

void SecurityTests::enforcesRoleBoundaries() {
    QVERIFY(AuthorizationPolicy::allows(SessionRole::OfflineDemo, Permission::RunAcquisition));
    QVERIFY(!AuthorizationPolicy::allows(SessionRole::Operator, Permission::ManageMethods));
    QVERIFY(AuthorizationPolicy::allows(SessionRole::Engineer, Permission::ManageMethods));
    QVERIFY(!AuthorizationPolicy::allows(SessionRole::OfflineDemo, Permission::HardwareCriticalCommand));
    QVERIFY(AuthorizationPolicy::allows(SessionRole::Administrator, Permission::HardwareCriticalCommand));
}

QTEST_APPLESS_MAIN(SecurityTests)
#include "SecurityTests.moc"
