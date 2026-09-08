#pragma once
#include "domain/Models.h"

namespace qitest {
class ChromatogramEngine final {
public:
    enum class Kind { Tic, Bpc, Eic };
    static constexpr int MaximumScans = 5000;
    static constexpr int MaximumPoints = 1000000;
    static bool validate(const QVector<SpectrumScan> &scans, QString *error = nullptr);
    // For display only, SpectrumPoint.mz contains seconds on a time axis.
    static QVector<SpectrumPoint> trace(const QVector<SpectrumScan> &scans, Kind kind,
        int msLevel = 1, double targetMz = 0.0, double toleranceDa = 0.5);
    struct Integral {
        bool valid = false;
        double area = 0.0; // input intensity * seconds; not concentration
        QString error;
    };
    static Integral integrate(const QVector<SpectrumPoint> &trace, double fromSeconds,
        double toSeconds, bool endpointBaseline);
};
}
