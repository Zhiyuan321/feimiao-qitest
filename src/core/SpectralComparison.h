#pragma once
#include "domain/Models.h"

namespace qitest {
struct SpectralPair { int query = 0, reference = 0; double deltaDa = 0; };
struct SpectralComparisonResult {
    bool valid = false;
    QString error;
    double cosine = 0;
    QVector<SpectralPair> pairs;
};

// Peak-list comparison, not an identification/classification or concentration.
// Reciprocal nearest m/z pairs within an absolute Da tolerance; ties choose
// the lower m/z. Each peak contributes to at most one pair. All positive peaks
// contribute to the denominator, including unmatched peaks. O(n log m + m log n).
class SpectralComparison {
public:
    static constexpr int MaximumPeaks = 10000;
    static constexpr const char *Version = "qitest-reciprocal-cosine-1";
    static SpectralComparisonResult compare(const QVector<SpectrumPoint> &query,
        const QVector<SpectrumPoint> &reference, double toleranceDa);
};
}
