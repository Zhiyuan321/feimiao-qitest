#pragma once
#include "domain/Models.h"
#include <QJsonObject>

namespace qitest {
class IonThresholdScreening final {
public:
    static constexpr const char *Version="eic-ion-sum-2";
    static constexpr const char *LegacyVersion="eic-three-ion-sum-1";
    static constexpr double ToleranceDa=0.5;
    // Immutable .lib snapshot, captured before detection. Does not read external files.
    // Returns COMPLETE, PARTIAL (invalid entries), or FAILED (invalid input/snapshot).
    static QString apply(const QVector<SpectrumScan> &scans, const QJsonObject &snapshot,
                         AnalysisResult *result, QString *error=nullptr);
};
}
