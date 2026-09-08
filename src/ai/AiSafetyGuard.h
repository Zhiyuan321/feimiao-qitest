#pragma once

#include <QString>

namespace qitest {

class AiSafetyGuard final {
public:
    static QString enforce(const QString &modelText, const QString &deterministicEvidence = {});
};

} // namespace qitest
