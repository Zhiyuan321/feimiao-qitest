#pragma once
#include "core/ChromatogramEngine.h"

namespace qitest {
// Reproducible offline area, not an identification or concentration result.
// Retains the exact extracted trace and extraction parameters, not all raw scans.
struct IntegrationSnapshot {
    ChromatogramEngine::Kind kind = ChromatogramEngine::Kind::Tic;
    int msLevel = 1;
    double targetMz = 100, toleranceDa = 0.5;
    double fromSeconds = 0, toSeconds = 0;
    bool endpointBaseline = false;
    QVector<SpectrumPoint> trace;
    double area = 0;
};

class IntegrationDocument final {
public:
    static constexpr qint64 MaximumBytes = 2 * 1024 * 1024;
    static bool save(const QString &path, const IntegrationSnapshot &snapshot, QString *error);
    // Atomic in-memory replacement; hash and recalculated area must both agree.
    static bool load(const QString &path, IntegrationSnapshot *snapshot, QString *error);
};
}
